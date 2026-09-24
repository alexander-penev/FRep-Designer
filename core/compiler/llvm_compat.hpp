#pragma once
// core/compiler/llvm_compat.hpp
//
// Compatibility shims for LLVM IRBuilder API differences across versions.
//
// LLVM 22 changed several IRBuilder methods: CreateMinNum / CreateMaxNum
// gained an FMFSource parameter inserted *before* the Name argument, and
// CreateUnaryIntrinsic / CreateBinaryIntrinsic changed their FMFSource
// parameter type from `Instruction*` to the new `FMFSource` class.
//
// These thin wrappers paper over the differences so the rest of the code
// base targets a single API. The project officially targets LLVM 22+, but
// these shims also let it build against LLVM 20/21 in fallback mode.

#include <llvm/Config/llvm-config.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Module.h>
#include <llvm/TargetParser/Triple.h>

namespace frep::llvm_compat {

// Module::setTargetTriple signature.
// LLVM 21 and earlier: accepts StringRef (commit r129868 changed it).
// LLVM 22:             accepts Triple. Passing a std::string fails to compile.
//
// This shim accepts a string-like value and always does the right thing.
inline void set_target_triple(llvm::Module& mod, llvm::StringRef triple) {
#if LLVM_VERSION_MAJOR >= 22
    mod.setTargetTriple(llvm::Triple(triple));
#else
    mod.setTargetTriple(triple);
#endif
}

// minnum(LHS, RHS) with an optional result name.
inline llvm::Value* min_num(llvm::IRBuilder<>& b,
                            llvm::Value* lhs, llvm::Value* rhs,
                            const llvm::Twine& name = "") {
#if LLVM_VERSION_MAJOR >= 22
    return b.CreateMinNum(lhs, rhs, /*FMFSource=*/{}, name);
#else
    return b.CreateMinNum(lhs, rhs, name);
#endif
}

// maxnum(LHS, RHS) with an optional result name.
inline llvm::Value* max_num(llvm::IRBuilder<>& b,
                            llvm::Value* lhs, llvm::Value* rhs,
                            const llvm::Twine& name = "") {
#if LLVM_VERSION_MAJOR >= 22
    return b.CreateMaxNum(lhs, rhs, /*FMFSource=*/{}, name);
#else
    return b.CreateMaxNum(lhs, rhs, name);
#endif
}

// Unary intrinsic call (e.g. sqrt, fabs) with an optional result name.
inline llvm::Value* unary_intrinsic(llvm::IRBuilder<>& b,
                                    llvm::Intrinsic::ID id, llvm::Value* v,
                                    const llvm::Twine& name = "") {
#if LLVM_VERSION_MAJOR >= 22
    return b.CreateUnaryIntrinsic(id, v, /*FMFSource=*/{}, name);
#else
    return b.CreateUnaryIntrinsic(id, v, /*FMFSource=*/nullptr, name);
#endif
}

// Intrinsic declaration lookup. LLVM 20 renamed Intrinsic::getDeclaration to
// getOrInsertDeclaration and LLVM 22 removed the old spelling, which is what
// stopped this project building against the very version its CMakeLists
// requires ("LLVM 22+"): 28 call sites in custom_expr.cpp, one error each.
// Absorbing it here is what this header is for.
inline llvm::Function* get_declaration(llvm::Module* m, llvm::Intrinsic::ID id,
                                       llvm::ArrayRef<llvm::Type*> tys = {}) {
#if LLVM_VERSION_MAJOR >= 20
    return llvm::Intrinsic::getOrInsertDeclaration(m, id, tys);
#else
    return llvm::Intrinsic::getDeclaration(m, id, tys);
#endif
}

// Binary intrinsic call (e.g. minnum, maxnum) with an optional result name.
inline llvm::Value* binary_intrinsic(llvm::IRBuilder<>& b,
                                     llvm::Intrinsic::ID id,
                                     llvm::Value* lhs, llvm::Value* rhs,
                                     const llvm::Twine& name = "") {
#if LLVM_VERSION_MAJOR >= 22
    return b.CreateBinaryIntrinsic(id, lhs, rhs, /*FMFSource=*/{}, name);
#else
    return b.CreateBinaryIntrinsic(id, lhs, rhs, /*FMFSource=*/nullptr, name);
#endif
}

} // namespace frep::llvm_compat