//===--- DiscardedReturnValueCheck.h - clang-tidy ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MISC_DISCARDEDRETURNVALUECHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MISC_DISCARDEDRETURNVALUECHECK_H

#include "../ClangTidyCheck.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include <cinttypes>

namespace clang {
namespace tidy {
namespace misc {

/// Flags function calls which return value is discarded if most of the
/// other calls of the function consume the return value.
///
/// For the user-facing documentation see:
/// http://clang.llvm.org/extra/clang-tidy/checks/misc-discarded-return-value.html
class DiscardedReturnValueCheck : public ClangTidyCheck {
public:
  struct FunctionInfo {
    std::size_t ConsumedCalls;
    std::size_t TotalCalls;
    llvm::SmallPtrSet<const CallExpr *, 32> DiscardedCEs;

    /// Returns ConsumedCalls / TotalCalls expressed as a whole percentage.
    std::uint8_t ratio() const;
  };
  using FunctionMapTy = llvm::DenseMap<const FunctionDecl *, FunctionInfo>;

  DiscardedReturnValueCheck(StringRef Name, ClangTidyContext *Context);
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void storeOptions(ClangTidyOptions::OptionMap &Opts) override;
  void onStartOfTranslationUnit() override;
  void onEndOfTranslationUnit() override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;

private:
  /// The threshold (expressed as a whole percentage) the calls to a function
  /// must be consumed for discarded calls to be warned about. A threshold of
  /// 80 means that if 8 calls out of 10 calls (80%) to a function is consumed,
  /// the remaining 2 will be warned.
  const std::uint8_t ConsumeThreshold;

  /// Stores AST nodes which we have observed to be consuming calls.
  /// (This is a helper data structure to prevent matchers matching consuming
  /// contexts firing multiple times and messing up the statistics created.)
  llvm::DenseMap<const CallExpr *, SmallVector<const void *, 2>> ConsumedCalls;

  FunctionMapTy CallMap;

  void registerCall(const CallExpr *CE, const FunctionDecl *FD,
                    const void *ConsumingContext);
  void diagnose(const FunctionDecl *FD, const FunctionInfo &F);
};

} // namespace misc
} // namespace tidy
} // namespace clang

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MISC_DISCARDEDRETURNVALUECHECK_H
