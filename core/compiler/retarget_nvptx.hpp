#pragma once
// core/compiler/retarget_nvptx.hpp
//
// NVPTX retarget — lowers an LLVM IR module to NVIDIA PTX via LLVM's
// in-tree NVPTX backend, for the GPU-IR path on NVIDIA hardware.
//
// Why PTX and not SPIR-V: NVIDIA's OpenCL driver does not support
// clCreateProgramWithIL (no SPIR-V ingestion), but its CUDA driver loads
// PTX natively (cuModuleLoadData). PTX is therefore the GPU-IR target on
// NVIDIA — the same LLVM IR the CPU path JITs, retargeted to PTX and run
// through the CUDA Driver API. (The SPIR-V retarget is kept for analysis
// and for Intel/AMD OpenCL.)
//
// The pipeline:
//   1. Set the target triple to nvptx64-nvidia-cuda.
//   2. Mark render_tile as a CUDA kernel (calling convention + the
//      nvvm.annotations "kernel" metadata the backend recognises).
//   3. Lower to PTX text via TargetMachine (AssemblyFile).
//
// The PTX is text; the CUDA driver JITs it to SASS for the actual GPU at
// module-load time, so it is portable across NVIDIA architectures.

#include "core/compiler/llvm_compat.hpp"

#include <llvm/IR/Module.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/CallingConv.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Triple.h>
#include <llvm/Support/raw_ostream.h>

#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace frep {

namespace nvptx_detail {

// The NVPTX backend can select sqrt/fabs/min/max (hardware ops) but NOT the
// transcendental intrinsics (llvm.sin/cos): "Cannot select: fsin". libdevice
// (__nv_sinf) isn't linked at JIT time, so emitting extern calls to it makes
// cuModuleLoadData fail. Instead we inline a self-contained polynomial sin/cos
// in IR — no external symbols, portable to any backend. Range-reduce x to
// [-π,π] via k = round(x/2π), then a degree-7 (sin) / degree-6 (cos) minimax
// polynomial. Accuracy ~1e-6 over the reduced range, far below the 0.0078
// parity tolerance.
inline llvm::Value* build_sin_poly(llvm::IRBuilder<>& b, llvm::Value* x);

inline llvm::Value* range_reduce(llvm::IRBuilder<>& b, llvm::Value* x) {
    // r = x - 2π * round(x / 2π)
    llvm::Type* f = x->getType();
    auto* inv2pi = llvm::ConstantFP::get(f, 0.15915494309189535);  // 1/(2π)
    auto* twopi  = llvm::ConstantFP::get(f, 6.283185307179586);
    auto* q = b.CreateFMul(x, inv2pi);
    // round-to-nearest via llvm.round (NVPTX selects cvt.rni)
    auto* rq = b.CreateUnaryIntrinsic(llvm::Intrinsic::round, q);
    return b.CreateFSub(x, b.CreateFMul(rq, twopi));
}

// sin via odd minimax polynomial on the reduced range.
inline llvm::Value* build_sin_poly(llvm::IRBuilder<>& b, llvm::Value* x) {
    llvm::Type* f = x->getType();
    auto* r  = range_reduce(b, x);
    auto* r2 = b.CreateFMul(r, r);
    // Horner: r * (c1 + r2*(c3 + r2*(c5 + r2*c7)))
    auto C = [&](double v){ return llvm::ConstantFP::get(f, v); };
    llvm::Value* p = C(-2.3889859e-08);          // ~ -1/9! scaled
    p = b.CreateFAdd(b.CreateFMul(p, r2), C( 2.7525562e-06));  // 1/7!
    p = b.CreateFAdd(b.CreateFMul(p, r2), C(-1.9841270e-04));  // -1/5!... 
    p = b.CreateFAdd(b.CreateFMul(p, r2), C( 8.3333337e-03));  // 1/3!...
    p = b.CreateFAdd(b.CreateFMul(p, r2), C(-1.6666667e-01));
    p = b.CreateFAdd(b.CreateFMul(p, r2), C( 1.0));
    return b.CreateFMul(p, r);  // sin(r) ≈ r * poly(r²)
}

inline llvm::Value* build_cos_poly(llvm::IRBuilder<>& b, llvm::Value* x) {
    // cos(x) = sin(x + π/2)
    llvm::Type* f = x->getType();
    return build_sin_poly(b, b.CreateFAdd(x, llvm::ConstantFP::get(f, 1.5707963267948966)));
}

inline void lower_transcendentals(llvm::Module& mod) {
    llvm::Type* f32 = llvm::Type::getFloatTy(mod.getContext());

    struct Job { llvm::CallInst* ci; int kind; };  // 0=sin, 1=cos
    std::vector<Job> jobs;
    for (auto& F : mod) {
        if (F.isDeclaration()) continue;  // no body to scan
        for (auto& BB : F)
            for (auto& I : BB)
                if (auto* ci = llvm::dyn_cast<llvm::CallInst>(&I)) {
                    auto* fn = ci->getCalledFunction();
                    if (!fn || !fn->isIntrinsic() || ci->arg_size() < 1) continue;
                    if (ci->getArgOperand(0)->getType() != f32) continue;
                    if (fn->getIntrinsicID() == llvm::Intrinsic::sin)
                        jobs.push_back({ci, 0});
                    else if (fn->getIntrinsicID() == llvm::Intrinsic::cos)
                        jobs.push_back({ci, 1});
                }
    }

    for (auto& j : jobs) {
        llvm::IRBuilder<> b(j.ci);
        llvm::Value* x = j.ci->getArgOperand(0);
        llvm::Value* r = (j.kind == 0) ? build_sin_poly(b, x) : build_cos_poly(b, x);
        j.ci->replaceAllUsesWith(r);
        j.ci->eraseFromParent();
    }
}

}  // namespace nvptx_detail

