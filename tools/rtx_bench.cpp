// tools/rtx_bench.cpp — GpuRtx broad-phase scaling benchmark.
//
// Sweeps a sphere-grid scene over increasing object counts and times two
// renderers on each:
//   compute (cpu_ir): O(N) flat-union scene_sdf per march step
//   gpu_rtx multi-BLAS: one BLAS per CSG group; RT cores cull groups a ray
//     misses, so each intersection shader evaluates only one sphere
//
// The headline output is the scaling curve: as N grows, compute time should
// rise ~linearly while the RT broad-phase rises far slower. Reports the
// trace-only RT time (setup is amortizable) next to the CPU time.
//
// Usage: frep_rtx_bench [--width W] [--height H] [--counts 1,4,16,64]

#include "core/exec/bench_scenes.hpp"
#include "core/exec/cpu_executor.hpp"
#include "core/exec/multipath.hpp"
#include "core/gpu/rtx_caps.hpp"
#include "core/gpu/rtx_ctx.hpp"
#include "core/gpu/rtx_accel.hpp"
#include "core/gpu/rtx_shaders.hpp"
#include "core/gpu/rtx_pipeline.hpp"
#include "core/gpu/rtx_csg_groups.hpp"
#include "core/gpu/glsl_compile.hpp"
#include "core/gpu/shader_push_builder.hpp"
#include "core/compiler/codegen.hpp"
#include "core/power/energy_meter.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <memory>
#include <optional>

using namespace frep;

namespace {

// Compile one RT stage SPIR-V from GLSL text (returns words or empty on error).
std::vector<std::uint32_t> compile_stage(const std::string& src, const char* stage,
                                         std::string& err) {
    auto spv = gpu::compile_rt_stage_to_spv(src, stage);
    if (!spv) { err = spv.error(); return {}; }
    std::ifstream f(*spv, std::ios::binary | std::ios::ate);
    if (!f) { err = "cannot read " + *spv; return {}; }
    auto n = f.tellg(); f.seekg(0);
    std::vector<std::uint32_t> out((std::size_t)n / sizeof(std::uint32_t));
    f.read(reinterpret_cast<char*>(out.data()), n);
    std::remove(spv->c_str());
    return out;
}

FRepNode::Ptr scene_root(const SceneGraph& s) {
    for (auto& [id, o] : s.objects()) return o.geometry;
    return nullptr;
}

gpu::RtAabb to_aabb(const FRepNode::AABB& b, float m = 0.05f) {
    return gpu::RtAabb{ {b.min_x - m, b.min_y - m, b.min_z - m},
                        {b.max_x + m, b.max_y + m, b.max_z + m} };
}

}  // namespace

// ── energy needs a longer window than one frame ─────────────────────────────
//
// The first version bracketed ONE render with the counter. At 512x512 that is
// ~3.8 ms of CPU raymarch and ~0.55 ms of RT trace, and neither counter can
// resolve that: RAPL updates about every millisecond, and NVML's power reading
// is a sampled average whose update period is tens of milliseconds. What came
// back was not the energy of the work, it was the counter's quantisation.
//
// It read as a plausible table, which is the dangerous part. On an RTX 2080
// (25 Sep 2026) the cpu column was NON-MONOTONIC - N=4 rendered slower than
// N=1 yet reported nearly double the efficiency - and the ratio said the RT
// cores were ~12x LESS efficient per pixel than the CPU while being 6.9x
// faster. For a 2080 at a couple of hundred watts against a CPU package at
// some tens, the expectation is the opposite.
//
// So: repeat the operation until the window is long enough for the counter,
// and measure energy and time over THAT window, the same one. The throughput
// columns keep their old meaning (one hot loop, excluding JIT and setup) so
// the table stays comparable with earlier runs; the energy columns are now
// internally consistent instead of agreeing with nothing.
struct EnergySample {
    double joules  = 0.0;
    double seconds = 0.0;
    long   reps    = 0;
    bool   ok      = false;
};

/// Repeat `once` until at least `min_ms` has passed, with the counter open
/// across the whole run.
template <class F>
static EnergySample measure_energy(power::EnergyCounter* c, double min_ms,
                                   F&& once) {
    EnergySample s;
    if (!c || !c->available()) return s;
    const auto t0 = std::chrono::steady_clock::now();
    c->begin();
    double elapsed_ms = 0.0;
    do {
        once();
        ++s.reps;
        elapsed_ms = std::chrono::duration<double, std::milli>(
                         std::chrono::steady_clock::now() - t0).count();
    } while (elapsed_ms < min_ms);
    auto j = c->end();
    s.seconds = elapsed_ms / 1000.0;
    if (j && *j > 0.0) { s.joules = *j; s.ok = true; }
    return s;
}

// A reading outside this band is the counter misbehaving, not a result: no
// CPU package or discrete GPU doing continuous work draws less than half a
// watt, and nothing here draws two kilowatts. Printing the watts alongside is
// what makes the number checkable at a glance - Mpix/kWh is not.
constexpr double kMinWatts = 0.5;
constexpr double kMaxWatts = 2000.0;

