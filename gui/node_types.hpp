#pragma once
// gui/node_types.hpp
//
// Metadata for the node types in the graph editor.
//
// Each node type has: an input count, a parameter list (name + default + range),
// and a category (for color coding). The editor reads this metadata to draw
// the nodes and generate property fields; the tree builder uses it to
// instantiate the correct FRepNode.

#include <QColor>
#include <QString>

#include <vector>

namespace frep::gui {

// A node parameter — name, default value, min/max for the slider.
struct NodeParam {
    QString name;
    float   default_value;
    float   min_value;
    float   max_value;
};

// Category — determines the header color.
enum class NodeCategory {
    Primitive,   // Sphere, Box, Plane — green
    Operation,   // Union, Intersection... — blue
    Transform,   // Translate, Scale, RotateY — orange
    Output,      // the special output node — purple
};

// Descriptor of a node type.
struct NodeTypeInfo {
    QString                 type_name;   // matches FRepNode::type_name()
    QString                 display;     // name shown in the UI
    NodeCategory            category;
    int                     input_count; // 0 for primitives, 1 for transforms/Negate, 2 for CSG
    std::vector<NodeParam>  params;

    QColor header_color() const {
        switch (category) {
            case NodeCategory::Primitive: return QColor(0x4c, 0xaf, 0x50);
            case NodeCategory::Operation: return QColor(0x21, 0x96, 0xf3);
            case NodeCategory::Transform: return QColor(0xff, 0x98, 0x00);
            case NodeCategory::Output:    return QColor(0x9c, 0x27, 0xb0);
        }
        return Qt::gray;
    }
};

// Catalog of all built-in node types.
inline const std::vector<NodeTypeInfo>& node_catalog() {
    static const std::vector<NodeTypeInfo> catalog = {
        // ── Primitives ────────────────────────────────────────────────────────
        { "Sphere", "Sphere", NodeCategory::Primitive, 0,
          { {"r", 1.0f, 0.1f, 5.0f} } },
        { "Box", "Box", NodeCategory::Primitive, 0,
          { {"hx", 1.0f, 0.1f, 5.0f},
            {"hy", 1.0f, 0.1f, 5.0f},
            {"hz", 1.0f, 0.1f, 5.0f} } },
        { "Plane", "Plane", NodeCategory::Primitive, 0,
          { {"nx", 0.0f, -1.0f, 1.0f},
            {"ny", 1.0f, -1.0f, 1.0f},
            {"nz", 0.0f, -1.0f, 1.0f},
            {"d",  0.0f, -5.0f, 5.0f} } },

        // ── Operations ────────────────────────────────────────────────────────
        { "Union",        "Union (∪)",        NodeCategory::Operation, 2, {} },
        { "Intersection", "Intersection (∩)", NodeCategory::Operation, 2, {} },
        { "Difference",   "Difference (∖)",   NodeCategory::Operation, 2, {} },
        { "SmoothUnion",  "Smooth Union",     NodeCategory::Operation, 2,
          { {"k", 0.4f, 0.05f, 2.0f} } },
        { "Negate",       "Negate (¬)",       NodeCategory::Operation, 1, {} },

        // ── Transforms ────────────────────────────────────────────────────────
        { "Translate", "Translate", NodeCategory::Transform, 1,
          { {"tx", 0.0f, -5.0f, 5.0f},
            {"ty", 0.0f, -5.0f, 5.0f},
            {"tz", 0.0f, -5.0f, 5.0f} } },
        { "Scale", "Scale", NodeCategory::Transform, 1,
          { {"s", 1.0f, 0.1f, 5.0f} } },
        { "RotateY", "Rotate Y", NodeCategory::Transform, 1,
          { {"a", 0.0f, -3.14159f, 3.14159f} } },
    };
    return catalog;
}

// Finds a descriptor by type_name. Returns nullptr if it does not exist.
inline const NodeTypeInfo* find_node_type(const QString& type_name) {
    for (const auto& info : node_catalog())
        if (info.type_name == type_name)
            return &info;
    return nullptr;
}

} // namespace frep::gui

// Forward-declare for the plugin-info helper signature. We do not include
// core/plugin/plugin_api.hpp from this header because that pulls in LLVM
// headers, which confuse Qt's MOC and significantly slow compilation of
// every translation unit that includes this header.
namespace frep::plugin {
class PluginRegistry;
struct PluginInfo;
}

namespace frep::gui {

// Builds a NodeTypeInfo on the fly from a registered plugin primitive.
// Used by the node graph context menu so plugin primitives (Torus,
// Octahedron, Capsule, ...) appear alongside the built-in catalog.
//
// Implemented out-of-line in node_types.cpp.
NodeTypeInfo make_plugin_node_info_by_type_name(
    const plugin::PluginRegistry& reg, const QString& node_type_name);

} // namespace frep::gui
