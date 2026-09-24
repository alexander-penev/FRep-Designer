#pragma once
// core/frep/primitives.hpp

#include "node.hpp"
#include "scalar.hpp"
#include "core/compiler/llvm_compat.hpp"
#include <cmath>
#include <functional>

namespace frep {

// ── Sphere ────────────────────────────────────────────────────────────────────
// True SDF:  f(x,y,z) = sqrt(x^2+y^2+z^2) - r   (<=0 inside)
// Unlike the traditional F-Rep form (x^2+y^2+z^2 - r^2), here the value
// equals the Euclidean distance — important for correct sphere tracing.
class SphereNode final : public FRepNode {
    const char* type_name() const noexcept override { return "Sphere"; }
public:
    /// Index constants, in node_param_schema(Sphere) order. The schema and
    /// this enum are checked against each other in test_param_schema.cpp.
    enum : int { R };
    explicit SphereNode(double r, std::string nid = "sphere") {
        kind = NodeKind::Sphere; id = std::move(nid);
        params.init({"r"}, {r});
    }

    llvm::Value* codegen(CgCtx& c, llvm::Value* x, llvm::Value* y, llvm::Value* z) const override {
        auto& b  = c.b;
        auto  sx = b.CreateFMul(x, x, "x2");
        auto  sy = b.CreateFMul(y, y, "y2");
        auto  sz = b.CreateFMul(z, z, "z2");
        auto  s  = b.CreateFAdd(b.CreateFAdd(sx, sy), sz, "len2");
        // unary_intrinsic: LLVM-version-compatible wrapper for unary intrinsics.
        auto  ln = frep::llvm_compat::unary_intrinsic(b, llvm::Intrinsic::sqrt, s, "len");
        return b.CreateFSub(ln, c.param_value(id, "r", params[R]), "sph");
    }

    // Exact AD for the sphere SDF.
    // f = sqrt(x^2+y^2+z^2) - r
    // df/dx = x / sqrt(x^2+y^2+z^2)
    DualVal codegen_grad(CgCtx& c, DualVal x, DualVal y, DualVal z) const override;
    AABB aabb() const override;

    template <class T>
    T eval_t(T x, T y, T z) const {
        using S = ScalarTraits<T>;
        return S::sqrtv(x*x + y*y + z*z) - S::from(params[R]);
    }
    FREP_EVAL_T

    std::size_t structural_hash() const noexcept override {
        return std::hash<double>{}(params[R]) ^ 0xF5E8'A1C3ull;
    }
};

// ── Box ───────────────────────────────────────────────────────────────────────
// f = max(|x|-hx, max(|y|-hy, |z|-hz))
class BoxNode final : public FRepNode {
    const char* type_name() const noexcept override { return "Box"; }
public:
    enum : int { Hx, Hy, Hz };
    BoxNode(double hx, double hy, double hz, std::string nid = "box") {
        kind = NodeKind::Box; id = std::move(nid);
        params.init({"hx", "hy", "hz"}, {hx, hy, hz});
    }

    llvm::Value* codegen(CgCtx& c, llvm::Value* x, llvm::Value* y, llvm::Value* z) const override {
        auto& b = c.b;
        auto fabs_v = [&](llvm::Value* v) {
            return frep::llvm_compat::unary_intrinsic(b, llvm::Intrinsic::fabs, v);
        };
        auto mx = [&](llvm::Value* a, llvm::Value* bv) {
            return frep::llvm_compat::max_num(b, a, bv);
        };
        auto* f0 = c.fc(0.0f);  // width-aware (splats in SIMD mode)
        auto dx = b.CreateFSub(fabs_v(x), c.param_value(id, "hx", params[Hx]), "dx");
        auto dy = b.CreateFSub(fabs_v(y), c.param_value(id, "hy", params[Hy]), "dy");
        auto dz = b.CreateFSub(fabs_v(z), c.param_value(id, "hz", params[Hz]), "dz");
        // True Euclidean box SDF: length(max(d,0)) + min(max(dx,dy,dz),0).
        // Matches eval() (see the note there) — Chebyshev max(d) alone
        // under-estimates the distance to far corners, breaking BVH
        // pruning and slowing sphere-tracing.
        auto ox = mx(dx, f0), oy = mx(dy, f0), oz = mx(dz, f0);
        auto sx = b.CreateFMul(ox, ox), sy = b.CreateFMul(oy, oy), sz = b.CreateFMul(oz, oz);
        auto sum = b.CreateFAdd(sx, b.CreateFAdd(sy, sz));
        auto outside = frep::llvm_compat::unary_intrinsic(b, llvm::Intrinsic::sqrt, sum);
        auto cheb = mx(dx, mx(dy, dz));
        auto inside = frep::llvm_compat::min_num(b, cheb, f0);
        return b.CreateFAdd(outside, inside);
    }

