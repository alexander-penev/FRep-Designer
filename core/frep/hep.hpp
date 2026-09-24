#pragma once
// core/frep/hep.hpp
//
// The six node types a high-energy-physics detector geometry needs and the
// built-in set does not have. They exist because a Geant4 / GDML detector is
// made almost entirely of tubes, cones, trapezoids and polyhedra placed by
// arbitrary rotations, and without them a converted detector arrives as
// CustomExpr leaves - which work on every compute path but have NO aabb()
// override, so one of them makes the whole scene's box infinite and the BVH,
// the RTX proxy (core/exec/rtx_executor.hpp) and marching cubes all lose it.
//
// EACH NODE IS 1:1 WITH ITS G4 SOLID, parameter for parameter and in the same
// order. That is deliberate: a converter that mirrors its source can be
// checked against that source, and one that "improves" on it cannot. The
// fields below are the same arithmetic, written in the same order, as the
// reference implementation in the G4-FRep converter - not merely the same
// value. Reordering a sum here would change the last bits and break the
// bit-identity check that certifies the conversion.
//
//   Tube            G4Tubs        rmin rmax hz phi0 dphi
//   Cone            G4Cons        rmin1 rmax1 rmin2 rmax2 hz phi0 dphi
//   SphericalShell  G4Sphere      rmin rmax phi0 dphi theta0 dtheta
//   Trapezoid       G4Trd         dx1 dx2 dy1 dy2 hz
//   Polyhedron      G4Polyhedra   rmin1 rmax1 rmin2 rmax2 hz phi0 dphi nside
//   Frame           a rigid 3x4   r0..r8 tx ty tz
//
// A POLYCONE NEEDS NO NODE: it is a union of Cone sections, which is what
// both Geant4 and the converter already make it.
//
// WHY Frame, when Translate and RotateX/Y/Z exist. Three successive
// single-axis rotations cannot reproduce one stored 3x3 to the bit - the
// angles come out of an atan2/asin extraction and each rotation rounds again.
// Measured on the converter's own models: 3.41e-13 mm, in DOUBLE, before
// float ever enters. Frame also collapses up to four nodes into one, and a
// detector has one placement per volume: calib_world is 25 nodes as a tree
// and 43 once its frames are expanded into single-axis rotations.
//
// SLANTED FACES ARE DIVIDED BY sqrt(1+k^2). That factor is what makes a cone's
// or a trapezoid's field the true distance to the slanted plane rather than a
// radial difference, which is what keeps the max() of the pieces 1-Lipschitz
// and therefore safe for sphere tracing.
//
// ANGULAR PARAMETERS STAY CONSTANT IN THE INCREMENTAL PATH. phi0, dphi,
// theta0, dtheta and nside enter the IR through sines and cosines folded at
// compile time; the radii, half-lengths and frame entries go through
// param_value and can be runtime slots. A sin() of a loaded value would be
// possible but it would also make eval() and codegen() disagree the moment a
// policy promoted one of them, so the two are kept in step by construction.

#include "node.hpp"
#include "scalar.hpp"
#include "core/compiler/llvm_compat.hpp"

#include <cmath>
#include <functional>
#include <vector>

