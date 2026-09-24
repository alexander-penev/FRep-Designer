// core/gpu/glsl_hep.cpp
//
// GLSL for the six HEP node types.
//
// Without these a converted detector cannot run on the GPU at all: the
// emitter's switch does not know the kinds, and its plugin fallback
// (FRepNode::emit_glsl) takes no coordinates - it hands the node its child
// expressions and a name prefix, and its own example writes the literal
// names x, y, z. That is correct for a node sitting at the root and wrong
// under any transform, where the point arrives as t3_x or as a Frame's
// rebuilt names. A primitive has to be told which expressions its
// coordinates are, which is what the emitter's own switch does.
//
// Each body mirrors hep_codegen.cpp expression for expression and in the
// same order, for the same reason that file mirrors eval(): three
// descriptions of one solid that are only equal because they were written
// to be. tests/test_glsl_hep.cpp checks the GLSL against the CPU rather
// than trusting it.
//
// EVERY PARAMETER GOES THROUGH pval(), so a parameter the policy made a
// runtime slot is read from the shared buffer and the shader source does
// not change when its value does. That only started working for these kinds
// when node_param_schema learned about them - before, they had no schema,
// so slot_of() never matched and every parameter silently baked.
//
// ANGLES ARE FOLDED, as in the IR: phi0, dphi, theta0 and dtheta enter as
// sines and cosines computed here in double and printed as literals. A
// sin() of a buffer read would be possible but would make the GLSL and the
// IR disagree the moment a policy promoted one of them.

#include "core/frep/hep.hpp"
#include "core/gpu/glsl_emitter.hpp"

#include <cmath>
#include <string>

