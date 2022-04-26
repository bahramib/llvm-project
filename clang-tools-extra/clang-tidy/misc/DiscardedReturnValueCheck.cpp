//===--- DiscardedReturnValueCheck.cpp - clang-tidy -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DiscardedReturnValueCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Index/USRGeneration.h"
#include "llvm/Support/YAMLParser.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tidy::misc;

namespace {

/// The format of the YAML document the checker uses in multi-pass project-level
/// mode. This is the same for the per-TU and the per-project data.
struct SerializedFunction {
  std::string ID;
  std::size_t ConsumedCalls, TotalCalls;
};
using FunctionVec = std::vector<SerializedFunction>;

} // namespace

namespace llvm {
namespace yaml {

template <> struct MappingTraits<SerializedFunction> {
  static void mapping(IO &IO, SerializedFunction &F) {
    IO.mapRequired("ID", F.ID);
    IO.mapRequired("Consumed", F.ConsumedCalls);
    IO.mapRequired("Total", F.TotalCalls);
  }
};

} // namespace yaml
} // namespace llvm

LLVM_YAML_IS_SEQUENCE_VECTOR(SerializedFunction)

static Optional<FunctionVec> loadYAML(StringRef File) {
  using namespace llvm;

  ErrorOr<std::unique_ptr<MemoryBuffer>> IStream =
      MemoryBuffer::getFileAsStream(File);
  if (!IStream)
    return None;

  FunctionVec R;
  yaml::Input YIn{**IStream};
  YIn >> R;

  return R;
}

static bool loadYAML(StringRef FromFile,
                     DiscardedReturnValueCheck::FunctionMapTy &ToMap) {
  Optional<FunctionVec> OV = loadYAML(FromFile);
  if (!OV)
    return false;

  for (const SerializedFunction &SF : *OV) {
    DiscardedReturnValueCheck::FunctionInfo &F =
        ToMap
            .try_emplace(
                SF.ID,
                DiscardedReturnValueCheck::FunctionInfo{0, 0, nullptr, {}})
            .first->second;

    F.ConsumedCalls += SF.ConsumedCalls;
    F.TotalCalls += SF.TotalCalls;
  }

  return true;
}

static FunctionVec
yamlize(const DiscardedReturnValueCheck::FunctionMapTy &Map) {
  FunctionVec SFs;
  llvm::transform(Map, std::back_inserter(SFs), [](auto &&E) {
    return SerializedFunction{E.first().str(), E.second.ConsumedCalls,
                              E.second.TotalCalls};
  });
  return SFs;
}

static void writeYAML(StringRef Whence, FunctionVec Elements,
                      StringRef ToFile) {
  std::error_code EC;
  llvm::raw_fd_ostream FS(ToFile, EC, llvm::sys::fs::OF_Text);
  if (EC) {
    llvm::errs() << "DiscardedReturnValueCheck: Failed to write " << Whence
                 << " output file: " << EC.message();
    llvm::report_fatal_error("", false);
    return;
  }

  llvm::yaml::Output YAMLOut(FS);
  YAMLOut << Elements;
}

static constexpr llvm::StringLiteral Call = "call";
static constexpr llvm::StringLiteral Consume = "consume";

