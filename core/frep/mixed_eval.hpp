// core/frep/mixed_eval.hpp
//
// Two types over one geometry: a cheap one that may REJECT, and an exact one
// that is the only one allowed to ANSWER.
//
// The reason to have both is that the two questions a field is asked are not
// the same question. "Is the surface anywhere near this point?" tolerates a
// coarse answer as long as the error is bounded. "How far is the surface?"
// does not. A pipeline that uses one type for both either pays f64 for every
// rejection or reports an f32 distance as though it were a distance.
//
// THE BAND MUST BE MEASURED, AND IT IS AN ESTIMATE, NOT A CERTIFICATE.
//
//     loEstimate(p) = lo(p) - delta      upEstimate(p) = lo(p) + delta
//
// delta starts at zero and STAYS zero until measureDelta() has run on this
// geometry over the region the caller will query. A delta carried over from
// another model, or assumed from the type's epsilon, is worthless: f32's
// error scales with the coordinate, so the same tree at the origin and at
// 10 m has deltas three orders of magnitude apart. The accessors refuse
// rather than silently returning lo(p) when nothing has been measured.
//
// But measureDelta() SAMPLES, so the band it produces is a sampled maximum
// and a point it never saw can exceed it. Measured on the detector-scale
// test scene: delta 6.3e-04 mm from 20000 points, then 2 of 50000 fresh
// points outside it, the worst by 6.6e-06 mm - about 1% of delta. That is
// small, and it is not zero, and the difference between those two words is
// the difference between an estimate and a proof.
//
// So the accessors are named for what they are. Use them to decide WHERE TO
// SPEND EFFORT - which is what refine() does, and where being wrong costs
// accuracy already bounded by the caller's own tolerance. Do NOT use them
// for a rejection that must be sound: discarding geometry on an estimate
// discards it for good. A sound rejection needs interval arithmetic over the
// node (core/compiler/node_interval.hpp), which encloses rather than samples.
//
// COVERAGE IS PART OF THE ANSWER. FRepNode::evalw falls back to float for a
// node that has not been converted, which is correct but not wide. A tree
// with one such node evaluated "in Hi" is worse than one evaluated entirely
// in Lo, because the error is real and in an unknown place. wideCoverage()
// counts it, and exact() is false unless every node carries a real wide
// evaluation.
//
// THE CHEAP TYPE BUYS NO SPEED ON THIS CPU. MEASURED THREE WAYS.
//
// INTERPRETED, f32 is SLOWER than f64 on every node kind - sphere 1.34x,
// box 1.43x, tube 1.13x, cone 1.48x, poly 1.67x, frame 1.78x - and on a real
// sphere trace the mixed path never beat plain f64 at any tolerance (27.3 vs
// 29.8 ns/step at 1 mm; 22.4 vs 26.1 at 1e-3 mm). The cause is the parameter
// store: it holds double, so eval_t<float> narrows every parameter on the
// way in and scalar SSE returns nothing for it.
//
// COMPILED AND SCALAR, the two are EQUAL within run-to-run variance:
// 0.81-1.01x over nine runs, mostly 0.93-1.01 (tests/test_codegen_f64.cpp).
// The JIT folds the parameters into constants of the right type, so the
// conversions the interpreter pays are simply not there. The interpreter's
// slowness is a property of the INTERPRETER, not of f32 - worth stating
// precisely, because the first version of this comment said "structural
// rather than incidental" and that was too strong.
//
// COMPILED AND VECTORISED, f32 finally pays, and by the factor the mechanism
// predicts (tests/test_simd_scalar_type.cpp, AVX-512, 16 float lanes):
//
//     f32 x16   0.73 ns/point
//     f64 x8    1.57 ns/point   same register bytes   -> f32 2.03-2.14x
//     f64 x16   1.73 ns/point   same lane count       -> f32 2.23-2.35x
//
// So the rule is not "f32 is cheap" or "f32 is not cheap" - it is that f32
// pays exactly where it buys LANES, and nowhere else. Scalar code puts one
// value in a register whatever its width, which is why both the interpreter
// and the scalar JIT see nothing. That makes Lo = float right for a vector
// cull and Hi = double right for a reported distance, which is the split
// this class exists to express.
//
// (The benchmark that produced the compiled numbers got them wrong first:
// it converted points inside the timed loop, so every f32 call paid three
// narrowings belonging to the harness and read as 1.29x slower. Each type
// now gets its points in its own type.)
//
// The CPU speed in this area came from the data structure, not the type:
// moving parameters from a string-keyed hash map to an ordered vector was
// 4.6x on the same benchmark. See param_store.hpp.
//
// This mirrors G4-FRep's MixedTape<Lo,Hi> (include/frep/model/tape.hh), down
// to the rule that the cheap tape never reports a value.

#pragma once

#include "core/frep/node.hpp"
#include "core/frep/scalar.hpp"

#include <cmath>
#include <cstddef>
#include <random>
#include <stdexcept>

namespace frep {

template <class Lo, class Hi>
class MixedEval {
    static_assert(sizeof(Hi) >= sizeof(Lo),
                  "Hi is the exact type; it cannot be narrower than Lo");

public:
    explicit MixedEval(const FRepNode& n) : n_(&n) {}

