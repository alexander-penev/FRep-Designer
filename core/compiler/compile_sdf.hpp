// Minimal helper for the cross-system benchmarks: JIT-compile the whole-scene
// SDF (scene_sdf, Inlined mode) and hand back a plain function pointer.
// The returned engine keeps the JIT'd code alive.
#pragma once
#include "core/compiler/codegen.hpp"
#include "core/compiler/jit_engine.hpp"
#include "core/frep/scene.hpp"
#include "core/frep/operations.hpp"
#include "core/frep/custom_expr.hpp"
#include "core/frep/expr_ast.hpp"
#include <llvm/IR/Verifier.h>
#include <expected>
#include <memory>
#include <vector>
#include <cmath>
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
    std::vector<FRepNode::Ptr> geoms;
    for (auto& [id, obj] : scene.objects())
        if (obj.visible) geoms.push_back(obj.geometry);
    if (geoms.empty()) return std::unexpected("empty scene");
    auto root = union_all(std::move(geoms));

    auto ctx = std::make_unique<llvm::LLVMContext>();
    auto mod = std::make_unique<llvm::Module>("bench_simd", *ctx);
    auto& C = *ctx;
    auto* vt  = llvm::VectorType::get(llvm::Type::getFloatTy(C), width, false);
    auto* pf  = llvm::PointerType::getUnqual(C);
    auto* fty = llvm::FunctionType::get(llvm::Type::getVoidTy(C), {pf,pf,pf,pf}, false);
    auto* fn  = llvm::Function::Create(fty, llvm::Function::ExternalLinkage,
                                       "scene_sdf_simd", *mod);
    fn->addFnAttr(llvm::Attribute::NoUnwind);
    auto it = fn->arg_begin();
    llvm::Value *pX=&*it++, *pY=&*it++, *pZ=&*it++, *pO=&*it++;
    auto* bb = llvm::BasicBlock::Create(C, "entry", fn);
    llvm::IRBuilder<> b(bb);
    auto ld = [&](llvm::Value* p){ return b.CreateAlignedLoad(vt, p, llvm::MaybeAlign(4)); };
    CgCtx cg{C, *mod, b}; cg.width = width;
    auto r = root->codegen(cg, ld(pX), ld(pY), ld(pZ));
    if (!r) return std::unexpected("codegen failed");
    b.CreateAlignedStore(r, pO, llvm::MaybeAlign(4));
    b.CreateRetVoid();
    if (llvm::verifyFunction(*fn, &llvm::errs()))
        return std::unexpected("verify failed");

    auto eng = std::make_unique<JitEngine>();
    auto jf = eng->load_as<SceneSdfSimdFn>(std::move(mod), std::move(ctx), "scene_sdf_simd");
    if (!jf) return std::unexpected(jf.error());
    return CompiledSdfSimd{*jf, width, std::move(eng)};
}

// Interval SDF (single CustomExprNode). fn(B[6],O[2]) with B=[xlo,xhi,...],
// O=[flo,fhi]. Enables octree pruning: a box with fhi<0 is fully inside,
// flo>0 fully outside, so its cells need no per-point eval.
using SceneSdfIntervalFn = void (*)(const float*, float*);

struct CompiledSdfInterval {
    SceneSdfIntervalFn         fn = nullptr;
    std::unique_ptr<JitEngine> engine;
};

inline std::expected<CompiledSdfInterval, std::string>
compile_scene_sdf_interval(const SceneGraph& scene) {
    const expr::NodePtr* ce = nullptr; int n = 0;
    for (auto& [id, obj] : scene.objects())
        if (obj.visible) { ce = static_cast<const expr::NodePtr*>(obj.geometry->custom_expr_ast()); ++n; }
    if (n != 1 || !ce) return std::unexpected("interval path needs a single CustomExprNode scene");
    auto ctx = std::make_unique<llvm::LLVMContext>();
    auto mod = std::make_unique<llvm::Module>("bench_ival", *ctx);
    CustomExprCompiler c;
    if (!c.compile_interval(*mod, *ctx, "scene_sdf_ival", *ce))
        return std::unexpected(c.last_error());
    auto eng = std::make_unique<JitEngine>();
    auto fn = eng->load_as<SceneSdfIntervalFn>(std::move(mod), std::move(ctx), "scene_sdf_ival");
    if (!fn) return std::unexpected(fn.error());
    return CompiledSdfInterval{*fn, std::move(eng)};
}