    DualVal codegen_grad(CgCtx& c, DualVal x, DualVal y, DualVal z) const override;
    AABB aabb() const override;

    template <class T>
    T eval_t(T x, T y, T z) const {
        using S = ScalarTraits<T>;
        // True Euclidean box SDF (Inigo Quilez). The Chebyshev form max(dx,
        // dy,dz) under-estimates the distance to a far corner, which breaks
        // the BVH's pruning invariant and shortens every sphere-tracing step.
        const T dx = S::absv(x) - S::from(params[Hx]);
        const T dy = S::absv(y) - S::from(params[Hy]);
        const T dz = S::absv(z) - S::from(params[Hz]);
        const T z0 = S::from(0.0);
        const T ox = S::maxv(dx, z0), oy = S::maxv(dy, z0), oz = S::maxv(dz, z0);
        const T outside = S::sqrtv(ox*ox + oy*oy + oz*oz);
        const T inside  = S::minv(S::maxv(dx, S::maxv(dy, dz)), z0);
        return outside + inside;
    }
    FREP_EVAL_T

    std::size_t structural_hash() const noexcept override {
        std::size_t h = 0xBBB2;
        // By position, so the hash is stable: the old loop walked an
        // unordered_map, whose order is unspecified by the standard.
        for (std::size_t i = 0; i < params.size(); ++i)
            h ^= std::hash<double>{}(params.value(i)) + 0x9e37'79b9ull +
                 (h << 6) + (h >> 2);
        return h;
    }
};

// ── Plane ─────────────────────────────────────────────────────────────────────
// f = dot(n, p) + d    (n is a unit normal vector)
class PlaneNode final : public FRepNode {
    const char* type_name() const noexcept override { return "Plane"; }
public:
    enum : int { Nx, Ny, Nz, D };
    PlaneNode(double nx, double ny, double nz, double d, std::string nid = "plane") {
        kind = NodeKind::Plane; id = std::move(nid);
        params.init({"nx", "ny", "nz", "d"}, {nx, ny, nz, d});
    }

    llvm::Value* codegen(CgCtx& c, llvm::Value* x, llvm::Value* y, llvm::Value* z) const override {
        auto& b = c.b;
        auto dot = b.CreateFAdd(
            b.CreateFAdd(
                b.CreateFMul(c.param_value(id, "nx", params[Nx]), x),
                b.CreateFMul(c.param_value(id, "ny", params[Ny]), y)),
            b.CreateFMul(c.param_value(id, "nz", params[Nz]), z), "dot");
        return b.CreateFAdd(dot, c.param_value(id, "d", params[D]), "plane");
    }

    DualVal codegen_grad(CgCtx& c, DualVal x, DualVal y, DualVal z) const override;
    // PlaneNode does not override aabb() — the plane is infinite.

    template <class T>
    T eval_t(T x, T y, T z) const {
        using S = ScalarTraits<T>;
        return S::from(params[Nx])*x + S::from(params[Ny])*y + S::from(params[Nz])*z + S::from(params[D]);
    }
    FREP_EVAL_T

    std::size_t structural_hash() const noexcept override {
        std::size_t h = 0xF1A7;
        // By position, so the hash is stable: the old loop walked an
        // unordered_map, whose order is unspecified by the standard.
        for (std::size_t i = 0; i < params.size(); ++i)
            h ^= std::hash<double>{}(params.value(i)) + 0x9e37'79b9ull +
                 (h << 6) + (h >> 2);
        return h;
    }
};

} // namespace frep