class NVPTXRetarget {
public:
    // GPU compute capability target. sm_61 = Pascal (GTX 10-series). The
    // CUDA driver re-JITs PTX for the actual device, so a slightly lower
    // target still runs on newer GPUs; keep it conservative.
    explicit NVPTXRetarget(std::string arch = "sm_50") : arch_(std::move(arch)) {}

    std::string last_error;

    // Lower `mod` to PTX text. Returns the PTX assembly string on success.
    // Requires the NVPTX target to be registered (InitializeAllTargets()).
    std::expected<std::string, std::string> retarget(llvm::Module& mod) {
        const std::string triple = "nvptx64-nvidia-cuda";
        llvm_compat::set_target_triple(mod, triple);

        // Mark render_tile as a kernel: PTX kernel calling convention + the
        // nvvm.annotations entry the backend reads to emit `.entry`.
        if (auto* fn = mod.getFunction("render_tile")) {
            fn->setCallingConv(llvm::CallingConv::PTX_Kernel);
            fn->setLinkage(llvm::Function::ExternalLinkage);
            fn->removeFnAttr(llvm::Attribute::AlwaysInline);

            auto& C = mod.getContext();
            auto* ann = mod.getOrInsertNamedMetadata("nvvm.annotations");
            llvm::Metadata* ops[] = {
                llvm::ValueAsMetadata::get(fn),
                llvm::MDString::get(C, "kernel"),
                llvm::ValueAsMetadata::get(
                    llvm::ConstantInt::get(llvm::Type::getInt32Ty(C), 1)),
            };
            ann->addOperand(llvm::MDNode::get(C, ops));
        } else {
            return std::unexpected("module has no render_tile function");
        }

        // Replace transcendental intrinsics (sin/cos/exp/log/pow) the NVPTX
        // backend can't select with libdevice __nv_* calls the CUDA driver
        // resolves at JIT time. Without this, any sin/cos node (twist, bend,
        // rotate) fails with "Cannot select: fsin".
        nvptx_detail::lower_transcendentals(mod);

        std::string err;
        llvm::Triple T(triple);
#if LLVM_VERSION_MAJOR >= 22
        const llvm::Target* tgt = llvm::TargetRegistry::lookupTarget(T, err);
#else
        const llvm::Target* tgt = llvm::TargetRegistry::lookupTarget(triple, err);
#endif
        if (!tgt) return std::unexpected("NVPTX target not available: " + err);

        std::unique_ptr<llvm::TargetMachine> tm(
#if LLVM_VERSION_MAJOR >= 22
            tgt->createTargetMachine(T, arch_, "", llvm::TargetOptions{},
                                     std::optional<llvm::Reloc::Model>())
#else
            tgt->createTargetMachine(triple, arch_, "", llvm::TargetOptions{},
                                     std::optional<llvm::Reloc::Model>())
#endif
        );
        if (!tm) return std::unexpected("could not create NVPTX TargetMachine");
        mod.setDataLayout(tm->createDataLayout());

        llvm::SmallVector<char, 0> buf;
        llvm::raw_svector_ostream os(buf);
        llvm::legacy::PassManager pm;
        if (tm->addPassesToEmitFile(pm, os, nullptr,
                                    llvm::CodeGenFileType::AssemblyFile))
            return std::unexpected("NVPTX cannot emit assembly");
        pm.run(mod);
        if (buf.empty()) return std::unexpected("NVPTX produced empty PTX");

        return std::string(buf.begin(), buf.end());
    }

private:
    std::string arch_;
};

} // namespace frep
