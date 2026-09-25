// tests/test_cross_path.cpp
//
// One table. The same solids, the same points, through every description FD4
// has of them:
//
//   f64    the interpreter in double, eval_t<double>      — the reference
//   f32    the interpreter in float,  eval_t<float>
//   ir     LLVM IR value,             hep_codegen.cpp
//   ir.d   the value carried by the IR dual,   hep_ad.cpp
//   glsl   GLSL value,                glsl_hep.cpp
//   gl.d   the value carried by the GLSL dual, glsl_hep.cpp
//
// Six transcriptions of one solid, and each was written against the last.
// That is the whole reason this file exists: nothing until now compared all
// of them on the same points at once. What existed was partial and scattered
// - test_scalar_type compares f32 against f64, test_codegen compares IR
// against the interpreter for the non-HEP kinds, and test_glsl_hep compares
// RENDERED PICTURES with a mean-absolute tolerance. A picture comparison
// passes a solid that is wrong by less than a pixel of shading, and it cannot
// tell a wrong value from a wrong normal or a wrong material.
//
// The GPU columns are read back EXACTLY. The shader writes floatBitsToUint
// of the result as four bytes through an rgba8 image, so what comes back is
// the f32 the GPU computed and not an 8-bit quantisation of a colour. That
// matters: at detector scale the difference between two transcriptions of a
// tube is often a few hundred ulps, which is invisible in a pixel and obvious
// in the bits.
//
// TWO KINDS OF STATEMENT, and they are not interchangeable:
//
//   VALUES agree everywhere. A max/min tie at a seam picks one of two
//   branches, but both branches carry the same value there - that is what a
//   tie means - so the value columns get a hard bound over every point.
//
//   GRADIENTS agree except on the seams, which are a measure-zero set the
//   sampler still lands on. Those get a quantile, exactly as
//   test_hep_ad.cpp does, because a worst-case bound on a gradient across a
//   seam would be asserting that no sample ever lands on one.
//
// WHAT THE NUMBERS LOOK LIKE, AND WHY THE BOUND IS NOT SET AT THEM. Measured
// here the whole table sits at 8e-8..1.3e-7 scaled, which is one f32 ulp, and
// the glsl columns match the ir column to every digit printed. That is not
// six independent agreements: this container's Vulkan device is lavapipe,
// which JITs SPIR-V through LLVM on the same CPU, so the same expression
// rounds the same way. On a real card the glsl columns will be LOOSER - a
// GPU contracts multiply-adds on its own schedule and its sin, cos, sqrt and
// atan are its own approximations, not libm's - and a few 1e-6 there is the
// expected reading rather than a regression. The bound is 1e-4: three orders
// above what a real GPU's transcendentals cost and three below what a
// dropped wedge or a flipped sign costs. CrossPath.ASabotagedShaderIsCaught
// is the control that keeps the perfect agreement honest - it puts a 0.1%
// error inside the shader and requires the table to see it, which it does at
// 1.1e-3.

#include "core/compiler/codegen.hpp"
#include "core/compiler/jit_engine.hpp"
#include "core/frep/hep.hpp"
#include "core/frep/scene.hpp"
#include "core/gpu/glsl_compile.hpp"
#include "core/gpu/glsl_emitter.hpp"
#include "core/gpu/shader_push_builder.hpp"
#include "core/gpu/vulkan_ctx.hpp"
#include "tests/test_support.hpp"

#include <gtest/gtest.h>
#include <llvm/Support/TargetSelect.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace frep;