namespace frep::hep {

// Combine parameter values into one hash. Every node below uses it, so two
// nodes of the same type differing in any parameter differ here too.
inline std::size_t phash(std::size_t seed, std::initializer_list<double> vs) {
    for (double v : vs)
        seed = seed * 1099511628211ull ^ std::hash<double>{}(v);
    return seed;
}

/// The angular sector [a, a+d] as two half-planes through the z axis. Exact
/// signed distance to each, so the result is 1-Lipschitz; the reflex case
/// (d > pi) is a union of the two half-spaces, hence the min.
///
/// Returns false when there is no cut at all, in which case the caller omits
/// the term entirely - max(f, -inf) is f, so omitting is value-identical and
/// saves the node an instruction.
/// The trigonometry is done in DOUBLE and narrowed once. The parameters are
/// float - that narrowing is the export's own loss and is measured elsewhere -
/// but computing sin/cos of them in float would add a second, avoidable one,
/// and it is what made the first version of this node disagree with the
/// reference implementation by 2.4e-04 on a hexagonal section.
template <class T>
inline bool wedge_coeffs(T a, T d, T& sa, T& ca, T& sb, T& cb, bool& use_max) {
    using S = ScalarTraits<T>;
    if (d >= S::from(2.0 * M_PI) - S::from(1e-6)) return false;
    const double b = double(a) + double(d);
    sa = S::from(std::sin(double(a)));
    ca = S::from(std::cos(double(a)));
    sb = S::from(-std::sin(b));
    cb = S::from(std::cos(b));
    use_max = (d <= S::from(M_PI));
    return true;
}
/// sqrt(1 + k^2) for a slanted face, computed in DOUBLE and narrowed once.
/// The same reason as wedge_coeffs: the parameters are float, but squaring a
/// float slope and taking a float square root adds a second rounding the
/// reference implementation does not have, and that was the last bit-identity
/// failure - one tapered hexagonal section, 1.22e-04.
template <class T>
inline T lat_scale(double num, double den) {
    const double k = num / den;
    return ScalarTraits<T>::from(std::sqrt(1.0 + k * k));
}

template <class T>
inline T wedge_eval(T x, T y, T a, T d) {
    using S = ScalarTraits<T>;
    T sa, ca, sb, cb;
    bool use_max;
    if (!wedge_coeffs<T>(a, d, sa, ca, sb, cb, use_max)) return S::from(-1e30);
    const T fa = x * sa - y * ca;
    const T fb = x * sb + y * cb;
    return use_max ? S::maxv(fa, fb) : S::minv(fa, fb);
}

// ── Tube (G4Tubs) ────────────────────────────────────────────────────────────
// f = max( rho - rmax, |z| - hz [, rmin - rho] [, wedge] )
class TubeNode final : public FRepNode {
    const char* type_name() const noexcept override { return "Tube"; }
public:
    enum : int { Rmin, Rmax, Hz, Phi0, Dphi };
    TubeNode(double rmin, double rmax, double hz, double phi0, double dphi,
             std::string nid = "tube") {
        kind = NodeKind::Tube;
        id = std::move(nid);
        params.init({"rmin", "rmax", "hz", "phi0", "dphi"},
                    {rmin, rmax, hz, phi0, dphi});
    }

    template <class T>
    T eval_t(T x, T y, T z) const {
        using S = ScalarTraits<T>;
        const T rho = S::sqrtv(x * x + y * y);
        T f = S::maxv(rho - S::from(params[Rmax]), S::absv(z) - S::from(params[Hz]));
        if (params[Rmin] > 0.0) f = S::maxv(f, S::from(params[Rmin]) - rho);
        return S::maxv(f, wedge_eval<T>(x, y, S::from(params[Phi0]), S::from(params[Dphi])));
    }
    FREP_EVAL_T

    llvm::Value* codegen(CgCtx& c, llvm::Value* x, llvm::Value* y,
                         llvm::Value* z) const override;

    /// Analytic forward-mode gradient. Mirrors eval_t/codegen piece for
    /// piece; the subgradient at a max() seam is the active piece's, which
    /// is what ad_ir::max already selects.
    DualVal codegen_grad(CgCtx& c, DualVal x, DualVal y,
                         DualVal z) const override;

    AABB aabb() const override {
        const float r = params[Rmax], h = params[Hz];
        return {-r, -r, -h, r, r, h};
    }

    std::size_t structural_hash() const noexcept override {
        return phash(0x7C0B'E101ull,
                     {params[Rmin], params[Rmax], params[Hz],
                      params[Phi0], params[Dphi]});
    }
};

// ── Cone (G4Cons) ────────────────────────────────────────────────────────────
// A frustum: the radii vary linearly with z, and each slanted face is divided
// by sqrt(1+k^2) so the value is the distance to the face, not a radial gap.
class ConeNode final : public FRepNode {
    const char* type_name() const noexcept override { return "Cone"; }
public:
    enum : int { Rmin1, Rmax1, Rmin2, Rmax2, Hz, Phi0, Dphi };
    ConeNode(double rmin1, double rmax1, double rmin2, double rmax2, double hz,
             double phi0, double dphi, std::string nid = "cone") {
        kind = NodeKind::Cone;
        id = std::move(nid);
        params.init({"rmin1", "rmax1", "rmin2", "rmax2", "hz", "phi0", "dphi"},
                    {rmin1, rmax1, rmin2, rmax2, hz, phi0, dphi});
    }

