#pragma once
// core/frep/transforms.hpp
//
// Transforms — apply the inverse transform to the coordinates before
// passing them to the child node (the standard F-Rep approach).

#include "node.hpp"
#include "scalar.hpp"
#include <cmath>

namespace frep {

// ── Translate ─────────────────────────────────────────────────────────────────
// f(x-tx, y-ty, z-tz)
class TranslateNode final : public FRepNode {
    const char* type_name() const noexcept override { return "Translate"; }
public:
    enum : int { Tx, Ty, Tz };
    TranslateNode(FRepNode::Ptr child, float tx, float ty, float tz,
                  std::string nid = "tr") {
        kind = NodeKind::Translate; id = std::move(nid);
        params.init({"tx", "ty", "tz"}, {tx, ty, tz});
        children = {std::move(child)};
    }

    llvm::Value* codegen(CgCtx& c, llvm::Value* x, llvm::Value* y, llvm::Value* z) const override {
        auto& b = c.b;
        auto xt = b.CreateFSub(x, c.param_value(id, "tx", params[Tx]), "xt");
        auto yt = b.CreateFSub(y, c.param_value(id, "ty", params[Ty]), "yt");
        auto zt = b.CreateFSub(z, c.param_value(id, "tz", params[Tz]), "zt");
        return children[0]->codegen(c, xt, yt, zt);
    }


    DualVal codegen_grad(CgCtx& c, DualVal x, DualVal y, DualVal z) const override;
    AABB aabb() const override;
    template <class T>
    T eval_t(T x, T y, T z) const {
        using S = ScalarTraits<T>;
        return children[0]->eval_as<T>(x - S::from(params[Tx]), y - S::from(params[Ty]), z - S::from(params[Tz]));
    }
    FREP_EVAL_T
    std::size_t structural_hash() const noexcept override {
        std::size_t h = children[0]->structural_hash() ^ 0x7788'99AAull;
        // By position, so the hash is stable: the old loop walked an
        // unordered_map, whose order is unspecified by the standard.
        for (std::size_t i = 0; i < params.size(); ++i)
            h ^= std::hash<double>{}(params.value(i)) + 0x9e37'79b9ull +
                 (h << 6) + (h >> 2);
        return h;
    }
};

// ── Scale (uniform) ───────────────────────────────────────────────────────────
// f(x/s, y/s, z/s) * s    — preserves the SDF metric
class ScaleNode final : public FRepNode {
    const char* type_name() const noexcept override { return "Scale"; }
public:
    // Uniform scale (backward-compatible): stores sx=sy=sz=s.
    enum : int { Sx, Sy, Sz };
    ScaleNode(FRepNode::Ptr child, float s, std::string nid = "sc") {
        kind = NodeKind::Scale; id = std::move(nid);
        params.init({"sx", "sy", "sz"}, {s, s, s});
        children = {std::move(child)};
    }
    // Non-uniform scale: independent per-axis factors.
    ScaleNode(FRepNode::Ptr child, float sx, float sy, float sz, std::string nid = "sc") {
        kind = NodeKind::Scale; id = std::move(nid);
        params.init({"sx", "sy", "sz"}, {sx, sy, sz});
        children = {std::move(child)};
    }

    llvm::Value* codegen(CgCtx& c, llvm::Value* x, llvm::Value* y, llvm::Value* z) const override {
        auto& b   = c.b;
        auto sx = c.param_value(id, "sx", params[Sx]);
        auto sy = c.param_value(id, "sy", params[Sy]);
        auto sz = c.param_value(id, "sz", params[Sz]);
        auto  xs  = b.CreateFDiv(x, sx, "xs");
        auto  ys  = b.CreateFDiv(y, sy, "ys");
        auto  zs  = b.CreateFDiv(z, sz, "zs");
        auto  sdf = children[0]->codegen(c, xs, ys, zs);
        // Non-uniform scale is not distance-preserving; multiply by the SMALLEST
        // axis factor to keep the result a conservative (never-overshooting) SDF.
        auto mn = frep::llvm_compat::binary_intrinsic(b, llvm::Intrinsic::minnum, sx, sy);
        mn = frep::llvm_compat::binary_intrinsic(b, llvm::Intrinsic::minnum, mn, sz);
        return b.CreateFMul(sdf, mn, "sc_sdf");
    }

