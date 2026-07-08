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
#include "llvm/Plugins/PassPlugin.h"
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

/// Build a splatted byte-vector constant from a single per-field bit pattern.
///
/// A carrier byte packs 8/\p FieldBits fields of \p FieldBits bits each.
/// \p FieldPattern gives the desired bits *within one field* (only its low
/// \p FieldBits are used); the pattern is replicated across every field of a
/// byte, and that byte is splatted across all lanes of \p CarrierTy. This is
/// the one width-parameterized mask primitive shared by every field-aware
/// handler (add's carry masks, the shift boundary masks, ...):
///
///   N=1: pattern 0b1    -> 0xFF        pattern 0b0    -> 0x00
///   N=2: pattern 0b10   -> 0xAA        pattern 0b01   -> 0x55
///   N=4: pattern 0b1000 -> 0x88        pattern 0b0111 -> 0x77
static Constant *splatFieldPattern(IRBuilder<> &B, FixedVectorType *CarrierTy,
                                   unsigned FieldBits, unsigned FieldPattern) {
  unsigned FieldsPerByte = 8 / FieldBits;
  uint8_t Field = FieldPattern & ((1u << FieldBits) - 1);
  uint8_t Byte = 0;
  for (unsigned I = 0; I < FieldsPerByte; ++I)
    Byte |= Field << (I * FieldBits);
  return ConstantVector::getSplat(CarrierTy->getElementCount(),
                                  ConstantInt::get(B.getInt8Ty(), Byte));
}

/// and/or/xor: bit-independent, so the carrier body is the identical opcode on
/// bytes. (`not` arrives as `xor %a, -1` and rides this same handler.)
static Value *lowerBitwise(CarrierOp &Op, ArrayRef<Value *> Ops) {
  return Op.B.CreateBinOp(static_cast<Instruction::BinaryOps>(Op.Opcode),
                          Ops[0], Ops[1]);
}

/// SWAR add for i4 and i2 fields packed into a byte-vector carrier.
///
/// See the file-level comment for the derivation. The two masks are chosen by
/// field width:
///   i4 (FieldBits == 4): hmask = 0x88, lmask = 0x77
///   i2 (FieldBits == 2): hmask = 0xAA, lmask = 0x55
///
/// i1 add never reaches this handler; tryLower re-maps it to Xor first.
static Value *lowerAdd(CarrierOp &Op, ArrayRef<Value *> Ops) {
  IRBuilder<> &B = Op.B;
  unsigned N = Op.FieldBits;

  // Per-field masks via the width-parameterized builder:
  //   HMask = top bit of each field   (i4 -> 0x88, i2 -> 0xAA)
  //   LMask = everything below it     (i4 -> 0x77, i2 -> 0x55)
  Value *HMask = splatFieldPattern(B, Op.CarrierTy, N, 1u << (N - 1));
  Value *LMask = splatFieldPattern(B, Op.CarrierTy, N, (1u << (N - 1)) - 1);

  // Mask off the top bit of every field in each operand.
  Value *ALow = B.CreateAnd(Ops[0], LMask, "add.alo");
  Value *BLow = B.CreateAnd(Ops[1], LMask, "add.blo");

  // add; no carry can escape because the top bit was cleared.
  Value *Sum = B.CreateAdd(ALow, BLow, "add.sum");

  // Restore the top bits. The top bit of each field is a 1-bit add,
  // i.e. XOR of the two input top bits.
  Value *TopXor = B.CreateXor(Ops[0], Ops[1], "add.topxor");
  Value *TopBits = B.CreateAnd(TopXor, HMask, "add.topbits");

  return B.CreateXor(Sum, TopBits, "add.result");
}

/// Byte-lane shift scaffolding for shl/lshr (U2-A skeleton).
///
/// tryLower has already bitcast both operands to the byte carrier, so the two
/// structural steps live here: shift the carrier with the same opcode, then AND
/// with a per-field *boundary mask* that confines each field's bits to its own
/// lane. A whole-byte shift lets bits cross a field boundary into a neighbor
/// (e.g. an i4 shl by 1 spills bit 3 into bit 4); the boundary mask clears the
/// bits that crossed so each field behaves as an independent narrow shift.
///
/// The boundary mask below is a *placeholder*: the all-ones (identity) field
/// mask, built via splatFieldPattern to prove the primitive is wired into this
/// path. It confines nothing yet -- the correct mask depends on the shift
/// amount s (shl must clear the low s bits of each field, lshr the high s bits)
/// and, together with per-field/variable-amount handling, is what Track B
/// (U2-B) derives and drops in here. Until then the numeric result is not yet
/// correct (the shl/lshr differential tests stay XFAIL); the skeleton only has
/// to compile and expose the shift-then-mask shape.
static Value *lowerShift(CarrierOp &Op, ArrayRef<Value *> Ops) {
  IRBuilder<> &B = Op.B;
  unsigned N = Op.FieldBits;

  Value *Shifted =
      B.CreateBinOp(static_cast<Instruction::BinaryOps>(Op.Opcode), Ops[0],
                    Ops[1], "shift.raw");

  // TODO(Track B / U2-B): replace the identity mask with the amount-dependent
  // boundary mask (shl -> clear low s bits per field; lshr -> clear high s
  // bits per field).
  Value *BoundaryMask =
      splatFieldPattern(B, Op.CarrierTy, N, (1u << N) - 1);

  return B.CreateAnd(Shifted, BoundaryMask, "shift.confined");
}

/// Map an opcode to its handler, or nullptr if Nybbler does not lower it.
/// Registering a new operation is a single case here plus its handler function.
///
/// Note: i1 `add` is re-mapped to Xor before dispatch (see tryLower) because
/// 1-bit addition is exactly XOR; it falls through to lowerBitwise.
static CarrierHandler getHandler(unsigned Opcode) {
  switch (Opcode) {
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:
    return lowerBitwise;
  case Instruction::Add:
    return lowerAdd;
  case Instruction::Shl:
  case Instruction::LShr:
    return lowerShift;
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

  unsigned Opcode = BO->getOpcode();

  auto *FieldTy = dyn_cast<FixedVectorType>(BO->getType());
  if (!FieldTy || !isNarrowFieldVector(FieldTy))
    return false;

  // i1 add is XOR (addition mod 2 == XOR); re-map before handler lookup so
  // it falls through to the existing lowerBitwise path with no extra logic.
  unsigned FieldBits = FieldTy->getScalarSizeInBits();
  if (Opcode == Instruction::Add && FieldBits == 1)
    Opcode = Instruction::Xor;

  CarrierHandler Handler = getHandler(Opcode);
  if (!Handler)
    return false;

  // total = K * N. Skip when not a whole number of bytes (no padding in Slice 1).
  unsigned Total = FieldTy->getNumElements() * FieldBits;
  if (Total % 8 != 0)
    return false;

  LLVM_DEBUG(dbgs() << "nybbler: lowering " << *BO << "\n");

  IRBuilder<> B(BO);
  auto *CarrierTy = FixedVectorType::get(B.getInt8Ty(), Total / 8);

  Value *LHS = B.CreateBitCast(BO->getOperand(0), CarrierTy);
  Value *RHS = B.CreateBitCast(BO->getOperand(1), CarrierTy);

  CarrierOp Op{B, FieldTy, CarrierTy, FieldBits, Opcode};
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
    if (getHandler(I.getOpcode()) ||
        // Also collect i1 add before the Xor remap happens.
        (I.getOpcode() == Instruction::Add &&
         isNarrowFieldVector(I.getType()) &&
         cast<FixedVectorType>(I.getType())->getScalarSizeInBits() == 1))
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
