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
#include <iterator>
#include <stdexcept>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace frep {

class ParamStore {
public:
    // ── copyable, deliberately and checked ────────────────────────────────
    //
    // The unique_ptr below is an implementation detail of "a plugin node may
    // invent names"; it is NOT a statement about ownership semantics. Letting
    // it delete the copy operations made every node class holding a
    // ParamStore non-copyable, and the failure surfaced far from here, as
    // std::construct_at refusing to copy a MeshSDFNode in the GUI. The test
    // suite never caught it because nothing there copies a node that way and
    // the GUI is not built where Qt6 is absent - which is exactly where the
    // change was made.
    ParamStore() = default;
    ParamStore(const ParamStore& o)
        : n_(o.n_), nn_(o.nn_),
          extra_(o.extra_ ? std::make_unique<std::vector<std::string>>(*o.extra_)
                          : nullptr),
          v_(o.v_) {}
    ParamStore& operator=(const ParamStore& o) {
        if (this != &o) { ParamStore t(o); swap(t); }
        return *this;
    }
    ParamStore(ParamStore&&) noexcept = default;
    ParamStore& operator=(ParamStore&&) noexcept = default;
    ~ParamStore() = default;

    void swap(ParamStore& o) noexcept {
        std::swap(n_, o.n_); std::swap(nn_, o.nn_);
        extra_.swap(o.extra_); v_.swap(o.v_);
    }

    // ── the hot path: position, fixed by the node's own index constants ────
    double operator[](int i) const noexcept { return v_[std::size_t(i)]; }
    double& operator[](int i) noexcept { return v_[std::size_t(i)]; }
    const double* data() const noexcept { return v_.data(); }

    // ── by name: I/O, the editor, emitters, anything generic over kinds ───

    /// Existing parameter, or a new one appended at the end. An appended
    /// name is NOT in the kind's table, so it is owned here - that is the
    /// plugin case, where the names are the node's own invention.
    double& operator[](std::string_view n) {
        if (double* p = get(n)) return *p;
        if (!extra_) extra_ = std::make_unique<std::vector<std::string>>();
        extra_->emplace_back(n);
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
    const std::string& name(std::size_t i) const noexcept {
        return i < nn_ ? n_[i] : (*extra_)[i - nn_];
    }
    double value(std::size_t i) const noexcept { return v_[i]; }

    /// Take the values, and BORROW the names from a table the caller owns.
    ///
    /// The names are a property of the KIND, not of the node: every Frame
    /// spells "r0".."r8", "tx", "ty", "tz" and every Tube spells "rmin",
    /// "rmax", "hz", "phi0", "dphi". Storing them per node cost more than the
    /// numbers did - measured over the converter's models, 43-46% of the node
    /// memory went to repeating those strings against 11% for the doubles
    /// they label. A node class keeps one static table and hands it here.
    ///
    /// The table has to outlive the store, which a function-local static in
    /// the node class does by construction.
    void init(const std::vector<std::string>& names,
              std::initializer_list<double> vals) {
        n_ = names.data();
        nn_ = names.size();
        v_.assign(vals.begin(), vals.end());
    }

    // ── iteration: `for (const auto& [name, value] : store)` ──────────────
    //
    // The old member was an unordered_map and three GUI call sites walked it
    // with a structured binding. Rewriting them as index loops would have
    // been the bigger change and the worse code, so the store yields the same
    // shape from its two parallel arrays. Read-only: a name is borrowed from
    // the kind's table and is not a caller's to rewrite.
    struct Entry {
        const std::string& name;
        double value;
    };

    class const_iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type        = Entry;
        using difference_type   = std::ptrdiff_t;
        using reference         = Entry;
        const_iterator() = default;
        const_iterator(const ParamStore* p, std::size_t i) noexcept
            : p_(p), i_(i) {}
        Entry operator*() const { return Entry{p_->name(i_), p_->value(i_)}; }
        const_iterator& operator++() noexcept { ++i_; return *this; }
        const_iterator operator++(int) noexcept { auto t = *this; ++i_; return t; }
        bool operator==(const const_iterator& o) const noexcept { return i_ == o.i_; }
    private:
        const ParamStore* p_ = nullptr;
        std::size_t       i_ = 0;
    };

    const_iterator begin() const noexcept { return {this, 0}; }
    const_iterator end()   const noexcept { return {this, v_.size()}; }
    const_iterator cbegin() const noexcept { return begin(); }
    const_iterator cend()   const noexcept { return end(); }

    static constexpr std::size_t npos = std::size_t(-1);

private:
    std::size_t index_of(std::string_view n) const noexcept {
        for (std::size_t i = 0; i < nn_; ++i)
            if (n_[i] == n) return i;
        if (extra_)
            for (std::size_t i = 0; i < extra_->size(); ++i)
                if ((*extra_)[i] == n) return nn_ + i;
        return npos;
    }

    const std::string* n_ = nullptr;   // borrowed: the kind's table
    std::size_t        nn_ = 0;
    /// Only a plugin node invents names, so this is null for every built-in
    /// one. An empty vector here would have been 24 dead bytes on every node
    /// in the scene - which is the mistake this whole change is about.
    std::unique_ptr<std::vector<std::string>> extra_;
    std::vector<double> v_;
};

// The copy operations above are load-bearing for every node class; assert it
// here so a future member cannot delete them again without saying so.
static_assert(std::is_copy_constructible_v<ParamStore> &&
              std::is_copy_assignable_v<ParamStore> &&
              std::is_nothrow_move_constructible_v<ParamStore>,
              "ParamStore must stay copyable: node classes hold one by value");

}  // namespace frep
