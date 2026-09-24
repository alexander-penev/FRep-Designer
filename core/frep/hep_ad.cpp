// core/frep/hep_ad.cpp
//
// Analytic forward-mode gradients for the six HEP node types.
//
// Without these the nodes inherited FRepNode's finite-difference fallback,
// which evaluates the whole subtree THREE times and steps by h = 1e-3 in
// absolute units. At detector scale that step is the same order as the f32
// spacing (~1e-3 mm at 1e4 mm), so the difference quotient lands on a few
// discrete values and the gradient stops being a unit vector. Measured on a
// Tube, |grad| where it should be 1:
//
//     scale      1 mm   1.043
//     scale    100 mm   1.007
//     scale   1000 mm   1.023
//     scale  10000 mm   1.092
//
// against a Sphere's analytic gradient, which is 1.0000 at every scale. A
// normal that is 9% long is a wrong direction, not a slightly noisy one, and
// this project exists to get derivatives from the geometry rather than around
// it.
//
// Each body mirrors eval_t and codegen piece for piece and in the same order.
// The value the dual carries must therefore equal what codegen() emits, which
// is why a division by a constant goes through ad_ir::div against a constant
// dual rather than a multiply by its reciprocal.
//
// THE SUBGRADIENT AT A SEAM is the active piece's: ad_ir::max already selects
// the dual of whichever branch won, which is the right choice for a field
// built as a max of pieces. At the seam itself the derivative does not exist
// and either one-sided value is admissible.

#include "ad_ir.hpp"
#include "hep.hpp"