    template <class T>
    T eval_t(T x, T y, T z) const {
        using S = ScalarTraits<T>;
        const T rho = S::sqrtv(x * x + y * y);
        const T hz = S::from(params[Hz]);
        const T u = (z + hz) / (S::from(2.0) * hz);
        const T r1 = S::from(params[Rmax1]), r2 = S::from(params[Rmax2]);
        const T Rmax = r1 + (r2 - r1) * u;
        T f = S::maxv((rho - Rmax) / lat_scale<T>(double(r2) - double(r1),
                                                  2.0 * double(hz)),
                      S::absv(z) - hz);
        const T i1 = S::from(params[Rmin1]), i2 = S::from(params[Rmin2]);
        if (i1 > S::from(0.0) || i2 > S::from(0.0)) {
            const T Rmin = i1 + (i2 - i1) * u;
            f = S::maxv(f, (Rmin - rho) / lat_scale<T>(double(i2) - double(i1),
                                                       2.0 * double(hz)));
        }
        return S::maxv(f, wedge_eval<T>(x, y, S::from(params[Phi0]), S::from(params[Dphi])));
    }
    FREP_EVAL_T

    llvm::Value* codegen(CgCtx& c, llvm::Value* x, llvm::Value* y,
                         llvm::Value* z) const override;

    /// Analytic forward-mode gradient. Mirrors eval_t/codegen piece for
    /// piece; the subgradient at a max() seam is the active piece's, which
    /// is what ad_ir::max already selects.
    DualVal codegen_grad(CgCtx& c, DualVal x, DualVal y,
                         DualVal z) const override;

    AABB aabb() const override {
        const float r = std::max(params[Rmax1], params[Rmax2]);
        const float h = params[Hz];
        return {-r, -r, -h, r, r, h};
    }

    std::size_t structural_hash() const noexcept override {
        return phash(0x7C0B'E102ull,
                     {params[Rmin1], params[Rmax1], params[Rmin2],
                      params[Rmax2], params[Hz], params[Phi0],
                      params[Dphi]});
    }
};

// ── SphericalShell (G4Sphere) ────────────────────────────────────────────────
// A shell between two radii, optionally cut by a phi wedge and by two theta
// cones through the origin. rho*cos(t) - z*sin(t) is the exact signed distance
// to the cone of half-angle t.
//
// rad is sqrt(rho*rho + z*z) with rho ALREADY a square root - not
// sqrt(x*x+y*y+z*z). The two are not the same float, and the reference
// implementation computes the former.
class SphericalShellNode final : public FRepNode {
    const char* type_name() const noexcept override { return "SphericalShell"; }
public:
    enum : int { Rmin, Rmax, Phi0, Dphi, Theta0, Dtheta };
    SphericalShellNode(double rmin, double rmax, double phi0, double dphi,
                       float theta0, float dtheta, std::string nid = "shell") {
        kind = NodeKind::SphericalShell;
        id = std::move(nid);
        params.init({"rmin", "rmax", "phi0", "dphi", "theta0", "dtheta"},
                    {rmin, rmax, phi0, dphi, theta0, dtheta});
    }

    template <class T>
    T eval_t(T x, T y, T z) const {
        using S = ScalarTraits<T>;
        const T rho = S::sqrtv(x * x + y * y);
        const T rad = S::sqrtv(rho * rho + z * z);
        T f = rad - S::from(params[Rmax]);
        if (params[Rmin] > 0.0) f = S::maxv(f, S::from(params[Rmin]) - rad);
        f = S::maxv(f, wedge_eval<T>(x, y, S::from(params[Phi0]), S::from(params[Dphi])));
        const T t0 = S::from(params[Theta0]);
        const T t1 = t0 + S::from(params[Dtheta]);
        // cos/sin in double and narrowed once - see wedge_coeffs.
        if (t0 > S::from(1e-6))
            f = S::maxv(f, -(rho * S::from(std::cos(double(t0))) -
                             z * S::from(std::sin(double(t0)))));
        if (t1 < S::from(M_PI) - S::from(1e-6))
            f = S::maxv(f, rho * S::from(std::cos(double(t1))) -
                              z * S::from(std::sin(double(t1))));
        return f;
    }
    FREP_EVAL_T

