//===- DynamicPass.cpp - Implementation of a dynamic configurable pass ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a configurable pass that can apply patterns liberally
// and be plugged in a pass pipeline.
//
//===----------------------------------------------------------------------===//

#include "PassDetail.h"
#include "mlir/Analysis/SliceAnalysis.h"
#include "mlir/Dialect/Affine/IR/AffineOps.h"
#include "mlir/Dialect/Linalg/IR/LinalgOps.h"
#include "mlir/Dialect/Linalg/IR/LinalgTypes.h"
#include "mlir/Dialect/Linalg/Passes.h"
#include "mlir/Dialect/Linalg/Transforms/Hoisting.h"
#include "mlir/Dialect/Linalg/Transforms/Transforms.h"
#include "mlir/Dialect/Linalg/Utils/Utils.h"
#include "mlir/Dialect/SCF/Transforms.h"
#include "mlir/Dialect/Tensor/IR/Tensor.h"
#include "mlir/Dialect/Vector/VectorTransforms.h"
#include "mlir/IR/AffineExpr.h"
#include "mlir/IR/AffineMap.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Support/LLVM.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"
#include "mlir/Transforms/LoopUtils.h"
#include "mlir/Transforms/Passes.h"
#include "mlir/Transforms/Utils.h"

using namespace mlir;
using namespace mlir::vector;
using namespace linalg;

