// core/frep/hep_codegen.cpp
//
// LLVM IR for the six HEP node types. Each function mirrors the matching
// eval() in hep.hpp expression for expression and in the same order - the two
// are checked against each other, and a reordering that looks harmless changes
// the last bits and breaks that check.
//
// Radii, half-lengths and frame entries go through c.param_value() so a policy
// can promote them to runtime slots. Angles do not: they enter as folded sines
// and cosines, because a sin() of a loaded value would make eval() and
// codegen() disagree the moment one was promoted. See hep.hpp's header.

#include "hep.hpp"

namespace frep::hep {

namespace {

using llvm::Value;

Value* fabs_(CgCtx& c, Value* v) {
    return llvm_compat::unary_intrinsic(c.b, llvm::Intrinsic::fabs, v, "abs");
}
Value* sqrt_(CgCtx& c, Value* v) {
    return llvm_compat::unary_intrinsic(c.b, llvm::Intrinsic::sqrt, v, "sqrt");
}
Value* mx(CgCtx& c, Value* a, Value* b) { return llvm_compat::max_num(c.b, a, b, "mx"); }
Value* mn(CgCtx& c, Value* a, Value* b) { return llvm_compat::min_num(c.b, a, b, "mn"); }

/// rho = sqrt(x*x + y*y)
Value* rho_(CgCtx& c, Value* x, Value* y) {
    auto& b = c.b;
    return sqrt_(c, b.CreateFAdd(b.CreateFMul(x, x, "x2"),
                                 b.CreateFMul(y, y, "y2"), "rho2"));
}

/// The phi wedge, or null when there is no cut - in which case the caller
/// omits the term, exactly as eval() does.
Value* wedge_(CgCtx& c, Value* x, Value* y, float a, float d) {
    float sa, ca, sb, cb;
    bool use_max = true;
    if (!wedge_coeffs(a, d, sa, ca, sb, cb, use_max)) return nullptr;
    auto& b = c.b;
    auto* fa = b.CreateFSub(b.CreateFMul(x, c.fc(sa)), b.CreateFMul(y, c.fc(ca)), "wa");
    auto* fb = b.CreateFAdd(b.CreateFMul(x, c.fc(sb)), b.CreateFMul(y, c.fc(cb)), "wb");
    return use_max ? mx(c, fa, fb) : mn(c, fa, fb);
}

/// |z| - hz
Value* zcap_(CgCtx& c, Value* z, Value* hz) {
    return c.b.CreateFSub(fabs_(c, z), hz, "cap");
}

/// The linear interpolation a frustum's radii need: u = (z + hz) / (2 hz).
Value* uof_(CgCtx& c, Value* z, Value* hz) {
    auto& b = c.b;
    return b.CreateFDiv(b.CreateFAdd(z, hz, "zph"),
                        b.CreateFMul(c.fc(2.0f), hz, "2hz"), "u");
}

}  // namespace

// ── Tube ─────────────────────────────────────────────────────────────────────
Value* TubeNode::codegen(CgCtx& c, Value* x, Value* y, Value* z) const {
    auto& b = c.b;
    auto* rmax = c.param_value(id, "rmax", params[Rmax]);
    auto* hz = c.param_value(id, "hz", params[Hz]);
    auto* rho = rho_(c, x, y);
    Value* f = mx(c, b.CreateFSub(rho, rmax, "outer"), zcap_(c, z, hz));
    if (params[Rmin] > 0.0f) {
        auto* rmin = c.param_value(id, "rmin", params[Rmin]);
        f = mx(c, f, b.CreateFSub(rmin, rho, "inner"));
    }
    if (auto* w = wedge_(c, x, y, float(params[Phi0]), float(params[Dphi])))
        f = mx(c, f, w);
    return f;
}

// ── Cone ─────────────────────────────────────────────────────────────────────
Value* ConeNode::codegen(CgCtx& c, Value* x, Value* y, Value* z) const {
    auto& b = c.b;
    const float hzf = params[Hz];
    const float a1 = params[Rmax1], a2 = params[Rmax2];
        auto* hz = c.param_value(id, "hz", hzf);
    auto* r1 = c.param_value(id, "rmax1", a1);
    auto* u = uof_(c, z, hz);
    auto* Rmax = b.CreateFAdd(r1, b.CreateFMul(c.fc(a2 - a1), u, "dr"), "Rmax");
    auto* rho = rho_(c, x, y);
    Value* f = mx(c,
                  b.CreateFDiv(b.CreateFSub(rho, Rmax, "dout"),
                               c.fc(lat_scale<float>(double(a2) - double(a1), 2.0 * double(hzf))), "lat"),
                  zcap_(c, z, hz));
    const float i1 = params[Rmin1], i2 = params[Rmin2];
    if (i1 > 0.0f || i2 > 0.0f) {
                auto* q1 = c.param_value(id, "rmin1", i1);
        auto* Rmin = b.CreateFAdd(q1, b.CreateFMul(c.fc(i2 - i1), u, "dri"), "Rmin");
        f = mx(c, f,
               b.CreateFDiv(b.CreateFSub(Rmin, rho, "din"),
                            c.fc(lat_scale<float>(double(i2) - double(i1), 2.0 * double(hzf))), "lati"));
    }
    if (auto* w = wedge_(c, x, y, float(params[Phi0]), float(params[Dphi])))
        f = mx(c, f, w);
    return f;
}

// ── SphericalShell ───────────────────────────────────────────────────────────
Value* SphericalShellNode::codegen(CgCtx& c, Value* x, Value* y, Value* z) const {
    auto& b = c.b;
    auto* rho = rho_(c, x, y);
    // rad from rho, not from x,y,z - see hep.hpp.
    auto* rad = sqrt_(c, b.CreateFAdd(b.CreateFMul(rho, rho, "rho2"),
                                      b.CreateFMul(z, z, "z2"), "rad2"));
    auto* rmax = c.param_value(id, "rmax", params[Rmax]);
    Value* f = b.CreateFSub(rad, rmax, "outer");
    if (params[Rmin] > 0.0f) {
        auto* rmin = c.param_value(id, "rmin", params[Rmin]);
        f = mx(c, f, b.CreateFSub(rmin, rad, "inner"));
    }
    if (auto* w = wedge_(c, x, y, float(params[Phi0]), float(params[Dphi])))
        f = mx(c, f, w);
    const float t0 = params[Theta0];
    const float t1 = t0 + params[Dtheta];
    auto cone = [&](float ct, float st, bool neg) {
        auto* v = b.CreateFSub(b.CreateFMul(rho, c.fc(ct)),
                               b.CreateFMul(z, c.fc(st)), "cone");
        return neg ? b.CreateFNeg(v, "ncone") : v;
    };
    if (t0 > 1e-6f) f = mx(c, f, cone(std::cos(t0), std::sin(t0), true));
    if (t1 < float(M_PI) - 1e-6f) f = mx(c, f, cone(std::cos(t1), std::sin(t1), false));
    return f;
}

// ── Trapezoid ────────────────────────────────────────────────────────────────
Value* TrapezoidNode::codegen(CgCtx& c, Value* x, Value* y, Value* z) const {
    auto& b = c.b;
    const float hzf = params[Hz];
    const float x1 = params[Dx1], x2 = params[Dx2];
    const float y1 = params[Dy1], y2 = params[Dy2];
        auto* hz = c.param_value(id, "hz", hzf);
    auto* u = uof_(c, z, hz);
    auto face = [&](const char* p0, float v0, float v1, Value* coord) {
        auto* base = c.param_value(id, p0, v0);
        auto* half = b.CreateFAdd(base, b.CreateFMul(c.fc(v1 - v0), u, "dh"), "half");
        return b.CreateFDiv(b.CreateFSub(fabs_(c, coord), half, "d"),
                            c.fc(lat_scale<float>(double(v1) - double(v0),
                                           2.0 * double(hzf))), "face");
    };
    auto* fx = face("dx1", x1, x2, x);
    auto* fy = face("dy1", y1, y2, y);
    return mx(c, mx(c, fx, fy), zcap_(c, z, hz));
}

// ── Polyhedron ───────────────────────────────────────────────────────────────
Value* PolyhedronNode::codegen(CgCtx& c, Value* x, Value* y, Value* z) const {
    auto& b = c.b;
    const std::vector<double>& fc = faces();
    // A max of linear functions: ns-1 maxima over compile-time constants, no
    // branch, no transcendental. Emitted in the same order eval() takes them.
    Value* r = nullptr;
    for (std::size_t j = 0; j + 1 < fc.size(); j += 2) {
        auto* u = b.CreateFAdd(b.CreateFMul(x, c.fc(float(fc[j]))),
                               b.CreateFMul(y, c.fc(float(fc[j + 1]))), "face");
        r = r ? mx(c, r, u) : u;
    }
    const float hzf = params[Hz];
    const float a1 = params[Rmax1], a2 = params[Rmax2];
        auto* hz = c.param_value(id, "hz", hzf);
    auto* r1 = c.param_value(id, "rmax1", a1);
    auto* u = uof_(c, z, hz);
    auto* Rmax = b.CreateFAdd(r1, b.CreateFMul(c.fc(a2 - a1), u, "dr"), "Rmax");
    Value* f = mx(c,
                  b.CreateFDiv(b.CreateFSub(r, Rmax, "dout"),
                               c.fc(lat_scale<float>(double(a2) - double(a1), 2.0 * double(hzf))), "lat"),
                  zcap_(c, z, hz));
    const float i1 = params[Rmin1], i2 = params[Rmin2];
    if (i1 > 0.0f || i2 > 0.0f) {
                auto* q1 = c.param_value(id, "rmin1", i1);
        auto* Rmin = b.CreateFAdd(q1, b.CreateFMul(c.fc(i2 - i1), u, "dri"), "Rmin");
        f = mx(c, f,
               b.CreateFDiv(b.CreateFSub(Rmin, r, "din"),
                            c.fc(lat_scale<float>(double(i2) - double(i1), 2.0 * double(hzf))), "lati"));
    }
    if (auto* w = wedge_(c, x, y, float(params[Phi0]), float(params[Dphi])))
        f = mx(c, f, w);
    return f;
}

// ── Frame ────────────────────────────────────────────────────────────────────
Value* FrameNode::codegen(CgCtx& c, Value* x, Value* y, Value* z) const {
    auto& b = c.b;
    // The name stays for the slot key; the VALUE comes by index.
    auto P = [&](const char* n, int i) {
        return c.param_value(id, n, float(params[i]));
    };
    auto* dx = b.CreateFSub(x, P("tx", Tx), "dx");
    auto* dy = b.CreateFSub(y, P("ty", Ty), "dy");
    auto* dz = b.CreateFSub(z, P("tz", Tz), "dz");
    static const char* kRow[9] = {"r0", "r1", "r2", "r3", "r4",
                                  "r5", "r6", "r7", "r8"};
    auto row = [&](int a, int d, int g) {
        return b.CreateFAdd(
            b.CreateFAdd(b.CreateFMul(P(kRow[a], R0 + a), dx),
                         b.CreateFMul(P(kRow[d], R0 + d), dy)),
            b.CreateFMul(P(kRow[g], R0 + g), dz), "q");
    };
    // q = R^T (p - t), with R row-major in r0..r8.
    return children[0]->codegen(c, row(0, 3, 6), row(1, 4, 7), row(2, 5, 8));
}

}  // namespace frep::hep