    llvm::Value* codegen(CgCtx& c, llvm::Value* x, llvm::Value* y,
                         llvm::Value* z) const override;

    /// Analytic forward-mode gradient. Mirrors eval_t/codegen piece for
    /// piece; the subgradient at a max() seam is the active piece's, which
    /// is what ad_ir::max already selects.
    DualVal codegen_grad(CgCtx& c, DualVal x, DualVal y,
                         DualVal z) const override;

    AABB aabb() const override {
        const float r = params[Rmax];
        return {-r, -r, -r, r, r, r};
    }

    std::size_t structural_hash() const noexcept override {
        return phash(0x7C0B'E103ull,
                     {params[Rmin], params[Rmax], params[Phi0],
                      params[Dphi], params[Theta0],
                      params[Dtheta]});
    }
};

// ── Trapezoid (G4Trd) ────────────────────────────────────────────────────────
// Six planar faces; the four slanted ones are divided by their own
// sqrt(1+k^2). No curvature anywhere in this solid.
class TrapezoidNode final : public FRepNode {
    const char* type_name() const noexcept override { return "Trapezoid"; }
public:
    enum : int { Dx1, Dx2, Dy1, Dy2, Hz };
    TrapezoidNode(double dx1, double dx2, double dy1, double dy2, double hz,
                  std::string nid = "trd") {
        kind = NodeKind::Trapezoid;
        id = std::move(nid);
        params.init({"dx1", "dx2", "dy1", "dy2", "hz"},
                    {dx1, dx2, dy1, dy2, hz});
    }

    template <class T>
    T eval_t(T x, T y, T z) const {
        using S = ScalarTraits<T>;
        const T hz = S::from(params[Hz]);
        const T u = (z + hz) / (S::from(2.0) * hz);
        const T x1 = S::from(params[Dx1]), x2 = S::from(params[Dx2]);
        const T y1 = S::from(params[Dy1]), y2 = S::from(params[Dy2]);
        const T X = x1 + (x2 - x1) * u, Y = y1 + (y2 - y1) * u;
        const T fx = (S::absv(x) - X) /
                     lat_scale<T>(double(x2) - double(x1), 2.0 * double(hz));
        const T fy = (S::absv(y) - Y) /
                     lat_scale<T>(double(y2) - double(y1), 2.0 * double(hz));
        return S::maxv(S::maxv(fx, fy), S::absv(z) - hz);
    }
    FREP_EVAL_T

    llvm::Value* codegen(CgCtx& c, llvm::Value* x, llvm::Value* y,
                         llvm::Value* z) const override;

    /// Analytic forward-mode gradient. Mirrors eval_t/codegen piece for
    /// piece; the subgradient at a max() seam is the active piece's, which
    /// is what ad_ir::max already selects.
    DualVal codegen_grad(CgCtx& c, DualVal x, DualVal y,
                         DualVal z) const override;

    AABB aabb() const override {
        const float ax = std::max(params[Dx1], params[Dx2]);
        const float ay = std::max(params[Dy1], params[Dy2]);
        const float h = params[Hz];
        return {-ax, -ay, -h, ax, ay, h};
    }

    std::size_t structural_hash() const noexcept override {
        return phash(0x7C0B'E104ull,
                     {params[Dx1], params[Dx2], params[Dy1],
                      params[Dy2], params[Hz]});
    }
};

// ── Polyhedron (one G4Polyhedra section) ─────────────────────────────────────
// A cone whose radial coordinate is measured to the nearest FLAT face instead
// of to the axis. rmin/rmax are TANGENT distances to those faces, which is
// G4Polyhedra's own convention and what GDML passes through.
//
// The face coordinate is written as a MAX OF LINEAR FUNCTIONS, not as an
// atan2 plus a sector lookup. A max of linear functions is continuous
// everywhere; a lookup needs a branch cut, and wherever that cut lands the
// field jumps. It is also branch-free, which every GPU backend wants.
class PolyhedronNode final : public FRepNode {
    const char* type_name() const noexcept override { return "Polyhedron"; }
public:
    enum : int { Rmin1, Rmax1, Rmin2, Rmax2, Hz, Phi0, Dphi, Nside };
    PolyhedronNode(double rmin1, double rmax1, double rmin2, double rmax2, double hz,
                   float phi0, float dphi, int nside, std::string nid = "poly") {
        kind = NodeKind::Polyhedron;
        id = std::move(nid);
        params.init({"rmin1", "rmax1", "rmin2", "rmax2", "hz", "phi0", "dphi",
                     "nside"},
                    {rmin1, rmax1, rmin2, rmax2, hz, phi0, dphi, double(nside)});
    }

