// tests/test_codegen_f64.cpp
//
// The compiled field in double, against the interpreted one.
//
// emit_scene_sdf and emit_scene_sdf_f64 walk the SAME codegen, with only
// CgCtx::scalar different, so the two cannot describe different solids - the
// point of routing the type through the context rather than writing a second
// emitter. What the tests check is that the claim survives the JIT:
//
//   A  the f64 IR agrees with evalw() to the last bits it can;
//   B  the f32 IR still agrees with eval() exactly as before, so adding the
//      wide path changed nothing on the narrow one;
//   C  the f64 answer is far closer to an analytic reference than the f32
//      answer, which is the only reason to have it - it is NOT faster, see
//      core/frep/mixed_eval.hpp;
//   D  a node that cannot emit wide is NAMED before emission rather than
//      crashing the IR verifier.

#include "core/compiler/codegen.hpp"
#include "core/compiler/jit_engine.hpp"
#include "core/frep/hep.hpp"
#include "core/frep/operations.hpp"
#include "core/frep/primitives.hpp"
#include "core/frep/transforms.hpp"
#include "tests/test_support.hpp"

#include <gtest/gtest.h>
#include <llvm/Support/TargetSelect.h>

#include <chrono>
#include <cmath>
#include <memory>
#include <random>

using namespace frep;

namespace {

using F32Fn = float (*)(float, float, float);
using F64Fn = double (*)(double, double, double);

/// Emit `root` at the given scalar type and JIT it behind a "render_tile"
/// wrapper, which is the name JitEngine::load looks up.
void* jit_at(const FRepNode& root, bool wide) {
    auto ctx = std::make_unique<llvm::LLVMContext>();
    SceneCodegen cg(*ctx);
    if (wide) cg.emit_scene_sdf_f64(root);
    else cg.emit_scene_sdf(root);

    auto& mod = *cg.module();
    auto* sdf = mod.getFunction(wide ? "scene_sdf_f64" : "scene_sdf");
    if (!sdf) return nullptr;

    auto* t = wide ? llvm::Type::getDoubleTy(*ctx) : llvm::Type::getFloatTy(*ctx);
    auto* fty = llvm::FunctionType::get(t, {t, t, t}, false);
    auto* wrapper = llvm::Function::Create(fty, llvm::Function::ExternalLinkage,
                                           "render_tile", &mod);
    auto* bb = llvm::BasicBlock::Create(*ctx, "entry", wrapper);
    llvm::IRBuilder<> b(bb);
    auto it = wrapper->arg_begin();
    auto* x = &*it++;
    auto* y = &*it++;
    auto* z = &*it++;
    std::vector<llvm::Value*> args{x, y, z};
    // The params buffer is unused in Constant mode and DCE-ed by O3.
    if (sdf->arg_size() == 4)
        args.push_back(llvm::ConstantPointerNull::get(
            llvm::PointerType::getUnqual(*ctx)));
    b.CreateRet(b.CreateCall(sdf, args));

    auto mod_ptr = cg.take_module();
    test::jit_pool().emplace_back(std::make_unique<JitEngine>());
    auto fn_or = test::jit_pool().back()->load(std::move(mod_ptr), std::move(ctx));
    return fn_or ? reinterpret_cast<void*>(*fn_or) : nullptr;
}

/// A detector-scale solid: a hollow cylinder, where f32 is visibly coarse.
FRepNode::Ptr scene() {
    return std::make_shared<DifferenceNode>(
        std::make_shared<hep::TubeNode>(0.0, 2950.625, 3100.875, 0.0, 7.0),
        std::make_shared<hep::TubeNode>(0.0, 1210.125, 4000.25, 0.0, 7.0));
}

std::vector<std::array<double, 3>> pts(int n, double ext, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> U(-ext, ext);
    std::vector<std::array<double, 3>> p;
    for (int i = 0; i < n; ++i) p.push_back({U(rng), U(rng), U(rng)});
    return p;
}

class LLVMInit : public ::testing::Environment {
public:
    void SetUp() override {
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
    }
};
[[maybe_unused]] auto* g_env = ::testing::AddGlobalTestEnvironment(new LLVMInit);

}  // namespace

TEST(CodegenF64, TheWideIrAgreesWithTheWideInterpreter) {
    const auto n = scene();
    auto* f = reinterpret_cast<F64Fn>(jit_at(*n, true));
    ASSERT_NE(f, nullptr);
    double worst = 0.0;
    long differ = 0;
    for (const auto& p : pts(20000, 4500.0, 0xC0DE)) {
        const double a = f(p[0], p[1], p[2]);
        const double b = n->evalw(p[0], p[1], p[2]);
        if (a != b) {
            ++differ;
            worst = std::max(worst, std::fabs(a - b));
        }
    }
    // Not asserted bit-identical: LLVM may contract a multiply and an add
    // into one fma, which is a DIFFERENT and slightly better result than the
    // interpreter's two roundings. Asserting equality here would be
    // asserting that no optimiser ever improves the arithmetic.
    EXPECT_LT(worst, 1e-9) << differ << " of 20000 differ";
    std::printf("  f64 IR vs evalw: %ld of 20000 differ, worst %.3g mm\n",
                differ, worst);
}

