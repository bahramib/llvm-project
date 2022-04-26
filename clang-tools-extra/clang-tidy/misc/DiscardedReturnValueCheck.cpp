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

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tidy::misc;

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
                                             const void *ConsumingContext) {
  assert(CE && FD);
  FD = FD->getCanonicalDecl();
  FunctionInfo &Data =
      CallMap.try_emplace(FD, FunctionInfo{0, 0, {}}).first->second;

  if (ConsumingContext) {
    using ConsumeVecTy = decltype(ConsumedCalls)::mapped_type;
    ConsumeVecTy &ConsumeVec =
        ConsumedCalls.try_emplace(CE, ConsumeVecTy{}).first->second;
    if (llvm::find(ConsumeVec, ConsumingContext) != ConsumeVec.end())
      return;
    ConsumeVec.emplace_back(ConsumingContext);
  } else
    Data.DiscardedCEs.insert(CE);

  if (ConsumingContext)
    ++Data.ConsumedCalls;
  ++Data.TotalCalls;
}

void DiscardedReturnValueCheck::diagnose(const FunctionDecl *FD,
                                         const FunctionInfo &F) {
  if (F.ratio() < ConsumeThreshold)
    return;

  for (const CallExpr *Call : F.DiscardedCEs) {
    diag(Call->getExprLoc(),
         "return value of %0 is used in most calls, but not in this one",
         DiagnosticIDs::Warning)
        << FD << F.ratio() << F.ConsumedCalls << F.TotalCalls;

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
  ConsumedCalls.clear();
  CallMap.clear();
}

void DiscardedReturnValueCheck::onEndOfTranslationUnit() {
  // Once everything had been counted, emit the results.
  for (const auto &Call : CallMap)
    diagnose(Call.first, Call.second);
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

void DiscardedReturnValueCheck::check(const MatchFinder::MatchResult &Result) {
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
    return registerCall(CE, CE->getDirectCallee(), ConsumeNode);

  // If ConsumeNode is left to be nullptr, the current match is not through the
  // "consuming" matcher. It might be the only match of this function (and then
  // it is discarded), or it might have been matched earlier and consumed.
  if (ConsumedCalls.find(CE) == ConsumedCalls.end())
    return registerCall(CE, CE->getDirectCallee(), nullptr);
}

} // namespace misc
} // namespace tidy
} // namespace clang
