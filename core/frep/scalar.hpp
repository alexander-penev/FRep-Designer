// core/frep/scalar.hpp
//
// The arithmetic type as a parameter, not a typedef.
//
// FD4 evaluates in float everywhere: FRepNode::eval is float, params is a map
// of float, CgCtx::f32 is the only constant maker. That is the right default -
// a render tile wants f32 and a GPU wants f32 - but it is a CEILING, and three
// separate things sit under it:
//
//   1. THE PARAMETERS. A radius read from a detector description is a double.
//      Storing it in a float map costs ~1e-4 mm at 10 m BEFORE any evaluation
//      happens, and no amount of careful arithmetic gets it back. This is why
//      params is a map of double: the type of the STORED value and the type of
//      the EVALUATION are different questions, and only the second one is a
//      speed/accuracy trade.
//
//   2. THE SURFACE BAND. A tolerance is a statement about a type. f32 spacing
//      at 1e4 mm is 9.8e-4 mm, so a 1e-9 band is empty there and every query
//      reads either "on the surface" or nothing does. tol() carries the band
//      the type can support, so a caller is told rather than silently
//      mis-served.
//
//   3. WHAT MAY BE REPORTED. A cheap type answers "can this be rejected?" It
//      does not answer "how far is the surface?" Those are different
//      questions and only the second needs the exact type. A pipeline is
//      therefore assembled from a cheap type for rejection and an exact one
//      for the root - see MixedEval - rather than one type for everything.
//
// Required of a specialisation:
//   type       V (the value type), W (a wider accumulator, may equal V)
//   convert    from(double), to(V)
//   arithmetic operator + - * / on V, plus sqrtv, absv, minv, maxv
//   rounding   up(V), down(V)      - one-ulp outward, identity when exact
//   limits     eps(), tol(), maxFinite(), exactAdditive(), name()
//
// The set matches G4-FRep's include/frep/core/scalar.hh so a type that works
// there works here. f16 and fixed point are defined there and deliberately not
// here: FD4 has no executor that can carry them yet, and a traits block with
// no executor behind it is a promise, not a capability.

#pragma once

#include <cmath>
#include <limits>

namespace frep {

template <class T>
struct ScalarTraits;

template <>
struct ScalarTraits<double> {
    using V = double;
    using W = double;
    static V from(double d) noexcept { return d; }
    static double to(V v) noexcept { return v; }
    static V sqrtv(V v) noexcept { return std::sqrt(v); }
    static V absv(V v) noexcept { return std::fabs(v); }
    static V minv(V a, V b) noexcept { return a < b ? a : b; }
    static V maxv(V a, V b) noexcept { return a > b ? a : b; }
    static V up(V v) noexcept {
        return std::nextafter(v, std::numeric_limits<V>::infinity());
    }
    static V down(V v) noexcept {
        return std::nextafter(v, -std::numeric_limits<V>::infinity());
    }
    static V eps() noexcept { return 2.22e-16; }
    static V tol() noexcept { return 1e-9; }        // Geant4's kCarTolerance
    static V maxFinite() noexcept { return 1.79e308; }
    static bool exactAdditive() noexcept { return false; }
    static const char* name() noexcept { return "f64"; }
};

template <>
struct ScalarTraits<float> {
    using V = float;
    using W = float;
    static V from(double d) noexcept { return float(d); }
    static double to(V v) noexcept { return double(v); }
    static V sqrtv(V v) noexcept { return std::sqrt(v); }
    static V absv(V v) noexcept { return std::fabs(v); }
    static V minv(V a, V b) noexcept { return a < b ? a : b; }
    static V maxv(V a, V b) noexcept { return a > b ? a : b; }
    static V up(V v) noexcept {
        return std::nextafterf(v, std::numeric_limits<V>::infinity());
    }
    static V down(V v) noexcept {
        return std::nextafterf(v, -std::numeric_limits<V>::infinity());
    }
    static V eps() noexcept { return 1.19e-7f; }
    // f32 spacing at 1e4 mm is 9.8e-4 mm; a 1e-9 band is meaningless here.
    static V tol() noexcept { return 1e-3f; }
    static V maxFinite() noexcept { return 3.4e38f; }
    static bool exactAdditive() noexcept { return false; }
    static const char* name() noexcept { return "f32"; }
};

// Which of a node's two evaluators a caller is asking for. Kept as a type
// rather than a bool so a third one does not have to change every signature.
enum class ScalarKind { F32, F64 };

}  // namespace frep

/// The three lines every converted node carries. eval and evalw are generated
/// from ONE templated body, so the float path and the double path cannot
/// drift - the same discipline codegen() already owes eval(). wide_eval says
/// the double one is real rather than a forward through float.
#define FREP_EVAL_T                                                           \
    float eval(float x, float y, float z) const override {                    \
        return eval_t<float>(x, y, z);                                        \
    }                                                                         \
    double evalw(double x, double y, double z) const override {               \
        return eval_t<double>(x, y, z);                                       \
    }                                                                         \
    bool wide_eval() const noexcept override { return true; }
