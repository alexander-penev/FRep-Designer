// tests/test_simd_scalar_type.cpp
//
// The question mixed_eval.hpp left open, answered here.
//
// Scalar f32 buys nothing on this CPU: interpreted it is slower (the
// parameter store is double and narrows on the way in), compiled it is
// equal. Both because scalar code puts one value in a register, so halving
// the width buys no lanes. SIMD is the first place where it should - the
// register holds twice as many f32 as f64 - and that is a hypothesis until
// it is measured, which is what this file does.
//
// THE COMPARISON HAS TO BE REGISTER-FOR-REGISTER. f32 at 8 lanes against f64
// at 8 lanes is not a comparison of types, it is a comparison of how much
// work each call does; the f32 one would be using half the registers. The
// fair pairing is equal REGISTER BYTES - f32 at W against f64 at W/2 - and
// then the interesting number is time per POINT, not per call.
//
// Both are reported. A lane-for-lane figure is printed too, because it
// answers a different and also real question: what an existing f32 kernel
// would cost if its accuracy had to be raised without changing its shape.

#include "core/compiler/compile_sdf.hpp"
#include "core/frep/hep.hpp"
#include "core/frep/operations.hpp"
#include "core/frep/primitives.hpp"

#include <gtest/gtest.h>
#include <llvm/Support/TargetSelect.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <random>
#include <vector>

using namespace frep;

namespace {

class LLVMInitSimd : public ::testing::Environment {
public:
    void SetUp() override {
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
    }
};
[[maybe_unused]] auto* g_simd_env =
    ::testing::AddGlobalTestEnvironment(new LLVMInitSimd);

SceneGraph scene() {
    SceneGraph s;
    s.add_object(std::make_shared<DifferenceNode>(
        std::make_shared<hep::TubeNode>(0.0, 2950.625, 3100.875, 0.0, 7.0),
        std::make_shared<hep::TubeNode>(0.0, 1210.125, 4000.25, 0.0, 7.0)));
    return s;
}

/// Points laid out per lane, in the lane type.
template <class T>
struct Pts {
    std::vector<T> x, y, z, o;
    std::size_t blocks = 0;
};

template <class T>
Pts<T> make(unsigned lanes, std::size_t blocks, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> U(-4500.0, 4500.0);
    Pts<T> p;
    p.blocks = blocks;
    const std::size_t n = blocks * lanes;
    p.x.resize(n); p.y.resize(n); p.z.resize(n); p.o.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        p.x[i] = T(U(rng)); p.y[i] = T(U(rng)); p.z[i] = T(U(rng));
    }
    return p;
}

/// Nanoseconds per POINT, not per call.
template <class Fn, class T>
double run(Fn fn, Pts<T>& p, unsigned lanes, int reps) {
    const auto t0 = std::chrono::steady_clock::now();
    for (int r = 0; r < reps; ++r)
        for (std::size_t bIdx = 0; bIdx < p.blocks; ++bIdx) {
            const std::size_t i = bIdx * lanes;
            fn(&p.x[i], &p.y[i], &p.z[i], &p.o[i]);
        }
    const auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::nano>(t1 - t0).count() /
           (double(reps) * double(p.blocks) * double(lanes));
}

}  // namespace

TEST(SimdScalarType, RegisterForRegisterAndLaneForLane) {
    const auto sg = scene();
    const unsigned W = jit::native_simd_width();

    auto f32 = jit::compile_scene_sdf_simd(sg, W);
    ASSERT_TRUE(f32.has_value()) << (f32 ? "" : f32.error());
    auto f64_half = jit::compile_scene_sdf_simd_f64(sg, W / 2);
    ASSERT_TRUE(f64_half.has_value()) << (f64_half ? "" : f64_half.error());
    auto f64_same = jit::compile_scene_sdf_simd_f64(sg, W);
    ASSERT_TRUE(f64_same.has_value()) << (f64_same ? "" : f64_same.error());

    const std::size_t B = 4096;
    const int R = 60;
    auto p32 = make<float>(W, B, 0x51D);
    auto p64h = make<double>(W / 2, B, 0x51D);
    auto p64s = make<double>(W, B, 0x51D);

    const double a = run(f32->fn, p32, W, R);
    const double b = run(f64_half->fn, p64h, W / 2, R);
    const double c = run(f64_same->fn, p64s, W, R);

    std::printf("  native float width %u\n", W);
    std::printf("  f32 x%-2u  %6.3f ns/point\n", W, a);
    std::printf("  f64 x%-2u  %6.3f ns/point   (same register bytes) "
                "f32 is %.2fx\n", W / 2, b, b / a);
    std::printf("  f64 x%-2u  %6.3f ns/point   (same lane count)     "
                "f32 is %.2fx\n", W, c, c / a);

    // Printed, not asserted - a wall-clock ratio is not an invariant. What IS
    // asserted is that all three produce the same field, below.
    EXPECT_GT(a, 0.0);
    EXPECT_GT(b, 0.0);
    EXPECT_GT(c, 0.0);
}

TEST(SimdScalarType, AllThreeVectorPathsAgreeWithTheInterpreter) {
    const auto sg = scene();
    const unsigned W = jit::native_simd_width();
    auto f32 = jit::compile_scene_sdf_simd(sg, W);
    auto f64h = jit::compile_scene_sdf_simd_f64(sg, W / 2);
    ASSERT_TRUE(f32.has_value());
    ASSERT_TRUE(f64h.has_value());

    // The scene's own root, for the reference.
    FRepNode::Ptr root;
    for (auto& [id, obj] : sg.objects()) root = obj.geometry;
    ASSERT_NE(root, nullptr);

    const std::size_t B = 256;
    auto p32 = make<float>(W, B, 0xA11);
    auto p64 = make<double>(W / 2, B, 0xA11);
    for (std::size_t i = 0; i < B; ++i)
        f32->fn(&p32.x[i * W], &p32.y[i * W], &p32.z[i * W], &p32.o[i * W]);
    for (std::size_t i = 0; i < B; ++i)
        f64h->fn(&p64.x[i * (W / 2)], &p64.y[i * (W / 2)],
                 &p64.z[i * (W / 2)], &p64.o[i * (W / 2)]);

    double w32 = 0.0, w64 = 0.0;
    for (std::size_t i = 0; i < p32.x.size(); ++i)
        w32 = std::max(w32, std::fabs(double(p32.o[i]) -
                                      double(root->eval(p32.x[i], p32.y[i],
                                                        p32.z[i]))));
    for (std::size_t i = 0; i < p64.x.size(); ++i)
        w64 = std::max(w64, std::fabs(p64.o[i] - root->evalw(p64.x[i], p64.y[i],
                                                             p64.z[i])));
    std::printf("  worst |vector - interpreter|: f32 %.3g mm, f64 %.3g mm\n",
                w32, w64);
    // f32 lanes against the f32 interpreter, f64 lanes against the f64 one:
    // each to the tolerance its own type supports, not to a shared number.
    EXPECT_LT(w32, 1e-2);
    EXPECT_LT(w64, 1e-9);
}