    /// The face normals, derived from phi0, dphi and nside. Recomputing the
    /// cos/sin per face per evaluation costs more than the whole rest of the
    /// primitive - 315 ns against 39 ns for a six-sided section in the
    /// reference implementation - so they are cached, and the cache is keyed
    /// on the three parameters it derives from so that editing one of them
    /// cannot leave the field evaluating the old polygon.
    const std::vector<double>& faces() const {
        const double p0 = params[Phi0], dp = params[Dphi],
                     ns = params[Nside];
        if (f_.size() != std::size_t(2 * int(ns)) || fp0_ != p0 || fdp_ != dp) {
            const int n = int(ns);
            f_.resize(std::size_t(2 * n));
            // In double, narrowed once - see wedge_coeffs.
            const double w = double(dp) / double(n);
            for (int j = 0; j < n; ++j) {
                const double ph = double(p0) + (double(j) + 0.5) * w;
                f_[std::size_t(2 * j)] = std::cos(ph);
                f_[std::size_t(2 * j + 1)] = std::sin(ph);
            }
            fp0_ = p0;
            fdp_ = dp;
        }
        return f_;
    }

    template <class T>
    T eval_t(T x, T y, T z) const {
        using S = ScalarTraits<T>;
        const std::vector<double>& fc = faces();
        T r = S::from(-1e30);
        for (std::size_t j = 0; j + 1 < fc.size(); j += 2)
            r = S::maxv(r, x * S::from(fc[j]) + y * S::from(fc[j + 1]));
        const T hz = S::from(params[Hz]);
        const T u = (z + hz) / (S::from(2.0) * hz);
        const T a1 = S::from(params[Rmax1]), a2 = S::from(params[Rmax2]);
        const T Rmax = a1 + (a2 - a1) * u;
        T f = S::maxv((r - Rmax) / lat_scale<T>(double(a2) - double(a1),
                                                2.0 * double(hz)),
                      S::absv(z) - hz);
        const T i1 = S::from(params[Rmin1]), i2 = S::from(params[Rmin2]);
        if (i1 > S::from(0.0) || i2 > S::from(0.0)) {
            const T Rmin = i1 + (i2 - i1) * u;
            f = S::maxv(f, (Rmin - r) / lat_scale<T>(double(i2) - double(i1),
                                                     2.0 * double(hz)));
        }
        return S::maxv(f, wedge_eval<T>(x, y, S::from(params[Phi0]), S::from(params[Dphi])));
    }
    FREP_EVAL_T

    llvm::Value* codegen(CgCtx& c, llvm::Value* x, llvm::Value* y,
                         llvm::Value* z) const override;

    /// Analytic forward-mode gradient. Mirrors eval_t/codegen piece for
    /// piece; the subgradient at a max() seam is the active piece's, which
    /// is what ad_ir::max already selects.
    DualVal codegen_grad(CgCtx& c, DualVal x, DualVal y,
                         DualVal z) const override;

    /// rmax is the distance to a FACE, so the corner radius is larger by
    /// 1/cos(w/2). Using rmax directly would cut the corners off the box and
    /// make the BVH drop rays that really do hit the solid.
    AABB aabb() const override {
        const float n = params[Nside];
        const float w = params[Dphi] / (n > 0.0f ? n : 1.0f);
        const float r = std::max(params[Rmax1], params[Rmax2]) /
                        std::cos(0.5f * w);
        const float h = params[Hz];
        return {-r, -r, -h, r, r, h};
    }

