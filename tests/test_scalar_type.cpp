// tests/test_scalar_type.cpp
//
// The scalar type is a parameter of evaluation, not of the tree. Three things
// have to hold for that to be worth anything:
//
//   A  The double path is REAL where a node says it is. Against an analytic
//      reference the f64 answer must be orders better than the f32 one -
//      asserted on the MAXIMUM over many points, never point by point,
//      because two approximations of the same number cross over constantly
//      and a per-point ordering is a coin flip dressed as an invariant.
//
//   B  The float path is UNCHANGED. eval and evalw come from one templated
//      body, and the whole point of routing every parameter through
//      ScalarTraits<T>::from() is that instantiating it at float reproduces
//      the arithmetic that was there before. Checked against closed forms
//      that are exact in float, so the test states a value rather than a
//      diff against yesterday's binary.
//
//   C  A node that has NOT been converted says so. evalw falls back to float
//      and returns a correct number; what must never happen is that it
//      returns that number while claiming to be double, because then the
//      error is real and in an unknown place.

#include "core/frep/primitives.hpp"
#include "core/frep/operations.hpp"
#include "core/frep/transforms.hpp"
#include "core/frep/hep.hpp"
#include "core/frep/scalar.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <random>
#include <vector>

using namespace frep;

namespace {

std::vector<std::array<double, 3>> points(int n, double ext) {
    std::mt19937_64 rng(20260920);
    std::uniform_real_distribution<double> U(-ext, ext);
    std::vector<std::array<double, 3>> p;
    p.reserve(std::size_t(n));
    for (int i = 0; i < n; ++i) p.push_back({U(rng), U(rng), U(rng)});
    return p;
}

/// Worst |eval - ref| and worst |evalw - ref| over the same points.
struct Err { double f32 = 0.0, f64 = 0.0; };

template <class Ref>
Err sweep(const FRepNode& n, double ext, Ref ref, int np = 4000) {
    Err e;
    for (const auto& p : points(np, ext)) {
        const double r = ref(p[0], p[1], p[2]);
        e.f32 = std::max(e.f32, std::fabs(double(n.eval(float(p[0]), float(p[1]),
                                                        float(p[2]))) - r));
        e.f64 = std::max(e.f64, std::fabs(n.evalw(p[0], p[1], p[2]) - r));
    }
    return e;
}

}  // namespace

// ── A. the double path is real ───────────────────────────────────────────────

TEST(ScalarType, SphereIsOrdersMoreAccurateInDouble) {
    // A radius no float can hold: the error starts in the PARAMETER, before
    // any arithmetic, which is why params is a map of double.
    const double R = 8425.123456789012;
    SphereNode s(R);
    const Err e = sweep(s, 10000.0, [&](double x, double y, double z) {
        return std::sqrt(x * x + y * y + z * z) - R;
    });
    EXPECT_GT(e.f32, 1e-4) << "f32 is expected to be coarse at 10 m";
    EXPECT_LT(e.f64, 1e-9);
    EXPECT_LT(e.f64 * 1e5, e.f32);
}

TEST(ScalarType, TubeIsOrdersMoreAccurateInDouble) {
    const double rmin = 1010.10101010101, rmax = 4321.098765432109,
                 hz = 7777.777777777777;
    hep::TubeNode t(rmin, rmax, hz, 0.0, 7.0);   // dphi > 2pi: no wedge
    const Err e = sweep(t, 9000.0, [&](double x, double y, double z) {
        const double rho = std::sqrt(x * x + y * y);
        return std::max(std::max(rho - rmax, std::fabs(z) - hz), rmin - rho);
    });
    EXPECT_GT(e.f32, 1e-4);
    EXPECT_LT(e.f64, 1e-9);
    EXPECT_LT(e.f64 * 1e5, e.f32);
}