namespace frep::gpu {

namespace {
/// "(expr)" - parenthesised once, so the caller never has to think about it.
std::string par(const std::string& e) { return "(" + e + ")"; }
}  // namespace

/// rho = sqrt(x*x + y*y), as an explicit sqrt rather than GLSL length().
/// Same reason as emit_sphere: length() may lower to a reduced-precision
/// reciprocal-sqrt sequence, which would shift every value on this node
/// against the CPU by a systematic amount rather than at the last bit.
std::string GlslEmitter::hep_rho(Ctx& c, const std::string& x,
                                 const std::string& y) {
    auto v = c.fresh("rho");
    c.sdf_body << "    float " << v << " = sqrt(" << par(x) << "*" << par(x)
               << " + " << par(y) << "*" << par(y) << ");\n";
    return v;
}

/// The phi wedge, or an empty string when there is no cut - in which case
/// the caller omits the term entirely, exactly as eval() and codegen() do.
std::string GlslEmitter::hep_wedge(Ctx& c, const FRepNode& n,
                                   const std::string& x,
                                   const std::string& y) {
    float sa, ca, sb, cb;
    bool use_max = true;
    if (!hep::wedge_coeffs<float>(float(n.params.at("phi0")),
                                  float(n.params.at("dphi")), sa, ca, sb, cb,
                                  use_max))
        return {};
    auto v = c.fresh("wd");
    c.sdf_body << "    float " << v << " = " << (use_max ? "max" : "min") << "("
               << par(x) << "*" << flit(sa) << " - " << par(y)
               << "*" << flit(ca) << ", " << par(x) << "*"
               << flit(sb) << " + " << par(y) << "*"
               << flit(cb) << ");\n";
    return v;
}

/// u = (z + hz) / (2 hz), the frustum interpolation.
std::string GlslEmitter::hep_u(Ctx& c, const std::string& z,
                               const std::string& hz) {
    auto v = c.fresh("u");
    c.sdf_body << "    float " << v << " = (" << par(z) << " + " << hz
               << ") / (2.0 * " << hz << ");\n";
    return v;
}

/// dst = max(dst, term)
void GlslEmitter::hep_fold(Ctx& c, const std::string& dst,
                           const std::string& term) {
    c.sdf_body << "    " << dst << " = max(" << dst << ", " << term << ");\n";
}

// ── Tube ─────────────────────────────────────────────────────────────────────
std::string GlslEmitter::emit_hep_tube(Ctx& c, const FRepNode& n,
                                       const std::string& x,
                                       const std::string& y,
                                       const std::string& z) {
    const std::string rho = hep_rho(c, x, y);
    const std::string rmax = pval(c, n, "rmax"), hz = pval(c, n, "hz");
    auto v = c.fresh("tube");
    c.sdf_body << "    float " << v << " = max(" << rho << " - " << rmax
               << ", abs(" << par(z) << ") - " << hz << ");\n";
    if (n.params.at("rmin") > 0.0)
        hep_fold(c, v, pval(c, n, "rmin") + " - " + rho);
    if (auto w = hep_wedge(c, n, x, y); !w.empty()) hep_fold(c, v, w);
    return v;
}

// ── Cone ─────────────────────────────────────────────────────────────────────
std::string GlslEmitter::emit_hep_cone(Ctx& c, const FRepNode& n,
                                       const std::string& x,
                                       const std::string& y,
                                       const std::string& z) {
    const double hzf = n.params.at("hz");
    const double a1 = n.params.at("rmax1"), a2 = n.params.at("rmax2");
    const std::string hz = pval(c, n, "hz");
    const std::string u = hep_u(c, z, hz);
    const std::string rho = hep_rho(c, x, y);

    auto Rmax = c.fresh("Rmax");
    c.sdf_body << "    float " << Rmax << " = " << pval(c, n, "rmax1") << " + "
               << flit(float(a2 - a1)) << " * " << u << ";\n";
    auto v = c.fresh("cone");
    c.sdf_body << "    float " << v << " = max((" << rho << " - " << Rmax
               << ") / " << flit(hep::lat_scale<float>(a2 - a1, 2.0 * hzf))
               << ", abs(" << par(z) << ") - " << hz << ");\n";

    const double i1 = n.params.at("rmin1"), i2 = n.params.at("rmin2");
    if (i1 > 0.0 || i2 > 0.0) {
        auto Rmin = c.fresh("Rmin");
        c.sdf_body << "    float " << Rmin << " = " << pval(c, n, "rmin1")
                   << " + " << flit(float(i2 - i1)) << " * " << u << ";\n";
        hep_fold(c, v,
                 "(" + Rmin + " - " + rho + ") / " +
                     flit(hep::lat_scale<float>(i2 - i1, 2.0 * hzf)));
    }
    if (auto w = hep_wedge(c, n, x, y); !w.empty()) hep_fold(c, v, w);
    return v;
}

// ── SphericalShell ───────────────────────────────────────────────────────────
std::string GlslEmitter::emit_hep_shell(Ctx& c, const FRepNode& n,
                                        const std::string& x,
                                        const std::string& y,
                                        const std::string& z) {
    const std::string rho = hep_rho(c, x, y);
    // rad from rho, not from x,y,z - see hep.hpp. The order matters: this is
    // a sqrt of a sum containing another sqrt, and reassociating it would
    // move the last bits away from the CPU.
    auto rad = c.fresh("rad");
    c.sdf_body << "    float " << rad << " = sqrt(" << rho << "*" << rho
               << " + " << par(z) << "*" << par(z) << ");\n";
    auto v = c.fresh("shell");
    c.sdf_body << "    float " << v << " = " << rad << " - "
               << pval(c, n, "rmax") << ";\n";
    if (n.params.at("rmin") > 0.0)
        hep_fold(c, v, pval(c, n, "rmin") + " - " + rad);
    if (auto w = hep_wedge(c, n, x, y); !w.empty()) hep_fold(c, v, w);

    const double t0 = n.params.at("theta0");
    const double t1 = t0 + n.params.at("dtheta");
    auto cone = [&](double t, bool neg) {
        const std::string e = rho + "*" + flit(float(std::cos(t))) + " - " +
                              par(z) + "*" + flit(float(std::sin(t)));
        return neg ? "-(" + e + ")" : "(" + e + ")";
    };
    if (t0 > 1e-6) hep_fold(c, v, cone(t0, true));
    if (t1 < M_PI - 1e-6) hep_fold(c, v, cone(t1, false));
    return v;
}

// ── Trapezoid ────────────────────────────────────────────────────────────────
std::string GlslEmitter::emit_hep_trd(Ctx& c, const FRepNode& n,
                                      const std::string& x,
                                      const std::string& y,
                                      const std::string& z) {
    const double hzf = n.params.at("hz");
    const std::string hz = pval(c, n, "hz");
    const std::string u = hep_u(c, z, hz);
    auto face = [&](const char* p0, double v0, double v1,
                    const std::string& coord) {
        auto half = c.fresh("half");
        c.sdf_body << "    float " << half << " = " << pval(c, n, p0) << " + "
                   << flit(float(v1 - v0)) << " * " << u << ";\n";
        return "((abs(" + par(coord) + ") - " + half + ") / " +
               flit(hep::lat_scale<float>(v1 - v0, 2.0 * hzf)) + ")";
    };
    const std::string fx =
        face("dx1", n.params.at("dx1"), n.params.at("dx2"), x);
    const std::string fy =
        face("dy1", n.params.at("dy1"), n.params.at("dy2"), y);
    auto v = c.fresh("trd");
    c.sdf_body << "    float " << v << " = max(max(" << fx << ", " << fy
               << "), abs(" << par(z) << ") - " << hz << ");\n";
    return v;
}

// ── Polyhedron ───────────────────────────────────────────────────────────────
std::string GlslEmitter::emit_hep_poly(Ctx& c, const FRepNode& n,
                                       const std::string& x,
                                       const std::string& y,
                                       const std::string& z) {
    const auto* p = static_cast<const hep::PolyhedronNode*>(&n);
    const std::vector<double>& fc = p->faces();
    // A max of linear functions, unrolled: branch-free, which is what every
    // GPU backend wants, and in the same order the IR takes them.
    auto r = c.fresh("pface");
    c.sdf_body << "    float " << r << " = ";
    for (std::size_t j = 0; j + 1 < fc.size(); j += 2) {
        const std::string term = par(x) + "*" + flit(float(fc[j])) + " + " +
                                 par(y) + "*" + flit(float(fc[j + 1]));
        if (j == 0) c.sdf_body << "(" << term << ")";
        else c.sdf_body << ";\n    " << r << " = max(" << r << ", " << term << ")";
    }
    c.sdf_body << ";\n";

    const double hzf = n.params.at("hz");
    const double a1 = n.params.at("rmax1"), a2 = n.params.at("rmax2");
    const std::string hz = pval(c, n, "hz");
    const std::string u = hep_u(c, z, hz);
    auto Rmax = c.fresh("Rmax");
    c.sdf_body << "    float " << Rmax << " = " << pval(c, n, "rmax1") << " + "
               << flit(float(a2 - a1)) << " * " << u << ";\n";
    auto v = c.fresh("poly");
    c.sdf_body << "    float " << v << " = max((" << r << " - " << Rmax
               << ") / " << flit(hep::lat_scale<float>(a2 - a1, 2.0 * hzf))
               << ", abs(" << par(z) << ") - " << hz << ");\n";

    const double i1 = n.params.at("rmin1"), i2 = n.params.at("rmin2");
    if (i1 > 0.0 || i2 > 0.0) {
        auto Rmin = c.fresh("Rmin");
        c.sdf_body << "    float " << Rmin << " = " << pval(c, n, "rmin1")
                   << " + " << flit(float(i2 - i1)) << " * " << u << ";\n";
        hep_fold(c, v,
                 "(" + Rmin + " - " + r + ") / " +
                     flit(hep::lat_scale<float>(i2 - i1, 2.0 * hzf)));
    }
    if (auto w = hep_wedge(c, n, x, y); !w.empty()) hep_fold(c, v, w);
    return v;
}

// ── Frame: q = R^T (p - t) ───────────────────────────────────────────────────
std::string GlslEmitter::emit_hep_frame_coords(Ctx& c, const FRepNode& n,
                                               const std::string& x,
                                               const std::string& y,
                                               const std::string& z,
                                               std::string& qx, std::string& qy,
                                               std::string& qz) {
    auto dx = c.fresh("fdx"), dy = c.fresh("fdy"), dz = c.fresh("fdz");
    c.sdf_body << "    float " << dx << " = " << par(x) << " - "
               << pval(c, n, "tx") << ";\n"
               << "    float " << dy << " = " << par(y) << " - "
               << pval(c, n, "ty") << ";\n"
               << "    float " << dz << " = " << par(z) << " - "
               << pval(c, n, "tz") << ";\n";
    auto row = [&](const char* a, const char* d, const char* g,
                   const char* nm) {
        auto q = c.fresh(nm);
        c.sdf_body << "    float " << q << " = " << pval(c, n, a) << "*" << dx
                   << " + " << pval(c, n, d) << "*" << dy << " + "
                   << pval(c, n, g) << "*" << dz << ";\n";
        return q;
    };
    qx = row("r0", "r3", "r6", "fqx");
    qy = row("r1", "r4", "r7", "fqy");
    qz = row("r2", "r5", "r8", "fqz");
    return qx;
}

// ═════════════════════════════════════════════════════════════════════════════
// The same six in dual arithmetic.
//
// Mirrors hep_ad.cpp, which mirrors hep_codegen.cpp, which mirrors eval_t.
// Four descriptions of one solid, equal only because each was written
// against the last; the tests are what make that a fact rather than a hope.
//
// d_max already picks the winning branch's gradient, so a max of pieces
// carries the active piece's subgradient with no extra work - the same
// property ad_ir::max has on the CPU.
// ═════════════════════════════════════════════════════════════════════════════

std::string GlslEmitter::hep_rho_d(Ctx& c, const std::string& x,
                                   const std::string& y) {
    auto v = c.fresh_d();
    c.grad_body << "    Dual " << v << " = d_sqrt(d_add(d_mul(" << x << ", "
                << x << "), d_mul(" << y << ", " << y << ")));\n";
    return v;
}

std::string GlslEmitter::hep_wedge_d(Ctx& c, const FRepNode& n,
                                     const std::string& x,
                                     const std::string& y) {
    float sa, ca, sb, cb;
    bool use_max = true;
    if (!hep::wedge_coeffs<float>(float(n.params.at("phi0")),
                                  float(n.params.at("dphi")), sa, ca, sb, cb,
                                  use_max))
        return {};
    auto v = c.fresh_d();
    c.grad_body << "    Dual " << v << " = " << (use_max ? "d_max" : "d_min")
                << "(d_sub(d_mul_s(" << x << ", " << flit(sa)
                << "), d_mul_s(" << y << ", " << flit(ca)
                << ")), d_add(d_mul_s(" << x << ", " << flit(sb)
                << "), d_mul_s(" << y << ", " << flit(cb) << ")));\n";
    return v;
}

std::string GlslEmitter::hep_u_d(Ctx& c, const std::string& z,
                                 const std::string& hz) {
    auto v = c.fresh_d();
    c.grad_body << "    Dual " << v << " = d_div_s(d_add_s(" << z << ", " << hz
                << "), 2.0 * " << hz << ");\n";
    return v;
}

void GlslEmitter::hep_fold_d(Ctx& c, const std::string& dst,
                             const std::string& term) {
    c.grad_body << "    " << dst << " = d_max(" << dst << ", " << term
                << ");\n";
}

// ── Tube ─────────────────────────────────────────────────────────────────────
std::string GlslEmitter::emit_hep_tube_dual(Ctx& c, const FRepNode& n,
                                            const std::string& x,
                                            const std::string& y,
                                            const std::string& z) {
    const std::string rho = hep_rho_d(c, x, y);
    auto v = c.fresh_d();
    c.grad_body << "    Dual " << v << " = d_max(d_sub_s(" << rho << ", "
                << pval(c, n, "rmax") << "), d_sub_s(d_abs(" << z << "), "
                << pval(c, n, "hz") << "));\n";
    if (n.params.at("rmin") > 0.0)
        hep_fold_d(c, v, "d_neg(d_sub_s(" + rho + ", " + pval(c, n, "rmin") + "))");
    if (auto w = hep_wedge_d(c, n, x, y); !w.empty()) hep_fold_d(c, v, w);
    return v;
}

// ── Cone ─────────────────────────────────────────────────────────────────────
std::string GlslEmitter::emit_hep_cone_dual(Ctx& c, const FRepNode& n,
                                            const std::string& x,
                                            const std::string& y,
                                            const std::string& z) {
    const double hzf = n.params.at("hz");
    const double a1 = n.params.at("rmax1"), a2 = n.params.at("rmax2");
    const std::string hz = pval(c, n, "hz");
    const std::string u = hep_u_d(c, z, hz);
    const std::string rho = hep_rho_d(c, x, y);
    auto Rmax = c.fresh_d();
    c.grad_body << "    Dual " << Rmax << " = d_add_s(d_mul_s(" << u << ", "
                << flit(float(a2 - a1)) << "), " << pval(c, n, "rmax1")
                << ");\n";
    auto v = c.fresh_d();
    c.grad_body << "    Dual " << v << " = d_max(d_div_s(d_sub(" << rho << ", "
                << Rmax << "), " << flit(hep::lat_scale<float>(a2 - a1, 2.0 * hzf))
                << "), d_sub_s(d_abs(" << z << "), " << hz << "));\n";
    const double i1 = n.params.at("rmin1"), i2 = n.params.at("rmin2");
    if (i1 > 0.0 || i2 > 0.0) {
        auto Rmin = c.fresh_d();
        c.grad_body << "    Dual " << Rmin << " = d_add_s(d_mul_s(" << u << ", "
                    << flit(float(i2 - i1)) << "), " << pval(c, n, "rmin1")
                    << ");\n";
        hep_fold_d(c, v,
                   "d_div_s(d_sub(" + Rmin + ", " + rho + "), " +
                       flit(hep::lat_scale<float>(i2 - i1, 2.0 * hzf)) + ")");
    }
    if (auto w = hep_wedge_d(c, n, x, y); !w.empty()) hep_fold_d(c, v, w);
    return v;
}

// ── SphericalShell ───────────────────────────────────────────────────────────
std::string GlslEmitter::emit_hep_shell_dual(Ctx& c, const FRepNode& n,
                                             const std::string& x,
                                             const std::string& y,
                                             const std::string& z) {
    const std::string rho = hep_rho_d(c, x, y);
    auto rad = c.fresh_d();
    c.grad_body << "    Dual " << rad << " = d_sqrt(d_add(d_mul(" << rho << ", "
                << rho << "), d_mul(" << z << ", " << z << ")));\n";
    auto v = c.fresh_d();
    c.grad_body << "    Dual " << v << " = d_sub_s(" << rad << ", "
                << pval(c, n, "rmax") << ");\n";
    if (n.params.at("rmin") > 0.0)
        hep_fold_d(c, v, "d_neg(d_sub_s(" + rad + ", " + pval(c, n, "rmin") + "))");
    if (auto w = hep_wedge_d(c, n, x, y); !w.empty()) hep_fold_d(c, v, w);

    const double t0 = n.params.at("theta0");
    const double t1 = t0 + n.params.at("dtheta");
    auto cone = [&](double t, bool neg) {
        const std::string e = "d_sub(d_mul_s(" + rho + ", " +
                              flit(float(std::cos(t))) + "), d_mul_s(" + z +
                              ", " + flit(float(std::sin(t))) + "))";
        return neg ? "d_neg(" + e + ")" : e;
    };
    if (t0 > 1e-6) hep_fold_d(c, v, cone(t0, true));
    if (t1 < M_PI - 1e-6) hep_fold_d(c, v, cone(t1, false));
    return v;
}

// ── Trapezoid ────────────────────────────────────────────────────────────────
std::string GlslEmitter::emit_hep_trd_dual(Ctx& c, const FRepNode& n,
                                           const std::string& x,
                                           const std::string& y,
                                           const std::string& z) {
    const double hzf = n.params.at("hz");
    const std::string hz = pval(c, n, "hz");
    const std::string u = hep_u_d(c, z, hz);
    auto face = [&](const char* p0, double v0, double v1,
                    const std::string& coord) {
        auto half = c.fresh_d();
        c.grad_body << "    Dual " << half << " = d_add_s(d_mul_s(" << u << ", "
                    << flit(float(v1 - v0)) << "), " << pval(c, n, p0) << ");\n";
        return "d_div_s(d_sub(d_abs(" + coord + "), " + half + "), " +
               flit(hep::lat_scale<float>(v1 - v0, 2.0 * hzf)) + ")";
    };
    const std::string fx = face("dx1", n.params.at("dx1"), n.params.at("dx2"), x);
    const std::string fy = face("dy1", n.params.at("dy1"), n.params.at("dy2"), y);
    auto v = c.fresh_d();
    c.grad_body << "    Dual " << v << " = d_max(d_max(" << fx << ", " << fy
                << "), d_sub_s(d_abs(" << z << "), " << hz << "));\n";
    return v;
}

// ── Polyhedron ───────────────────────────────────────────────────────────────
std::string GlslEmitter::emit_hep_poly_dual(Ctx& c, const FRepNode& n,
                                            const std::string& x,
                                            const std::string& y,
                                            const std::string& z) {
    const auto* p = static_cast<const hep::PolyhedronNode*>(&n);
    const std::vector<double>& fc = p->faces();
    auto r = c.fresh_d();
    for (std::size_t j = 0; j + 1 < fc.size(); j += 2) {
        const std::string term = "d_add(d_mul_s(" + x + ", " +
                                 flit(float(fc[j])) + "), d_mul_s(" + y + ", " +
                                 flit(float(fc[j + 1])) + "))";
        if (j == 0)
            c.grad_body << "    Dual " << r << " = " << term << ";\n";
        else
            c.grad_body << "    " << r << " = d_max(" << r << ", " << term
                        << ");\n";
    }
    const double hzf = n.params.at("hz");
    const double a1 = n.params.at("rmax1"), a2 = n.params.at("rmax2");
    const std::string hz = pval(c, n, "hz");
    const std::string u = hep_u_d(c, z, hz);
    auto Rmax = c.fresh_d();
    c.grad_body << "    Dual " << Rmax << " = d_add_s(d_mul_s(" << u << ", "
                << flit(float(a2 - a1)) << "), " << pval(c, n, "rmax1") << ");\n";
    auto v = c.fresh_d();
    c.grad_body << "    Dual " << v << " = d_max(d_div_s(d_sub(" << r << ", "
                << Rmax << "), " << flit(hep::lat_scale<float>(a2 - a1, 2.0 * hzf))
                << "), d_sub_s(d_abs(" << z << "), " << hz << "));\n";
    const double i1 = n.params.at("rmin1"), i2 = n.params.at("rmin2");
    if (i1 > 0.0 || i2 > 0.0) {
        auto Rmin = c.fresh_d();
        c.grad_body << "    Dual " << Rmin << " = d_add_s(d_mul_s(" << u << ", "
                    << flit(float(i2 - i1)) << "), " << pval(c, n, "rmin1")
                    << ");\n";
        hep_fold_d(c, v,
                   "d_div_s(d_sub(" + Rmin + ", " + r + "), " +
                       flit(hep::lat_scale<float>(i2 - i1, 2.0 * hzf)) + ")");
    }
    if (auto w = hep_wedge_d(c, n, x, y); !w.empty()) hep_fold_d(c, v, w);
    return v;
}

// ── Frame ────────────────────────────────────────────────────────────────────
void GlslEmitter::emit_hep_frame_dual_coords(Ctx& c, const FRepNode& n,
                                             const std::string& x,
                                             const std::string& y,
                                             const std::string& z,
                                             std::string& qx, std::string& qy,
                                             std::string& qz) {
    // R is constant, so the duals transform by the same linear map and no
    // Jacobian factor is needed; a rotation returns a unit gradient unit.
    auto dx = c.fresh_d(), dy = c.fresh_d(), dz = c.fresh_d();
    c.grad_body << "    Dual " << dx << " = d_sub_s(" << x << ", "
                << pval(c, n, "tx") << ");\n"
                << "    Dual " << dy << " = d_sub_s(" << y << ", "
                << pval(c, n, "ty") << ");\n"
                << "    Dual " << dz << " = d_sub_s(" << z << ", "
                << pval(c, n, "tz") << ");\n";
    auto row = [&](const char* a, const char* d, const char* g) {
        auto q = c.fresh_d();
        c.grad_body << "    Dual " << q << " = d_add(d_add(d_mul_s(" << dx
                    << ", " << pval(c, n, a) << "), d_mul_s(" << dy << ", "
                    << pval(c, n, d) << ")), d_mul_s(" << dz << ", "
                    << pval(c, n, g) << "));\n";
        return q;
    };
    qx = row("r0", "r3", "r6");
    qy = row("r1", "r4", "r7");
    qz = row("r2", "r5", "r8");
}

}  // namespace frep::gpu
