// Minimal helper for the cross-system benchmarks: JIT-compile the whole-scene
// SDF (scene_sdf, Inlined mode) and hand back a plain function pointer.
// The returned engine keeps the JIT'd code alive.
#pragma once
#include "core/compiler/codegen.hpp"
#include "core/compiler/jit_engine.hpp"
#include "core/frep/scene.hpp"
#include "core/frep/operations.hpp"
#include "core/frep/custom_expr.hpp"
#include <expected>
#include <memory>
#include <string>
namespace frep::jit {

using SceneSdfFn = float (*)(float, float, float);

struct CompiledSdf {
    SceneSdfFn                 fn = nullptr;
    std::unique_ptr<JitEngine> engine;   // owns the code
};

inline std::expected<CompiledSdf, std::string>
compile_scene_sdf(const SceneGraph& scene) {
    auto ctx = std::make_unique<llvm::LLVMContext>();
    SceneCodegen cg(*ctx, {}, "bench_sdf");
    std::vector<FRepNode::Ptr> geoms;
    for (auto& [id, obj] : scene.objects())
        if (obj.visible) geoms.push_back(obj.geometry);
    if (geoms.empty()) return std::unexpected("empty scene");
    cg.emit_scene_sdf(*union_all(std::move(geoms)));
    auto eng = std::make_unique<JitEngine>();
    auto fn  = eng->load_as<SceneSdfFn>(cg.take_module(), std::move(ctx), "scene_sdf");
    if (!fn) return std::unexpected(fn.error());
    return CompiledSdf{*fn, std::move(eng)};
}

// SIMD (W-lane) whole-scene SDF. Only supported when the scene is a single
// CustomExprNode (the common case for imported analytic scenes); returns an
// error otherwise so the caller can fall back to the scalar path.
using SceneSdfSimdFn = void (*)(const float*, const float*, const float*, float*);

struct CompiledSdfSimd {
    SceneSdfSimdFn             fn = nullptr;
    unsigned                   width = 0;
    std::unique_ptr<JitEngine> engine;
};

inline std::expected<CompiledSdfSimd, std::string>
compile_scene_sdf_simd(const SceneGraph& scene, unsigned width = 8) {
    const CustomExprNode* ce = nullptr; int n = 0;
    for (auto& [id, obj] : scene.objects())
        if (obj.visible) { ce = dynamic_cast<const CustomExprNode*>(obj.geometry.get()); ++n; }
    if (n != 1 || !ce) return std::unexpected("SIMD path needs a single CustomExprNode scene");

    auto ctx = std::make_unique<llvm::LLVMContext>();
    auto mod = std::make_unique<llvm::Module>("bench_simd", *ctx);
    CustomExprCompiler c;
    if (!c.compile_vec(*mod, *ctx, "scene_sdf_simd", ce->ast(), width))
        return std::unexpected(c.last_error());
    auto eng = std::make_unique<JitEngine>();
    auto fn = eng->load_as<SceneSdfSimdFn>(std::move(mod), std::move(ctx), "scene_sdf_simd");
    if (!fn) return std::unexpected(fn.error());
    return CompiledSdfSimd{*fn, width, std::move(eng)};
}

} // namespace frep::jit
