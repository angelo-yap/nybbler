//===- Nybbler.h - Narrow-field vector lowering pass ----------------------===//
//
// Nybbler lowers vertical vector operations over narrow integer fields
// (i1/i2/i4) into legal-width SWAR sequences so that any backend can lower
// them directly to SIMD instead of scalarizing.
//
// Slice 1 covers the bitwise operations (and/or/xor) only. See
// docs/superpowers/specs/2026-06-22-nybbler-design.md.
//
//===----------------------------------------------------------------------===//

#ifndef NYBBLER_H
#define NYBBLER_H

#include "llvm/IR/PassManager.h"

namespace llvm {
class Type;
}

namespace nybbler {

/// True iff \p T is a fixed-width vector whose element is an integer of bit
/// width 1, 2, or 4 (a "narrow field" not directly supported by SIMD hardware).
bool isNarrowFieldVector(llvm::Type *T);

/// Function pass that rewrites narrow-field bitwise vector ops into byte-vector
/// (SWAR carrier) equivalents.
struct NybblerPass : llvm::PassInfoMixin<NybblerPass> {
  llvm::PreservedAnalyses run(llvm::Function &F,
                              llvm::FunctionAnalysisManager &AM);
};

} // namespace nybbler

#endif // NYBBLER_H
