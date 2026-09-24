#pragma once
// core/compiler/param_binding_table.hpp
//
// ParamBindingTable — the single, backend-agnostic authority for where each
// model parameter lives (baked constant vs. runtime buffer slot) and, for the
// runtime ones, which slot in the shared parameter buffer they occupy.
//
// One deterministic scene walk plus a CompilePolicy produce a table that EVERY
// backend consults:
//
//   CPU_IR    — load from the JIT'd render_tile's `float* params`
//   GPU_IR    — OpenCL/CUDA kernel's `global float* params` (same ABI/layout)
//   GPU_GLSL  — std430 `Params { float v[]; }` at a fixed binding
//   GPU_RTX   — std430 SSBO read in the intersection/closest-hit shaders
//
// Because every backend reads the SAME slot for the SAME (node_id, param), the
// runtime buffer layout is identical across paths: the host fills one buffer
// and all executors agree on it. That is what lets a parameter edit refresh a
// buffer instead of regenerating a shader/kernel on ALL paths, not just CPU —
// provided the parameter was placed Runtime. Constant-placed parameters are
// still baked, so changing one (or changing the placement itself) is a
// structural change that does require regeneration.
//
// Determinism: the table is built by walking the node schema (a fixed,
// canonical parameter order per node kind) in pre-order, NOT by iterating the
// unordered params map. Same scene + same policy ⇒ byte-identical table across
// runs and across backends.
//
// This header is intentionally free of any LLVM/Vulkan/CUDA dependency so the
// placement logic can be unit-tested on its own. The project builds a table
// from a real FRepNode through the thin NodeView adapter (see scene_bindings).

#include "core/compiler/compile_policy.hpp"
#include "core/frep/node_kind.hpp"
#include "core/frep/param_store.hpp"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace frep {

// NodeKind is shared with node.hpp via node_kind.hpp. It used to be
// mirrored here as a second enum so this header need not include the
// LLVM-coupled node.hpp; that copy drifted by two positions and silently
// handed most kinds the wrong schema. See node_kind.hpp.

// One bound runtime parameter.
struct ParamSlot {
    std::string node_id;
    std::string param_name;
    int         slot = -1;       // index into the shared runtime buffer
    float       default_value = 0.0f;
    ParamClass  cls = ParamClass::Geometry;
};

// The canonical, ordered (param, class) schema a node of `kind` exposes. The
// order fixes slot assignment; classes drive class-based policies. Centralises
// what used to be the scattered `param_class` argument at each emit site.
inline const std::vector<std::pair<std::string, ParamClass>>&
node_param_schema(NodeKind kind) {
    using P = std::pair<std::string, ParamClass>;
    using V = std::vector<P>;
    const auto G = ParamClass::Geometry;
    const auto D = ParamClass::Deform;
    static const V empty{};
    static const V sphere{{"r", G}};
    static const V box{{"hx", G}, {"hy", G}, {"hz", G}};
    static const V plane{{"nx", G}, {"ny", G}, {"nz", G}, {"d", G}};
    static const V smooth{{"k", G}};
    static const V translate{{"tx", G}, {"ty", G}, {"tz", G}};
    // sx/sy/sz, not "s": ScaleNode stores three factors even when
    // constructed from one, and the old single-entry schema matched none of
    // them, so a Scale node could never have a runtime parameter.
    static const V scale{{"sx", G}, {"sy", G}, {"sz", G}};
    static const V angle{{"a", G}};                       // RotateX/Y/Z
    static const V twist{{"k", D}};
    static const V bend{{"k", D}};
    static const V taper{{"t", D}, {"h", D}};
    // HEP nodes. The order is the constructor's, and it fixes slot
    // assignment, so entries are appended rather than reordered.
    static const V tube{{"rmin", G}, {"rmax", G}, {"hz", G},
                        {"phi0", G}, {"dphi", G}};
    static const V cone{{"rmin1", G}, {"rmax1", G}, {"rmin2", G},
                        {"rmax2", G}, {"hz", G}, {"phi0", G}, {"dphi", G}};
    static const V shell{{"rmin", G}, {"rmax", G}, {"phi0", G},
                         {"dphi", G}, {"theta0", G}, {"dtheta", G}};
    static const V trd{{"dx1", G}, {"dx2", G}, {"dy1", G}, {"dy2", G},
                       {"hz", G}};
    static const V poly{{"rmin1", G}, {"rmax1", G}, {"rmin2", G},
                        {"rmax2", G}, {"hz", G}, {"phi0", G}, {"dphi", G},
                        {"nside", G}};
    static const V frame{{"r0", G}, {"r1", G}, {"r2", G},
                         {"r3", G}, {"r4", G}, {"r5", G},
                         {"r6", G}, {"r7", G}, {"r8", G},
                         {"tx", G}, {"ty", G}, {"tz", G}};
    switch (kind) {
        case NodeKind::Sphere:         return sphere;
        case NodeKind::Box:            return box;
        case NodeKind::Plane:          return plane;
        case NodeKind::SmoothUnion:    return smooth;
        case NodeKind::Translate:      return translate;
        case NodeKind::Scale:          return scale;
        case NodeKind::RotateX:
        case NodeKind::RotateY:
        case NodeKind::RotateZ:        return angle;
        case NodeKind::TwistY:         return twist;
        case NodeKind::BendXY:         return bend;
        case NodeKind::TaperY:         return taper;
        case NodeKind::Tube:           return tube;
        case NodeKind::Cone:           return cone;
        case NodeKind::SphericalShell: return shell;
        case NodeKind::Trapezoid:      return trd;
        case NodeKind::Polyhedron:     return poly;
        case NodeKind::Frame:          return frame;
        // Union/Intersection/Difference/Negate/Scene/Instance/Plugin carry
        // no parameters of their own. Plugin nodes bind through their own
        // capsule rather than this table.
        case NodeKind::Union:
        case NodeKind::Intersection:
        case NodeKind::Difference:
        case NodeKind::Negate:
        case NodeKind::Scene:
        case NodeKind::Instance:
        case NodeKind::Plugin:         return empty;
    }
    return empty;
}