namespace {

/// Configurable pass to apply pattern-based tiling and fusion.
struct LinalgStrategyTileAndFusePass
    : public LinalgStrategyTileAndFusePassBase<LinalgStrategyTileAndFusePass> {

  LinalgStrategyTileAndFusePass() = default;

  LinalgStrategyTileAndFusePass(StringRef opName,
                                LinalgTilingAndFusionOptions opt,
                                LinalgTransformationFilter filt)
      : options(opt), filter(filt) {
    this->anchorOpName.setValue(opName.str());
  }

  void runOnFunction() override {
    auto funcOp = getFunction();
    if (!anchorFuncName.empty() && funcOp.getName() != anchorFuncName)
      return;

    RewritePatternSet tilingAndFusionPattern(funcOp.getContext());
    if (!anchorOpName.empty()) {
      tilingAndFusionPattern.add<LinalgTileAndFuseTensorOpsPattern>(
          anchorOpName, funcOp.getContext(), options, filter);
    } else {
      tilingAndFusionPattern.add<LinalgTileAndFuseTensorOpsPattern>(
          funcOp.getContext(), options, filter);
    }
    // Search the root operation using bottom up traversal.
    GreedyRewriteConfig config;
    config.useTopDownTraversal = false;
    (void)applyPatternsAndFoldGreedily(
        funcOp, std::move(tilingAndFusionPattern), config);
  }

  LinalgTilingAndFusionOptions options;
  LinalgTransformationFilter filter;
};

/// Configurable pass to apply pattern-based linalg tiling.
struct LinalgStrategyTilePass
    : public LinalgStrategyTilePassBase<LinalgStrategyTilePass> {

  LinalgStrategyTilePass() = default;

  LinalgStrategyTilePass(StringRef opName, LinalgTilingOptions opt,
                         LinalgTransformationFilter filt)
      : options(opt), filter(filt) {
    this->anchorOpName.setValue(opName.str());
  }

  void runOnFunction() override {
    auto funcOp = getFunction();
    if (!anchorFuncName.empty() && funcOp.getName() != anchorFuncName)
      return;

    RewritePatternSet tilingPattern(funcOp.getContext());
    if (!anchorOpName.empty()) {
      tilingPattern.add<LinalgGenericTilingPattern>(
          anchorOpName, funcOp.getContext(), options, filter);
    } else {
      tilingPattern.add<LinalgGenericTilingPattern>(funcOp.getContext(), filter,
                                                    options);
    }
    (void)applyPatternsAndFoldGreedily(funcOp, std::move(tilingPattern));
  }

  LinalgTilingOptions options;
  LinalgTransformationFilter filter;
};

/// Configurable pass to apply hoisting and padding.
struct LinalgStrategyPadPass
    : public LinalgStrategyPadPassBase<LinalgStrategyPadPass> {

  LinalgStrategyPadPass() = default;

  LinalgStrategyPadPass(StringRef opName, LinalgPaddingOptions opt,
                        LinalgTransformationFilter filt)
      : options(opt), filter(filt) {
    this->anchorOpName.setValue(opName.str());
  }

  void runOnFunction() override {
    auto funcOp = getFunction();
    if (!anchorFuncName.empty() && funcOp.getName() != anchorFuncName)
      return;

    RewritePatternSet paddingPattern(funcOp.getContext());
    if (!anchorOpName.empty()) {
      paddingPattern.add<LinalgPaddingPattern>(
          anchorOpName, funcOp.getContext(), options, filter);
    } else {
      paddingPattern.add<LinalgPaddingPattern>(funcOp.getContext(), options,
                                               filter);
    }
    // Traverse the operations top down to pad producers before consumers. The
    // extract slice operation introduced after every padded operation enables
    // padding its consumers. Padding an operation whose producers have not been
    // padded before fails due to the missing extract slice operations. In this
    // case, the padding pattern increments the transformation marker without
    // padding the operation. The top down traversal is thus not only a
    // performance optimization but also needed to pad all operations along the
    // use-def chains.
    GreedyRewriteConfig config;
    config.useTopDownTraversal = true;
    if (failed(applyPatternsAndFoldGreedily(funcOp, std::move(paddingPattern),
                                            config)))
      signalPassFailure();
  }

  LinalgPaddingOptions options;
  LinalgTransformationFilter filter;
};

/// Configurable pass to apply pattern-based linalg generalization.
struct LinalgStrategyGeneralizePass
    : public LinalgStrategyGeneralizePassBase<LinalgStrategyGeneralizePass> {

  LinalgStrategyGeneralizePass() = default;

  LinalgStrategyGeneralizePass(StringRef opName,
                               LinalgTransformationFilter filter)
      : filter(filter) {
    this->anchorOpName.setValue(opName.str());
  }

  void runOnFunction() override {
    auto funcOp = getFunction();
    if (!anchorFuncName.empty() && funcOp.getName() != anchorFuncName)
      return;

    RewritePatternSet generalizationPattern(funcOp.getContext());
    if (!anchorOpName.empty()) {
      generalizationPattern.add<LinalgGeneralizationPattern>(
          anchorOpName, funcOp.getContext(), filter);
    } else {
      generalizationPattern.add<LinalgGeneralizationPattern>(
          funcOp.getContext(), filter);
    }
    if (failed(applyPatternsAndFoldGreedily(funcOp,
                                            std::move(generalizationPattern))))
      signalPassFailure();
  }

  LinalgTransformationFilter filter;
};

/// Configurable pass to apply lowering of coarser-grained named linalg ops into
/// finer-grained named versions.
struct LinalgStrategyDecomposePass
    : public LinalgStrategyDecomposePassBase<LinalgStrategyDecomposePass> {

  LinalgStrategyDecomposePass() = default;

  LinalgStrategyDecomposePass(LinalgTransformationFilter filter)
      : filter(filter) {}

  void runOnFunction() override {
    auto funcOp = getFunction();
    if (!anchorFuncName.empty() && funcOp.getName() != anchorFuncName)
      return;
    RewritePatternSet decompositionPattern(funcOp.getContext());
    populateDecomposeConvolutionPatterns(decompositionPattern, filter);
    if (failed(applyPatternsAndFoldGreedily(funcOp,
                                            std::move(decompositionPattern))))
      signalPassFailure();
  }

  LinalgTransformationFilter filter;
};

/// Configurable pass to apply pattern-based linalg generalization.
struct LinalgStrategyInterchangePass
    : public LinalgStrategyInterchangePassBase<LinalgStrategyInterchangePass> {

  LinalgStrategyInterchangePass() = default;

  LinalgStrategyInterchangePass(ArrayRef<int64_t> iteratorInterchange,
                                LinalgTransformationFilter filter)
      : iteratorInterchange(iteratorInterchange.begin(),
                            iteratorInterchange.end()),
        filter(filter) {}

  void runOnFunction() override {
    auto funcOp = getFunction();
    if (!anchorFuncName.empty() && funcOp.getName() != anchorFuncName)
      return;

    SmallVector<unsigned> interchangeVector(iteratorInterchange.begin(),
                                            iteratorInterchange.end());
    RewritePatternSet interchangePattern(funcOp.getContext());
    interchangePattern.add<GenericOpInterchangePattern>(
        funcOp.getContext(), interchangeVector, filter);
    if (failed(applyPatternsAndFoldGreedily(funcOp,
                                            std::move(interchangePattern))))
      signalPassFailure();
  }

  SmallVector<int64_t> iteratorInterchange;
  LinalgTransformationFilter filter;
};

/// Configurable pass to apply pattern-based linalg promotion.
struct LinalgStrategyPromotePass
    : public LinalgStrategyPromotePassBase<LinalgStrategyPromotePass> {

  LinalgStrategyPromotePass() = default;

  LinalgStrategyPromotePass(StringRef opName, LinalgPromotionOptions opt,
                            LinalgTransformationFilter filt)
      : options(opt), filter(filt) {
    this->anchorOpName.setValue(opName.str());
  }

  void runOnFunction() override {
    auto funcOp = getFunction();
    if (!anchorFuncName.empty() && funcOp.getName() != anchorFuncName)
      return;

    RewritePatternSet promotionPattern(funcOp.getContext());
    if (!anchorOpName.empty()) {
      promotionPattern.add<LinalgBasePromotionPattern>(
          anchorOpName, funcOp.getContext(), options, filter);
    } else {
      promotionPattern.add<LinalgBasePromotionPattern>(funcOp.getContext(),
                                                       filter, options);
    }
    (void)applyPatternsAndFoldGreedily(funcOp, std::move(promotionPattern));
  }

  LinalgPromotionOptions options;
  LinalgTransformationFilter filter;
};

/// Configurable pass to apply pattern-based linalg vectorization.
struct LinalgStrategyVectorizePass
    : public LinalgStrategyVectorizePassBase<LinalgStrategyVectorizePass> {

  LinalgStrategyVectorizePass() = default;

  LinalgStrategyVectorizePass(StringRef opName, LinalgVectorizationOptions opt,
                              LinalgTransformationFilter filt,
                              bool padVectorize = false)
      : options(opt), filter(filt) {
    this->anchorOpName.setValue(opName.str());
    this->vectorizePadding.setValue(padVectorize);
  }

  void runOnFunction() override {
    auto funcOp = getFunction();
    if (!anchorFuncName.empty() && funcOp.getName() != anchorFuncName)
      return;

    RewritePatternSet vectorizationPatterns(funcOp.getContext());
    if (!anchorOpName.empty()) {
      vectorizationPatterns.add<LinalgVectorizationPattern>(
          anchorOpName, funcOp.getContext(), options, filter);
    } else {
      vectorizationPatterns.add<LinalgVectorizationPattern>(funcOp.getContext(),
                                                            filter, options);
    }
    vector::populateVectorTransferPermutationMapLoweringPatterns(
        vectorizationPatterns);
    vector::populateVectorReductionToContractPatterns(vectorizationPatterns);
    vectorizationPatterns.add<linalg::LinalgCopyVTRForwardingPattern,
                              linalg::LinalgCopyVTWForwardingPattern>(
        funcOp.getContext(), /*benefit=*/2);
    if (vectorizePadding) {
      linalg::populatePadTensorOpVectorizationPatterns(vectorizationPatterns);
    }
    (void)applyPatternsAndFoldGreedily(funcOp,
                                       std::move(vectorizationPatterns));
  }

  LinalgVectorizationOptions options;
  LinalgTransformationFilter filter;
};

/// Configurable pass to enable the application of other pattern-based linalg
/// passes.
struct LinalgStrategyEnablePass
    : public LinalgStrategyEnablePassBase<LinalgStrategyEnablePass> {

  LinalgStrategyEnablePass(LinalgEnablingOptions opt,
                           LinalgTransformationFilter filt)
      : options(opt), filter(filt) {}

  void runOnFunction() override {
    auto funcOp = getFunction();
    if (!anchorFuncName.empty() && funcOp.getName() != anchorFuncName)
      return;

    MLIRContext *context = funcOp.getContext();
    RewritePatternSet patterns =
        linalg::getLinalgTilingCanonicalizationPatterns(context);
    scf::populateSCFForLoopCanonicalizationPatterns(patterns);
    if (failed(applyPatternsAndFoldGreedily(funcOp, std::move(patterns))))
      return signalPassFailure();

    if (options.licm) {
      if (funcOp
              ->walk([&](LoopLikeOpInterface loopLike) {
                if (failed(moveLoopInvariantCode(loopLike)))
                  return WalkResult::interrupt();
                return WalkResult::advance();
              })
              .wasInterrupted())
        return signalPassFailure();
    }

    promoteSingleIterationLoops(funcOp);
    if (options.hoistRedundantVectorTransfers)
      hoistRedundantVectorTransfers(funcOp);

    if (options.hoistRedundantVectorTransfersOnTensor)
      hoistRedundantVectorTransfersOnTensor(funcOp);

    // Run CSE to cleanup after canonicalization.
    OpPassManager dynamicPM("builtin.func");
    dynamicPM.addPass(createCSEPass());
    if (failed(runPipeline(dynamicPM, funcOp)))
      return signalPassFailure();
  }

  LinalgEnablingOptions options;
  LinalgTransformationFilter filter;
};

/// Configurable pass to lower vector operations.
struct LinalgStrategyLowerVectorsPass
    : public LinalgStrategyLowerVectorsPassBase<
          LinalgStrategyLowerVectorsPass> {

  LinalgStrategyLowerVectorsPass(LinalgVectorLoweringOptions opt,
                                 LinalgTransformationFilter filt)
      : options(opt), filter(filt) {}

  void runOnFunction() override {
    auto funcOp = getFunction();
    if (!anchorFuncName.empty() && funcOp.getName() != anchorFuncName)
      return;

    MLIRContext *context = funcOp.getContext();
    RewritePatternSet patterns(context);
    vector::populateVectorToVectorCanonicalizationPatterns(patterns);
    // In a progressive lowering of vectors, this would be the 1st step.
    if (options.contractionLowering) {
      patterns.add<ContractionOpToOuterProductOpLowering,
                   ContractionOpToMatmulOpLowering, ContractionOpLowering>(
          options.vectorTransformOptions, context);
      vector::populateVectorTransferPermutationMapLoweringPatterns(patterns);
    }
    // In a progressive lowering of vectors, this would be the 2nd step.
    if (options.multiReductionLowering) {
      vector::populateVectorMultiReductionLoweringPatterns(
          patterns,
          options.vectorTransformOptions.vectorMultiReductionLowering);
    }
    // In a progressive lowering of vectors, this would be the 3rd step.
    if (options.transferPartialRewrite) {
      patterns.add<vector::VectorTransferFullPartialRewriter>(
          context, options.vectorTransformOptions);
    }
    // In a progressive lowering of vectors, this would be the 4th step.
    if (options.transferLowering) {
      vector::populateVectorTransferLoweringPatterns(patterns,
                                                     options.maxTransferRank);
    }
    // In a progressive lowering of vectors, this would be the 5th step.
    if (options.transferToSCFConversion) {
      populateVectorToSCFConversionPatterns(
          patterns, options.vectorTransferToSCFOptions.setTargetRank(
                        options.maxTransferRank));
    }
    // In a progressive lowering of vectors, this would be the 6th step.
    if (options.shapeCastLowering) {
      vector::populateVectorShapeCastLoweringPatterns(patterns);
    }
    // In a progressive lowering of vectors, this would be the 7th step.
    if (options.transposeLowering) {
      vector::populateVectorTransposeLoweringPatterns(
          patterns, options.vectorTransformOptions);
      if (options.avx2Lowering)
        x86vector::avx2::populateSpecializedTransposeLoweringPatterns(
            patterns, options.avx2LoweringOptions, /*benefit=*/10);
    }
    (void)applyPatternsAndFoldGreedily(funcOp, std::move(patterns));
  }

  LinalgVectorLoweringOptions options;
  LinalgTransformationFilter filter;
};

/// Configurable pass to lower vector operations.
struct LinalgStrategyRemoveMarkersPass
    : public LinalgStrategyRemoveMarkersPassBase<
          LinalgStrategyRemoveMarkersPass> {

  void runOnFunction() override {
    auto funcOp = getFunction();
    if (!anchorFuncName.empty() && funcOp.getName() != anchorFuncName)
      return;
    funcOp.walk([](LinalgOp op) {
      op->removeAttr(LinalgTransforms::kLinalgTransformMarker);
    });
  }
};
} // namespace

