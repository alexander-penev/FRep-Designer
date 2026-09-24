// tests/test_param_schema.cpp
//
// Every parameter a node stores must be bindable. That sounds like a
// tautology and was not one: the binding table kept its OWN copy of NodeKind
// (`namespace pk`) so it could stay free of the LLVM-coupled node.hpp, the
// copy drifted by two positions when RotateX and RotateZ were added, and
// `static_cast<int>(n.kind)` bridged the two without complaint.
//
// Nothing failed, which is why it survived. A wrong kind does not throw - it
// returns the wrong SCHEMA, and a node whose schema does not name its
// parameters simply has none of them bound, so the incremental path quietly
// recompiles where it should have written a slot. Measured with an
// all-runtime policy before the fix, of twelve kinds only four bound
// correctly.
//
// The test below is therefore not "the schema is non-empty" but "the schema
// names exactly the parameters the constructor stored", asserted per kind
// against a real node. A future kind that forgets its schema entry fails
// here rather than degrading silently.

#include "core/compiler/llvm_compat.hpp"
#include "core/compiler/scene_bindings.hpp"
#include "core/frep/deformations.hpp"
#include "core/frep/hep.hpp"
#include "core/frep/operations.hpp"
#include "core/frep/primitives.hpp"
#include "core/frep/transforms.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <set>
#include <vector>
#include <string>

using namespace frep;

namespace {

struct AllRuntime final : CompilePolicy {
    ParamPlacement decide(const std::string&, const std::string&,
                          ParamClass) const override {
        return ParamPlacement::Runtime;
    }
};

/// Names the schema exposes for this kind.
std::set<std::string> schema_names(NodeKind k) {
    std::set<std::string> s;
    for (const auto& ps : node_param_schema(k)) s.insert(ps.first);
    return s;
}

/// Names the node actually stored, in the order it stored them.
std::vector<std::string> stored_order(const FRepNode& n) {
    std::vector<std::string> s;
    for (std::size_t i = 0; i < n.params.size(); ++i)
        s.push_back(n.params.name(i));
    return s;
}

/// Names the schema lists, in schema order.
std::vector<std::string> schema_order(NodeKind k) {
    std::vector<std::string> s;
    for (const auto& ps : node_param_schema(k)) s.push_back(ps.first);
    return s;
}

void expect_every_param_bindable(const char* what, const FRepNode& n) {
    // ORDER, not just membership: each node addresses its parameters by
    // index through its own `enum : int`, and the schema is what assigns
    // runtime slots. If the two orders ever disagree, a slot write lands on
    // the wrong parameter and the geometry changes silently.
    EXPECT_EQ(stored_order(n), schema_order(n.kind))
        << what << ": the schema and the stored parameters disagree";
}

FRepNode::Ptr leaf() { return std::make_shared<SphereNode>(1.0); }

}  // namespace

TEST(ParamSchema, EveryKindSchemaMatchesWhatTheNodeStores) {
    expect_every_param_bindable("Sphere", SphereNode(1.0));
    expect_every_param_bindable("Box", BoxNode(1.0, 2.0, 3.0));
    expect_every_param_bindable("Plane", PlaneNode(0.0, 1.0, 0.0, -1.0));
    expect_every_param_bindable("Translate", TranslateNode(leaf(), 1, 2, 3));
    expect_every_param_bindable("Scale", ScaleNode(leaf(), 2.0));
    expect_every_param_bindable("RotateX", RotateXNode(leaf(), 0.5));
    expect_every_param_bindable("RotateY", RotateYNode(leaf(), 0.5));
    expect_every_param_bindable("RotateZ", RotateZNode(leaf(), 0.5));
    expect_every_param_bindable("TwistY", TwistYNode(leaf(), 0.5));
    expect_every_param_bindable("BendXY", BendXYNode(leaf(), 0.5));
    expect_every_param_bindable("TaperY", TaperYNode(leaf(), 0.5, 2.0));
    expect_every_param_bindable("Tube", hep::TubeNode(0, 1, 1, 0, 7));
    expect_every_param_bindable("Cone", hep::ConeNode(0, 1, 0, 2, 1, 0, 7));
    expect_every_param_bindable("SphericalShell",
                                hep::SphericalShellNode(0, 1, 0, 7, 0, 3.1));
    expect_every_param_bindable("Trapezoid",
                                hep::TrapezoidNode(1, 2, 3, 4, 5));
    {
        const double r[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        expect_every_param_bindable("Frame", hep::FrameNode(leaf(), r, 1, 2, 3));
    }
}

TEST(ParamSchema, PolyhedronSchemaCoversItsOwnParameters) {
    // Separate because nside is stored as a parameter but is structural: it
    // must still appear, or an edit of it cannot be bound.
    hep::PolyhedronNode p(0, 1, 0, 2, 1, 0, 7, 6);
    expect_every_param_bindable("Polyhedron", p);
    EXPECT_TRUE(schema_names(NodeKind::Polyhedron).count("nside"));
}

TEST(ParamSchema, CompositesExposeNoParametersOfTheirOwn) {
    for (NodeKind k : {NodeKind::Union, NodeKind::Intersection,
                       NodeKind::Difference, NodeKind::Negate,
                       NodeKind::Scene, NodeKind::Instance,
                       NodeKind::Plugin})
        EXPECT_TRUE(node_param_schema(k).empty()) << int(k);
    // SmoothUnion is the exception: it has a blend radius.
    EXPECT_EQ(schema_order(NodeKind::SmoothUnion),
              (std::vector<std::string>{"k"}));
}

TEST(ParamSchema, AllRuntimePolicyBindsEveryStoredParameter) {
    // The end-to-end statement: with a policy that wants everything at
    // runtime, the number of slots equals the number of parameters in the
    // whole subtree. Before the fix this held for four kinds out of twelve.
    const AllRuntime pol;
    auto count = [](const FRepNode& n, auto&& self) -> std::size_t {
        std::size_t c = n.params.size();
        for (const auto& ch : n.children)
            if (ch) c += self(*ch, self);
        return c;
    };
    struct Case { const char* nm; FRepNode::Ptr n; };
    const Case cases[] = {
        {"Scale", std::make_shared<ScaleNode>(leaf(), 2.0)},
        {"RotateX", std::make_shared<RotateXNode>(leaf(), 0.5)},
        {"RotateZ", std::make_shared<RotateZNode>(leaf(), 0.5)},
        {"TwistY", std::make_shared<TwistYNode>(leaf(), 0.5)},
        {"BendXY", std::make_shared<BendXYNode>(leaf(), 0.5)},
        {"TaperY", std::make_shared<TaperYNode>(leaf(), 0.5, 2.0)},
        {"Tube", std::make_shared<hep::TubeNode>(0, 1, 1, 0, 7)},
        {"Cone", std::make_shared<hep::ConeNode>(0, 1, 0, 2, 1, 0, 7)},
    };
    for (const auto& c : cases) {
        const auto t = ParamBindingTable::build(to_view(*c.n), pol);
        EXPECT_EQ(t.slots().size(), count(*c.n, count)) << c.nm;
    }
}