    std::size_t structural_hash() const noexcept override {
        return phash(0x7C0B'E105ull,
                     {params[Rmin1], params[Rmax1], params[Rmin2],
                      params[Rmax2], params[Hz], params[Phi0],
                      params[Dphi], params[Nside]});
    }

private:
    mutable std::vector<double> f_;
    mutable double fp0_ = 1e30, fdp_ = 1e30;
};

// ── Frame: one rigid placement ───────────────────────────────────────────────
// The child sees q = R^T (p - t), with R row-major in r0..r8. This is the same
// convention the G4-FRep converter stores, so a placement copies across
// verbatim - no angle extraction, no re-composition, no rounding.
class FrameNode final : public FRepNode {
    const char* type_name() const noexcept override { return "Frame"; }
public:
    enum : int { R0, R1, R2, R3, R4, R5, R6, R7, R8, Tx, Ty, Tz };
    FrameNode(Ptr child, const double r[9], double tx, double ty, double tz,
              std::string nid = "frame") {
        kind = NodeKind::Frame;
        id = std::move(nid);
        children.push_back(std::move(child));
        params.init({"r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8",
                     "tx", "ty", "tz"},
                    {r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8],
                     tx, ty, tz});
    }

    template <class T>
    T eval_t(T x, T y, T z) const {
        using S = ScalarTraits<T>;
        const T dx = x - S::from(params[Tx]), dy = y - S::from(params[Ty]), dz = z - S::from(params[Tz]);
        const T r0 = S::from(params[R0]), r1 = S::from(params[R1]), r2 = S::from(params[R2]);
        const T r3 = S::from(params[R3]), r4 = S::from(params[R4]), r5 = S::from(params[R5]);
        const T r6 = S::from(params[R6]), r7 = S::from(params[R7]), r8 = S::from(params[R8]);
        return children[0]->eval_as<T>(r0 * dx + r3 * dy + r6 * dz,
                                       r1 * dx + r4 * dy + r7 * dz,
                                       r2 * dx + r5 * dy + r8 * dz);
    }
    FREP_EVAL_T

    llvm::Value* codegen(CgCtx& c, llvm::Value* x, llvm::Value* y,
                         llvm::Value* z) const override;

    /// Analytic forward-mode gradient. Mirrors eval_t/codegen piece for
    /// piece; the subgradient at a max() seam is the active piece's, which
    /// is what ad_ir::max already selects.
    DualVal codegen_grad(CgCtx& c, DualVal x, DualVal y,
                         DualVal z) const override;

    /// The child's box mapped forward (p = R q + t) and re-boxed. The AABB of
    /// a transformed AABB is conservative, which is the safe direction.
    AABB aabb() const override {
        const AABB a = children[0]->aabb();
        const float r0 = params[R0], r1 = params[R1], r2 = params[R2];
        const float r3 = params[R3], r4 = params[R4], r5 = params[R5];
        const float r6 = params[R6], r7 = params[R7], r8 = params[R8];
        const float tx = params[Tx], ty = params[Ty], tz = params[Tz];
        AABB o{1e30f, 1e30f, 1e30f, -1e30f, -1e30f, -1e30f};
        for (int k = 0; k < 8; ++k) {
            const float qx = (k & 1) ? a.max_x : a.min_x;
            const float qy = (k & 2) ? a.max_y : a.min_y;
            const float qz = (k & 4) ? a.max_z : a.min_z;
            const float px = r0 * qx + r1 * qy + r2 * qz + tx;
            const float py = r3 * qx + r4 * qy + r5 * qz + ty;
            const float pz = r6 * qx + r7 * qy + r8 * qz + tz;
            o.min_x = std::min(o.min_x, px); o.max_x = std::max(o.max_x, px);
            o.min_y = std::min(o.min_y, py); o.max_y = std::max(o.max_y, py);
            o.min_z = std::min(o.min_z, pz); o.max_z = std::max(o.max_z, pz);
        }
        return o;
    }

    std::size_t structural_hash() const noexcept override {
        std::size_t h = phash(0x7C0B'E106ull,
                              {params[R0], params[R1], params[R2],
                               params[R3], params[R4], params[R5],
                               params[R6], params[R7], params[R8],
                               params[Tx], params[Ty], params[Tz]});
        return h * 1099511628211ull ^ children[0]->structural_hash();
    }
};

}  // namespace frep::hep