/// Create a LinalgStrategyTileAndFusePass.
std::unique_ptr<OperationPass<FuncOp>>
mlir::createLinalgStrategyTileAndFusePass(StringRef opName,
                                          LinalgTilingAndFusionOptions options,
                                          LinalgTransformationFilter filter) {
  return std::make_unique<LinalgStrategyTileAndFusePass>(opName, options,
                                                         filter);
}

/// Create a LinalgStrategyTilePass.
std::unique_ptr<OperationPass<FuncOp>>
mlir::createLinalgStrategyTilePass(StringRef opName, LinalgTilingOptions opt,
                                   LinalgTransformationFilter filter) {
  return std::make_unique<LinalgStrategyTilePass>(opName, opt, filter);
}

/// Create a LinalgStrategyPadPass.
std::unique_ptr<OperationPass<FuncOp>>
mlir::createLinalgStrategyPadPass(StringRef opName, LinalgPaddingOptions opt,
                                  LinalgTransformationFilter filter) {
  return std::make_unique<LinalgStrategyPadPass>(opName, opt, filter);
}

/// Create a LinalgStrategyPromotePass.
std::unique_ptr<OperationPass<FuncOp>>
mlir::createLinalgStrategyPromotePass(StringRef opName,
                                      LinalgPromotionOptions opt,
                                      LinalgTransformationFilter filter) {
  return std::make_unique<LinalgStrategyPromotePass>(opName, opt, filter);
}