    DualVal codegen_grad(CgCtx& c, DualVal x, DualVal y, DualVal z) const override;
    AABB aabb() const override;
    template <class T>
    T eval_t(T x, T y, T z) const {
        using S = ScalarTraits<T>;
        const T sx = S::from(params[Sx]), sy = S::from(params[Sy]), sz = S::from(params[Sz]);
        const T mn = S::minv(sx, S::minv(sy, sz));
        return children[0]->eval_as<T>(x/sx, y/sy, z/sz) * mn;
    }
    FREP_EVAL_T
    std::size_t structural_hash() const noexcept override {
        return children[0]->structural_hash()
             ^ std::hash<double>{}(params[Sx])
             ^ (std::hash<double>{}(params[Sy]) << 1)
             ^ (std::hash<double>{}(params[Sz]) << 2)
             ^ 0x9900'1122ull;
    }
};

// ── RotateY ───────────────────────────────────────────────────────────────────
// Inverse transform (rot(-a)):
//   x' =  cos(a)*x + sin(a)*z
//   z' = -sin(a)*x + cos(a)*z
class RotateYNode final : public FRepNode {
    const char* type_name() const noexcept override { return "RotateY"; }
public:
    enum : int { A };
    RotateYNode(FRepNode::Ptr child, float angle_rad, std::string nid = "ry") {
        kind = NodeKind::RotateY; id = std::move(nid);
        params.init({"a"}, {angle_rad});
        children = {std::move(child)};
    }

    llvm::Value* codegen(CgCtx& c, llvm::Value* x, llvm::Value* y, llvm::Value* z) const override {
        auto& b  = c.b;
        // In Constant mode param_value returns fc(literal) and cos/sin
        // intrinsics fold to constants. In Incremental mode the cos/sin
        // run at runtime (their JIT'd versions are cheap, ~10 cycles).
        auto  a_v = c.param_value(id, "a", params[A]);
        auto  ca = frep::llvm_compat::unary_intrinsic(b, llvm::Intrinsic::cos, a_v);
        auto  sa = frep::llvm_compat::unary_intrinsic(b, llvm::Intrinsic::sin, a_v);
        auto  xr = b.CreateFAdd(b.CreateFMul(ca, x), b.CreateFMul(sa, z), "xr");
        auto  zr = b.CreateFSub(b.CreateFMul(ca, z), b.CreateFMul(sa, x), "zr");
        return children[0]->codegen(c, xr, y, zr);
    }