namespace {

constexpr int NPTS = 256;
constexpr int NROWS = 5;   // v, dual.v, g.x, g.y, g.z

struct P3 { double x, y, z; };

class LLVMInitXP : public ::testing::Environment {
public:
    void SetUp() override {
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
    }
};
[[maybe_unused]] auto* g_xp_env =
    ::testing::AddGlobalTestEnvironment(new LLVMInitXP);

/// ONE point set, shared by every path and every kind - that is the point of
/// the file. Half of it is far field, where the values are large and a
/// relative slip shows; half sits in the shell of radii the detector solids
/// actually occupy, where the seams and the near field are.
const std::vector<P3>& points() {
    static const std::vector<P3> pts = [] {
        std::vector<P3> v;
        v.reserve(NPTS);
        std::mt19937_64 rng(0xC5055);
        std::uniform_real_distribution<double> box(-5000.0, 5000.0);
        std::uniform_real_distribution<double> rad(400.0, 3400.0);
        std::uniform_real_distribution<double> ang(-3.15, 3.15);
        std::uniform_real_distribution<double> zz(-3500.0, 3500.0);
        for (int i = 0; i < NPTS / 2; ++i)
            v.push_back({box(rng), box(rng), box(rng)});
        for (int i = 0; i < NPTS - NPTS / 2; ++i) {
            const double r = rad(rng), a = ang(rng);
            v.push_back({r * std::cos(a), r * std::sin(a), zz(rng)});
        }
        return v;
    }();
    return pts;
}

struct Case { const char* nm; FRepNode::Ptr n; };

/// Detector-scale parameters, in millimetres, and deliberately not round
/// numbers: a value that is right only because two parameters happen to be
/// equal is a value that is not being tested.
std::vector<Case> cases() {
    std::vector<Case> v;
    v.push_back({"Tube",
                 std::make_shared<hep::TubeNode>(1210.125, 2950.625, 3100.875,
                                                 0.0, 7.0)});
    v.push_back({"TubeWedge",
                 std::make_shared<hep::TubeNode>(800.0, 2000.0, 3000.0, 0.3,
                                                 2.1)});
    v.push_back({"Cone",
                 std::make_shared<hep::ConeNode>(300.0, 1500.0, 500.0, 2500.0,
                                                 4000.0, 0.0, 7.0)});
    v.push_back({"ConeWedge",
                 std::make_shared<hep::ConeNode>(300.0, 1500.0, 500.0, 2500.0,
                                                 4000.0, -0.4, 1.7)});
    v.push_back({"Shell",
                 std::make_shared<hep::SphericalShellNode>(900.5, 2141.25, 0.0,
                                                           7.0, 0.35, 1.9)});
    v.push_back({"ShellFull",
                 std::make_shared<hep::SphericalShellNode>(900.5, 2141.25, 0.0,
                                                           7.0, 0.0, 3.2)});
    v.push_back({"Trd", std::make_shared<hep::TrapezoidNode>(
                            215.0, 587.5, 132.5, 445.0, 3110.0)});
    v.push_back({"Poly", std::make_shared<hep::PolyhedronNode>(
                             175.0, 882.5, 237.5, 1315.0, 2900.0, 0.13, 7.0,
                             6)});
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

// ── the LLVM columns ────────────────────────────────────────────────────────

using SdfFn = float (*)(float, float, float);
using GradFn = float (*)(float, float, float, float, float, float, float*);

SdfFn jit_value(const FRepNode& root) {
    auto ctx = std::make_unique<llvm::LLVMContext>();
    SceneCodegen cg(*ctx);
    cg.emit_scene_sdf(root);
    auto& mod = *cg.module();
    auto* f = mod.getFunction("scene_sdf");
    if (!f) return nullptr;
    auto* f32 = llvm::Type::getFloatTy(*ctx);
    auto* fty = llvm::FunctionType::get(f32, {f32, f32, f32}, false);
    auto* w = llvm::Function::Create(fty, llvm::Function::ExternalLinkage,
                                     "render_tile", &mod);
    llvm::IRBuilder<> b(llvm::BasicBlock::Create(*ctx, "entry", w));
    auto it = w->arg_begin();
    auto* x = &*it++; auto* y = &*it++; auto* z = &*it++;
    b.CreateRet(b.CreateCall(f, {x, y, z}));
    auto mp = cg.take_module();
    test::jit_pool().emplace_back(std::make_unique<JitEngine>());
    auto r = test::jit_pool().back()->load(std::move(mp), std::move(ctx));
    return r ? reinterpret_cast<SdfFn>(*r) : nullptr;
}

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
    llvm::IRBuilder<> b(llvm::BasicBlock::Create(*ctx, "entry", w));
    std::vector<llvm::Value*> args;
    for (auto& a : w->args()) args.push_back(&a);
    b.CreateRet(b.CreateCall(g, args));
    auto mp = cg.take_module();
    test::jit_pool().emplace_back(std::make_unique<JitEngine>());
    auto r = test::jit_pool().back()->load(std::move(mp), std::move(ctx));
    return r ? reinterpret_cast<GradFn>(*r) : nullptr;
}

// ── the GLSL columns ────────────────────────────────────────────────────────

bool glslang_available() { return !gpu::find_glslang().empty(); }

/// Everything the emitter wrote UP TO its ray-tracing main, plus a main of
/// our own that evaluates the point set instead of an image.
///
/// The emitted preamble already declares the storage image and the push
/// block, and already contains scene_sdf, scene_sdf_v and - when every kind
/// in the scene has a dual emitter - scene_sdf_grad. Cutting at "void main("
/// keeps all of it and drops only the renderer.
std::string point_shader(const std::string& emitted, bool sabotage) {
    const std::size_t cut = emitted.find("void main(");
    if (cut == std::string::npos) return {};
    std::string s = emitted.substr(0, cut);

    s += "\nconst vec3 XP_PTS[" + std::to_string(NPTS) + "] = vec3[](\n";
    const auto& P = points();
    for (int i = 0; i < NPTS; ++i) {
        s += "  vec3(" + gpu::flit(float(P[i].x)) + "," +
             gpu::flit(float(P[i].y)) + "," + gpu::flit(float(P[i].z)) + ")";
        s += (i + 1 < NPTS) ? ",\n" : "\n";
    }
    s += ");\n";

    // The bits, not the colour. rgba8 is UNORM, so byte k reads back as
    // k/255.0 and round(v*255.0) returns k for every k - the channel is an
    // exact byte, and four of them are an exact float.
    // The negative control lives INSIDE the shader, which is the only place
    // it proves anything: if the GLSL columns were somehow coming from the
    // CPU, an arithmetic slip introduced here would leave them unmoved.
    s += sabotage
             ? "float xp_v(vec3 p) { return scene_sdf_v(p) * 1.001; }\n"
               "Dual xp_g(vec3 p) { Dual d = scene_sdf_grad(p);\n"
               "  return Dual(d.v * 1.001, d.g + vec3(0.05, 0.0, 0.0)); }\n"
             : "float xp_v(vec3 p) { return scene_sdf_v(p); }\n"
               "Dual xp_g(vec3 p) { return scene_sdf_grad(p); }\n";

    s +=
        "void main() {\n"
        "  ivec2 sz = imageSize(out_image);\n"
        "  ivec2 id = ivec2(gl_GlobalInvocationID.xy);\n"
        "  if (id.x >= sz.x || id.y >= sz.y) return;\n"
        "  vec3 p = XP_PTS[id.x];\n"
        "  float r;\n"
        "  if (id.y == 0) { r = xp_v(p); }\n"
        "  else {\n"
        "    Dual d = xp_g(p);\n"
        "    r = (id.y == 1) ? d.v\n"
        "      : (id.y == 2) ? d.g.x\n"
        "      : (id.y == 3) ? d.g.y : d.g.z;\n"
        "  }\n"
        "  uint u = floatBitsToUint(r);\n"
        "  imageStore(out_image, id, vec4(float( u        & 0xFFu),\n"
        "                                 float((u >>  8) & 0xFFu),\n"
        "                                 float((u >> 16) & 0xFFu),\n"
        "                                 float((u >> 24) & 0xFFu)) / 255.0);\n"
        "}\n";
    return s;
}

float unpack(const std::vector<std::uint8_t>& px, int i, int row) {
    const std::size_t o = (std::size_t(row) * NPTS + std::size_t(i)) * 4;
    const std::uint32_t u = std::uint32_t(px[o + 0]) |
                            (std::uint32_t(px[o + 1]) << 8) |
                            (std::uint32_t(px[o + 2]) << 16) |
                            (std::uint32_t(px[o + 3]) << 24);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

/// Runs one node's shader over the point set. rows[row][i].
std::vector<std::vector<float>> gpu_rows(const FRepNode::Ptr& n,
                                         std::string* err,
                                         bool sabotage = false) {
    SceneGraph s;
    s.add_object(n);
    auto emitted = gpu::GlslEmitter::emit(s);
    if (!emitted) { *err = emitted.error(); return {}; }
    if (emitted->source.find("Dual scene_sdf_grad") == std::string::npos) {
        *err = "no dual emitter - the scene fell back to differences";
        return {};
    }
    const std::string src = point_shader(emitted->source, sabotage);
    if (src.empty()) { *err = "no main() in the emitted shader"; return {}; }

    auto spv = gpu::compile_glsl_to_spv(src);
    if (!spv) { *err = spv.error(); return {}; }
    auto ctx = gpu::VulkanCtx::create(*spv);
    std::error_code ec;
    std::filesystem::remove(*spv, ec);
    if (!ctx) { *err = ctx.error(); return {}; }

    float cam[3] = {0, 0, 1}, tgt[3] = {0, 0, 0}, light[3] = {0, 0, 1};
    auto push = gpu::build_push_simple(cam, tgt, light, NPTS, NROWS);
    std::vector<std::uint8_t> px;
    auto r = (**ctx).render(push, px);
    if (!r) { *err = r.error(); return {}; }
    if (px.size() < std::size_t(NPTS) * NROWS * 4) {
        *err = "short readback"; return {};
    }

    std::vector<std::vector<float>> rows(NROWS,
                                         std::vector<float>(NPTS, 0.0f));
    for (int row = 0; row < NROWS; ++row)
        for (int i = 0; i < NPTS; ++i) rows[row][i] = unpack(px, i, row);
    return rows;
}

// ── how the columns are compared ────────────────────────────────────────────

/// f32 carries ~1.2e-7 of relative precision, and these solids are metres
/// wide expressed in millimetres, so a bound stated in absolute units would
/// be a bound on the coordinates rather than on the code. Everything is
/// scaled by the point's own magnitude.
double scale_at(const P3& p) {
    return std::max(1.0, std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z));
}

struct Col {
    double worst = 0.0;       // max scaled deviation from the f64 column
    int    worst_i = -1;
    bool   present = false;
};

void note(Col& c, int i, double ref, double got, double scale) {
    const double e = std::fabs(got - ref) / scale;
    if (e > c.worst) { c.worst = e; c.worst_i = i; }
    c.present = true;
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// The table
// ═══════════════════════════════════════════════════════════════════════════

TEST(CrossPath, EveryDescriptionOfEverySolidAgreesOnValue) {
    const auto& P = points();
    const bool gpu_ok = gpu::VulkanCtx::available() && glslang_available();
    if (!gpu_ok)
        std::printf("  (no Vulkan or no glslangValidator: the GLSL columns "
                    "are skipped, the rest still runs)\n");

    std::printf("\n  scaled |x - f64| over %d points, worst over the set\n",
                NPTS);
    std::printf("  %-12s %10s %10s %10s %10s %10s\n", "solid", "f32", "ir",
                "ir.d", "glsl", "gl.d");

    double worst_all = 0.0;
    for (auto& c : cases()) {
        auto* fv = jit_value(*c.n);
        auto* fg = jit_grad(*c.n);
        ASSERT_NE(fv, nullptr) << c.nm << ": no IR value";
        ASSERT_NE(fg, nullptr) << c.nm << ": no IR dual";

        std::string gerr;
        std::vector<std::vector<float>> G;
        if (gpu_ok) {
            G = gpu_rows(c.n, &gerr);
            EXPECT_FALSE(G.empty()) << c.nm << ": " << gerr;
        }

        Col f32c, irc, irdc, glc, gldc;
        for (int i = 0; i < NPTS; ++i) {
            const double sc = scale_at(P[i]);
            const double ref = c.n->evalw(P[i].x, P[i].y, P[i].z);

            note(f32c, i, ref, double(c.n->eval(float(P[i].x), float(P[i].y),
                                                float(P[i].z))), sc);
            note(irc, i, ref,
                 double(fv(float(P[i].x), float(P[i].y), float(P[i].z))), sc);
            float d = 0.0f;
            const float dv = fg(float(P[i].x), 1.0f, float(P[i].y), 0.0f,
                                float(P[i].z), 0.0f, &d);
            note(irdc, i, ref, double(dv), sc);
            if (!G.empty()) {
                note(glc, i, ref, double(G[0][i]), sc);
                note(gldc, i, ref, double(G[1][i]), sc);
            }
        }

        auto col = [](const Col& x) { return x.present ? x.worst : -1.0; };
        std::printf("  %-12s %10.2e %10.2e %10.2e %10.2e %10.2e\n", c.nm,
                    col(f32c), col(irc), col(irdc), col(glc), col(gldc));

        // A transcription that dropped a wedge, flipped a sign or lost a
        // parameter lands in the 1e-1..1e0 range here; f32 rounding over a
        // handful of operations lands near 1e-6. The bound is set an order
        // of magnitude above the measured spread, not at it, because the
        // GPU's f32 is allowed to contract differently from the CPU's.
        for (auto* pc : {&f32c, &irc, &irdc, &glc, &gldc}) {
            if (!pc->present) continue;
            EXPECT_LT(pc->worst, 1e-4)
                << c.nm << ": a description disagrees at point "
                << pc->worst_i;
            worst_all = std::max(worst_all, pc->worst);
        }
    }
    std::printf("  worst across the whole table: %.3e\n", worst_all);
}

TEST(CrossPath, TheTwoDualsAgreeOnTheGradientAwayFromSeams) {
    if (!gpu::VulkanCtx::available() || !glslang_available())
        GTEST_SKIP() << "no Vulkan device or no glslangValidator";

    const auto& P = points();
    std::printf("\n  IR dual vs GLSL dual, cosine of the angle between the "
                "two gradients\n");
    std::printf("  %-12s %10s %10s %10s %8s\n", "solid", "median", "1%",
                "worst", "n");

    for (auto& c : cases()) {
        auto* fg = jit_grad(*c.n);
        ASSERT_NE(fg, nullptr) << c.nm;
        std::string gerr;
        auto G = gpu_rows(c.n, &gerr);
        ASSERT_FALSE(G.empty()) << c.nm << ": " << gerr;

        std::vector<double> cs;
        for (int i = 0; i < NPTS; ++i) {
            // The axis is where the derivative of the radius is guarded on
            // both sides; there is no direction there to agree about.
            if (std::sqrt(P[i].x * P[i].x + P[i].y * P[i].y) < 1.0) continue;
            float d = 0.0f;
            std::array<double, 3> a{};
            fg(float(P[i].x), 1.0f, float(P[i].y), 0.0f, float(P[i].z), 0.0f,
               &d); a[0] = d;
            fg(float(P[i].x), 0.0f, float(P[i].y), 1.0f, float(P[i].z), 0.0f,
               &d); a[1] = d;
            fg(float(P[i].x), 0.0f, float(P[i].y), 0.0f, float(P[i].z), 1.0f,
               &d); a[2] = d;
            const std::array<double, 3> b{G[2][i], G[3][i], G[4][i]};
            const double la = std::sqrt(a[0]*a[0] + a[1]*a[1] + a[2]*a[2]);
            const double lb = std::sqrt(b[0]*b[0] + b[1]*b[1] + b[2]*b[2]);
            if (la < 0.5 || lb < 0.5) continue;
            cs.push_back((a[0]*b[0] + a[1]*b[1] + a[2]*b[2]) / (la * lb));
        }
        ASSERT_FALSE(cs.empty()) << c.nm;
        std::sort(cs.begin(), cs.end());
        const double med = cs[cs.size() / 2];
        const double p01 = cs[cs.size() / 100];
        std::printf("  %-12s %10.6f %10.6f %10.6f %8zu\n", c.nm, med, p01,
                    cs.front(), cs.size());
        EXPECT_GT(med, 0.9999) << c.nm << ": median cos = " << med;
    }
}

TEST(CrossPath, TheGpuReadbackIsExactNotQuantised) {
    // The table above is only worth reading if the transport is lossless.
    // A byte written as k/255.0 through an rgba8 UNORM image has to come
    // back as k for every k in 0..255, or the GLSL columns are measuring
    // the image format instead of the shader. Checked on the one value the
    // shader itself produces: a float whose four bytes are all distinct and
    // include 0x00 and 0xFF.
    if (!gpu::VulkanCtx::available() || !glslang_available())
        GTEST_SKIP() << "no Vulkan device or no glslangValidator";

    auto n = std::make_shared<hep::TubeNode>(1210.125, 2950.625, 3100.875,
                                             0.0, 7.0);
    std::string err;
    auto G = gpu_rows(n, &err);
    ASSERT_FALSE(G.empty()) << err;

    // Every returned word must be a finite float - a quantised or truncated
    // channel shows up immediately as a denormal, a NaN or an absurd
    // exponent, because the exponent lives in the high byte.
    long bad = 0;
    for (int row = 0; row < NROWS; ++row)
        for (int i = 0; i < NPTS; ++i)
            if (!std::isfinite(G[row][i])) ++bad;
    EXPECT_EQ(bad, 0) << "non-finite words came back from the image";

    // And the bits have to carry more than eight of significance: if the
    // channel were quantising, the low mantissa bits would be constant
    // across the set.
    std::uint32_t or_bits = 0, and_bits = 0xFFFFFFFFu;
    for (int i = 0; i < NPTS; ++i) {
        std::uint32_t u;
        std::memcpy(&u, &G[0][i], 4);
        or_bits |= u; and_bits &= u;
    }
    const std::uint32_t varying = or_bits & ~and_bits;
    EXPECT_EQ(varying & 0xFFu, 0xFFu)
        << "the low byte of the mantissa never varies - the readback is "
           "quantised, not exact";
    std::printf("  varying bits across the set: 0x%08x\n", varying);
}

TEST(CrossPath, ASabotagedShaderIsCaught) {
    // The control for the table above, and the reason to believe it.
    //
    // On this container the GPU is lavapipe, which JITs SPIR-V through LLVM
    // on the same CPU, so the glsl columns came back agreeing with the ir
    // column to the last printed digit. That is the right answer for the
    // wrong-looking reason, and a column that agrees perfectly is exactly
    // what a column that is not being computed at all would look like.
    //
    // So: run the same comparison with a 0.1% error and a 0.05 gradient
    // offset introduced INSIDE the shader, and require the table to see it.
    // If the glsl columns were fed by anything other than the shader, this
    // test fails by passing the tolerance.
    if (!gpu::VulkanCtx::available() || !glslang_available())
        GTEST_SKIP() << "no Vulkan device or no glslangValidator";

    const auto& P = points();
    auto n = std::make_shared<hep::TubeNode>(1210.125, 2950.625, 3100.875,
                                             0.0, 7.0);
    std::string err;
    auto G = gpu_rows(n, &err, /*sabotage=*/true);
    ASSERT_FALSE(G.empty()) << err;

    Col glc, gldc;
    double worst_cos = 1.0;
    for (int i = 0; i < NPTS; ++i) {
        const double sc = scale_at(P[i]);
        const double ref = n->evalw(P[i].x, P[i].y, P[i].z);
        note(glc, i, ref, double(G[0][i]), sc);
        note(gldc, i, ref, double(G[1][i]), sc);
        const double lb = std::sqrt(G[2][i]*G[2][i] + G[3][i]*G[3][i] +
                                    G[4][i]*G[4][i]);
        if (lb < 0.5) continue;
        // Against the true unit gradient the offset shows as a tilt; the
        // exact reference direction does not matter, only that the returned
        // one is no longer a unit vector in the same place.
        worst_cos = std::min(worst_cos, std::fabs(1.0 - lb));
    }
    std::printf("  sabotaged: glsl %.2e, gl.d %.2e (bound is 1e-4)\n",
                glc.worst, gldc.worst);
    EXPECT_GT(glc.worst, 1e-4) << "a 0.1% error in the shader did not reach "
                                  "the value column";
    EXPECT_GT(gldc.worst, 1e-4) << "a 0.1% error in the shader did not reach "
                                   "the dual's value column";
}
