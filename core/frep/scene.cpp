// core/frep/scene.cpp
//
// Out-of-line SceneGraph members that need the full definition of node
// types (which would create an include cycle if pulled into scene.hpp).

#include "core/frep/scene.hpp"
#include "core/compiler/llvm_compat.hpp"
#include "core/frep/transforms.hpp"

#include <string>

namespace frep {

void SceneGraph::set_translation(const std::string& id,
                                 std::array<float, 3> t) {
    auto it = objects_.find(id);
    if (it == objects_.end()) return;

    FRepNode::Ptr& geom = it->second.geometry;
    if (!geom) return;

    const bool is_zero = (t[0] == 0.0f && t[1] == 0.0f && t[2] == 0.0f);

    if (std::string(geom->type_name()) == "Translate") {
        // Root is already a Translate — adjust it in place. If the new
        // offset is zero, unwrap back to the child so we don't leave an
        // identity Translate cluttering the tree (keeps the node graph
        // clean and avoids unbounded nesting under repeated edits).
        if (is_zero && !geom->children.empty()) {
            geom = geom->children[0];
        } else {
            geom->params["tx"] = t[0];
            geom->params["ty"] = t[1];
            geom->params["tz"] = t[2];
        }
    } else if (!is_zero) {
        // Wrap the existing geometry in a fresh Translate. The wrapper
        // takes the object's id so the scene key (which is geom->id)
        // stays stable; the wrapped child gets a derived id.
        std::string root_id   = geom->id;
        std::string child_id  = root_id + "/geom";
        geom->id = child_id;
        geom = std::make_shared<TranslateNode>(geom, t[0], t[1], t[2], root_id);
    }
    // else: not a Translate and offset is zero → nothing to do.

    dirty_ = true;
    ++revision_;
}

// ── Rotation / scale gizmo ───────────────────────────────────────────────
//
// Translation, rotation, and scale are stored as nested transform-node
// wrappers around the object's geometry, in a canonical order so the
// three gizmo fields compose predictably regardless of edit sequence:
//
//     Translate( RotateY( Scale( geometry ) ) )
//
// i.e. scale is applied first (innermost), then rotation, then
// translation (outermost) — the usual T·R·S convention. Each setter
// locates its own node type in the chain (if present) and updates it in
// place, inserts it at the correct depth if absent, or unwraps it when
// the value returns to identity. The helpers below keep the chain in
// canonical order; because each transform type appears at most once,
// repeated edits never nest duplicates.
//
// Identity values: translation (0,0,0), rotation 0 rad, scale 1.0.

namespace {

// Returns the node's transform type tag, or empty if it's not one of the
// three gizmo wrappers. Used to walk/splice the canonical chain.
const char* gizmo_tag(const FRepNode::Ptr& n) {
    if (!n) return "";
    std::string t = n->type_name();
    if (t == "Translate") return "Translate";
    if (t == "RotateY")   return "RotateY";
    if (t == "Scale")     return "Scale";
    return "";
}

} // namespace

void SceneGraph::set_rotation_y(const std::string& id, float angle_rad) {
    auto it = objects_.find(id);
    if (it == objects_.end()) return;
    FRepNode::Ptr& geom = it->second.geometry;
    if (!geom) return;

    const bool is_identity = (angle_rad == 0.0f);

    // RotateY sits below Translate but above Scale. Walk past a leading
    // Translate (if any) to reach the rotation slot.
    FRepNode::Ptr* slot = &geom;
    if (gizmo_tag(*slot) == std::string("Translate") && !(*slot)->children.empty())
        slot = &(*slot)->children[0];

    if (gizmo_tag(*slot) == std::string("RotateY")) {
        if (is_identity && !(*slot)->children.empty()) {
            *slot = (*slot)->children[0];          // unwrap to child
        } else {
            (*slot)->params["a"] = angle_rad;  // update in place
        }
    } else if (!is_identity) {
        std::string root_id  = (*slot)->id;
        std::string child_id = root_id + "/rot";
        (*slot)->id = child_id;
        *slot = std::make_shared<RotateYNode>(*slot, angle_rad, root_id);
    }

    dirty_ = true;
    ++revision_;
}

void SceneGraph::set_scale(const std::string& id, float s) {
    auto it = objects_.find(id);
    if (it == objects_.end()) return;
    FRepNode::Ptr& geom = it->second.geometry;
    if (!geom) return;

    const bool is_identity = (s == 1.0f);

    // Scale is the innermost wrapper: walk past Translate and RotateY.
    FRepNode::Ptr* slot = &geom;
    if (gizmo_tag(*slot) == std::string("Translate") && !(*slot)->children.empty())
        slot = &(*slot)->children[0];
    if (gizmo_tag(*slot) == std::string("RotateY") && !(*slot)->children.empty())
        slot = &(*slot)->children[0];

    if (gizmo_tag(*slot) == std::string("Scale")) {
        if (is_identity && !(*slot)->children.empty()) {
            *slot = (*slot)->children[0];
        } else {
            (*slot)->params["s"] = s;
        }
    } else if (!is_identity) {
        std::string root_id  = (*slot)->id;
        std::string child_id = root_id + "/scl";
        (*slot)->id = child_id;
        *slot = std::make_shared<ScaleNode>(*slot, s, root_id);
    }

    dirty_ = true;
    ++revision_;
}

float SceneGraph::get_rotation_y(const std::string& id) const {
    auto it = objects_.find(id);
    if (it == objects_.end() || !it->second.geometry) return 0.0f;
    FRepNode::Ptr n = it->second.geometry;
    if (gizmo_tag(n) == std::string("Translate") && !n->children.empty())
        n = n->children[0];
    if (gizmo_tag(n) == std::string("RotateY")) {
        auto p = n->params.find("a");
        if (p != n->params.end()) return p->second;
    }
    return 0.0f;
}

float SceneGraph::get_scale(const std::string& id) const {
    auto it = objects_.find(id);
    if (it == objects_.end() || !it->second.geometry) return 1.0f;
    FRepNode::Ptr n = it->second.geometry;
    if (gizmo_tag(n) == std::string("Translate") && !n->children.empty())
        n = n->children[0];
    if (gizmo_tag(n) == std::string("RotateY") && !n->children.empty())
        n = n->children[0];
    if (gizmo_tag(n) == std::string("Scale")) {
        auto p = n->params.find("s");
        if (p != n->params.end()) return p->second;
    }
    return 1.0f;
}

} // namespace frep