// (kind, param) -> class, via the schema. Geometry if unknown.
inline ParamClass classify_param(NodeKind kind, const std::string& param) {
    for (const auto& ps : node_param_schema(kind))
        if (ps.first == param) return ps.second;
    return ParamClass::Geometry;
}

class ParamBindingTable {
public:
    // A minimal read-only view of a node so the table can be built and unit
    // tested without depending on the LLVM-coupled FRepNode. The project
    // adapts a real scene into this view (children by value; trees are small).
    struct NodeView {
        NodeKind                                     kind = NodeKind::Scene;
        std::string                                  id;
        const ParamStore*                            params = nullptr;
        std::vector<NodeView>                        children;
    };

    static ParamBindingTable build(const NodeView& root,
                                   const CompilePolicy& policy) {
        ParamBindingTable t;
        t.walk(root, policy);
        return t;
    }

    // Slot for a runtime parameter, or -1 if it is Constant / unknown.
    int slot_of(const std::string& node_id, const std::string& param) const {
        auto it = slot_.find(node_id + "::" + param);
        return it == slot_.end() ? -1 : it->second;
    }
    bool is_runtime(const std::string& node_id, const std::string& param) const {
        return slot_of(node_id, param) >= 0;
    }

    const std::vector<ParamSlot>& slots() const { return slots_; }
    int  runtime_count() const { return static_cast<int>(slots_.size()); }
    bool empty()         const { return slots_.empty(); }

    // Seed values for the runtime buffer, in slot order. The host uploads
    // these once; later edits overwrite individual slots in place.
    std::vector<float> seed_buffer() const {
        std::vector<float> b(slots_.size(), 0.0f);
        for (const auto& s : slots_) b[s.slot] = s.default_value;
        return b;
    }

    // A stable fingerprint of the *placement* (which params are runtime, in
    // what slots) — but NOT of their runtime values. Backends fold this into
    // their shader/IR cache key so that editing a runtime value is a cache hit
    // while changing the placement (or a constant value) is a miss.
    std::size_t placement_hash() const {
        std::size_t h = 1469598103934665603ull;  // FNV-1a
        auto mix = [&](std::size_t v) { h ^= v; h *= 1099511628211ull; };
        for (const auto& s : slots_) {
            for (char c : s.node_id)    mix(static_cast<unsigned char>(c));
            for (char c : s.param_name) mix(static_cast<unsigned char>(c));
            mix(static_cast<std::size_t>(s.slot));
        }
        return h;
    }

private:
    std::vector<ParamSlot>              slots_;
    std::unordered_map<std::string,int> slot_;

    void walk(const NodeView& n, const CompilePolicy& policy) {
        for (const auto& ps : node_param_schema(n.kind)) {
            const std::string& name = ps.first;
            if (!n.params || !n.params->contains(name)) continue;  // not set
            if (policy.decide(n.id, name, ps.second) != ParamPlacement::Runtime)
                continue;                                       // baked constant
            const std::string key = n.id + "::" + name;
            if (slot_.count(key)) continue;                     // already bound
            int slot = static_cast<int>(slots_.size());
            slot_[key] = slot;
            // Narrowed on purpose: a runtime slot lands in the kernel's
            // `float* params` buffer, whose ABI is shared with OpenCL and
            // CUDA. The map keeps the full double for the constant path and
            // for evalw; the slot carries what the buffer can hold.
            slots_.push_back({n.id, name, slot,
                              float(n.params->at(name)), ps.second});
        }
        for (const auto& c : n.children) walk(c, policy);
    }
};

} // namespace frep