/// "<Mpix/kWh> (<W> W)", or a dash and the reason.
static std::string energy_cell(const EnergySample& s, double pixels_per_rep) {
    if (!s.ok || s.seconds <= 0.0) return "-";
    const double w = s.joules / s.seconds;
    char buf[64];
    if (w < kMinWatts || w > kMaxWatts) {
        std::snprintf(buf, sizeof(buf), "? %.1fW", w);
        return buf;
    }
    const double mpk =
        power::pixels_per_kwh(pixels_per_rep * (double)s.reps, s.joules) / 1e6;
    std::snprintf(buf, sizeof(buf), "%.0f (%.0fW)", mpk, w);
    return buf;
}

int main(int argc, char** argv) {
    int W = 256, H = 256;
    bool energy = false;
    double energy_ms = 250.0;   // minimum energy window; see measure_energy
    std::vector<int> counts = {1, 4, 16, 64};
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](int d){ return i + 1 < argc ? std::atoi(argv[++i]) : d; };
        if (a == "--help" || a == "-h") {
            std::printf(
                "usage: %s [options]\n"
                "  --width N         frame width  (default 256)\n"
                "  --height N        frame height (default 256)\n"
                "  --counts A,B,...  CSG group counts to sweep (default 1,4,16,64)\n"
                "  --energy          measure CPU (RAPL) + GPU (NVML) energy, "
                "report Mpix/kWh and watts\n"
                "  --energy-ms N     minimum energy window, ms (default 250). "
                "One frame is far below\n"
                "                    what RAPL or NVML can resolve; the work "
                "repeats until N ms have passed.\n",
                argv[0]);
            return 0;
        }
        else if (a == "--width") W = next(W);
        else if (a == "--height") H = next(H);
        else if (a == "--energy") energy = true;
        else if (a == "--energy-ms" && i + 1 < argc) energy_ms = std::atof(argv[++i]);
        else if (a == "--counts") {
            counts.clear();
            std::string s = argv[++i]; size_t p = 0;
            while (p < s.size()) {
                size_t c = s.find(',', p);
                counts.push_back(std::atoi(s.substr(p, c - p).c_str()));
                if (c == std::string::npos) break; p = c + 1;
            }
        }
    }

    auto caps = gpu::detect_rtx_caps();
    std::printf("rtx_bench %dx%d  (%.3f Mpix/frame)\n[gpu_rtx backend] %s\n\n",
                W, H, (double)W * H / 1e6, caps.describe().c_str());

    // Optional energy counters (RAPL for CPU, NVML for GPU). Probed once; if a
    // counter isn't available the column is simply omitted — never invented.
    std::unique_ptr<power::EnergyCounter> cpu_e, gpu_e;
    if (energy) {
        cpu_e = power::make_cpu_energy_counter();
        gpu_e = power::make_gpu_energy_counter(0);
        std::printf("  [energy] cpu: %s  gpu: %s\n",
                    cpu_e->available() ? cpu_e->domain().c_str()
                                       : "unavailable",
                    gpu_e->available() ? gpu_e->domain().c_str()
                                       : "unavailable");
        if (!cpu_e->available())
            std::printf("  [energy] cpu unavailable — for RAPL without root: "
                        "sysctl kernel.perf_event_paranoid=0 (perf PMU), or "
                        "chmod a+r /sys/class/powercap/intel-rapl:*/energy_uj\n");
        std::printf("\n");
    }
    const bool cpu_e_ok = cpu_e && cpu_e->available();
    const bool gpu_e_ok = gpu_e && gpu_e->available();

    // Throughput, not "speedup": CPU and GPU cores aren't commensurable, so a
    // single ratio is misleading. The architectural result is that independent
    // devices ADD — running both concurrently gives cpu + rtx, which scaling a
    // single path can't reach (you can't turn 12 CPU cores into 512). Energy
    // (pix/kWh) and cost (pix/$) are separate axes for separate questions.
    if (energy)
        std::printf("  %5s  %7s  %12s  %12s  %12s  %16s  %16s\n",
                    "N", "groups", "cpu Mpix/s", "rtx Mpix/s", "sum Mpix/s",
                    "cpu Mpix/kWh", "rtx Mpix/kWh");
    else
        std::printf("  %5s  %7s  %12s  %12s  %12s\n",
                    "N", "groups", "cpu Mpix/s", "rtx Mpix/s", "sum Mpix/s");
    std::printf("  --------------------------------------------------------------"
                "%s\n", energy ? "--------------------------------" : "");

    const double mpix = (double)W * H / 1e6;

    TracerConfig cfg;
    exec::CpuIrExecutor cpu(SceneCodegen::SceneSdfMode::Inlined, cfg);

    for (int n : counts) {
        SceneGraph scene = bench::make_sphere_grid(n);
        auto root = scene_root(scene);
        auto groups = gpu::partition_csg_groups(root);

        // CPU reference: the executor's render_ms is the raymarch hot loop
        // (excludes JIT compile), the like-for-like against the RT trace_ms.
        // Both become throughput below. Energy is measured around the render
        // call (a warmup render first so JIT compile isn't charged to energy).
        cpu.render(scene, W, H, exec::Tile{0, 0, W, H});  // warmup (JIT)
        auto rc = cpu.render(scene, W, H, exec::Tile{0, 0, W, H});
        if (!rc.ok) { std::printf("  %5d  cpu render failed: %s\n", n, rc.error.c_str()); continue; }
        double cpu_trace_ms = rc.render_ms;  // raymarch hot loop only
        EnergySample cpu_es;
        if (cpu_e_ok)
            cpu_es = measure_energy(cpu_e.get(), energy_ms, [&] {
                (void)cpu.render(scene, W, H, exec::Tile{0, 0, W, H});
            });

        // RT multi-BLAS path.
        double rtx_trace_ms = -1.0;
        EnergySample gpu_es;
        std::string err;
        do {
            auto ctx = gpu::RtxCtx::create();
            if (!ctx) { err = ctx.error(); break; }

            // Per-group scenes + shaders.
            std::vector<SceneGraph> gscenes;
            for (auto& g : groups) { SceneGraph s; s.add_object(g.root); gscenes.push_back(std::move(s)); }
            auto sh = gpu::emit_rt_group_shaders(scene, gscenes, cfg);
            if (!sh) { err = sh.error(); break; }

            auto rgen  = compile_stage(sh->rgen,  "rgen",  err); if (!err.empty()) break;
            auto rchit = compile_stage(sh->rchit, "rchit", err); if (!err.empty()) break;
            auto rmiss = compile_stage(sh->rmiss, "rmiss", err); if (!err.empty()) break;
            std::vector<std::vector<std::uint32_t>> rints;
            for (auto& src : sh->rint_per_group) {
                rints.push_back(compile_stage(src, "rint", err));
                if (!err.empty()) break;
            }
            if (!err.empty()) break;

            // Per-group AABBs → multi-BLAS.
            std::vector<gpu::RtAabb> boxes;
            for (auto& g : groups) boxes.push_back(to_aabb(g.box));
            auto accel = gpu::RtAccel::build_groups(*ctx, boxes);
            if (!accel) { err = accel.error(); break; }

            gpu::ShaderPush sp = gpu::build_push_from_scene(scene, W, H);
            gpu::RtPushConstants pc;
            std::memcpy(&pc, &sp, sizeof(pc));

            auto img = gpu::rtx_trace_groups(*ctx, *accel, rgen, rints, rchit, rmiss, pc, W, H);
            if (!img) { err = img.error(); break; }
            rtx_trace_ms = img->trace_ms;
            // The recurring per-frame GPU work, repeated over one window.
            if (gpu_e_ok)
                gpu_es = measure_energy(gpu_e.get(), energy_ms, [&] {
                    (void)gpu::rtx_trace_groups(*ctx, *accel, rgen, rints,
                                                rchit, rmiss, pc, W, H);
                });
        } while (false);

        if (rtx_trace_ms < 0) {
            std::printf("  %5d  %7zu  %12.1f  rtx failed: %s\n",
                        n, groups.size(), mpix / (cpu_trace_ms / 1000.0), err.c_str());
        } else {
            double cpu_tp = mpix / (cpu_trace_ms / 1000.0);
            double rtx_tp = mpix / (rtx_trace_ms / 1000.0);
            if (energy) {
                // A counter that read nothing, or one whose watts are outside
                // the plausible band, prints a mark rather than a number.
                const double px = (double)W * H;
                std::printf("  %5d  %7zu  %12.1f  %12.1f  %12.1f  %16s  %16s\n",
                            n, groups.size(), cpu_tp, rtx_tp, cpu_tp + rtx_tp,
                            energy_cell(cpu_es, px).c_str(),
                            energy_cell(gpu_es, px).c_str());
            } else {
                std::printf("  %5d  %7zu  %12.1f  %12.1f  %12.1f\n",
                            n, groups.size(), cpu_tp, rtx_tp, cpu_tp + rtx_tp);
            }
        }
    }
    std::printf(
        "\n  Throughput = full-frame pixels / hot-loop time (cpu raymarch excl.\n"
        "  JIT; rtx vkCmdTraceRays excl. pipeline/SBT/AS setup).\n"
        "  sum = cpu + rtx: what running both paths concurrently delivers — the\n"
        "  heterogeneous win, since scaling one device can't reach it. Adding\n"
        "  more paths (gpu_glsl, gpu_ir, remote nodes) raises the sum further.\n");
    if (energy)
        std::printf(
            "  Mpix/kWh = energy efficiency from RAPL (cpu) / NVML (gpu) counters.\n"
            "  This is the separate axis that decides *which* devices to add to a\n"
            "  datacenter; a dash means the counter wasn't available. Note rtx\n"
            "  energy is trace-only and excludes the amortizable setup.\n");
    else
        std::printf(
            "  Energy (pix/kWh) and cost (pix/$) are separate axes; rerun with\n"
            "  --energy for RAPL/NVML measurement of the efficiency axis.\n");
    return 0;
}
