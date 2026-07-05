#pragma once
// core/frep/transforms.hpp
//
// Transforms — apply the inverse transform to the coordinates before
// passing them to the child node (the standard F-Rep approach).

#include "node.hpp"
#include <cmath>

namespace frep {

// ── Translate ─────────────────────────────────────────────────────────────────
// f(x-tx, y-ty, z-tz)
class TranslateNode final : public FRepNode {
    const char* type_name() const noexcept override { return "Translate"; }
public:
    TranslateNode(FRepNode::Ptr child, float tx, float ty, float tz,
                  std::string nid = "tr") {
        kind = NodeKind::Translate; id = std::move(nid);
        params["tx"] = tx; params["ty"] = ty; params["tz"] = tz;
        children = {std::move(child)};
    }

    llvm::Value* codegen(CgCtx& c, llvm::Value* x, llvm::Value* y, llvm::Value* z) const override {
        auto& b = c.b;
        auto xt = b.CreateFSub(x, c.param_value(id, "tx", params.at("tx")), "xt");
        auto yt = b.CreateFSub(y, c.param_value(id, "ty", params.at("ty")), "yt");
        auto zt = b.CreateFSub(z, c.param_value(id, "tz", params.at("tz")), "zt");
        return children[0]->codegen(c, xt, yt, zt);
    }


    DualVal codegen_grad(CgCtx& c, DualVal x, DualVal y, DualVal z) const override;
    AABB aabb() const override;
    float eval(float x, float y, float z) const override {
        return children[0]->eval(x - params.at("tx"),
                                 y - params.at("ty"),
                                 z - params.at("tz"));
    }
    std::size_t structural_hash() const noexcept override {
        std::size_t h = children[0]->structural_hash() ^ 0x7788'99AAull;
        for (auto& [k, v] : params)
            h ^= std::hash<float>{}(v) + 0x9e37'79b9ull + (h << 6) + (h >> 2);
        return h;
    }
};

// ── Scale (uniform) ───────────────────────────────────────────────────────────
// f(x/s, y/s, z/s) * s    — preserves the SDF metric
class ScaleNode final : public FRepNode {
    const char* type_name() const noexcept override { return "Scale"; }
public:
    ScaleNode(FRepNode::Ptr child, float s, std::string nid = "sc") {
        kind = NodeKind::Scale; id = std::move(nid);
        params["s"] = s;
        children = {std::move(child)};
    }

    llvm::Value* codegen(CgCtx& c, llvm::Value* x, llvm::Value* y, llvm::Value* z) const override {
        auto& b   = c.b;
        auto s_v  = c.param_value(id, "s", params.at("s"));
        auto inv  = b.CreateFDiv(c.fc(1.0f), s_v, "inv_s");
        auto  xs  = b.CreateFMul(x, inv, "xs");
        auto  ys  = b.CreateFMul(y, inv, "ys");
        auto  zs  = b.CreateFMul(z, inv, "zs");
        auto  sdf = children[0]->codegen(c, xs, ys, zs);
        return b.CreateFMul(sdf, s_v, "sc_sdf");
    }


    DualVal codegen_grad(CgCtx& c, DualVal x, DualVal y, DualVal z) const override;
    AABB aabb() const override;
    float eval(float x, float y, float z) const override {
        float sv = params.at("s");
        return children[0]->eval(x/sv, y/sv, z/sv) * sv;
    }
    std::size_t structural_hash() const noexcept override {
        return children[0]->structural_hash()
             ^ std::hash<float>{}(params.at("s"))
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
    RotateYNode(FRepNode::Ptr child, float angle_rad, std::string nid = "ry") {
        kind = NodeKind::RotateY; id = std::move(nid);
        params["a"] = angle_rad;
        children = {std::move(child)};
    }

    llvm::Value* codegen(CgCtx& c, llvm::Value* x, llvm::Value* y, llvm::Value* z) const override {
        auto& b  = c.b;
        // In Constant mode param_value returns fc(literal) and cos/sin
        // intrinsics fold to constants. In Incremental mode the cos/sin
        // run at runtime (their JIT'd versions are cheap, ~10 cycles).
        auto  a_v = c.param_value(id, "a", params.at("a"));
        auto  ca = frep::llvm_compat::unary_intrinsic(b, llvm::Intrinsic::cos, a_v);
        auto  sa = frep::llvm_compat::unary_intrinsic(b, llvm::Intrinsic::sin, a_v);
        auto  xr = b.CreateFAdd(b.CreateFMul(ca, x), b.CreateFMul(sa, z), "xr");
        auto  zr = b.CreateFSub(b.CreateFMul(ca, z), b.CreateFMul(sa, x), "zr");
        return children[0]->codegen(c, xr, y, zr);
    }


    DualVal codegen_grad(CgCtx& c, DualVal x, DualVal y, DualVal z) const override;
    AABB aabb() const override;
    float eval(float x, float y, float z) const override {
        float a = params.at("a");
        float ca = std::cos(a), sa = std::sin(a);
        return children[0]->eval(ca*x + sa*z, y, ca*z - sa*x);
    }
    std::size_t structural_hash() const noexcept override {
        return children[0]->structural_hash()
             ^ std::hash<float>{}(params.at("a"))
             ^ 0xAA11'BB22ull;
    }
};

} // namespace frep
