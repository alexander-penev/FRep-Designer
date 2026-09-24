// tests/test_glsl_hep.cpp
//
// The six HEP node types on the GPU path.
//
//   A  They EMIT. Before this, the emitter's switch did not know the kinds
//      and the plugin fallback could not serve them (it is handed child
//      expressions and a name prefix, never the coordinates), so a converted
//      detector could not reach the GPU at all.
//   B  The emitted GLSL COMPILES to SPIR-V. String-built shader source fails
//      at the first type slip, and nothing else in the build catches it.
//   C  It RUNS, and agrees with the CPU. A shader that compiles can still be
//      the wrong solid; this container has lavapipe, so the comparison is a
//      real one rather than a promise about hardware elsewhere.
//   D  The normals are ANALYTIC. Every kind needs a dual emitter or the
//      whole scene falls back to central differences - the same defect
//      measured on the CPU, where a Tube's |grad| reached 1.092 at 10 m.

#include "core/exec/cpu_executor.hpp"
#include "core/exec/multipath.hpp"
#include "core/exec/gpu_executor.hpp"
#include "core/frep/hep.hpp"
#include "core/frep/operations.hpp"
#include "core/gpu/glsl_compile.hpp"
#include "core/gpu/glsl_emitter.hpp"
#include "core/gpu/vulkan_ctx.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <system_error>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace frep;

namespace {

/// Detector-shaped, and small enough to render fast on a software device.
SceneGraph hep_scene() {
    SceneGraph s;
    s.add_object(std::make_shared<hep::TubeNode>(0.45, 0.9, 1.1, 0.0, 7.0));
    s.add_object(std::make_shared<hep::ConeNode>(0.1, 0.5, 0.2, 0.8, 0.9, 0.0,
                                                 7.0));
    s.add_object(std::make_shared<hep::SphericalShellNode>(0.6, 0.85, 0.0, 7.0,
                                                           0.4, 2.2));
    s.add_object(std::make_shared<hep::TrapezoidNode>(0.3, 0.6, 0.2, 0.5, 0.8));
    s.add_object(std::make_shared<hep::PolyhedronNode>(0.2, 0.7, 0.3, 0.9, 1.0,
                                                       0.13, 7.0, 6));
    {
        const double r[9] = {0.8, 0.0, 0.6, 0.0, 1.0, 0.0, -0.6, 0.0, 0.8};
        s.add_object(std::make_shared<hep::FrameNode>(
            std::make_shared<hep::TubeNode>(0.0, 0.4, 0.7, 0.0, 7.0), r, 0.3,
            -0.2, 0.1));
    }
    return s;
}

bool glslang_available() { return !gpu::find_glslang().empty(); }

struct Diff { double max_abs = 0.0; double mean_abs = 0.0; long over = 0; };

Diff compare(const std::vector<float>& a, const std::vector<float>& b,
             double tol) {
    Diff d;
    const std::size_t n = std::min(a.size(), b.size());
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double e = std::fabs(double(a[i]) - double(b[i]));
        d.max_abs = std::max(d.max_abs, e);
        sum += e;
        if (e > tol) ++d.over;
    }
    d.mean_abs = n ? sum / double(n) : 0.0;
    return d;
}

}  // namespace

TEST(GlslHep, AllSixKindsEmitWithoutFallingBack) {
    auto emitted = gpu::GlslEmitter::emit(hep_scene());
    ASSERT_TRUE(emitted.has_value()) << emitted.error();
    const std::string& src = emitted->source;

    // Each kind leaves a recognisable trace. Checking for the VARIABLE name
    // prefixes rather than for arbitrary substrings, because those come from
    // the emitters themselves and change if a kind silently stops emitting.
    for (const char* marker : {"tube", "cone", "shell", "trd", "poly", "fqx"})
        EXPECT_NE(src.find(marker), std::string::npos)
            << "no trace of " << marker << " in the emitted shader";

    // A CustomExpr or a bail-out would show up as the emitter refusing.
    EXPECT_EQ(src.find("unsupported node type"), std::string::npos);
}

TEST(GlslHep, TheNormalsAreAnalyticNotCentralDifferences) {
    auto emitted = gpu::GlslEmitter::emit(hep_scene());
    ASSERT_TRUE(emitted.has_value()) << emitted.error();
    const std::string& src = emitted->source;

    EXPECT_NE(src.find("Dual scene_sdf_grad"), std::string::npos)
        << "the dual path was abandoned - some HEP kind has no dual emitter";
    EXPECT_NE(src.find("scene_sdf_grad(p).g"), std::string::npos);
    EXPECT_EQ(src.find("scene_sdf_v(p + vec3(h,0,0))"), std::string::npos)
        << "central-difference normal leaked into a scene that has duals";
    // The helpers the HEP duals need must be in the preamble.
    EXPECT_NE(src.find("Dual d_div_s"), std::string::npos);
    EXPECT_NE(src.find("Dual d_abs"), std::string::npos);
}

TEST(GlslHep, TheEmittedShaderCompilesToSpirv) {
    auto emitted = gpu::GlslEmitter::emit(hep_scene());
    ASSERT_TRUE(emitted.has_value()) << emitted.error();
    if (!glslang_available()) GTEST_SKIP() << "glslangValidator not on PATH";
    auto spv = gpu::compile_glsl_to_spv(emitted->source);
    ASSERT_TRUE(spv.has_value()) << spv.error();
    // compile_glsl_to_spv returns the PATH of the .spv, not its contents -
    // spv->size() is the length of a filename. Measuring that and calling it
    // a shader size is how a test ends up reporting 25 words for a 16208
    // word module, which is what the first version of this line did.
    std::error_code ec;
    const auto bytes = std::filesystem::file_size(*spv, ec);
    ASSERT_FALSE(ec) << "no .spv at " << *spv;
    EXPECT_GT(bytes, 1024u) << "a real module, not an empty file";
    std::printf("  SPIR-V: %zu words from %zu chars of GLSL\n",
                std::size_t(bytes / 4), emitted->source.size());
    std::filesystem::remove(*spv, ec);
}

TEST(GlslHep, TheGpuAgreesWithTheCpuOnHepGeometry) {
    if (!gpu::VulkanCtx::available()) GTEST_SKIP() << "no Vulkan device";
    if (!glslang_available()) GTEST_SKIP() << "glslangValidator not on PATH";

    const SceneGraph s = hep_scene();
    const int W = 96, H = 72;

    exec::CpuIrExecutor cpu_exec;
    auto rc = cpu_exec.render(s, W, H, exec::Tile{0, 0, W, H});
    ASSERT_TRUE(rc.ok) << rc.error;

    exec::GpuGlslExecutor gpu_exec;
    auto rg = gpu_exec.render(s, W, H, exec::Tile{0, 0, W, H});
    ASSERT_TRUE(rg.ok) << rg.error;

    ASSERT_EQ(rc.rgba.size(), rg.rgba.size());
    // A shading pipeline, not a distance query: the two paths round
    // differently at every stage, so the statement is that they render the
    // SAME PICTURE, not the same bits. A wrong solid or a wrong normal moves
    // whole regions and shows up in the max; f32 divergence does not.
    const Diff d = compare(rc.rgba, rg.rgba, 0.05);
    std::printf("  CPU vs GLSL over %zu channels: max %.4f, mean %.5f, "
                "%ld past 0.05\n", rc.rgba.size(), d.max_abs, d.mean_abs,
                d.over);
    EXPECT_LT(d.mean_abs, 0.02);
    EXPECT_LT(double(d.over) / double(rc.rgba.size()), 0.02);
}
