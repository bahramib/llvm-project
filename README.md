Enhancing Clang-Tidy with project-level knowledge
=================================================

> The phrases *"MUST"*, "*SHOULD*", "*MIGHT*", etc. when emphasised ***SHOULD*** be understood as per [IEEE RFC 2119](http://datatracker.ietf.org/doc/html/rfc2119).

There are several classes of problems for which analysis rules are easily expressed as Clang-Tidy checks, but could not be reasonably found with the current infrastructure.
We will hereby refer these -- after the most significant family of such rules, and a previous parallel implementation in the *Clang Static Analyser* -- as **"statistical checkers"**.

Currently, Clang-Tidy executes each matcher implementation separately for each AST unit, even if multiple source files are given to the invocation.
In practice as of 14.0, each `ClangTidyCheck` class is instantiated once per process and are fed multiple ASTs, but these `ASTUnit`s die between executions, so keeping any references from a check instance's fields *into* the AST data structure is dangerous.
This rules out the option to implement a statistical check in a meaningful way where all the data is kept in memory. It simply would not scale, as the `clang-tidy` binary itself is single-threaded internally!

The situation is even worse if multiple `clang-tidy` binaries execute in parallel, in which case the instantiated objects have no meaningful way of cooperation!

Related Work
------------

When discussing this problem, we built heavily on existing experience with two major technologies.
The first being [*MapReduce*](http://enwp.org/MapReduce), with which parallels in our proposed new architecture is not a coincidence.

The other is *Cross-Translation Unit (CTU)* analysis in the *Clang Static Analyser*.
With *CTU*, the *CSA* can evaluate expressions only defined in external translation units automatically, but for this, it must run a pre-processing phase where a symbol definition map is created.
The evaluation of external symbols is automatic and transparent to the checker implementations, which is made possible through the complicated engine implemented in *CSA*.
CTU is most commonly driven through -- and supported by -- [CodeChecker](http://github.com/Ericsson/CodeChecker).
The full [overview documentation](http://clang.llvm.org/docs/analyzer/user-docs/CrossTranslationUnit.html) for CTU is available in the official Clang tree.

According to CodeChecker's documentation, there is an [existing support for statistical analysis](http://codechecker.readthedocs.io/en/v6.18.2/analyzer/user_guide/#statistical-analysis-mode), but unfortunately I was unable to find public resources for the mentioned `statisticsCollector.ReturnValueCheck` or `statisticsCollector.SpecialReturnValue` checkers.
Our proposed infrastructure mirrors that of these checks, with a separate collect and diagnosis phase.

Motivating examples
-------------------

### Main example: `misc-discarded-return-value` (*MDRV*)

Consider a rule where we would like to warn users about function calls where they *discard* (i.e. not consume) the return value of the function.
We have several ways to achieve this: `Sema` warns for certain built-in operators (e.g. `-Wunsued-value` for raw `(int) + (int)`), and we have had the `[[nodiscard]]` specifier, but the former is very limited, why the latter requires the technical and legal ability to modify existing, potentially library, code.
Instead, let's say that we could inspect the call sites of each function in the project and classify whether they are **consumed** or **discarded**.
If the percentage of consumed calls for a particular function reaches a given threshold, we could warn every other call site, indicating that the user might have mistakenly discarded the return value.
(This complements `[[nodiscard]]` where the library author can pre-emptively decide that discarded values shall be warned.)

Together with `@bahramib`, we have created an implementation of this check that is available for review as [**D124446**](http://reviews.llvm.org/D124446).

Unfortunately, such a statistical check, while worthwhile for single-TU mode, might cause a lot of false positives and false negatives due to every invocation seeing only part of the whole picture.
If you set 80% threshold and have a TU with 5 out of 5 consumed calls to a function, and in another TU the "same function" is consumed only 3 out of 5 times, that gives us 100% and 60% non-discarded use cases, respectively.
Because the latter does not hit the 80% threshold, no warnings will be produced, while *on the project level*, 5 + 3 = 8 consumed calls out of 5 + 5 = 10 call sites would exactly be 80%.
Simply put, had you been running Clang-Tidy on a unity build[^1] translation unit, you would have gotten the warnings.


This problem is trivially extended to project-level information: if we are able to reliably identify the "same function" across multiple TUs, we can add up the calls (checked and discarded) to the function, and produce diagnostics based on a threshold calculated for the entirety of the input project.

This check is the minimal working example of a rule that can exploit the infrastructure we are proposing, as the *map* operation is the constant literal `1`, counting every call site, and the *reduce* operation is `+` on natural numbers.

### More complex example: `readability-suspicious-call-argument`

The [`readability-suspicious-call-argument`](http://releases.llvm.org/13.0.1/tools/clang/tools/extra/docs/clang-tidy/checks/readability-suspicious-call-argument.html) check currently only checks every parameter and argument name against one another, for each call site.
However, the *name-based* detection of *argument selection defects* have existing literature[^2][^3] that seem to indicate that in some cases, the ability to witness the pattern how people call the function may indicate that it was in fact the **parameter** that had a naming convention violation, and all the call sites are as intended.
It must be noted that this analysis rule is implemented downstream by a third-party in a way that the project-level information is stored in an SQLite database,[^4] and the analysis itself executes as a *CSA* plug-in (which gives the execution environment the ability to load external libraries like SQLite).

[^1]: [Unity build](http://enwp.org/Unity_build) is a technique where the entire project is formatted into a **single** translation unit (or a very small number of translation units), which is then compiled. This usually allows for more optimisations to take place.
[^2]: Andrew Rice, et al.: *Detecting argument selection defects*, OOPSLA 2017, pp. 1-22, [doi://10.1145/3133928](http://doi.org/10.1145/3133928)
[^3]: Michael Pradel, Thomas R. Gross: *Detecting anomalies in the order of equally-typed method arguments*, ISSTA 2011, pp. 232-242, [doi://10.1145/2001420.2001448](http://doi.org/10.1145/2001420.2001448)
[^4]: GrammaTech Inc.: Swap Detector - A library for detecting swapped arguments in function calls, [GitHub://@GrammaTech/swap-detector](http://github.com/GrammaTech/swap-detector)

This check could be first extended to collect information across all call sites investigated by it, and then extended to use all name pairs from the entire project, to be able to calculate more versatile metrics.

### Theoretical example: superfluous `friend` declarations

The last example for which I am unable to link existing implementation is the problem of having too wide `friend` declarations.[^5]
Some actual measurements on live projects can be found in the PhD thesis investigating "selective friends".
It is easy to imagine a check that would be able to collect how many `friend` declarations actually use private symbols, and suggest not breaking encapsulation where it is not needed based on the current source code.

[^5]: Gábor Márton: *Tools and Language Elements for Testing, Encapsulation and Controlling Abstraction in Large-Scale C++ Projects*, Ph.D. thesis, [http://martong.github.io](http://martong.github.io/gabor-marton-phd-thesis.pdf). The related part is found in Chapter 3 *Selective friend*, pp. 78-118.


Classification
--------------

In the following, we'll generally classify **problems** (that are found by *checks*) into three categories.
This is only for exposition in this proposal, and do not directly translate to any code-level constructs.

 1. Problems that are only meaningful locally. E.g., *"use lambda instead of `std::bind`"*.
 2. Problems that **might be** meaningful locally, but the diagnostics are improved by project-level knowledge.
    Most *statistical checks*, including the first two of the previous examples, fall into this category.
 3. Problems that are **only** meaningful when executed as a whole-project analysis.
    The last example about `friend` declarations fit into this category.

Infrastructure proposal (summary)
---------------------------------

The proposal is to implement a map-reduce-like infrastructure into Clang-Tidy.
We note that due to each check being unique without an underlying "expression evaluator" like in *CSA*, the infrastructure changes are more invasive than *CTU* in *CSA*.
Due to the dissimilarity between the conceptual baseline of the two analysis engines, our proposal intends to leave the format of the data to be owned by each individual check.
For serialisation, *YAML* is the suggested container, due to LLVM already using it in multiple places and having an extensive support library.
The infrastructure logic in Clang-Tidy itself should offer support methods by which instantiated checks can obtain "references" to data storages and paths, but the actual deserialisation should happen within the check's scope.
This is so that potentially complex data structures do not pollute the infrastructure core through global template instantiations.
We would like to forego the problems with the *Global Data Map* (GDM) in *CSA*, and consider the data produced by a particular check to be only interesting to that check.

We plan to divide the execution of Clang-Tidy into three phases -- hence the term *multipass* or *multiphase*.
Checks should be allowed to define their **collect** (map), **compact** (reduce) and **diagnose** phases.

The proposed infrastructure changes are available in patch [**D124447**](http://reviews.llvm.org/D124447), and the uplift of `misc-discarded-return-value` to these changes -- as a suggested "blueprint" for the exploiting of the infrastructure -- is in patch [**D124448**](http://reviews.llvm.org/D124448).

For the execution of Clang-Tidy, two new parameters are introduced:

 * `--multipass-phase`, which sets the executing binary into one of the three phases
 * `--multipass-dir`, which specifies a data directory which acts as persistence between parallel processes and the phases themselves

These parameters are crucial only for the driving of the multi-pass analysis, and deal with pseudo-temporary data.
As such, they ***SHOULD NOT*** be exposed and configurable via the `.clang-tidy` file.

### The three phases

#### Collecting per-TU data

In the **collect** phase, the checks receive the matched nodes as normal, but are expected to create the internal knowledge of the AST they were given without producing any diagnostics.
The collect phase ends with the serialisation of the obtained **"per-TU"** data to some data store.
In the current implementation proposal, it is achieved by saving the internal data structure to a file (within `--multipass-dir`, having a name derived from a pattern, not in the control of the check in particular!).

The **collect** phase ***MUST*** be possible to be executed in parallel, to support existing driver infrastructures like *CodeChecker*.
This is almost trivially achieved by designating for each input translation unit its own unique output file.
There is an ongoing question for cases where the same source file is compiled multiple times (with different configurations) in the same build cycle and analysis invocation.
This question has theoretical implication (e.g., **"Should two almost-identical compilations of the same file count everything twice in a check like *`MDRV`*?"**) and technical depth as to how to achieve separation.
Trivial separation might be achieved by hashing the compile command vector into the output file's pattern.
Our suggestion is for now to accept skewed statistics if the "same" entity (source file) is found multiple times in the measured "population".
(A better solution to this problem shall be discussed on the *JSON Compilation Database* level, or in conjunction with build system engineers, and not a problem that should be solved directly within Clang-Tidy.)

#### Transforming per-TU data to project-level data

In the second phase, **compact**, checks should create a project-level sum of the obtained per-TU data.
This operation should be achieved without reliance on the actual source code (i.e., the ASTs).
The *"reduce"* operation's input and output format need not be the same, to support representations appropriate for different classes of problems.
The output of this phase is a project-level datafile for each check.

To prevent having to deal with synchronisation issues within the checks themselves, this step is not intended to be run in parallel.
Semi-parallelism ***MIGHT*** be achieved by running Clang-Tidy invocations separately for each (participating) check with the same `--multipass-dir` input, in which case every process will compact a subset of the input files (keyed by the check's name) into a distinct output file (also keyed by the check's name).
The suggestion is to support querying capabilities from Clang-Tidy Main, with specifics (such as threading or check selection) implemented in the driver frameworks (IDEs, CodeChecker, custom CI scripts, etc.).

#### Emitting diagnostics

The last phase, **diagnose** deals with actually producing `diag()` calls and user-facing diagnostics.
For **Category 1** checks, this is the only phase, and for backwards compatibility reasons, this is the *default* phase.
Checks should be able to obtain the information whether or not data from the previous step is available, in which case they can use it.
Otherwise, the suggestion is to execute the *"collect"* step and the *"diagnose"* step internally in one go, and produce per-TU diagnostics.

This step ***MUST*** be possible to be executed in parallel.

### Backwards compatibility

The **diagnose** phase is the default phase if no other option is given, in which case existing checks are expected to behave as before, creating diagnostics based on the data they have available in the translation unit they run on.

Checks are expected to produce no apparent behaviour from the user's point of view (create no files, emit no diagnostics, cause no crashes) in case a previous phase their logic depends on were skipped.

API changes (in detail)
-----------------------

In the existing [**D124447**](http://reviews.llvm.org/D124447) patch:

 1. Trivialities:
   * Extend `ClangTidyCheck` with `protected` support methods which subclasses can use to query information related to the multi-phase architecture.
   * Write the new command-line options to appropriate query methods.
 2. Add a new `collect(MatchResult&)` function that behaves just like `check(MatchResult&)` does, except that it gets called in the 1st phase, while `check` is only called in the 3rd phase.
 3. Add the `postCollect()` and `compact()` methods which expose the check-specific implementation for saving and transforming data.

Further options that are not implemented in code yet, but should definitely be considered before merging an implementation:

 * Create a new abstract subclass `MultipassClangTidyCheck <: ClangTidyCheck`, which will require the aforementioned methods to be implemented, without `ClangTidyCheck` defaulting them to empty functions. This could allow exposing the fact that check X supports the proposed architecture to prevent even the instantiation of a check that is not capable of running in phase K, instead of being called with empty inputs or produce empty results.
 * The same option but instead of wedging in a node in the hierarchy, just expose the same information by `virtual bool` functions.
 * Adding a "middleclass" would allow for exposing some common logic (e.g., caching whether loading phase-2 compacted data in phase-3 was successful?) instead of having every check depend on it.
 * Should we **hard require** the use of YAML format?

In practice: Implementing the infrastructure changes for `misc-discarded-return-value`
--------------------------------------------------------------------------------------

Patch [**D124448**](http://reviews.llvm.org/D124448) shows how an existing (at least considering the point-of-view of that particular patch, as *MDRV* is not yet merged at the time of writing this post!) check is refactored to support the proposed changes.
The changes here are small, mostly because the per-TU and the project-level data is expressed in the exact same format.


Workflow case studies
---------------------

### Not using the proposed architecture at all

In this case, no functional changes are observed.
`--multipass-phase=diagnose` is set in the command-line automatically as an implicit default, and existing checks will execute their `check()` callback, business as usual.

### One pass of either current per-TU or proposed project-level analysis, e.g., in (non-distributed) CI

In this case, no *significant* changes are observed.
The only concern is that the executing environment needs to specify the appropriate flags in the proper order.
There is no *user-friendly* support for the driving of this feature however, but we expect it not to be a significant challenge.
We intend to develop support for this feature in *CodeChecker* as soon as possible, and given our previous experience with *CTU*, do not expect it to be a challenge.[^6]

[^6]: A hack that was used to create the baseline data for some of the measurements found in the comments of [**D124446**](http://reviews.llvm.org/D124446) can be found here: [GitHub://@whisperity/CodeChecker:hack-multipass-tidy-hijacking//CodeChecker-Hijacker.sh](http://github.com/whisperity/CodeChecker/blob/hack-multipass-tidy-hijacking/CodeChecker-Hijacker.sh#L82). It should be noted that CodeChecker already uses a semi-temporary directory for the handling of the analysis, so all this script does is point `--multipass-dir` to an appropriately named directory.

### Incremental analysis (most importantly *IDE*s)

The biggest challenge in terms of design comes with facilitating incremental analysis, i.e., when the developer intends to receive diagnostics for a source file that is currently being actively worked on.
We believe it is a reasonable assumption that a development environment knows at least the list of files that need an analysis.
In case a single file has changed, the pipeline needs to be re-evaluated for that file only.
Conveniently, the *compact* phase does **NOT** require source files as an argument, as it deals with a set of already stored data in a directory tree!

 * Put simply, what needs to happen is execute the first phase for the changed file, which will create an updated data output (per check).
 * Then, the *compact* step will re-"sum" the project-level information.
 * After which, the *diagnose* step, executed again for only the changed source file, will match and create diagnostics using the updated project-level information.

However, in this configuration, the *compact* step requires a potentially computationally intensive action to be done over and over again for which barely any of the input itself changed.
Consider that during the development of a Clang-Tidy check, we have somewhere about 2500 TUs, but usually only change *one*...
To combat the need for memoising data, there are two potential choices.

#### External: Partial sums

Partial sums refer to creating a *compact* output for a subset of the project and then using this single, already calculated file and the results from the updated TU, to create the full picture that contains data for the entire project.
This method should be (mathematically) possible for every check to implement due to the nature of "populations" in statistics, but needs support from the driving environment.
In the current submitted patch, [**D124447**](http://reviews.llvm.org/D124447), the partial sum method works out of the box (from Tidy's perspective), assuming that the driver will:

 1. Delete the resulting datafile for the changing TU
 2. Call *compact* to create a project-level datafile for all except the current TU
 3. Keep this saved up somewhere to be able to "re-compact" with every change to the current TU

Importantly, partial sums require only "forward" progress and needs no specific *additional* work from check developers, unlike...

#### Internal: Inverse *compact*

In this **hypothetical** mode, checks ***MUST*** implement the inverse of the *compact* operation, i.e., removing the "contents" of one datafile from the already loaded data.
This way, if we assume that for each TU we can store not just the newest, but the second newest datafile, the *compact* operation could:

 1. Load the already compacted project-level data (that was created from an older snapshot)
 2. Perform the inverse operation on this data and remove the TU's second oldest information
 3. Add the TU's newest information in
 4. Save

This option would lessen the need of managing the partial sums on the file system level, but cause more requirements from check authors.
For only changing a few files, it does not seem that this might be worth it, but this approach could help in case the amount of changed files fluctuates.

### Distributed analysis

The biggest open question is fully distributed analysis.
However, we believe that -- thanks to the discrete steps taken in the multi-pass analysis -- we can consider the resulting data files as outputs and inputs of each individual step, and synchronisation could be achieved by the facilitator of the distributed nature.

Appendix A. Side considerations for *"statistical checks"*
----------------------------------------------------------

As mentioned earlier, the main motivating example for this check is to give infrastructure-level support for a category of problems that could be found with checks but only if project-level information, usually statistical information, is available.
Usually, intra-TU statistics are gathered by associating a data structure that is "keyed" by AST nodes, which die when Clang-Tidy switches between analysing subsequent ASTs.
Unless careful consideration is made by the developer to wipe the data structure fields in `on(Start|End)TranslationUnit()`, there is a serious issue with keeping potential dangling references, which might never surface a crash or bug in production if Clang-Tidy is executed with per-file parallelism (e.g., via a driver script such as CodeChecker).

As of 14.0, a quick search found the following 5 checks to **directly** manage data structures and sub-object ownership in their `on...TU()` methods:

 * `modernize/DeprecatedHeadersCheck.cpp`
 * `mpi/BufferDerefCheck.cpp`
 * `mpi/TypeMismatchCheck.cpp`
 * `performance/UnnecessaryValueParamCheck.cpp`
 * `readability/BracesAroundStatementsCheck.cpp`

We suggest to investigate the means of creating a contextual object that can be more easily expressed to contain AST-node specific data, and the clean-up of these data structures should be driven by Tidy's core, instead of each check doing it manually.
