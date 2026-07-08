//===- Nybbler.cpp - Narrow-field vector lowering pass --------------------===//
//
// Lowers narrow-field (i1/i2/i4) vertical vector ops into byte-vector carrier
// ops. Covers: bitwise (and/or/xor), add, sub, shl, lshr.
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
// keyed by opcode in getHandler. Adding a future operation means writing one
// handler and registering its opcode -- nothing else.
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

/// SWAR add for i4/i2. i1 add is remapped to Xor before dispatch.
static Value *lowerAdd(CarrierOp &Op, ArrayRef<Value *> Ops) {
  IRBuilder<> &B = Op.B;
  unsigned N = Op.FieldBits;

  Value *HMask = splatFieldPattern(B, Op.CarrierTy, N, 1u << (N - 1));
  Value *LMask = splatFieldPattern(B, Op.CarrierTy, N, (1u << (N - 1)) - 1);

  Value *ALow    = B.CreateAnd(Ops[0], LMask, "add.alo");
  Value *BLow    = B.CreateAnd(Ops[1], LMask, "add.blo");
  Value *Sum     = B.CreateAdd(ALow, BLow, "add.sum");
  Value *TopXor  = B.CreateXor(Ops[0], Ops[1], "add.topxor");
  Value *TopBits = B.CreateAnd(TopXor, HMask, "add.topbits");
  return B.CreateXor(Sum, TopBits, "add.result");
}

/// SWAR sub for i4/i2. i1 sub is remapped to Xor before dispatch.
///
/// Dual of add: set the top bit of a as a borrow absorber so no borrow escapes
/// the field boundary. Formula:
///   result = ((a | hmask) - (b & lmask)) ^ hmask ^ ((a ^ b) & hmask)
///
/// Worked trace -- i4, lo=6-4=2, hi=5-3=2; byte a=0x56, b=0x34, hmask=0x88:
///   0xDE - 0x34 = 0xAA;  (a^b)&hmask = 0x00;  0xAA^0x88^0x00 = 0x22 ✓
static Value *lowerSub(CarrierOp &Op, ArrayRef<Value *> Ops) {
  IRBuilder<> &B = Op.B;
  unsigned N = Op.FieldBits;

  Value *HMask   = splatFieldPattern(B, Op.CarrierTy, N, 1u << (N - 1));
  Value *LMask   = splatFieldPattern(B, Op.CarrierTy, N, (1u << (N - 1)) - 1);
  Value *AHigh   = B.CreateOr(Ops[0],  HMask, "sub.ahi");
  Value *BLow    = B.CreateAnd(Ops[1], LMask, "sub.blo");
  Value *Raw     = B.CreateSub(AHigh, BLow, "sub.raw");
  Value *TopXor  = B.CreateXor(Ops[0], Ops[1], "sub.topxor");
  Value *TopBits = B.CreateAnd(TopXor, HMask, "sub.topbits");
  Value *Fix     = B.CreateXor(HMask, TopBits, "sub.fix");
  return B.CreateXor(Raw, Fix, "sub.result");
}