    /// The cheap field. Culling and traversal only - never reported.
    Lo lo(Lo x, Lo y, Lo z) const { return n_->eval_as<Lo>(x, y, z); }

    /// The exact field. The only value a caller may report as a distance.
    Hi hi(Hi x, Hi y, Hi z) const { return n_->eval_as<Hi>(x, y, z); }

    // ── the measured band ─────────────────────────────────────────────────

    bool measured() const noexcept { return measured_; }
    double delta() const noexcept { return delta_; }

    /// The largest |lo - hi| seen over `n` random points of the box, with the
    /// box recorded so a later query outside it can be spotted. Returns it.
    ///
    /// SAMPLED, so it is a typical magnitude and not a maximum: fresh points
    /// exceed it at the per-mille level, by a per-cent of its own size. Read
    /// the header before using the result as though it enclosed anything.
    double measureDelta(double lox, double loy, double loz, double hix,
                        double hiy, double hiz, int n = 20000,
                        std::uint64_t seed = 0x5EED) {
        std::mt19937_64 rng(seed);
        auto U = [&](double a, double b) {
            return std::uniform_real_distribution<double>(a, b)(rng);
        };
        double d = 0.0;
        for (int i = 0; i < n; ++i) {
            const double x = U(lox, hix), y = U(loy, hiy), z = U(loz, hiz);
            const double a = double(lo(Lo(x), Lo(y), Lo(z)));
            const double b = double(hi(Hi(x), Hi(y), Hi(z)));
            const double e = std::fabs(a - b);
            if (e > d) d = e;
        }
        blox_ = lox; bloy_ = loy; bloz_ = loz;
        bhix_ = hix; bhiy_ = hiy; bhiz_ = hiz;
        delta_ = d;
        measured_ = true;
        return d;
    }

    /// True when p lies inside the box the delta was measured over.
    bool inMeasuredRegion(double x, double y, double z) const noexcept {
        return measured_ && x >= blox_ && x <= bhix_ && y >= bloy_ &&
               y <= bhiy_ && z >= bloz_ && z <= bhiz_;
    }

    /// lo(p) minus the measured band. An estimate - see the header.
    double loEstimate(double x, double y, double z) const {
        requireMeasured();
        return double(lo(Lo(x), Lo(y), Lo(z))) - delta_;
    }
    /// lo(p) plus the measured band. An estimate - see the header.
    double upEstimate(double x, double y, double z) const {
        requireMeasured();
        return double(lo(Lo(x), Lo(y), Lo(z))) + delta_;
    }

    /// Adopt a delta established some other way - an interval enclosure, or
    /// an error analysis. Marks the band measured without sampling, because
    /// the caller is asserting something stronger than sampling can.
    void setDelta(double d) noexcept {
        delta_ = d;
        measured_ = true;
    }

    // ── the refinement the two types exist for ────────────────────────────

    /// Cheap first, exact only when it matters.
    ///
    /// If the cheap field puts the point further than `band` from the surface
    /// - with the measured delta already taken off - the cheap value is
    /// returned and `paid` is left alone. Otherwise the exact field is
    /// evaluated.
    ///
    /// This is the one place the sampled band is the right tool: the caller
    /// has already accepted an error of `band`, so a decision that is wrong
    /// by a per-cent of delta costs nothing it had not agreed to.
    ///
    /// The caller chooses `band`: a sphere tracer wants its step tolerance,
    /// a classifier wants zero. Choosing zero means "exact near the surface,
    /// cheap everywhere else", which is what a march spends most of its
    /// queries on.
    double refine(double x, double y, double z, double band,
                  std::size_t* paid = nullptr) const {
        requireMeasured();
        const double c = double(lo(Lo(x), Lo(y), Lo(z)));
        if (std::fabs(c) - delta_ > band) return c;
        if (paid) ++*paid;
        return double(hi(Hi(x), Hi(y), Hi(z)));
    }

    // ── coverage ──────────────────────────────────────────────────────────

    /// Nodes whose evalw is genuinely wide, over the whole subtree.
    static void countWide(const FRepNode& n, std::size_t& wide,
                          std::size_t& total) {
        ++total;
        if (n.wide_eval()) ++wide;
        for (const auto& c : n.children)
            if (c) countWide(*c, wide, total);
    }
    double wideCoverage() const {
        std::size_t w = 0, t = 0;
        countWide(*n_, w, t);
        return t ? double(w) / double(t) : 1.0;
    }
    /// False when any node falls back to float: hi() is then not Hi.
    bool exact() const { return wideCoverage() >= 1.0; }

private:
    void requireMeasured() const {
        if (!measured_)
            throw std::logic_error(
                "MixedEval: delta was never measured on this geometry, so "
                "there is no bound to use - call measureDelta() first");
    }

    const FRepNode* n_ = nullptr;
    double delta_ = 0.0;
    bool measured_ = false;
    double blox_ = 0, bloy_ = 0, bloz_ = 0, bhix_ = 0, bhiy_ = 0, bhiz_ = 0;
};

}  // namespace frep
