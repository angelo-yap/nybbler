//===- Nybbler.cpp - Narrow-field vector lowering pass --------------------===//
//
// Lowers narrow-field (i1/i2/i4) vertical vector ops into byte-vector carrier
// ops. Covers bitwise (and/or/xor), arithmetic (add/sub), logical shifts
// (shl/lshr), ashr, and comparisons (eq/ne/ult/slt).
//
// The carrier pattern (spec section 3) is the same for every arithmetic/
// bitwise/shift operation:
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
// dispatch engine (tryLower/lowerNarrowOp), and each operation is a single
// *handler* function keyed by (opcode, predicate) in getHandler. Adding a
// future operation means writing one handler and registering it -- nothing
// else.
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
// Everything common to every operation lives in lowerNarrowOp(); everything
// specific to one operation lives in a CarrierHandler. A handler receives the
// operands already bitcast to the byte-vector carrier plus the field geometry
// it needs (via CarrierOp), and returns the carrier-typed result.
// lowerNarrowOp handles the bitcasts in, the bitcast/trunc back, narrowing,
// replaceAllUsesWith, and erase.
//===----------------------------------------------------------------------===//

namespace {

/// Per-instruction context handed to a CarrierHandler. Carries the live builder
/// (insertion point already at the original instruction), the field vector type
/// \c FieldTy (<K' x iN>, already padded to a byte multiple if the original
/// lane count wasn't one), the byte carrier type \c CarrierTy (<K'*N/8 x i8>),
/// the field width \c FieldBits (N), the original opcode (so handlers shared
/// across several opcodes, like bitwise, can branch on it), and -- for icmp
/// only -- the comparison predicate.
struct CarrierOp {
  IRBuilder<> &B;
  FixedVectorType *FieldTy;
  FixedVectorType *CarrierTy;
  unsigned FieldBits;
  unsigned Opcode;
  ICmpInst::Predicate Predicate = ICmpInst::BAD_ICMP_PREDICATE;
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

/// Splat a per-byte-lane shift/rotate-amount constant (e.g. for CreateLShr /
/// CreateShl amount operands, which need one scalar per carrier byte lane
/// rather than a per-field pattern).
static Constant *splatAmount(IRBuilder<> &B, FixedVectorType *CarrierTy,
                             unsigned Amt) {
  return ConstantVector::getSplat(CarrierTy->getElementCount(),
                                  ConstantInt::get(B.getInt8Ty(), Amt));
}

/// Reduce each field of \p X down to a single "any bit set?" bit and
/// broadcast that bit across the whole field (all-ones if set, all-zeros if
/// not). Shared by eq/ne (reducing a xor) and available for reuse anywhere
/// else a boolean-per-field result needs expanding to a full-field mask.
///
/// Degenerates correctly at N=1: both loops below don't execute, so the
/// function is the identity, and eq/ne fall out of lowerEqNe as xnor/xor.
static Value *reduceAndBroadcastField(IRBuilder<> &B, FixedVectorType *CarrierTy,
                                      unsigned N, Value *X) {
  Value *FieldMask = splatFieldPattern(B, CarrierTy, N, (1u << N) - 1u);
  Value *Cur = B.CreateAnd(X, FieldMask, "reduce.masked");

  // OR-reduce toward bit 0 of each field. After an lshr by S, only the
  // bottom (N-S) bits of each field are legitimately this field's own data;
  // the top S bits were shifted in from the neighboring field, so mask them
  // off before folding in (same boundary-mask idea as lshr's confine step).
  for (unsigned S = 1; S < N; S <<= 1) {
    Value *Shifted = B.CreateLShr(Cur, splatAmount(B, CarrierTy, S), "reduce.shr");
    Value *KeepMask = splatFieldPattern(B, CarrierTy, N, (1u << (N - S)) - 1u);
    Cur = B.CreateOr(Cur, B.CreateAnd(Shifted, KeepMask, "reduce.shrm"),
                     "reduce.or");
  }

  // Bit 0 of each field now holds the OR of all its bits. Smear it back up
  // across the field (mirrors the StepSel smear used in lowerShift).
  Value *Bit0 = B.CreateAnd(Cur, splatFieldPattern(B, CarrierTy, N, 1u),
                            "reduce.bit0");
  Value *Bcast = Bit0;
  for (unsigned K = 1; K < N; K <<= 1)
    Bcast = B.CreateOr(Bcast, B.CreateShl(Bcast, splatAmount(B, CarrierTy, K)),
                       "reduce.bcast");
  return Bcast;
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
/// Shift-amount model: each field carries its own amount, and the lowering
/// masks each field amount into [0, N-1] before the barrel-shift sequence.
/// That keeps the carrier path aligned with the scalar reference for the
/// in-range cases the harness differentiates, while leaving the poison cases
/// (amounts >= N) out of the differential contract.
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
  Value *AmtMask   = splatFieldPattern(B, CT, N, N - 1u);
  Value *Amt       = B.CreateAnd(Ops[1], AmtMask, "shift.amtmasked");

  Value *Cur = Ops[0];
  // Barrel shift over every amount bit J (step S = 2^J) for J in [0, N). A step
  // with S >= N clears the field, so amounts >= N correctly produce 0.
  for (unsigned S = 1, J = 0; S < (1u << N); S <<= 1, ++J) {
    // Isolate amount bit J into each field's bit 0. A carrier-level lshr by J
    // brings it down; ANDing LowBit drops any bleed from adjacent fields.
    Value *AmtBit = (J == 0)
        ? Amt
        : B.CreateLShr(Amt, splatAmount(B, CT, J), "shift.amtshr");
    Value *HasStep = B.CreateAnd(AmtBit, LowBit, "shift.hasstep");

    // Expand each field's low bit 0/1 -> 0x0/all-ones. A carrier-wide subtract
    // (0 - bit) would let borrows bleed across packed fields, so instead smear
    // bit 0 up to the top of its field via in-field shifts: bit 0 shifted left
    // by < N stays inside the field, so no cross-field bleed and no masking.
    Value *StepSel = HasStep;
    for (unsigned K = 1; K < N; K <<= 1)
      StepSel = B.CreateOr(StepSel,
          B.CreateShl(StepSel, splatAmount(B, CT, K)), "shift.sel");

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
          splatAmount(B, CT, S), "shift.raw");
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

/// SWAR ashr for i4/i2, via the same per-field bit-serial conditional shift
/// skeleton as lowerShift (same variable-per-field-amount model, same
/// masked-to-[0,N-1] out-of-range handling -- decisions settled in U2-B,
/// reused here rather than re-derived), but the bits vacated at the high end
/// of each field are filled with that field's *sign* bit instead of zero.
///
/// The sign bit is computed once up front and smeared across the whole field
/// (mirror of lowerUlt's top-bit smear-down), then ORed into the vacated high
/// S bits at each step after confining the shifted-in bits to the low (N-S).
///
/// i1 is special-cased to identity: its only in-range amount is 0 (matching
/// the i1 shl/lshr special case).
static Value *lowerAshr(CarrierOp &Op, ArrayRef<Value *> Ops) {
  IRBuilder<> &B = Op.B;
  unsigned N = Op.FieldBits;
  FixedVectorType *CT = Op.CarrierTy;

  if (N == 1)
    return Ops[0];

  Value *FieldMask = splatFieldPattern(B, CT, N, (1u << N) - 1u);
  Value *LowBit    = splatFieldPattern(B, CT, N, 1u);
  Value *HMask     = splatFieldPattern(B, CT, N, 1u << (N - 1));
  Value *Amt = Ops[1];

  // Sign bit of each field, smeared across the whole field.
  Value *SignBit = B.CreateAnd(Ops[0], HMask, "ashr.sign");
  Value *SignBcast = SignBit;
  for (unsigned S = 1; S < N; S <<= 1)
    SignBcast = B.CreateOr(SignBcast,
        B.CreateLShr(SignBcast, splatAmount(B, CT, S)), "ashr.signbcast");

  Value *Cur = Ops[0];
  for (unsigned S = 1, J = 0; S < (1u << N); S <<= 1, ++J) {
    Value *AmtBit = (J == 0)
        ? Amt
        : B.CreateLShr(Amt, splatAmount(B, CT, J), "ashr.amtshr");
    Value *HasStep = B.CreateAnd(AmtBit, LowBit, "ashr.hasstep");

    Value *StepSel = HasStep;
    for (unsigned K = 1; K < N; K <<= 1)
      StepSel = B.CreateOr(StepSel,
          B.CreateShl(StepSel, splatAmount(B, CT, K)), "ashr.sel");

    // Bits surviving an in-field lshr by S: the bottom (N-S), same as lshr.
    // S >= N: no original bits survive, the whole field becomes sign fill.
    unsigned BPat = (S < N) ? ((1u << (N - S)) - 1u) : 0u;
    Value *Taken;
    if (BPat == 0) {
      Taken = B.CreateAnd(SignBcast, StepSel, "ashr.allsign");
    } else {
      unsigned FillPat = ((1u << N) - 1u) & ~BPat; // vacated top S bits
      Value *BMask = splatFieldPattern(B, CT, N, BPat);
      Value *FillMask = splatFieldPattern(B, CT, N, FillPat);
      Value *Shifted = B.CreateLShr(Cur, splatAmount(B, CT, S), "ashr.raw");
      Value *ShiftedM = B.CreateAnd(Shifted, BMask, "ashr.confined");
      Value *Fill = B.CreateAnd(SignBcast, FillMask, "ashr.fill");
      Value *Filled = B.CreateOr(ShiftedM, Fill, "ashr.filled");
      Taken = B.CreateAnd(Filled, StepSel, "ashr.taken");
    }

    Value *NotSel = B.CreateAnd(
        B.CreateXor(StepSel, FieldMask, "ashr.notsel_raw"),
        FieldMask, "ashr.notsel");
    Cur = B.CreateOr(Taken, B.CreateAnd(Cur, NotSel, "ashr.kept"),
                     "ashr.blend");
  }
  return Cur;
}

/// icmp eq/ne: xor the operands, then reduce+broadcast to an all-ones (ne) or
/// all-zeros (eq) per-field mask. `eq` is the bitwise complement of `ne`.
/// Degenerates correctly at i1 (see reduceAndBroadcastField).
static Value *lowerEqNe(CarrierOp &Op, ArrayRef<Value *> Ops) {
  IRBuilder<> &B = Op.B;
  Value *Xor = B.CreateXor(Ops[0], Ops[1], "eqne.xor");
  Value *Ne = reduceAndBroadcastField(B, Op.CarrierTy, Op.FieldBits, Xor);
  if (Op.Predicate == ICmpInst::ICMP_EQ)
    return B.CreateNot(Ne, "eq.result");
  return Ne;
}

/// icmp ult (i4/i2): classic SWAR unsigned-compare-by-subtraction. Reuses the
/// borrow-absorber shape from lowerSub (top bit of a forced to 1, top bit of
/// b forced to 0, so the borrow can't escape the field), then inspects the
/// per-field top bit of `raw ^ b ^ ~a`, which is 1 iff a <u b in that field.
/// Smears that top bit down across the whole field to get an all-ones/
/// all-zeros per-field mask.
///
/// Not valid at N=1 (HMask/LMask degenerate and discard the operands
/// entirely) -- i1 ult/slt are remapped to single-bit logic before dispatch,
/// so this handler is never invoked with FieldBits == 1.
static Value *lowerUlt(CarrierOp &Op, ArrayRef<Value *> Ops) {
  IRBuilder<> &B = Op.B;
  unsigned N = Op.FieldBits;
  FixedVectorType *CT = Op.CarrierTy;

  Value *HMask = splatFieldPattern(B, CT, N, 1u << (N - 1));
  Value *LMask = splatFieldPattern(B, CT, N, (1u << (N - 1)) - 1);

  Value *AHigh = B.CreateOr(Ops[0], HMask, "ult.ahi");
  Value *BLow  = B.CreateAnd(Ops[1], LMask, "ult.blo");
  Value *Raw   = B.CreateSub(AHigh, BLow, "ult.raw");

  Value *NotRaw = B.CreateNot(Raw, "ult.notraw");
  Value *NotA   = B.CreateNot(Ops[0], "ult.nota");
  Value *Xor    = B.CreateXor(Ops[0], Ops[1], "ult.xor");
  Value *NotXor = B.CreateNot(Xor, "ult.notxor");
  Value *T1     = B.CreateAnd(NotA, Ops[1], "ult.t1");
  Value *T2     = B.CreateAnd(NotXor, NotRaw, "ult.t2");
  Value *T      = B.CreateOr(T1, T2, "ult.t");
  Value *Top    = B.CreateAnd(T, HMask, "ult.top");

  // Smear the top bit down across the field (mirror of the bit0 smear-up
  // used in lowerShift/reduceAndBroadcastField).
  Value *Cur = Top;
  for (unsigned S = 1; S < N; S <<= 1)
    Cur = B.CreateOr(Cur, B.CreateLShr(Cur, splatAmount(B, CT, S), "ult.smear"),
                     "ult.or");
  return Cur;
}

/// icmp slt (i4/i2): signed-to-unsigned trick -- flip the sign bit of both
/// operands, then run the same unsigned-compare logic as lowerUlt.
/// Not valid at N=1; i1 slt is remapped before dispatch (see lowerUlt).
static Value *lowerSlt(CarrierOp &Op, ArrayRef<Value *> Ops) {
  IRBuilder<> &B = Op.B;
  Value *HMask = splatFieldPattern(B, Op.CarrierTy, Op.FieldBits,
                                   1u << (Op.FieldBits - 1));
  Value *A  = B.CreateXor(Ops[0], HMask, "slt.flipa");
  Value *Bv = B.CreateXor(Ops[1], HMask, "slt.flipb");
  return lowerUlt(Op, {A, Bv});
}

static CarrierHandler getHandler(unsigned Opcode, ICmpInst::Predicate Pred) {
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
  case Instruction::AShr:
    return lowerAshr;
  case Instruction::ICmp:
    switch (Pred) {
    case ICmpInst::ICMP_EQ:
    case ICmpInst::ICMP_NE:
      return lowerEqNe;
    case ICmpInst::ICMP_ULT:
      return lowerUlt;
    case ICmpInst::ICMP_SLT:
      return lowerSlt;
    default:
      return nullptr; // other predicates out of scope
    }
  default:
    return nullptr;
  }
}

/// Shared carrier-dispatch body for both BinaryOperator and ICmpInst callers.
/// \p FieldTy is always the *operand* field type <K x iN>. For arithmetic/
/// shift ops the instruction's own result type is <K x iN> too; for icmp the
/// result type is <K x i1>, so \p IsCompare selects the extra trunc step that
/// narrows each all-ones/all-zeros iN field down to a single i1 lane.
static bool lowerNarrowOp(Instruction &I, unsigned Opcode,
                         ICmpInst::Predicate Pred, Value *LHSOperand,
                         Value *RHSOperand, FixedVectorType *FieldTy,
                         bool IsCompare) {
  CarrierHandler Handler = getHandler(Opcode, Pred);
  if (!Handler)
    return false;

  LLVM_DEBUG(dbgs() << "nybbler: lowering " << I << "\n");

  IRBuilder<> B(&I);
  unsigned FieldBits = FieldTy->getScalarSizeInBits();

  // Pad non-byte-multiple vectors to the next byte boundary by appending zero
  // lanes, run the handler on the padded carrier, and drop the pad lanes on
  // the way back. Zero pad fields are inert for every handler: the field
  // masks are per-byte splats so they already cover pad fields; add/sub
  // confine carries/borrows within each field, shift/ashr steps confine
  // every carrier shift to within a field, and icmp's reduce/broadcast and
  // compare steps are likewise per-field -- so a pad lane's value can never
  // reach a real lane. Whatever junk the pad fields compute is discarded by
  // the narrowing shuffle.
  unsigned K = FieldTy->getNumElements();
  unsigned FieldsPerByte = 8 / FieldBits;
  unsigned PaddedK = (K + FieldsPerByte - 1) / FieldsPerByte * FieldsPerByte;

  Value *LHS = LHSOperand;
  Value *RHS = RHSOperand;
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

  CarrierOp Op{B, PaddedTy, CarrierTy, FieldBits, Opcode, Pred};
  Value *Carrier = Handler(Op, {CarrierLHS, CarrierRHS});

  Value *Result = B.CreateBitCast(Carrier, PaddedTy);

  if (IsCompare) {
    // Every field is all-ones or all-zeros; truncating each iN lane to i1
    // yields the correct boolean without any extra masking.
    auto *BoolTy = FixedVectorType::get(B.getInt1Ty(), PaddedK);
    Result = B.CreateTrunc(Result, BoolTy, "cmp.trunc");
  }

  if (PaddedK != K) {
    SmallVector<int, 16> NarrowMask;
    for (unsigned J = 0; J < K; ++J)
      NarrowMask.push_back(J);
    Result = B.CreateShuffleVector(Result, NarrowMask, "pad.narrow");
  }

  I.replaceAllUsesWith(Result);
  I.eraseFromParent();
  return true;
}

} // namespace

static bool tryLower(Instruction &I) {
  if (auto *BO = dyn_cast<BinaryOperator>(&I)) {
    unsigned Opcode = BO->getOpcode();

    auto *FieldTy = dyn_cast<FixedVectorType>(BO->getType());
    if (!FieldTy || !isNarrowFieldVector(FieldTy))
      return false;

    unsigned FieldBits = FieldTy->getScalarSizeInBits();

    // i1 add and i1 sub are both XOR (arithmetic mod 2); remap before
    // dispatch.
    if ((Opcode == Instruction::Add || Opcode == Instruction::Sub) &&
        FieldBits == 1)
      Opcode = Instruction::Xor;

    return lowerNarrowOp(I, Opcode, ICmpInst::BAD_ICMP_PREDICATE,
                        BO->getOperand(0), BO->getOperand(1), FieldTy,
                        /*IsCompare=*/false);
  }

  if (auto *CI = dyn_cast<ICmpInst>(&I)) {
    Value *LHS = CI->getOperand(0);
    Value *RHS = CI->getOperand(1);

    auto *FieldTy = dyn_cast<FixedVectorType>(LHS->getType());
    if (!FieldTy || !isNarrowFieldVector(FieldTy))
      return false;

    ICmpInst::Predicate Pred = CI->getPredicate();
    if (Pred != ICmpInst::ICMP_EQ && Pred != ICmpInst::ICMP_NE &&
        Pred != ICmpInst::ICMP_ULT && Pred != ICmpInst::ICMP_SLT)
      return false; // other predicates out of U3-B's scope

    unsigned FieldBits = FieldTy->getScalarSizeInBits();

    // i1 comparisons are trivial. eq/ne fall out of the general lowering
    // correctly (reduceAndBroadcastField degenerates to identity at N=1), but
    // ult/slt do NOT: their formula relies on separate high/low masks that
    // collapse to HMask=all-ones, LMask=all-zeros at N=1, which discards `a`
    // entirely and computes plain xor instead of a real less-than. So ult/slt
    // are remapped to single-bit logic before dispatch, mirroring the i1
    // add/sub -> xor remap above.
    if (FieldBits == 1 &&
        (Pred == ICmpInst::ICMP_ULT || Pred == ICmpInst::ICMP_SLT)) {
      IRBuilder<> B(CI);
      Value *Result;
      if (Pred == ICmpInst::ICMP_ULT)
        // 0 <u 1 is the only true case: ult(a,b) = ~a & b.
        Result = B.CreateAnd(B.CreateNot(LHS, "ult.i1.nota"), RHS,
                             "ult.i1.result");
      else
        // Bit=1 represents the only negative i1 value (-1). -1 <s 0 is the
        // only true case: slt(a,b) = a & ~b.
        Result = B.CreateAnd(LHS, B.CreateNot(RHS, "slt.i1.notb"),
                             "slt.i1.result");
      CI->replaceAllUsesWith(Result);
      CI->eraseFromParent();
      return true;
    }

    return lowerNarrowOp(I, Instruction::ICmp, Pred, LHS, RHS, FieldTy,
                        /*IsCompare=*/true);
  }

  return false;
}

PreservedAnalyses NybblerPass::run(Function &F, FunctionAnalysisManager &) {
  SmallVector<Instruction *, 16> Worklist;
  for (Instruction &I : instructions(F)) {
    if (auto *BO = dyn_cast<BinaryOperator>(&I)) {
      auto *FieldTy = dyn_cast<FixedVectorType>(BO->getType());
      if (FieldTy && isNarrowFieldVector(FieldTy))
        Worklist.push_back(&I);
    } else if (auto *CI = dyn_cast<ICmpInst>(&I)) {
      auto *FieldTy = dyn_cast<FixedVectorType>(CI->getOperand(0)->getType());
      if (FieldTy && isNarrowFieldVector(FieldTy))
        Worklist.push_back(&I);
    }
  }

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