// Count grid cells whose sign is resolved by interval pruning vs. those that
// must be evaluated point-wise. Returns {pruned_cells, leaf_cells}. Recurses an
// octree over [lo,hi]^3 sampled on an N^3 grid; `leaf` is the cell-count edge
// at which it stops subdividing and hands cells to the scalar/SIMD path.
inline std::pair<long,long>
octree_classify(SceneSdfIntervalFn iv, long N, float lo, float hi, int leaf = 4) {
    const float span = hi - lo;
    auto cell = [&](long i){ return lo + span * i / (N - 1); };
    long pruned = 0, leafc = 0;
    struct Box { long x0,x1,y0,y1,z0,z1; };
    std::vector<Box> st{{0,N-1,0,N-1,0,N-1}};
    float B[6], O[2];
    while (!st.empty()) {
        auto b = st.back(); st.pop_back();
        B[0]=cell(b.x0); B[1]=cell(b.x1);
        B[2]=cell(b.y0); B[3]=cell(b.y1);
        B[4]=cell(b.z0); B[5]=cell(b.z1);
        iv(B, O);
        long nx=b.x1-b.x0+1, ny=b.y1-b.y0+1, nz=b.z1-b.z0+1;
        long cells=nx*ny*nz;
        if (O[1] < 0 || O[0] > 0) { pruned += cells; continue; }   // sign resolved
        if (nx<=leaf && ny<=leaf && nz<=leaf) { leafc += cells; continue; }
        long mx=(b.x0+b.x1)/2, my=(b.y0+b.y1)/2, mz=(b.z0+b.z1)/2;
        auto sp=[&](long a,long c,long m,long&r0a,long&r1a,long&r0b,long&r1b){
            r0a=a; r1a=m; r0b=(m+1<=c?m+1:c); r1b=c; };
        long X0a,X1a,X0b,X1b,Y0a,Y1a,Y0b,Y1b,Z0a,Z1a,Z0b,Z1b;
        sp(b.x0,b.x1,mx,X0a,X1a,X0b,X1b);
        sp(b.y0,b.y1,my,Y0a,Y1a,Y0b,Y1b);
        sp(b.z0,b.z1,mz,Z0a,Z1a,Z0b,Z1b);
        for (int ix=0; ix<2; ++ix) for (int iy=0; iy<2; ++iy) for (int iz=0; iz<2; ++iz) {
            long xa=ix?X0b:X0a, xb=ix?X1b:X1a;
            long ya=iy?Y0b:Y0a, yb=iy?Y1b:Y1a;
            long za=iz?Z0b:Z0a, zb=iz?Z1b:Z1a;
            if (xa>xb||ya>yb||za>zb) continue;
            if (ix&&X0b==X1a) continue; if (iy&&Y0b==Y1a) continue; if (iz&&Z0b==Z1a) continue;
            st.push_back({xa,xb,ya,yb,za,zb});
        }
    }
    return {pruned, leafc};
}

// Octree leaf boxes (grid-index ranges) whose sign is NOT resolved by interval
// arithmetic, i.e. the only cells that need point evaluation. Pruned interior/
// exterior boxes are dropped; caller evaluates just these.
struct LeafBox { long x0,x1,y0,y1,z0,z1; };
inline std::vector<LeafBox>
octree_leaves(SceneSdfIntervalFn iv, long N, float lo, float hi, int leaf = 4) {
    const float span = hi - lo;
    auto cell = [&](long i){ return lo + span * i / (N - 1); };
    std::vector<LeafBox> out, st{{0,N-1,0,N-1,0,N-1}};
    float B[6], O[2];
    while (!st.empty()) {
        auto b = st.back(); st.pop_back();
        B[0]=cell(b.x0);B[1]=cell(b.x1);B[2]=cell(b.y0);B[3]=cell(b.y1);B[4]=cell(b.z0);B[5]=cell(b.z1);
        iv(B,O);
        if (O[1]<0 || O[0]>0) continue;
        long nx=b.x1-b.x0+1, ny=b.y1-b.y0+1, nz=b.z1-b.z0+1;
        if (nx<=leaf && ny<=leaf && nz<=leaf) { out.push_back(b); continue; }
        long mx=(b.x0+b.x1)/2,my=(b.y0+b.y1)/2,mz=(b.z0+b.z1)/2;
        for (int ix=0;ix<2;++ix)for(int iy=0;iy<2;++iy)for(int iz=0;iz<2;++iz){
            long xa=ix?mx:b.x0, xb=ix?b.x1:mx;
            long ya=iy?my:b.y0, yb=iy?b.y1:my;
            long za=iz?mz:b.z0, zb=iz?b.z1:mz;
            if (xa>xb||ya>yb||za>zb) continue;
            // skip duplicate half when an axis wasn't actually divided
            if (ix && mx==b.x0) continue;
            if (iy && my==b.y0) continue;
            if (iz && mz==b.z0) continue;
            st.push_back({xa,xb,ya,yb,za,zb});
        }
    }
    return out;
}