/// Create a LinalgStrategyGeneralizePass.
std::unique_ptr<OperationPass<FuncOp>>
mlir::createLinalgStrategyGeneralizePass(StringRef opName,
                                         LinalgTransformationFilter filter) {
  return std::make_unique<LinalgStrategyGeneralizePass>(opName, filter);
}

/// Create a LinalgStrategyDecomposePass.
// TODO: if/when we need finer control add an `opName` parameter.
std::unique_ptr<OperationPass<FuncOp>>
mlir::createLinalgStrategyDecomposePass(LinalgTransformationFilter filter) {
  return std::make_unique<LinalgStrategyDecomposePass>(filter);
}

/// Create a LinalgStrategyInterchangePass.
std::unique_ptr<OperationPass<FuncOp>>
mlir::createLinalgStrategyInterchangePass(ArrayRef<int64_t> iteratorInterchange,
                                          LinalgTransformationFilter filter) {
  return std::make_unique<LinalgStrategyInterchangePass>(iteratorInterchange,
                                                         filter);
}

/// Create a LinalgStrategyVectorizePass.
std::unique_ptr<OperationPass<FuncOp>> mlir::createLinalgStrategyVectorizePass(
    StringRef opName, LinalgVectorizationOptions opt,
    LinalgTransformationFilter filter, bool padVectorize) {
  return std::make_unique<LinalgStrategyVectorizePass>(opName, opt, filter,
                                                       padVectorize);
}

/// Create a LinalgStrategyEnablePass.
std::unique_ptr<OperationPass<FuncOp>>
mlir::createLinalgStrategyEnablePass(LinalgEnablingOptions opt,
                                     LinalgTransformationFilter filter) {
  return std::make_unique<LinalgStrategyEnablePass>(opt, filter);
}

/// Create a LinalgStrategyLowerVectorsPass.
std::unique_ptr<OperationPass<FuncOp>>
mlir::createLinalgStrategyLowerVectorsPass(LinalgVectorLoweringOptions opt,
                                           LinalgTransformationFilter filter) {
  return std::make_unique<LinalgStrategyLowerVectorsPass>(opt, filter);
}

/// Create a LinalgStrategyRemoveMarkersPass.
std::unique_ptr<OperationPass<FuncOp>>
mlir::createLinalgStrategyRemoveMarkersPass() {
  return std::make_unique<LinalgStrategyRemoveMarkersPass>();
}