TEST(CodegenF64, TheNarrowIrIsUnchangedByTheWidePathExisting) {
    const auto n = scene();
    auto* f = reinterpret_cast<F32Fn>(jit_at(*n, false));
    ASSERT_NE(f, nullptr);
    double worst = 0.0;
    for (const auto& p : pts(20000, 4500.0, 0xC0DE)) {
        const float a = f(float(p[0]), float(p[1]), float(p[2]));
        const float b = n->eval(float(p[0]), float(p[1]), float(p[2]));
        worst = std::max(worst, std::fabs(double(a) - double(b)));
    }
    EXPECT_LT(worst, 1e-3) << "worst " << worst;
    std::printf("  f32 IR vs eval:  worst %.3g mm\n", worst);
}

TEST(CodegenF64, TheWideIrIsOrdersCloserToTheAnalyticField) {
    const auto n = scene();
    auto* f32 = reinterpret_cast<F32Fn>(jit_at(*n, false));
    auto* f64 = reinterpret_cast<F64Fn>(jit_at(*n, true));
    ASSERT_NE(f32, nullptr);
    ASSERT_NE(f64, nullptr);
    const double Ro = 2950.625, Ho = 3100.875, Ri = 1210.125, Hi_ = 4000.25;
    double e32 = 0.0, e64 = 0.0;
    for (const auto& p : pts(20000, 4500.0, 0xFACE)) {
        const double rho = std::sqrt(p[0] * p[0] + p[1] * p[1]);
        const double outer =
            std::max(rho - Ro, std::fabs(p[2]) - Ho);
        const double inner =
            std::max(rho - Ri, std::fabs(p[2]) - Hi_);
        const double ref = std::max(outer, -inner);
        e32 = std::max(e32, std::fabs(double(f32(float(p[0]), float(p[1]),
                                                 float(p[2]))) - ref));
        e64 = std::max(e64, std::fabs(f64(p[0], p[1], p[2]) - ref));
    }
    EXPECT_GT(e32, 1e-4);
    EXPECT_LT(e64, 1e-9);
    EXPECT_LT(e64 * 1e4, e32);
    std::printf("  worst |IR - analytic|: f32 %.3g mm, f64 %.3g mm\n", e32, e64);
}

TEST(CodegenF64, ANodeThatCannotEmitWideIsNamedNotCrashed) {
    const auto n = scene();
    EXPECT_TRUE(SceneCodegen::f64_blockers(*n).empty());

    struct NoWide final : FRepNode {
        const char* type_name() const noexcept override { return "NoWide"; }
        llvm::Value* codegen(CgCtx& c, llvm::Value*, llvm::Value*,
                             llvm::Value*) const override {
            return c.fc(1.0);
        }
        float eval(float, float, float) const override { return 1.0f; }
        std::size_t structural_hash() const noexcept override { return 1; }
    };
    auto mixed = std::make_shared<UnionNode>(std::make_shared<SphereNode>(1.0),
                                             std::make_shared<NoWide>());
    const auto blockers = SceneCodegen::f64_blockers(*mixed);
    ASSERT_EQ(blockers.size(), 1u);
    EXPECT_EQ(blockers[0], "NoWide");
}

TEST(CodegenF64, CompiledF32AgainstCompiledF64) {
    // mixed_eval.hpp measures scalar f32 as SLOWER than f64 in the
    // INTERPRETER, because the parameters are stored as double and the f32
    // path converts every one on the way in. The compiled path folds those
    // constants, so the question has to be asked again here rather than
    // inherited.
    //
    // The first version of this benchmark answered it wrongly: it passed
    // float(p[0]) inside the timed loop and accumulated into a double, so
    // every f32 call paid three narrowings and one widening that belong to
    // the harness. Each type now gets its points in its own type, converted
    // once, and accumulates in its own type.
    const auto n = scene();
    auto* f32 = reinterpret_cast<F32Fn>(jit_at(*n, false));
    auto* f64 = reinterpret_cast<F64Fn>(jit_at(*n, true));
    ASSERT_NE(f32, nullptr);
    ASSERT_NE(f64, nullptr);

    const auto pd = pts(4000, 4500.0, 0x5A5A);
    std::vector<std::array<float, 3>> pf;
    pf.reserve(pd.size());
    for (const auto& p : pd)
        pf.push_back({float(p[0]), float(p[1]), float(p[2])});

    const int R = 400;
    const double nq = double(R) * double(pd.size());

    volatile float sf = 0.0f;
    auto t0 = std::chrono::steady_clock::now();
    for (int r = 0; r < R; ++r)
        for (const auto& p : pf) sf = sf + f32(p[0], p[1], p[2]);
    auto t1 = std::chrono::steady_clock::now();
    const double a =
        std::chrono::duration<double, std::nano>(t1 - t0).count() / nq;

    volatile double sd = 0.0;
    t0 = std::chrono::steady_clock::now();
    for (int r = 0; r < R; ++r)
        for (const auto& p : pd) sd = sd + f64(p[0], p[1], p[2]);
    t1 = std::chrono::steady_clock::now();
    const double b =
        std::chrono::duration<double, std::nano>(t1 - t0).count() / nq;

    // PRINTED, not asserted: a wall-clock ratio is not an invariant, and
    // this project has demoted four assertions that pretended otherwise.
    std::printf("  compiled: f32 %.2f ns, f64 %.2f ns, f32/f64 %.2fx\n", a, b,
                a / b);
    EXPECT_GT(a, 0.0);
    EXPECT_GT(b, 0.0);
}
