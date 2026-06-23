//===- Nybbler.cpp - Narrow-field vector lowering pass --------------------===//
//
// Slice 1: lower narrow-field (i1/i2/i4) vertical *bitwise* vector ops
// (and/or/xor) into byte-vector carrier ops.
//
// Lowering rule (spec section 3) for %r = <op> <K x iN> %a, %b:
//   1. total = K * N.
//   2. If total % 8 != 0, skip (leave to the default legalizer).
//   3. bitcast each operand from <K x iN> to <total/8 x i8>.
//   4. re-emit the same opcode on the carrier operands.
//   5. bitcast the result back to <K x iN>, replaceAllUsesWith, erase original.
//
// Correct by construction: and/or/xor act on each bit independently and never
// move a bit across a field boundary, so reinterpreting the same packed bits as
// a byte vector yields bit-identical per-field results. No masking needed.
//
//===----------------------------------------------------------------------===//

#include "Nybbler.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

#define DEBUG_TYPE "nybbler"

namespace nybbler {

bool isNarrowFieldVector(Type *T) {
  auto *VT = dyn_cast<FixedVectorType>(T);
  if (!VT)
    return false;
  auto *ElemTy = dyn_cast<IntegerType>(VT->getElementType());
  if (!ElemTy)
    return false;
  unsigned Bits = ElemTy->getBitWidth();
  return Bits == 1 || Bits == 2 || Bits == 4;
}

/// Returns true iff \p BO is a narrow-field bitwise op (and/or/xor) whose total
/// bit width is a multiple of 8 -- i.e. one this slice can lower.
static bool isLowerableBitwise(BinaryOperator *BO) {
  switch (BO->getOpcode()) {
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:
    break;
  default:
    return false;
  }

  auto *VT = dyn_cast<FixedVectorType>(BO->getType());
  if (!VT || !isNarrowFieldVector(VT))
    return false;

  // total = K * N. Skip when not a whole number of bytes (no padding in Slice 1).
  unsigned Total = VT->getNumElements() * VT->getScalarSizeInBits();
  return Total % 8 == 0;
}

/// Rewrite a single lowerable op into bitcast -> byte-op -> bitcast-back.
static void lowerBitwise(BinaryOperator *BO) {
  auto *VT = cast<FixedVectorType>(BO->getType());
  unsigned Total = VT->getNumElements() * VT->getScalarSizeInBits();

  IRBuilder<> B(BO);
  Type *CarrierTy =
      FixedVectorType::get(B.getInt8Ty(), Total / 8); // <total/8 x i8>

  Value *LHS = B.CreateBitCast(BO->getOperand(0), CarrierTy);
  Value *RHS = B.CreateBitCast(BO->getOperand(1), CarrierTy);
  Value *Byte = B.CreateBinOp(BO->getOpcode(), LHS, RHS);
  Value *Result = B.CreateBitCast(Byte, VT);

  BO->replaceAllUsesWith(Result);
  BO->eraseFromParent();
}

PreservedAnalyses NybblerPass::run(Function &F, FunctionAnalysisManager &) {
  // Collect first to avoid invalidating the iterator while we rewrite/erase.
  SmallVector<BinaryOperator *, 16> Worklist;
  for (Instruction &I : instructions(F))
    if (auto *BO = dyn_cast<BinaryOperator>(&I))
      if (isLowerableBitwise(BO))
        Worklist.push_back(BO);

  if (Worklist.empty())
    return PreservedAnalyses::all();

  for (BinaryOperator *BO : Worklist) {
    LLVM_DEBUG(dbgs() << "nybbler: lowering " << *BO << "\n");
    lowerBitwise(BO);
  }

  // We changed the IR; conservatively invalidate analyses.
  return PreservedAnalyses::none();
}

} // namespace nybbler

//===----------------------------------------------------------------------===//
// New Pass Manager plugin registration.
//===----------------------------------------------------------------------===//

llvm::PassPluginLibraryInfo getNybblerPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "Nybbler", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "nybbler") {
                    FPM.addPass(nybbler::NybblerPass());
                    return true;
                  }
                  return false;
                });
          }};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getNybblerPluginInfo();
}
