// core/frep/param_store.hpp
//
// A node's parameters as an ORDERED VECTOR, reachable by index on the hot
// path and by name everywhere else.
//
// The measurement that forced this: of the 45 ns a BoxNode::eval takes, 40
// are three lookups in an unordered_map<std::string, double>. FD4's CPU
// evaluator was ~90% hash table and ~10% arithmetic, and no amount of
// careful floating point moves a number that is dominated by hashing three
// short strings. A radius does not need to be found; its position is a
// property of the node's type.
//
// The order is the CONSTRUCTOR's insertion order, which is also the order
// node_param_schema() lists - tests/test_param_schema.cpp asserts the two
// agree name for name AND position, so a node's index constants cannot drift
// from the schema that assigns its runtime slots.
//
// Names are kept beside the values rather than being dropped, because the
// editor, the serialiser and the GLSL emitter all address parameters by name
// and none of them is on a hot path. A name lookup here is a linear scan over
// at most twelve short strings with no hashing and no allocation, which is
// already faster than the hash map it replaces; the point of the vector is
// that the node bodies do not scan at all.
//
// Names outside the schema are allowed and simply append: a plugin node
// defines its own parameters and must keep working.

#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace frep {

class ParamStore {
public:
    // ── the hot path: position, fixed by the node's own index constants ────
    double operator[](int i) const noexcept { return v_[std::size_t(i)]; }
    double& operator[](int i) noexcept { return v_[std::size_t(i)]; }
    const double* data() const noexcept { return v_.data(); }

    // ── by name: I/O, the editor, emitters, anything generic over kinds ───

    /// Existing parameter, or a new one appended at the end.
    double& operator[](std::string_view n) {
        if (double* p = get(n)) return *p;
        n_.emplace_back(n);
        v_.push_back(0.0);
        return v_.back();
    }

    double at(std::string_view n) const {
        if (const double* p = get(n)) return *p;
        throw std::out_of_range("FRepNode has no parameter '" +
                                std::string(n) + "'");
    }

    double* get(std::string_view n) noexcept {
        const std::size_t i = index_of(n);
        return i == npos ? nullptr : &v_[i];
    }
    const double* get(std::string_view n) const noexcept {
        const std::size_t i = index_of(n);
        return i == npos ? nullptr : &v_[i];
    }

    double value_or(std::string_view n, double d) const noexcept {
        const double* p = get(n);
        return p ? *p : d;
    }
    bool contains(std::string_view n) const noexcept {
        return index_of(n) != npos;
    }

    // ── by position, for serialising and hashing ──────────────────────────
    std::size_t size() const noexcept { return v_.size(); }
    bool empty() const noexcept { return v_.empty(); }
    const std::string& name(std::size_t i) const noexcept { return n_[i]; }
    double value(std::size_t i) const noexcept { return v_[i]; }

    /// Reserve `k` slots so a constructor can assign by index without
    /// inserting names one at a time. The names still come from the schema.
    void init(std::initializer_list<std::string_view> names,
              std::initializer_list<double> vals) {
        n_.assign(names.begin(), names.end());
        v_.assign(vals.begin(), vals.end());
    }

    static constexpr std::size_t npos = std::size_t(-1);

private:
    std::size_t index_of(std::string_view n) const noexcept {
        for (std::size_t i = 0; i < n_.size(); ++i)
            if (n_[i] == n) return i;
        return npos;
    }

    std::vector<std::string> n_;
    std::vector<double>      v_;
};

}  // namespace frep
