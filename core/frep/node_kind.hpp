// core/frep/node_kind.hpp
//
// NodeKind, on its own and free of LLVM.
//
// It lives here because two places need it and only one of them can include
// node.hpp. The parameter binding table is deliberately decoupled from the
// LLVM-coupled FRepNode so it can be built and unit tested on its own, and it
// used to carry its OWN copy of this enum (`namespace pk`) with
// `static_cast<int>(n.kind)` bridging the two. The copy drifted: RotateX and
// RotateZ were added to NodeKind at 11 and 12, which were TwistY and BendXY
// in the copy, and every kind from 11 upward shifted by two.
//
// Nothing detected it, because the bridge is a cast between two ints and a
// wrong kind does not fail - it just returns the WRONG SCHEMA, and a node
// whose schema does not list its parameters simply has none of them bound.
// Measured before the fix, with an all-runtime policy: of twelve kinds only
// Sphere, Box, Translate and RotateY bound their parameters. Scale, RotateX,
// RotateZ, TwistY, BendXY, TaperY and every HEP node bound NONE, so editing
// any of them forced a full recompile instead of a slot write.
//
// One enum, one definition. A second copy of a list that must stay in step
// with another is not decoupling, it is an unchecked invariant.

#pragma once

namespace frep {

enum class NodeKind {
    Sphere, Box, Plane,
    Union, Intersection, Difference, SmoothUnion,
    Negate,
    Translate, Scale, RotateY, RotateX, RotateZ,
    TwistY, BendXY, TaperY,
    Scene,
    Instance,
    // High-energy-physics primitives (core/frep/hep.hpp), 1:1 with the Geant4
    // solids a detector geometry is made of, plus one general rigid frame.
    // Native rather than plugin because aabb() is not optional: a node
    // without one is infinite, and one infinite leaf takes the BVH, the RTX
    // proxy and marching cubes with it.
    Tube, Cone, SphericalShell, Trapezoid, Polyhedron, Frame,
    // Plugin-defined nodes — emit goes through the FRepNode::emit_glsl
    // virtual fallback rather than the built-in switch table. Plugin
    // authors should set `kind = NodeKind::Plugin` in their node ctor.
    Plugin,
};

}  // namespace frep
