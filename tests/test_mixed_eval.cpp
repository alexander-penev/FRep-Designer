// tests/test_mixed_eval.cpp
//
// The two-type pipeline: a cheap field that may reject, an exact field that
// answers, and a band between them that has to have been MEASURED.
//
//   A  Before measureDelta(), there is no bound and asking for one throws.
//      A delta of zero that was never measured would be a bound that claims
//      f32 is exact, which is the worst possible default, so the class
//      refuses instead of returning lo(p).
//   B  After measuring, the band holds on points the measurement never saw -
//      MOSTLY. It is a sampled maximum, so the honest assertion is on the
//      rate and the size of the overshoot, not on zero. Asserting zero here
//      is how a sampled estimate gets mistaken for an enclosure, and the
//      first version of this test did exactly that and failed.
//   C  refine() agrees with hi() near the surface and stays inside the band
//      away from it - the statement that makes it safe to skip the exact
//      evaluation.
//   D  Coverage is reported, not assumed.
//
// The share of queries a march can answer cheaply is PRINTED rather than
// asserted: it is a property of the scene and the tolerance, and pinning it
// to a number would turn a measurement into a tripwire.

#include "core/frep/mixed_eval.hpp"
#include "core/frep/hep.hpp"
#include "core/frep/operations.hpp"
#include "core/frep/primitives.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <random>

using namespace frep;

namespace {

/// A detector-scale scene: metres in millimetres, where f32 is coarse.
FRepNode::Ptr scene() {
    auto shell = std::make_shared<hep::TubeNode>(1850.375, 2950.625, 3100.875,
                                                 0.0, 7.0);
    auto bore = std::make_shared<hep::TubeNode>(0.0, 1210.125, 4000.25, 0.0, 7.0);
    return std::make_shared<DifferenceNode>(shell, bore);
}

constexpr double kExt = 4500.0;

}  // namespace

TEST(MixedEval, ABoundThatWasNeverMeasuredIsNotABound) {
    const auto n = scene();
    MixedEval<float, double> m(*n);
    EXPECT_FALSE(m.measured());
    EXPECT_EQ(m.delta(), 0.0);
    EXPECT_THROW(m.loEstimate(0, 0, 0), std::logic_error);
    EXPECT_THROW(m.upEstimate(0, 0, 0), std::logic_error);
    EXPECT_THROW(m.refine(0, 0, 0, 1.0), std::logic_error);
    // lo() and hi() are always available - it is only the BAND that needs
    // measuring.
    EXPECT_TRUE(std::isfinite(m.lo(0, 0, 0)));
    EXPECT_TRUE(std::isfinite(m.hi(0, 0, 0)));
}

TEST(MixedEval, TheMeasuredDeltaIsTheSizeF32ActuallyIs) {
    const auto n = scene();
    MixedEval<float, double> m(*n);
    const double d = m.measureDelta(-kExt, -kExt, -kExt, kExt, kExt, kExt);
    EXPECT_TRUE(m.measured());
    // f32 spacing at 4500 mm is ~4.9e-4 mm; the delta is a few of those and
    // nowhere near Geant4's 1e-9 mm tolerance. The assertion is deliberately
    // loose in both directions - the point is the ORDER, which is what makes
    // a fixed delta from another model wrong.
    EXPECT_GT(d, 1e-5);
    EXPECT_LT(d, 1e-1);
    std::printf("  measured delta = %.4g mm over +-%.0f mm\n", d, kExt);
}

TEST(MixedEval, TheBandHoldsOnFreshPointsToWithinAPerCentOfItself) {
    const auto n = scene();
    MixedEval<float, double> m(*n);
    const double d =
        m.measureDelta(-kExt, -kExt, -kExt, kExt, kExt, kExt, 20000, 0x5EED);

    std::mt19937_64 rng(0xD1FF);   // a different stream on purpose
    std::uniform_real_distribution<double> U(-kExt, kExt);
    const int N = 50000;
    int viol = 0;
    double worst = 0.0;
    for (int i = 0; i < N; ++i) {
        const double x = U(rng), y = U(rng), z = U(rng);
        const double h = m.hi(x, y, z);
        const double a = m.loEstimate(x, y, z), b = m.upEstimate(x, y, z);
        if (h < a || h > b) {
            ++viol;
            worst = std::max(worst, std::max(a - h, h - b));
        }
    }
    // Two aggregate statements, because both are true and neither is "zero":
    // the band is exceeded rarely, and when it is, by a small fraction of
    // itself. A caller can act on that; it cannot act on a promise of zero
    // that sampling was never able to make.
    EXPECT_LT(double(viol) / N, 1e-3);
    EXPECT_LT(worst, 0.1 * d);
    std::printf("  %d of %d fresh points outside the band (%.3f%%), "
                "worst overshoot %.3g mm = %.1f%% of delta\n",
                viol, N, 100.0 * viol / N, worst, 100.0 * worst / d);
}

TEST(MixedEval, RefineIsExactNearTheSurfaceAndInsideTheBandAwayFromIt) {
    const auto n = scene();
    MixedEval<float, double> m(*n);
    const double d = m.measureDelta(-kExt, -kExt, -kExt, kExt, kExt, kExt);

    std::mt19937_64 rng(0xBEEF);
    std::uniform_real_distribution<double> U(-kExt, kExt);
    const double band = 1.0;       // 1 mm: a sphere tracer's step tolerance
    std::size_t paid = 0;
    int n_near = 0;
    double worstFar = 0.0;
    for (int i = 0; i < 50000; ++i) {
        const double x = U(rng), y = U(rng), z = U(rng);
        const std::size_t before = paid;
        const double r = m.refine(x, y, z, band, &paid);
        const double h = m.hi(x, y, z);
        if (paid != before) {
            ++n_near;
            EXPECT_EQ(r, h);       // near the surface: the exact value itself
        } else {
            worstFar = std::max(worstFar, std::fabs(r - h));
        }
    }
    EXPECT_LE(worstFar, d) << "a cheap answer left the measured band";
    std::printf("  %d of 50000 queries paid for f64 (%.1f%%), band %.3g mm, "
                "worst cheap error %.3g mm\n",
                n_near, 100.0 * n_near / 50000.0, band, worstFar);
}

TEST(MixedEval, CoverageIsReportedRatherThanAssumed) {
    const auto n = scene();
    MixedEval<float, double> m(*n);
    EXPECT_DOUBLE_EQ(m.wideCoverage(), 1.0);
    EXPECT_TRUE(m.exact());

    // A node without a real wide evaluation drags the whole tree down, and
    // that has to be visible: "evaluated in double" with one float node in
    // it is worse than all-float, because the error is in an unknown place.
    struct NoWide final : FRepNode {
        const char* type_name() const noexcept override { return "NoWide"; }
        llvm::Value* codegen(CgCtx&, llvm::Value*, llvm::Value*,
                             llvm::Value*) const override {
            return nullptr;
        }
        float eval(float, float, float) const override { return 1.0f; }
        std::size_t structural_hash() const noexcept override { return 1; }
    };
    auto mixed = std::make_shared<UnionNode>(std::make_shared<SphereNode>(1.0),
                                             std::make_shared<NoWide>());
    MixedEval<float, double> m2(*mixed);
    EXPECT_LT(m2.wideCoverage(), 1.0);
    EXPECT_FALSE(m2.exact());
}