// Lipschitz box bound: for an L-Lipschitz SDF, f over a box centred at c with
// circumradius r lies in [f(c)-L*r, f(c)+L*r]. Needs only the scalar SDF, so it
// prunes arbitrary node trees (no per-node interval codegen). Looser than true
// interval arithmetic, but sound whenever L bounds |grad f|. The frep4 SDF
// invariant gives L=1 for node trees; a CustomExpr that is not a true distance
// field (e.g. a gyroid implicit) needs its own L, else pruning is unsound.
inline std::vector<LeafBox>
octree_leaves_lipschitz(SceneSdfFn fn, long N, float lo, float hi, int leaf = 4,
                        float L = 1.0f) {
    const float span = hi - lo;
    auto cell = [&](long i){ return lo + span * i / (N - 1); };
    std::vector<LeafBox> out, st{{0,N-1,0,N-1,0,N-1}};
    while (!st.empty()) {
        auto b = st.back(); st.pop_back();
        float x0=cell(b.x0),x1=cell(b.x1),y0=cell(b.y0),y1=cell(b.y1),z0=cell(b.z0),z1=cell(b.z1);
        float cx=0.5f*(x0+x1), cy=0.5f*(y0+y1), cz=0.5f*(z0+z1);
        float hx=0.5f*(x1-x0), hy=0.5f*(y1-y0), hz=0.5f*(z1-z0);
        float r = L * std::sqrt(hx*hx+hy*hy+hz*hz);   // L * circumradius
        float f = fn(cx,cy,cz);
        if (f - r > 0 || f + r < 0) continue;      // sign resolved for whole box
        long nx=b.x1-b.x0+1, ny=b.y1-b.y0+1, nz=b.z1-b.z0+1;
        if (nx<=leaf && ny<=leaf && nz<=leaf) { out.push_back(b); continue; }
        long mx=(b.x0+b.x1)/2,my=(b.y0+b.y1)/2,mz=(b.z0+b.z1)/2;
        for (int ix=0;ix<2;++ix)for(int iy=0;iy<2;++iy)for(int iz=0;iz<2;++iz){
            long xa=ix?mx:b.x0, xb=ix?b.x1:mx;
            long ya=iy?my:b.y0, yb=iy?b.y1:my;
            long za=iz?mz:b.z0, zb=iz?b.z1:mz;
            if (xa>xb||ya>yb||za>zb) continue;
            if (ix&&mx==b.x0) continue; if (iy&&my==b.y0) continue; if (iz&&mz==b.z0) continue;
            st.push_back({xa,xb,ya,yb,za,zb});
        }
    }
    return out;
}

// Heuristic upper bound on |grad f| by finite differences on a coarse grid,
// scaled by a safety factor. Node trees satisfy the SDF invariant (L=1) and do
// not need this; a CustomExpr implicit (gyroid, gears) does. Heuristic: if the
// true maximum gradient sits between samples the bound can be too small, so the
// result is a *guess* — verify before relying on it for pruning.
inline float estimate_lipschitz(SceneSdfFn fn, long N, float lo, float hi,
                                float safety = 1.25f) {
    const float span = hi - lo, h = span / (N - 1);
    auto c = [&](long i){ return lo + span * i / (N - 1); };
    float g = 0;
    for (long i=0;i+1<N;++i) for (long j=0;j+1<N;++j) for (long k=0;k+1<N;++k) {
        float f0=fn(c(i),c(j),c(k));
        float gx=(fn(c(i+1),c(j),c(k))-f0)/h;
        float gy=(fn(c(i),c(j+1),c(k))-f0)/h;
        float gz=(fn(c(i),c(j),c(k+1))-f0)/h;
        float m=std::sqrt(gx*gx+gy*gy+gz*gz);
        if (m>g) g=m;
    }
    return g * safety;
}

} // namespace frep::jit
