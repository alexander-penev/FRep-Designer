// core/frep/aabb.cpp
//
// Conservative axis-aligned bounding boxes for all FRepNode types.
// Used for BVH construction in scene_material.

#include "operations.hpp"
#include "primitives.hpp"
#include "transforms.hpp"

#include <cmath>

namespace frep {

// ── Primitives ──────────────────────────────────────────────────────────────

FRepNode::AABB SphereNode::aabb() const {
    float r = params.at("r");
    return {-r, -r, -r, r, r, r};
}

FRepNode::AABB BoxNode::aabb() const {
    float hx = params.at("hx"), hy = params.at("hy"), hz = params.at("hz");
    return {-hx, -hy, -hz, hx, hy, hz};
}

// A plane is infinite — it stays infinite (the default).
// (PlaneNode does not override aabb())

// ── Operations ──────────────────────────────────────────────────────────────

// Union: the bounding box is the union of the two.
FRepNode::AABB UnionNode::aabb() const {
    return AABB::merge(children[0]->aabb(), children[1]->aabb());
}

// Intersection: the bounding box is the smaller one — conservatively we take
// the intersection box (it bounds the result).
FRepNode::AABB IntersectionNode::aabb() const {
    auto a = children[0]->aabb();
    auto b = children[1]->aabb();
    return {
        std::max(a.min_x, b.min_x), std::max(a.min_y, b.min_y),
        std::max(a.min_z, b.min_z),
        std::min(a.max_x, b.max_x), std::min(a.max_y, b.max_y),
        std::min(a.max_z, b.max_z)
    };
}

// Difference: A \ B is bounded by A.
FRepNode::AABB DifferenceNode::aabb() const {
    return children[0]->aabb();
}

// SmoothUnion: like Union, but slightly expanded by the blending radius k.
FRepNode::AABB SmoothUnionNode::aabb() const {
    auto m = AABB::merge(children[0]->aabb(), children[1]->aabb());
    float k = params.at("k");
    return {m.min_x - k, m.min_y - k, m.min_z - k,
            m.max_x + k, m.max_y + k, m.max_z + k};
}

// Negate: flips inside/outside — the bounding box becomes infinite.
FRepNode::AABB NegateNode::aabb() const {
    return AABB::infinite();
}

// ── Transforms ──────────────────────────────────────────────────────────────

FRepNode::AABB TranslateNode::aabb() const {
    auto c = children[0]->aabb();
    float tx = params.at("tx"), ty = params.at("ty"), tz = params.at("tz");
    return {c.min_x + tx, c.min_y + ty, c.min_z + tz,
            c.max_x + tx, c.max_y + ty, c.max_z + tz};
}

FRepNode::AABB ScaleNode::aabb() const {
    auto c = children[0]->aabb();
    float s = params.at("s");
    return {c.min_x * s, c.min_y * s, c.min_z * s,
            c.max_x * s, c.max_y * s, c.max_z * s};
}

// RotateY: the rotated box may be larger — conservatively we take
// the enclosing sphere in the XZ plane.
FRepNode::AABB RotateYNode::aabb() const {
    auto c = children[0]->aabb();
    // Radius of the XZ extent
    float rx = std::max(std::abs(c.min_x), std::abs(c.max_x));
    float rz = std::max(std::abs(c.min_z), std::abs(c.max_z));
    float r  = std::sqrt(rx * rx + rz * rz);
    return {-r, c.min_y, -r, r, c.max_y, r};
}

} // namespace frep