namespace frep::hep {

namespace ai = ad_ir;
using D = FRepNode::DualVal;

namespace {

/// rho = sqrt(x^2 + y^2), with the axis guarded.
///
/// d(sqrt(u))/du is 1/(2 sqrt(u)), which is infinite at u = 0 - and the axis
/// of a tube is an ordinary interior point that a navigator really does
/// query. The value is left exact and only the DERIVATIVE's denominator is
/// floored, so the field is unchanged and the gradient there is merely large
/// instead of NaN.
D rho_d(CgCtx& c, D x, D y) {
    auto& b = c.b;
    auto* u = b.CreateFAdd(b.CreateFMul(x.val, x.val),
                           b.CreateFMul(y.val, y.val), "rho2");
    auto* v = llvm_compat::unary_intrinsic(b, llvm::Intrinsic::sqrt, u, "rho");
    auto* du = b.CreateFAdd(b.CreateFMul(b.CreateFMul(c.fc(2.0), x.val), x.dot),
                            b.CreateFMul(b.CreateFMul(c.fc(2.0), y.val), y.dot),
                            "drho2");
    auto* den = llvm_compat::max_num(b, b.CreateFMul(c.fc(2.0), v),
                                     c.fc(1e-12), "den");
    return {v, b.CreateFDiv(du, den, "drho")};
}

/// The same wedge as eval_t, as a dual. Null when there is no cut, in which
/// case the caller omits the term exactly as the value path does.
bool wedge_d(CgCtx& c, D x, D y, double a, double d, D& out) {
    float sa, ca, sb, cb;
    bool use_max = true;
    if (!wedge_coeffs<float>(float(a), float(d), sa, ca, sb, cb, use_max))
        return false;
    D fa = ai::sub(c, ai::mul_s(c, x, sa), ai::mul_s(c, y, ca));
    D fb = ai::add(c, ai::mul_s(c, x, sb), ai::mul_s(c, y, cb));
    out = use_max ? ai::max(c, fa, fb) : ai::min(c, fa, fb);
    return true;
}

/// |z| - hz
D zcap_d(CgCtx& c, D z, llvm::Value* hz) {
    return ai::sub_s(c, ai::fabs(c, z), hz);
}

/// u = (z + hz) / (2 hz), the frustum interpolation. hz may be a runtime
/// slot, so the denominator is a value rather than a literal.
D uof_d(CgCtx& c, D z, llvm::Value* hz) {
    D num = ai::add_s(c, z, hz);
    D den = ai::constant(c, 0.0f);
    den.val = c.b.CreateFMul(c.fc(2.0), hz, "2hz");
    return ai::div(c, num, den);
}

/// a / k for a compile-time constant k, through FDiv so the VALUE matches
/// what codegen() emits bit for bit.
D div_k(CgCtx& c, D a, float k) { return ai::div(c, a, ai::constant(c, k)); }

}  // namespace

// ── Tube ─────────────────────────────────────────────────────────────────────
D TubeNode::codegen_grad(CgCtx& c, D x, D y, D z) const {
    auto* rmax = c.param_value(id, "rmax", float(params[Rmax]));
    auto* hz = c.param_value(id, "hz", float(params[Hz]));
    D rho = rho_d(c, x, y);
    D f = ai::max(c, ai::sub_s(c, rho, rmax), zcap_d(c, z, hz));
    if (params[Rmin] > 0.0) {
        auto* rmin = c.param_value(id, "rmin", float(params[Rmin]));
        f = ai::max(c, f, ai::neg(c, ai::sub_s(c, rho, rmin)));
    }
    D w{};
    if (wedge_d(c, x, y, params[Phi0], params[Dphi], w)) f = ai::max(c, f, w);
    return f;
}

// ── Cone ─────────────────────────────────────────────────────────────────────
D ConeNode::codegen_grad(CgCtx& c, D x, D y, D z) const {
    const double hzf = params[Hz];
    const double a1 = params[Rmax1], a2 = params[Rmax2];
    auto* hz = c.param_value(id, "hz", float(hzf));
    auto* r1 = c.param_value(id, "rmax1", float(a1));
    D u = uof_d(c, z, hz);
    // Rmax = r1 + (r2 - r1) u : u carries dz, so the slant contributes to the
    // gradient. This is the term a finite difference gets worst.
    D Rmax = ai::add_s(c, ai::mul_s(c, u, float(a2 - a1)), r1);
    D rho = rho_d(c, x, y);
    D f = ai::max(c,
                  div_k(c, ai::sub(c, rho, Rmax),
                        lat_scale<float>(a2 - a1, 2.0 * hzf)),
                  zcap_d(c, z, hz));
    const double i1 = params[Rmin1], i2 = params[Rmin2];
    if (i1 > 0.0 || i2 > 0.0) {
        auto* q1 = c.param_value(id, "rmin1", float(i1));
        D Rmin = ai::add_s(c, ai::mul_s(c, u, float(i2 - i1)), q1);
        f = ai::max(c, f,
                    div_k(c, ai::sub(c, Rmin, rho),
                          lat_scale<float>(i2 - i1, 2.0 * hzf)));
    }
    D w{};
    if (wedge_d(c, x, y, params[Phi0], params[Dphi], w)) f = ai::max(c, f, w);
    return f;
}

// ── SphericalShell ───────────────────────────────────────────────────────────
D SphericalShellNode::codegen_grad(CgCtx& c, D x, D y, D z) const {
    D rho = rho_d(c, x, y);
    // rad from rho, not from x,y,z - see hep.hpp. The same guard applies at
    // the origin, so it goes through the same helper shape.
    auto& b = c.b;
    auto* u = b.CreateFAdd(b.CreateFMul(rho.val, rho.val),
                           b.CreateFMul(z.val, z.val), "rad2");
    auto* v = llvm_compat::unary_intrinsic(b, llvm::Intrinsic::sqrt, u, "rad");
    auto* du =
        b.CreateFAdd(b.CreateFMul(b.CreateFMul(c.fc(2.0), rho.val), rho.dot),
                     b.CreateFMul(b.CreateFMul(c.fc(2.0), z.val), z.dot), "drad2");
    auto* den = llvm_compat::max_num(b, b.CreateFMul(c.fc(2.0), v), c.fc(1e-12));
    D rad{v, b.CreateFDiv(du, den, "drad")};

    auto* rmax = c.param_value(id, "rmax", float(params[Rmax]));
    D f = ai::sub_s(c, rad, rmax);
    if (params[Rmin] > 0.0) {
        auto* rmin = c.param_value(id, "rmin", float(params[Rmin]));
        f = ai::max(c, f, ai::neg(c, ai::sub_s(c, rad, rmin)));
    }
    D w{};
    if (wedge_d(c, x, y, params[Phi0], params[Dphi], w)) f = ai::max(c, f, w);

    const double t0 = params[Theta0];
    const double t1 = t0 + params[Dtheta];
    // rho cos(t) - z sin(t): linear in (rho, z), so the dual is the same
    // combination of their duals.
    auto cone = [&](double t, bool neg) {
        D v2 = ai::sub(c, ai::mul_s(c, rho, float(std::cos(t))),
                       ai::mul_s(c, z, float(std::sin(t))));
        return neg ? ai::neg(c, v2) : v2;
    };
    if (t0 > 1e-6) f = ai::max(c, f, cone(t0, true));
    if (t1 < M_PI - 1e-6) f = ai::max(c, f, cone(t1, false));
    return f;
}

// ── Trapezoid ────────────────────────────────────────────────────────────────
D TrapezoidNode::codegen_grad(CgCtx& c, D x, D y, D z) const {
    const double hzf = params[Hz];
    auto* hz = c.param_value(id, "hz", float(hzf));
    D u = uof_d(c, z, hz);
    auto face = [&](const char* p0, int idx, double v0, double v1, D coord) {
        auto* base = c.param_value(id, p0, float(v0));
        (void)idx;
        D half = ai::add_s(c, ai::mul_s(c, u, float(v1 - v0)), base);
        return div_k(c, ai::sub(c, ai::fabs(c, coord), half),
                     lat_scale<float>(v1 - v0, 2.0 * hzf));
    };
    D fx = face("dx1", Dx1, params[Dx1], params[Dx2], x);
    D fy = face("dy1", Dy1, params[Dy1], params[Dy2], y);
    return ai::max(c, ai::max(c, fx, fy), zcap_d(c, z, hz));
}

// ── Polyhedron ───────────────────────────────────────────────────────────────
D PolyhedronNode::codegen_grad(CgCtx& c, D x, D y, D z) const {
    const std::vector<double>& fc = faces();
    // A max of linear functions: its gradient is the winning face's normal,
    // which is exactly what ad_ir::max selects. No branch, no transcendental.
    D r{};
    bool first = true;
    for (std::size_t j = 0; j + 1 < fc.size(); j += 2) {
        D u = ai::add(c, ai::mul_s(c, x, float(fc[j])),
                      ai::mul_s(c, y, float(fc[j + 1])));
        r = first ? u : ai::max(c, r, u);
        first = false;
    }
    const double hzf = params[Hz];
    const double a1 = params[Rmax1], a2 = params[Rmax2];
    auto* hz = c.param_value(id, "hz", float(hzf));
    auto* r1 = c.param_value(id, "rmax1", float(a1));
    D u = uof_d(c, z, hz);
    D Rmax = ai::add_s(c, ai::mul_s(c, u, float(a2 - a1)), r1);
    D f = ai::max(c,
                  div_k(c, ai::sub(c, r, Rmax),
                        lat_scale<float>(a2 - a1, 2.0 * hzf)),
                  zcap_d(c, z, hz));
    const double i1 = params[Rmin1], i2 = params[Rmin2];
    if (i1 > 0.0 || i2 > 0.0) {
        auto* q1 = c.param_value(id, "rmin1", float(i1));
        D Rmin = ai::add_s(c, ai::mul_s(c, u, float(i2 - i1)), q1);
        f = ai::max(c, f,
                    div_k(c, ai::sub(c, Rmin, r),
                          lat_scale<float>(i2 - i1, 2.0 * hzf)));
    }
    D w{};
    if (wedge_d(c, x, y, params[Phi0], params[Dphi], w)) f = ai::max(c, f, w);
    return f;
}

// ── Frame ────────────────────────────────────────────────────────────────────
D FrameNode::codegen_grad(CgCtx& c, D x, D y, D z) const {
    // q = R^T (p - t) with R constant, so the duals transform by the same
    // linear map and no Jacobian term is needed. R is a rotation, so the
    // gradient comes back unit-length without rescaling.
    auto P = [&](const char* n, int i) {
        return c.param_value(id, n, float(params[i]));
    };
    D dx = ai::sub_s(c, x, P("tx", Tx));
    D dy = ai::sub_s(c, y, P("ty", Ty));
    D dz = ai::sub_s(c, z, P("tz", Tz));
    static const char* kRow[9] = {"r0", "r1", "r2", "r3", "r4",
                                  "r5", "r6", "r7", "r8"};
    auto row = [&](int a, int d, int g) {
        return ai::add(c,
                       ai::add(c, ai::mul_s(c, dx, P(kRow[a], R0 + a)),
                               ai::mul_s(c, dy, P(kRow[d], R0 + d))),
                       ai::mul_s(c, dz, P(kRow[g], R0 + g)));
    };
    return children[0]->codegen_grad(c, row(0, 3, 6), row(1, 4, 7),
                                     row(2, 5, 8));
}

}  // namespace frep::hep
