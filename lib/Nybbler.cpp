//===- Nybbler.cpp - Narrow-field vector lowering pass --------------------===//
//
// Lowers narrow-field (i1/i2/i4) vertical vector ops into byte-vector carrier
// ops. Covers: bitwise (and/or/xor), add, sub, shl, lshr.
//
// The carrier pattern (spec section 3) is the same for every operation:
//   1. total = K * N.
//   2. If total % 8 != 0, widen each operand to the next byte boundary by
//      appending zero lanes (K -> K' with K' * N % 8 == 0).
//   3. bitcast each (padded) operand from <K' x iN> to the carrier type
//      <K'*N/8 x i8>.
//   4. emit the operation's body on the carrier operands.
//   5. bitcast the result back to <K' x iN>, narrow to the original K lanes
//      (dropping the pad lanes), replaceAllUsesWith, erase original.
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
/// (insertion point already at the original instruction), the field vector type
/// \c FieldTy (<K' x iN>, already padded to a byte multiple if the original
/// lane count wasn't one), the byte carrier type \c CarrierTy (<K'*N/8 x i8>),
/// the field width \c FieldBits (N), and the original opcode (so handlers
/// shared across several opcodes, like bitwise, can branch on it).
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
/// For each power-of-two step s = 1, 2, ..., 2^(N-1):
///   - extract whether each field's amount has that bit set
///   - conditionally apply a carrier shift by s with a boundary mask
///   - blend shifted / unshifted per field
///
/// Shift-amount model: each field's amount is its full N-bit value; amounts
/// >= N shift every bit out, yielding 0. That out-of-range behaviour is our
/// choice, not something the scalar reference pins down: `shl/lshr iN x, amt`
/// with amt >= N is poison, and lli's observed result varies by target and
/// LLVM build (0 on some, amt % N on others -- it has flipped across CI
/// updates). The differential kernels therefore mask amounts into [0, N-1]
/// in-kernel, and only in-range amounts are verified against the reference.
///
/// i1 is special-cased to identity: its only in-range amount is 0.
static Value *lowerShift(CarrierOp &Op, ArrayRef<Value *> Ops) {
  IRBuilder<> &B = Op.B;
  unsigned N = Op.FieldBits;
  FixedVectorType *CT = Op.CarrierTy;
  bool IsShl = (Op.Opcode == Instruction::Shl);

  if (N == 1)
    return Ops[0];

  Value *FieldMask = splatFieldPattern(B, CT, N, (1u << N) - 1u);
  Value *LowBit    = splatFieldPattern(B, CT, N, 1u);
  Value *Zero      = splatFieldPattern(B, CT, N, 0u);
  Value *Amt = Ops[1]; // per-field amount = the field's full N-bit value

  Value *Cur = Ops[0];
  // Barrel shift over every amount bit J (step S = 2^J) for J in [0, N). A step
  // with S >= N clears the field, so amounts >= N correctly produce 0.
  for (unsigned S = 1, J = 0; S < (1u << N); S <<= 1, ++J) {
    // Isolate amount bit J into each field's bit 0. A carrier-level lshr by J
    // brings it down; ANDing LowBit drops any bleed from adjacent fields.
    Value *AmtBit = (J == 0)
        ? Amt
        : B.CreateLShr(Amt,
              ConstantVector::getSplat(CT->getElementCount(),
                  ConstantInt::get(B.getInt8Ty(), J)),
              "shift.amtshr");
    Value *HasStep = B.CreateAnd(AmtBit, LowBit, "shift.hasstep");

    // Expand each field's low bit 0/1 -> 0x0/all-ones. A carrier-wide subtract
    // (0 - bit) would let borrows bleed across packed fields, so instead smear
    // bit 0 up to the top of its field via in-field shifts: bit 0 shifted left
    // by < N stays inside the field, so no cross-field bleed and no masking.
    Value *StepSel = HasStep;
    for (unsigned K = 1; K < N; K <<= 1)
      StepSel = B.CreateOr(StepSel,
          B.CreateShl(StepSel,
              ConstantVector::getSplat(CT->getElementCount(),
                  ConstantInt::get(B.getInt8Ty(), K))),
          "shift.sel");

    // Bits surviving an in-field shift by S: shl keeps the top (N-S), lshr
    // keeps the bottom (N-S). S >= N keeps nothing -> the field is cleared.
    unsigned BPat = IsShl ? (((1u << N) - 1u) & ~((1u << S) - 1u))
                          : (S < N ? ((1u << (N - S)) - 1u) : 0u);
    Value *Taken;
    if (BPat == 0) {
      Taken = Zero; // shifting by S >= N clears the field
    } else {
      Value *BMask = splatFieldPattern(B, CT, N, BPat);
      Value *Shifted = B.CreateBinOp(
          static_cast<Instruction::BinaryOps>(Op.Opcode), Cur,
          ConstantVector::getSplat(CT->getElementCount(),
              ConstantInt::get(B.getInt8Ty(), S)),
          "shift.raw");
      Value *ShiftedM = B.CreateAnd(Shifted, BMask, "shift.confined");
      Taken = B.CreateAnd(ShiftedM, StepSel, "shift.taken");
    }

    // Per-field blend: (shifted & sel) | (unshifted & ~sel).
    Value *NotSel = B.CreateAnd(
        B.CreateXor(StepSel, FieldMask, "shift.notsel_raw"),
        FieldMask, "shift.notsel");
    Cur = B.CreateOr(Taken, B.CreateAnd(Cur, NotSel, "shift.kept"),
                     "shift.blend");
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

  LLVM_DEBUG(dbgs() << "nybbler: lowering " << *BO << "\n");

  IRBuilder<> B(BO);

  // Pad non-byte-multiple vectors to the next byte boundary by appending zero
  // lanes, run the handler on the padded carrier, and drop the pad lanes on
  // the way back. Zero pad fields are inert for every handler: the field masks
  // are per-byte splats so they already cover pad fields; add/sub confine
  // carries/borrows within each field, and a zero field neither generates one
  // nor lets one escape into the adjacent real field; shift steps operate on
  // each field independently (the boundary masks confine every carrier shift),
  // so a pad lane's value can never reach a real lane. Whatever junk the pad
  // fields do compute is discarded by the narrowing shuffle.
  unsigned K = FieldTy->getNumElements();
  unsigned FieldsPerByte = 8 / FieldBits;
  unsigned PaddedK = (K + FieldsPerByte - 1) / FieldsPerByte * FieldsPerByte;

  Value *LHS = BO->getOperand(0);
  Value *RHS = BO->getOperand(1);
  auto *PaddedTy = FieldTy;
  if (PaddedK != K) {
    PaddedTy = FixedVectorType::get(FieldTy->getElementType(), PaddedK);
    // Widen by shuffling with a zero vector: lanes >= K select lane K, i.e.
    // lane 0 of the zero operand.
    SmallVector<int, 16> WidenMask;
    for (unsigned J = 0; J < PaddedK; ++J)
      WidenMask.push_back(J < K ? J : K);
    Value *Zero = Constant::getNullValue(FieldTy);
    LHS = B.CreateShuffleVector(LHS, Zero, WidenMask, "pad.widen.lhs");
    RHS = B.CreateShuffleVector(RHS, Zero, WidenMask, "pad.widen.rhs");
  }

  auto *CarrierTy =
      FixedVectorType::get(B.getInt8Ty(), PaddedK * FieldBits / 8);

  Value *CarrierLHS = B.CreateBitCast(LHS, CarrierTy);
  Value *CarrierRHS = B.CreateBitCast(RHS, CarrierTy);

  CarrierOp Op{B, PaddedTy, CarrierTy, FieldBits, Opcode};
  Value *Carrier = Handler(Op, {CarrierLHS, CarrierRHS});

  Value *Result = B.CreateBitCast(Carrier, PaddedTy);
  if (PaddedK != K) {
    SmallVector<int, 16> NarrowMask;
    for (unsigned J = 0; J < K; ++J)
      NarrowMask.push_back(J);
    Result = B.CreateShuffleVector(Result, NarrowMask, "pad.narrow");
  }
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