/// SWAR shl/lshr for i4/i2 via per-field bit-serial conditional shifts.
///
/// Shift-amount model: per-field variable amounts, masked to [0, N-1] via
/// `& (N-1)`. This deliberately defines results for out-of-range amounts
/// rather than propagating LLVM IR poison semantics, matching the differential
/// harness' scalar reference behaviour.
///
/// For each power-of-two step s = 1, 2, ..., N/2:
///   - extract whether each field's amount has bit s set
///   - conditionally apply a carrier shift by s with a boundary mask
///   - blend shifted / unshifted per field
///
/// i1 shl/lshr: only valid amount is 0 (identity); handler returns Ops[0].
static Value *lowerShift(CarrierOp &Op, ArrayRef<Value *> Ops) {
  IRBuilder<> &B = Op.B;
  unsigned N = Op.FieldBits;
  FixedVectorType *CT = Op.CarrierTy;
  bool IsShl = (Op.Opcode == Instruction::Shl);

  if (N == 1)
    return Ops[0];

  // Mask amounts to [0, N-1]: log2(N) low bits per field.
  unsigned AmtBits = (N == 2) ? 1 : 2;
  Value *Amt = B.CreateAnd(Ops[1],
      splatFieldPattern(B, CT, N, (1u << AmtBits) - 1), "shift.amt");

  Value *Cur = Ops[0];
  for (unsigned S = 1; S < N; S <<= 1) {
    // Does this field's amount have the bit corresponding to this step set?
    unsigned BitIdx = (S == 1) ? 0 : 1; // N is {2,4}; steps are 1 and (optionally) 2.
    Constant *BitIdxC = ConstantVector::getSplat(
        CT->getElementCount(), ConstantInt::get(B.getInt8Ty(), BitIdx));
    Value *AmtShr  = B.CreateLShr(Amt, BitIdxC, "shift.amtshr");
    Value *HasStep = B.CreateAnd(AmtShr, splatFieldPattern(B, CT, N, 1u), "shift.hasstep");
    // Expand to per-field all-ones / all-zeros select mask, with borrows confined
    // within each field (borrow absorber = per-field top bit).
    Value *HMask   = splatFieldPattern(B, CT, N, 1u << (N - 1));
    Value *StepSel = B.CreateXor(B.CreateSub(HMask, HasStep, "shift.sel_raw"), HMask, "shift.sel");

    // Boundary mask: shl keeps top (N-S) bits; lshr keeps bottom (N-S) bits.
    unsigned BPat = IsShl ? (((1u << N) - 1u) & ~((1u << S) - 1u))
                           : ((1u << (N - S)) - 1u);
    Value *BMask   = splatFieldPattern(B, CT, N, BPat);
    Constant *ShiftAmt = ConstantVector::getSplat(
      CT->getElementCount(),
      ConstantInt::get(B.getInt8Ty(), S)
    );
    Value *Shifted = B.CreateBinOp(
      static_cast<Instruction::BinaryOps>(Op.Opcode), Cur, ShiftAmt,
      "shift.raw"
    );
    Value *ShiftedM = B.CreateAnd(Shifted, BMask, "shift.confined");

    // Per-field blend.
    Value *NotSel  = B.CreateAnd(
        B.CreateXor(StepSel, splatFieldPattern(B, CT, N, (1u << N) - 1u)),
        splatFieldPattern(B, CT, N, (1u << N) - 1u), "shift.notsel");
    Cur = B.CreateOr(B.CreateAnd(ShiftedM, StepSel,  "shift.taken"),
                     B.CreateAnd(Cur,       NotSel,   "shift.kept"), "shift.blend");
  }
  return Cur;
}

static CarrierHandler getHandler(unsigned Opcode) {
  switch (Opcode) {
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:
    return lowerBitwise;
  case Instruction::Add:
    return lowerAdd;
  case Instruction::Sub:
    return lowerSub;
  case Instruction::Shl:
  case Instruction::LShr:
    return lowerShift;
  default:
    return nullptr;
  }
}

} // namespace

static bool tryLower(Instruction &I) {
  auto *BO = dyn_cast<BinaryOperator>(&I);
  if (!BO)
    return false;

  unsigned Opcode = BO->getOpcode();

  auto *FieldTy = dyn_cast<FixedVectorType>(BO->getType());
  if (!FieldTy || !isNarrowFieldVector(FieldTy))
    return false;

  unsigned FieldBits = FieldTy->getScalarSizeInBits();

  // i1 add and i1 sub are both XOR (arithmetic mod 2); remap before dispatch.
  if ((Opcode == Instruction::Add || Opcode == Instruction::Sub) && FieldBits == 1)
    Opcode = Instruction::Xor;

  CarrierHandler Handler = getHandler(Opcode);
  if (!Handler)
    return false;

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
  SmallVector<Instruction *, 16> Worklist;
  for (Instruction &I : instructions(F))
    if (getHandler(I.getOpcode()) ||
        // Collect i1 add/sub before the Xor remap happens.
        ((I.getOpcode() == Instruction::Add || I.getOpcode() == Instruction::Sub) &&
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
