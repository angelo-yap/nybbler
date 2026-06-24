//===- Nybbler.cpp - Narrow-field vector lowering pass --------------------===//
//
// Lowers narrow-field (i1/i2/i4) vertical vector ops into byte-vector carrier
// ops. Slice 1 implements the *bitwise* operations (and/or/xor).
//
// The carrier pattern (spec section 3) is the same for every operation:
//   1. total = K * N.
//   2. If total % 8 != 0, skip (leave to the default legalizer; no padding yet).
//   3. bitcast each operand from <K x iN> to the carrier type <total/8 x i8>.
//   4. emit the operation's body on the carrier operands.
//   5. bitcast the result back to <K x iN>, replaceAllUsesWith, erase original.
//
// Only step 4 differs between operations. So that pattern lives once in the
// dispatch engine (tryLower), and each operation is a single *handler* function
// keyed by opcode in getHandler. Adding a future operation (add, sub, shifts,
// ...) means writing one handler and registering its opcode -- nothing else.
//
// Bitwise is the proof of the path: and/or/xor act on each bit independently and
// never move a bit across a field boundary, so reinterpreting the same packed
// bits as a byte vector and re-emitting the identical opcode yields bit-
// identical per-field results. No masking needed -- the handler is one CreateBinOp.
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

//===----------------------------------------------------------------------===//
// Carrier dispatch
//
// Everything common to every operation lives in tryLower(); everything specific
// to one operation lives in a CarrierHandler. A handler receives the operands
// already bitcast to the byte-vector carrier plus the field geometry it needs
// (via CarrierOp), and returns the carrier-typed result. tryLower handles the
// bitcasts in, the bitcast back, replaceAllUsesWith, and erase.
//===----------------------------------------------------------------------===//

namespace {

/// Per-instruction context handed to a CarrierHandler. Carries the live builder
/// (insertion point already at the original instruction), the original field
/// vector type \c FieldTy (<K x iN>), the byte carrier type \c CarrierTy
/// (<total/8 x i8>), the field width \c FieldBits (N), and the original opcode
/// (so handlers shared across several opcodes, like bitwise, can branch on it).
struct CarrierOp {
  IRBuilder<> &B;
  FixedVectorType *FieldTy;
  FixedVectorType *CarrierTy;
  unsigned FieldBits;
  unsigned Opcode;
};

/// Lowers one operation's body on the byte carrier. \p Ops are the operands
/// already bitcast to \c Op.CarrierTy; the return is the carrier-typed result.
using CarrierHandler = Value *(*)(CarrierOp &Op, ArrayRef<Value *> Ops);

/// and/or/xor: bit-independent, so the carrier body is the identical opcode on
/// bytes. (`not` arrives as `xor %a, -1` and rides this same handler.)
static Value *lowerBitwise(CarrierOp &Op, ArrayRef<Value *> Ops) {
  return Op.B.CreateBinOp(static_cast<Instruction::BinaryOps>(Op.Opcode),
                          Ops[0], Ops[1]);
}

/// Map an opcode to its handler, or nullptr if Nybbler does not lower it.
/// Registering a new operation is a single case here plus its handler function.
static CarrierHandler getHandler(unsigned Opcode) {
  switch (Opcode) {
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:
    return lowerBitwise;
  default:
    return nullptr;
  }
}

} // namespace

/// Try to lower \p I through the carrier path. Returns true iff it was rewritten.
static bool tryLower(Instruction &I) {
  auto *BO = dyn_cast<BinaryOperator>(&I);
  if (!BO)
    return false;

  CarrierHandler Handler = getHandler(BO->getOpcode());
  if (!Handler)
    return false;

  auto *FieldTy = dyn_cast<FixedVectorType>(BO->getType());
  if (!FieldTy || !isNarrowFieldVector(FieldTy))
    return false;

  // total = K * N. Skip when not a whole number of bytes (no padding in Slice 1).
  unsigned Total = FieldTy->getNumElements() * FieldTy->getScalarSizeInBits();
  if (Total % 8 != 0)
    return false;

  LLVM_DEBUG(dbgs() << "nybbler: lowering " << *BO << "\n");

  IRBuilder<> B(BO);
  auto *CarrierTy = FixedVectorType::get(B.getInt8Ty(), Total / 8);

  Value *LHS = B.CreateBitCast(BO->getOperand(0), CarrierTy);
  Value *RHS = B.CreateBitCast(BO->getOperand(1), CarrierTy);

  CarrierOp Op{B, FieldTy, CarrierTy, FieldTy->getScalarSizeInBits(),
               BO->getOpcode()};
  Value *Carrier = Handler(Op, {LHS, RHS});

  Value *Result = B.CreateBitCast(Carrier, FieldTy);
  BO->replaceAllUsesWith(Result);
  BO->eraseFromParent();
  return true;
}

PreservedAnalyses NybblerPass::run(Function &F, FunctionAnalysisManager &) {
  // Collect first to avoid invalidating the iterator while we rewrite/erase.
  SmallVector<Instruction *, 16> Worklist;
  for (Instruction &I : instructions(F))
    if (getHandler(I.getOpcode()))
      Worklist.push_back(&I);

  bool Changed = false;
  for (Instruction *I : Worklist)
    Changed |= tryLower(*I);

  // We changed the IR; conservatively invalidate analyses.
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
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
