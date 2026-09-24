// tests/test_hep_ad.cpp
//
// Analytic gradients for the six HEP nodes, against the property a distance
// field has to have and against an independent oracle.
//
//   A  |grad f| = 1. Every piece of these fields is a unit-gradient function
//      - that is what dividing a slanted face by sqrt(1+k^2) is FOR - and a
//      max of unit-gradient pieces has a unit gradient everywhere it is
//      differentiable. So this is not a tolerance, it is the invariant, and
//      it is what the finite-difference fallback broke: measured on a Tube,
//      |grad| was 1.043 at 1 mm and 1.092 at 10 m, because h = 1e-3 in
//      absolute units is the f32 spacing itself at detector scale.
//
//   B  The direction agrees with a central difference taken in DOUBLE at a
//      step scaled to the coordinate. That is a real oracle: it is computed
//      from evalw, not from the gradient code, so an error in the chain rule
//      cannot hide in both.
//
//   C  A rotated frame does not change the length. R is a rotation, so the
//      chain rule needs no rescaling - and if a transpose were flipped, the
//      direction would be wrong while the length stayed 1, which is why B
//      exists as well.

#include "core/compiler/codegen.hpp"
#include "core/compiler/jit_engine.hpp"
#include "core/frep/hep.hpp"
#include "tests/test_support.hpp"

#include <gtest/gtest.h>
#include <llvm/Support/TargetSelect.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace frep;