TEST(ScalarType, CompositesCarryTheTypeThroughChildren) {
    // The interesting part is not the leaf but the path: Frame -> Difference
    // -> Box/Sphere has to hand T down through eval_as at every level, and a
    // single node falling back to float would show up here.
    const double r[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    const double hx = 3333.333333333333, rr = 2222.222222222222;
    auto inner = std::make_shared<DifferenceNode>(
        std::make_shared<BoxNode>(hx, hx, hx), std::make_shared<SphereNode>(rr));
    hep::FrameNode f(inner, r, 0.0, 0.0, 0.0);
    const Err e = sweep(f, 6000.0, [&](double x, double y, double z) {
        const double dx = std::fabs(x) - hx, dy = std::fabs(y) - hx,
                     dz = std::fabs(z) - hx;
        const double ox = std::max(dx, 0.0), oy = std::max(dy, 0.0),
                     oz = std::max(dz, 0.0);
        const double box = std::sqrt(ox * ox + oy * oy + oz * oz) +
                           std::min(std::max(dx, std::max(dy, dz)), 0.0);
        return std::max(box, -(std::sqrt(x * x + y * y + z * z) - rr));
    });
    EXPECT_GT(e.f32, 1e-4);
    EXPECT_LT(e.f64, 1e-9);
    EXPECT_LT(e.f64 * 1e5, e.f32);
}

// ── B. the float path is unchanged ───────────────────────────────────────────

TEST(ScalarType, FloatPathKeepsItsExactValues) {
    // Every parameter and coordinate here is a dyadic rational, so the answer
    // is exact in float and the expected value can be WRITTEN DOWN rather
    // than compared against a previous build.
    SphereNode s(100.0);
    EXPECT_EQ(s.eval(60.0f, 80.0f, 0.0f), 0.0f);      // 3-4-5 scaled
    EXPECT_EQ(s.eval(30.0f, 40.0f, 0.0f), -50.0f);

    BoxNode b(4.0, 8.0, 16.0);
    EXPECT_EQ(b.eval(0.0f, 0.0f, 0.0f), -4.0f);       // nearest face
    EXPECT_EQ(b.eval(7.0f, 12.0f, 16.0f), 5.0f);      // 3-4-0 outside corner

    hep::TubeNode t(0.0, 8.0, 3.0, 0.0, 7.0);
    EXPECT_EQ(t.eval(6.0f, 8.0f, 0.0f), 2.0f);        // rho = 10
    EXPECT_EQ(t.eval(0.0f, 0.0f, 5.0f), 2.0f);        // the z cap wins

    DifferenceNode d(std::make_shared<BoxNode>(4.0, 4.0, 4.0),
                     std::make_shared<SphereNode>(2.0));
    EXPECT_EQ(d.eval(0.0f, 0.0f, 0.0f), 2.0f);        // inside the hole
}

TEST(ScalarType, FloatAndDoubleAgreeWhereFloatCanHoldTheAnswer) {
    // Points chosen so the answer is EXACT in float: either the branch has no
    // square root at all (inside a box is a max of differences), or its
    // argument is a perfect square. Anywhere else the two types differ by
    // construction and equality would be the wrong assertion - sqrt(2) is not
    // a float. This is what "one body" buys: where float can hold the answer,
    // the two paths produce the same bits.
    BoxNode b(4.0, 8.0, 16.0);
    const double bp[][3] = {{0, 0, 0}, {2, 0, 0}, {0, 7, 0}, {3, 6, 15},
                            {10, 0, 0}, {7, 12, 16}, {-10, 0, 0}};
    for (const auto& p : bp)
        EXPECT_EQ(double(b.eval(float(p[0]), float(p[1]), float(p[2]))),
                  b.evalw(p[0], p[1], p[2]))
            << "at " << p[0] << "," << p[1] << "," << p[2];

    hep::TubeNode t(0.0, 8.0, 3.0, 0.0, 7.0);
    const double tp[][3] = {{6, 8, 0}, {0, 0, 5}, {3, 4, 1}, {0, 0, 0}};
    for (const auto& p : tp)
        EXPECT_EQ(double(t.eval(float(p[0]), float(p[1]), float(p[2]))),
                  t.evalw(p[0], p[1], p[2]))
            << "at " << p[0] << "," << p[1] << "," << p[2];
}

// ── C. an unconverted node says so ───────────────────────────────────────────

TEST(ScalarType, ConvertedNodesDeclareTheirWideEval) {
    EXPECT_TRUE(SphereNode(1.0).wide_eval());
    EXPECT_TRUE(BoxNode(1.0, 1.0, 1.0).wide_eval());
    EXPECT_TRUE(hep::TubeNode(0.0, 1.0, 1.0, 0.0, 7.0).wide_eval());
    EXPECT_TRUE(UnionNode(std::make_shared<SphereNode>(1.0),
                          std::make_shared<SphereNode>(2.0))
                    .wide_eval());
    EXPECT_TRUE(TranslateNode(std::make_shared<SphereNode>(1.0), 1.0, 2.0, 3.0)
                    .wide_eval());
}

TEST(ScalarType, TraitsCarryTheBandTheTypeCanSupport) {
    // A tolerance is a statement about a type. Geant4's 1e-9 mm is a
    // statement about f64 and is empty in f32, where the spacing at 1e4 mm
    // is already 1e-3 mm.
    EXPECT_LT(ScalarTraits<double>::tol(), 1e-6);
    EXPECT_GT(ScalarTraits<float>::tol(), 1e-4f);
    EXPECT_GT(std::nextafterf(1e4f, 2e4f) - 1e4f, ScalarTraits<double>::tol());
    EXPECT_STREQ(ScalarTraits<float>::name(), "f32");
    EXPECT_STREQ(ScalarTraits<double>::name(), "f64");
    // up/down widen outward by one ulp and nothing more.
    EXPECT_GT(ScalarTraits<float>::up(1.0f), 1.0f);
    EXPECT_LT(ScalarTraits<float>::down(1.0f), 1.0f);
    EXPECT_EQ(ScalarTraits<float>::up(1.0f), std::nextafterf(1.0f, 2.0f));
}