namespace clang {
namespace tidy {
namespace misc {

std::uint8_t DiscardedReturnValueCheck::FunctionInfo::ratio() const {
  if (TotalCalls == 0)
    return 0;

  std::uint8_t R = static_cast<float>(ConsumedCalls * 100) / TotalCalls;
  assert(R <= 100 && "Invalid percentage, maybe bogus compacted data?");
  return R;
}

void DiscardedReturnValueCheck::registerCall(const CallExpr *CE,
                                             const FunctionDecl *FD,
                                             bool IncrementCounters,
                                             const void *ConsumingContext) {
  assert(CE && FD);
  FD = FD->getCanonicalDecl();
  auto &Data = [this, FD]() -> FunctionInfo & {
    auto IDIt = FunctionIDs.find(FD);
    if (IDIt == FunctionIDs.end()) {
      SmallString<128> ID;
      bool USRFailure = clang::index::generateUSRForDecl(FD, ID);
      if (USRFailure)
        ID = FD->getDeclName().getAsString();
      assert(!ID.empty() && "Generating name for function failed");

      IDIt = FunctionIDs.try_emplace(FD, ID.str().str()).first;
    }
    assert(IDIt != FunctionIDs.end());

    FunctionInfo &Data =
        FunctionInfos.try_emplace(IDIt->second, FunctionInfo{0, 0, FD, {}})
            .first->second;

    return Data;
  }();

  if (!Data.FD)
    // If the map already contains data which was loaded from an earlier project
    // pass, store FD (in the current TU) so diagose() can point to it properly.
    Data.FD = FD;

  if (ConsumingContext) {
    using ConsumeVecTy = decltype(ConsumedCalls)::mapped_type;
    ConsumeVecTy &ConsumeVec =
        ConsumedCalls.try_emplace(CE, ConsumeVecTy{}).first->second;
    if (llvm::find(ConsumeVec, ConsumingContext) != ConsumeVec.end())
      return;
    ConsumeVec.emplace_back(ConsumingContext);
  } else
    Data.DiscardedCEs.insert(CE);

  if (IncrementCounters) {
    if (ConsumingContext)
      ++Data.ConsumedCalls;
    ++Data.TotalCalls;
  }
}

void DiscardedReturnValueCheck::diagnose(const FunctionInfo &F) {
  if (F.ratio() < ConsumeThreshold)
    return;

  for (const CallExpr *Call : F.DiscardedCEs) {
    diag(Call->getExprLoc(),
         "return value of %0 is used in most calls, but not in this one",
         DiagnosticIDs::Warning)
        << F.FD << F.ratio() << F.ConsumedCalls << F.TotalCalls;

    diag(Call->getExprLoc(),
         "value consumed or checked in %0%% (%1 out of %2) of cases",
         DiagnosticIDs::Note)
        << F.ratio() << F.ConsumedCalls << F.TotalCalls;
  }
}

DiscardedReturnValueCheck::DiscardedReturnValueCheck(StringRef Name,
                                                     ClangTidyContext *Context)
    : ClangTidyCheck(Name, Context),
      ConsumeThreshold(Options.get("ConsumeThreshold", 80)) {}

void DiscardedReturnValueCheck::storeOptions(
    ClangTidyOptions::OptionMap &Opts) {
  Options.store(Opts, "ConsumeThreshold", ConsumeThreshold);
}

void DiscardedReturnValueCheck::onStartOfTranslationUnit() {
  CacheProjectDataLoadedSuccessfully.reset();
  ConsumedCalls.clear();
  FunctionInfos.clear();
  FunctionIDs.clear();
}

void DiscardedReturnValueCheck::onEndOfTranslationUnit() {
  // Once everything had been counted, emit the results.
  if (getPhase() == MultipassProjectPhase::Diagnose)
    for (const auto &Call : FunctionInfos)
      diagnose(Call.second);
}

namespace {

AST_MATCHER_P(DecltypeType, hasUnderlyingExpr,
              ast_matchers::internal::Matcher<Expr>, InnerMatcher) {
  return InnerMatcher.matches(*Node.getUnderlyingExpr(), Finder, Builder);
}

} // namespace

void DiscardedReturnValueCheck::registerMatchers(MatchFinder *Finder) {
  static const auto Call =
      callExpr(hasDeclaration(functionDecl(unless(anyOf(
                   returns(voidType()), hasAttr(attr::WarnUnusedResult))))),
               unless(cxxOperatorCallExpr(
                   unless(anyOf(hasAnyOverloadedOperatorName("()", "[]"),
                                allOf(hasAnyOverloadedOperatorName("*", "&"),
                                      argumentCountIs(1)))))))
          .bind(::Call);

  static const auto StaticAssert = staticAssertDecl(has(Call));
  static const auto VarDecl = decl(anyOf(varDecl(hasInitializer(Call)),
                                         fieldDecl(hasInClassInitializer(Call)),
                                         enumConstantDecl(has(Call))));
  static const auto CtorInits =
      cxxConstructorDecl(forEachConstructorInitializer(withInitializer(Call)));

  static const auto ExplicitCast = explicitCastExpr(hasSourceExpression(Call));
  static const auto New =
      expr(anyOf(initListExpr(forEach(Call)), cxxNewExpr(forEach(Call))));
  static const auto Delete = cxxDeleteExpr(has(Call));
  static const auto Argument = expr(anyOf(
      invocation(forEach(Call)), cxxUnresolvedConstructExpr(forEach(Call)),
      parenListExpr(forEach(Call))));
  static const auto Unary = expr(anyOf(
      unaryOperator(hasUnaryOperand(Call)), unaryExprOrTypeTraitExpr(has(Call)),
      arraySubscriptExpr(hasIndex(Call)),
      cxxOperatorCallExpr(hasUnaryOperand(Call))));
  static const auto Dereference = expr(
      anyOf(arraySubscriptExpr(hasBase(Call)), unresolvedMemberExpr(has(Call)),
            memberExpr(hasObjectExpression(Call))));
  static const auto BinaryLHS =
      anyOf(binaryOperator(hasLHS(Call)),
            conditionalOperator(hasTrueExpression(Call)),
            binaryConditionalOperator(hasTrueExpression(Call)));
  static const auto BinaryRHS =
      anyOf(binaryOperator(hasRHS(Call)),
            conditionalOperator(hasFalseExpression(Call)),
            binaryConditionalOperator(hasFalseExpression(Call)));
  static const auto If =
      anyOf(ifStmt(hasCondition(Call)), conditionalOperator(hasCondition(Call)),
            binaryConditionalOperator(hasCondition(Call)));
  static const auto While =
      stmt(anyOf(whileStmt(hasCondition(Call)), doStmt(hasCondition(Call))));
  static const auto For = anyOf(
      forStmt(eachOf(hasLoopInit(findAll(Call)), hasCondition(findAll(Call)),
                     hasIncrement(findAll(Call)))),
      cxxForRangeStmt(hasRangeInit(findAll(Call))));
  static const auto Switch = switchStmt(hasCondition(Call));
  static const auto Return = returnStmt(hasReturnValue(Call));

  static const auto Decltype = decltypeType(hasUnderlyingExpr(Call));
  static const auto TemplateArg =
      templateSpecializationType(forEachTemplateArgument(isExpr(Call)));
  static const auto VLA = variableArrayType(hasSizeExpr(Call));

  // Match consumed (using the return value) function calls.
  Finder->addMatcher(
      traverse(
          TK_IgnoreUnlessSpelledInSource,
          stmt(eachOf(ExplicitCast, New, Delete, Argument, Unary, Dereference,
                      BinaryLHS, BinaryRHS, If, While, For, Switch, Return)))
          .bind(Consume),
      this);
  Finder->addMatcher(traverse(TK_IgnoreUnlessSpelledInSource,
                              decl(eachOf(StaticAssert, VarDecl, CtorInits)))
                         .bind(Consume),
                     this);
  Finder->addMatcher(traverse(TK_IgnoreUnlessSpelledInSource,
                              type(eachOf(Decltype, TemplateArg, VLA)))
                         .bind(Consume),
                     this);

  // Matches discarded function calls.
  Finder->addMatcher(traverse(TK_IgnoreUnlessSpelledInSource, Call), this);
}

void DiscardedReturnValueCheck::matchResult(
    const MatchFinder::MatchResult &Result, bool ShouldCount) {
  const auto *CE = Result.Nodes.getNodeAs<CallExpr>(Call);
  assert(CE && "Bad matcher");

  // There are two matchers registered, one that matches the larger tree that
  // contains the consume of the call's result, and the other that matches every
  // call.
  const void *ConsumeNode = nullptr;
  if (const auto *D = Result.Nodes.getNodeAs<Decl>(Consume))
    ConsumeNode = D;
  if (const auto *S = Result.Nodes.getNodeAs<Stmt>(Consume))
    ConsumeNode = S;
  if (const auto *T = Result.Nodes.getNodeAs<Type>(Consume))
    ConsumeNode = T;
  if (ConsumeNode)
    return registerCall(CE, CE->getDirectCallee(), ShouldCount, ConsumeNode);

  // If ConsumeNode is left to be nullptr, the current match is not through the
  // "consuming" matcher. It might be the only match of this function (and then
  // it is discarded), or it might have been matched earlier and consumed.
  if (ConsumedCalls.find(CE) == ConsumedCalls.end())
    return registerCall(CE, CE->getDirectCallee(), ShouldCount, nullptr);
}

void DiscardedReturnValueCheck::collect(
    const ast_matchers::MatchFinder::MatchResult &Result) {
  matchResult(Result, true);
}

void DiscardedReturnValueCheck::check(const MatchFinder::MatchResult &Result) {
  // check() might be called without previous data existing if Tidy is executed
  // in single-phase mode.
  if (!CacheProjectDataLoadedSuccessfully.hasValue()) {
    StringRef ProjectLevelData = getCompactedDataPath();
    bool Success = false;
    if (!ProjectLevelData.empty() && FunctionInfos.empty())
      Success = loadYAML(ProjectLevelData, FunctionInfos);

    CacheProjectDataLoadedSuccessfully.emplace(Success);
  }
  // If there was previously loaded data, the current match callback should
  // not invalidate the counters we just loaded.
  bool ShouldCountMatches = !CacheProjectDataLoadedSuccessfully.getValue();

  matchResult(Result, ShouldCountMatches);
}

void DiscardedReturnValueCheck::postCollect(StringRef OutputFilename) {
  writeYAML("postCollect()", yamlize(FunctionInfos), OutputFilename);
}

void DiscardedReturnValueCheck::compact(
    const std::vector<std::string> &PerTUCollectedData, StringRef OutputFile) {
  for (const std::string &PerTUFilename : PerTUCollectedData)
    if (!loadYAML(PerTUFilename, FunctionInfos))
      llvm::errs()
          << "DiscardedReturnValueCheck: Failed to load compact() input file "
          << PerTUFilename << ".\n";

  writeYAML("compact()", yamlize(FunctionInfos), OutputFile);
}

} // namespace misc
} // namespace tidy
} // namespace clang