namespace {

using GradFn = float (*)(float, float, float, float, float, float, float*);

class LLVMInitAd : public ::testing::Environment {
public:
    void SetUp() override {
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
    }
};
[[maybe_unused]] auto* g_ad_env =
    ::testing::AddGlobalTestEnvironment(new LLVMInitAd);

GradFn jit_grad(const FRepNode& root) {
    auto ctx = std::make_unique<llvm::LLVMContext>();
    SceneCodegen cg(*ctx);
    cg.emit_scene_sdf_grad(root);
    auto& mod = *cg.module();
    auto* g = mod.getFunction("scene_sdf_grad");
    if (!g) return nullptr;
    auto* f32 = llvm::Type::getFloatTy(*ctx);
    auto* ptr = llvm::PointerType::getUnqual(*ctx);
    auto* fty =
        llvm::FunctionType::get(f32, {f32, f32, f32, f32, f32, f32, ptr}, false);
    auto* w = llvm::Function::Create(fty, llvm::Function::ExternalLinkage,
                                     "render_tile", &mod);
    auto* bb = llvm::BasicBlock::Create(*ctx, "entry", w);
    llvm::IRBuilder<> b(bb);
    std::vector<llvm::Value*> args;
    for (auto& a : w->args()) args.push_back(&a);
    b.CreateRet(b.CreateCall(g, args));
    auto mp = cg.take_module();
    test::jit_pool().emplace_back(std::make_unique<JitEngine>());
    auto r = test::jit_pool().back()->load(std::move(mp), std::move(ctx));
    return r ? reinterpret_cast<GradFn>(*r) : nullptr;
}

/// The three partials, one directional derivative each.
std::array<double, 3> grad_at(GradFn f, double x, double y, double z) {
    float d = 0.0f;
    std::array<double, 3> g{};
    f(float(x), 1.0f, float(y), 0.0f, float(z), 0.0f, &d); g[0] = d;
    f(float(x), 0.0f, float(y), 1.0f, float(z), 0.0f, &d); g[1] = d;
    f(float(x), 0.0f, float(y), 0.0f, float(z), 1.0f, &d); g[2] = d;
    return g;
}

double len(const std::array<double, 3>& g) {
    return std::sqrt(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
}

/// Central difference in double, step scaled to the coordinate - the oracle.
std::array<double, 3> fd_grad(const FRepNode& n, double x, double y, double z) {
    const double h = 1e-6 * std::max(1.0, std::max({std::fabs(x), std::fabs(y),
                                                    std::fabs(z)}));
    return {(n.evalw(x + h, y, z) - n.evalw(x - h, y, z)) / (2 * h),
            (n.evalw(x, y + h, z) - n.evalw(x, y - h, z)) / (2 * h),
            (n.evalw(x, y, z + h) - n.evalw(x, y, z - h)) / (2 * h)};
}

struct Case { const char* nm; FRepNode::Ptr n; };

std::vector<Case> cases() {
    std::vector<Case> v;
    v.push_back({"Tube", std::make_shared<hep::TubeNode>(1210.125, 2950.625,
                                                         3100.875, 0.0, 7.0)});
    v.push_back({"TubeWedge", std::make_shared<hep::TubeNode>(
                                  800.0, 2000.0, 3000.0, 0.3, 2.1)});
    v.push_back({"Cone", std::make_shared<hep::ConeNode>(
                             300.0, 1500.0, 500.0, 2500.0, 4000.0, 0.0, 7.0)});
    v.push_back({"Shell", std::make_shared<hep::SphericalShellNode>(
                              900.5, 2141.25, 0.0, 7.0, 0.35, 1.9)});
    v.push_back({"Trd", std::make_shared<hep::TrapezoidNode>(
                            215.0, 587.5, 132.5, 445.0, 3110.0)});
    v.push_back({"Poly", std::make_shared<hep::PolyhedronNode>(
                             175.0, 882.5, 237.5, 1315.0, 2900.0, 0.13, 7.0, 6)});
    {
        const double r[9] = {0.8, 0.0, 0.6, 0.0, 1.0, 0.0, -0.6, 0.0, 0.8};
        v.push_back({"Frame(Tube)",
                     std::make_shared<hep::FrameNode>(
                         std::make_shared<hep::TubeNode>(1210.125, 2950.625,
                                                         3100.875, 0.0, 7.0),
                         r, 150.0, -320.0, 480.0)});
    }
    return v;
}

}  // namespace

TEST(HepAd, TheGradientIsAUnitVectorAtEveryScale) {
    std::mt19937_64 rng(0x6AD);
    for (auto& c : cases()) {
        auto* f = jit_grad(*c.n);
        ASSERT_NE(f, nullptr) << c.nm;
        double worst = 0.0;
        long n = 0, skipped = 0;
        for (int i = 0; i < 4000; ++i) {
            std::uniform_real_distribution<double> U(-5000.0, 5000.0);
            const double x = U(rng), y = U(rng), z = U(rng);
            // The axis is where d(sqrt)/du is guarded, and a seam is where
            // two pieces tie; neither has a gradient to be wrong about.
            if (std::sqrt(x * x + y * y) < 1.0) { ++skipped; continue; }
            const auto g = grad_at(f, x, y, z);
            const double L = len(g);
            if (L == 0.0) { ++skipped; continue; }
            worst = std::max(worst, std::fabs(L - 1.0));
            ++n;
        }
        // f32 rounding over coordinates up to 5 m: a few 1e-4. The point is
        // that it does not GROW with scale, which is what the fallback did.
        EXPECT_LT(worst, 5e-3) << c.nm << ": worst ||grad| - 1| = " << worst;
        std::printf("  %-12s worst ||grad|-1| = %.3g over %ld points "
                    "(%ld skipped)\n", c.nm, worst, n, skipped);
    }
}

TEST(HepAd, TheDirectionAgreesWithADoubleCentralDifference) {
    std::mt19937_64 rng(0xD1F);
    for (auto& c : cases()) {
        auto* f = jit_grad(*c.n);
        ASSERT_NE(f, nullptr) << c.nm;
        double worstCos = 1.0;
        long n = 0;
        for (int i = 0; i < 2000; ++i) {
            std::uniform_real_distribution<double> U(-5000.0, 5000.0);
            const double x = U(rng), y = U(rng), z = U(rng);
            if (std::sqrt(x * x + y * y) < 100.0) continue;
            const auto a = grad_at(f, x, y, z);
            const auto b = fd_grad(*c.n, x, y, z);
            const double la = len(a), lb = len(b);
            if (la < 0.5 || lb < 0.5) continue;
            // Near a seam the two may pick different one-sided branches, and
            // both are admissible; those show up as a low cosine on a small
            // number of points, so the assertion is on the MEDIAN rather than
            // the worst - a worst-case bound here would be asserting that no
            // sample ever lands on a seam.
            const double cosang =
                (a[0] * b[0] + a[1] * b[1] + a[2] * b[2]) / (la * lb);
            worstCos = std::min(worstCos, cosang);
            ++n;
            if (cosang < 0.999) continue;   // counted, not fatal
        }
        // Collect again for a median, cheaply: re-run with a fixed stream.
        std::vector<double> cs;
        std::mt19937_64 r2(0xD1F);
        for (int i = 0; i < 2000; ++i) {
            std::uniform_real_distribution<double> U(-5000.0, 5000.0);
            const double x = U(r2), y = U(r2), z = U(r2);
            if (std::sqrt(x * x + y * y) < 100.0) continue;
            const auto a = grad_at(f, x, y, z);
            const auto b = fd_grad(*c.n, x, y, z);
            const double la = len(a), lb = len(b);
            if (la < 0.5 || lb < 0.5) continue;
            cs.push_back((a[0] * b[0] + a[1] * b[1] + a[2] * b[2]) / (la * lb));
        }
        ASSERT_FALSE(cs.empty()) << c.nm;
        std::sort(cs.begin(), cs.end());
        const double med = cs[cs.size() / 2];
        const double p01 = cs[cs.size() / 100];
        EXPECT_GT(med, 0.99999) << c.nm << ": median cos = " << med;
        std::printf("  %-12s median cos %.6f, 1%% %.6f, worst %.6f "
                    "over %zu points\n", c.nm, med, p01, cs.front(), cs.size());
    }
}

TEST(HepAd, AnalyticEmitsLessIrThanTheFiniteDifference) {
    // The fallback emits codegen() three times - at p+h, p-h and p - so it
    // should be the larger body. Counted as IR instructions, which are
    // deterministic, rather than as a wall-clock ratio, and counted for the
    // SAME node through both paths.
    //
    // 45 against 28, so 1.61x and not the 3x the call count suggests: the
    // Tube's value body is small next to the dual bookkeeping, and the
    // fallback's three calls share the parameter constants. The size was
    // never the reason to write these - the accuracy was - and quoting 3x
    // because three calls are emitted would be reasoning from the code
    // instead of from the measurement.
    auto tube = std::make_shared<hep::TubeNode>(1210.125, 2950.625, 3100.875,
                                                0.0, 7.0);

    auto count = [&](bool analytic) {
        llvm::LLVMContext ctx;
        llvm::Module mod("count", ctx);
        auto* f32 = llvm::Type::getFloatTy(ctx);
        auto* fty = llvm::FunctionType::get(
            f32, {f32, f32, f32, f32, f32, f32}, false);
        auto* fn = llvm::Function::Create(fty, llvm::Function::ExternalLinkage,
                                          "g", &mod);
        auto* bb = llvm::BasicBlock::Create(ctx, "entry", fn);
        llvm::IRBuilder<> b(bb);
        CgCtx c{ctx, mod, b};
        auto it = fn->arg_begin();
        FRepNode::DualVal x{&*it++, &*it++};
        FRepNode::DualVal y{&*it++, &*it++};
        FRepNode::DualVal z{&*it++, &*it++};
        const auto d = analytic
                           ? tube->codegen_grad(c, x, y, z)
                           : tube->FRepNode::codegen_grad(c, x, y, z);
        b.CreateRet(d.dot);
        std::size_t n = 0;
        for (auto& block : *fn) n += block.size();
        return n;
    };

    const std::size_t fd = count(false), an = count(true);
    std::printf("  Tube gradient IR: finite difference %zu, analytic %zu "
                "(%.2fx)\n", fd, an, double(fd) / double(an));
    EXPECT_LT(an, fd) << "the analytic body should be the smaller one";
}