    DualVal codegen_grad(CgCtx& c, DualVal x, DualVal y, DualVal z) const override;
    AABB aabb() const override;
    template <class T>
    T eval_t(T x, T y, T z) const {
        using S = ScalarTraits<T>;
        const T a = S::from(params[A]);
        // std::cos of a T: cosf for float, cos for double. The float path
        // therefore keeps exactly the bits codegen emits (llvm.cos.f32).
        const T ca = std::cos(a), sa = std::sin(a);
        return children[0]->eval_as<T>(ca*x + sa*z, y, ca*z - sa*x);
    }
    FREP_EVAL_T
    std::size_t structural_hash() const noexcept override {
        return children[0]->structural_hash()
             ^ std::hash<double>{}(params[A])
             ^ 0xAA11'BB22ull;
    }
};

// Rotate about the X axis: rotates (y, z), leaves x. Mirrors RotateYNode.
class RotateXNode final : public FRepNode {
    const char* type_name() const noexcept override { return "RotateX"; }
public:
    enum : int { A };
    RotateXNode(FRepNode::Ptr child, float angle_rad, std::string nid = "rx") {
        kind = NodeKind::RotateX; id = std::move(nid);
        params.init({"a"}, {angle_rad});
        children = {std::move(child)};
    }
    llvm::Value* codegen(CgCtx& c, llvm::Value* x, llvm::Value* y, llvm::Value* z) const override {
        auto& b  = c.b;
        auto  a_v = c.param_value(id, "a", params[A]);
        auto  ca = frep::llvm_compat::unary_intrinsic(b, llvm::Intrinsic::cos, a_v);
        auto  sa = frep::llvm_compat::unary_intrinsic(b, llvm::Intrinsic::sin, a_v);
        auto  yr = b.CreateFAdd(b.CreateFMul(ca, y), b.CreateFMul(sa, z), "yr");
        auto  zr = b.CreateFSub(b.CreateFMul(ca, z), b.CreateFMul(sa, y), "zr");
        return children[0]->codegen(c, x, yr, zr);
    }
    DualVal codegen_grad(CgCtx& c, DualVal x, DualVal y, DualVal z) const override;
    AABB aabb() const override;
    template <class T>
    T eval_t(T x, T y, T z) const {
        using S = ScalarTraits<T>;
        const T a = S::from(params[A]);
        // std::cos of a T: cosf for float, cos for double. The float path
        // therefore keeps exactly the bits codegen emits (llvm.cos.f32).
        const T ca = std::cos(a), sa = std::sin(a);
        return children[0]->eval_as<T>(x, ca*y + sa*z, ca*z - sa*y);
    }
    FREP_EVAL_T
    std::size_t structural_hash() const noexcept override {
        return children[0]->structural_hash()
             ^ std::hash<double>{}(params[A])
             ^ 0xBB22'CC33ull;
    }
};

// Rotate about the Z axis: rotates (x, y), leaves z. Mirrors RotateYNode.
class RotateZNode final : public FRepNode {
    const char* type_name() const noexcept override { return "RotateZ"; }
public:
    enum : int { A };
    RotateZNode(FRepNode::Ptr child, float angle_rad, std::string nid = "rz") {
        kind = NodeKind::RotateZ; id = std::move(nid);
        params.init({"a"}, {angle_rad});
        children = {std::move(child)};
    }
    llvm::Value* codegen(CgCtx& c, llvm::Value* x, llvm::Value* y, llvm::Value* z) const override {
        auto& b  = c.b;
        auto  a_v = c.param_value(id, "a", params[A]);
        auto  ca = frep::llvm_compat::unary_intrinsic(b, llvm::Intrinsic::cos, a_v);
        auto  sa = frep::llvm_compat::unary_intrinsic(b, llvm::Intrinsic::sin, a_v);
        auto  xr = b.CreateFAdd(b.CreateFMul(ca, x), b.CreateFMul(sa, y), "xr");
        auto  yr = b.CreateFSub(b.CreateFMul(ca, y), b.CreateFMul(sa, x), "yr");
        return children[0]->codegen(c, xr, yr, z);
    }
    DualVal codegen_grad(CgCtx& c, DualVal x, DualVal y, DualVal z) const override;
    AABB aabb() const override;
    template <class T>
    T eval_t(T x, T y, T z) const {
        using S = ScalarTraits<T>;
        const T a = S::from(params[A]);
        // std::cos of a T: cosf for float, cos for double. The float path
        // therefore keeps exactly the bits codegen emits (llvm.cos.f32).
        const T ca = std::cos(a), sa = std::sin(a);
        return children[0]->eval_as<T>(ca*x + sa*y, ca*y - sa*x, z);
    }
    FREP_EVAL_T
    std::size_t structural_hash() const noexcept override {
        return children[0]->structural_hash()
             ^ std::hash<double>{}(params[A])
             ^ 0xCC33'DD44ull;
    }
};

} // namespace frep
