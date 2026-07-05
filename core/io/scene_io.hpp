#pragma once
// core/io/scene_io.hpp
//
// Serialization/deserialization of SceneGraph to/from JSON.
//
// Format:
// {
//   "version": "4.0",
//   "camera": { "position": [...], "target": [...], "up": [...], "fov_deg": 55 },
//   "objects": [
//     {
//       "id": "sphere_1",
//       "visible": true,
//       "material": { "albedo": [r,g,b], "roughness": .., "metallic": .., "emission": .. },
//       "geometry": <node>
//     }, ...
//   ]
// }
//
// <node>:
// {
//   "type": "Sphere" | "Box" | ... ,
//   "id": "...",
//   "params": { "r": 1.0, ... },
//   "children": [ <node>, ... ],
//   "expr": "..."   // only for CustomExpr
// }

#include "core/frep/scene.hpp"

#include <string>

namespace frep::plugin { class PluginRegistry; }

namespace frep::io {

// Serializes a scene to a JSON string.
//
// If `base_dir` is non-empty, absolute texture paths that point inside
// that directory are rewritten as relative paths. This makes scene
// files portable — copying the .frep file together with its texture
// directory to another machine keeps the references working.
//
// If `embed_textures` is true, every textured material's RGBA8 pixels are
// embedded directly in the JSON (base64) under `texture_data`/`texture_width`/
// `texture_height`. This is for transport (e.g. distributed rendering) where
// the remote host has no access to the local texture files and procedural
// textures have no file at all. Default false keeps on-disk scene files small
// and readable (path-only); the pixels are reloaded from `texture_path`.
std::string serialize_scene(const SceneGraph& scene,
                            const std::string& base_dir = {},
                            bool embed_textures = false);

// Parses a JSON string → SceneGraph. Throws std::runtime_error on error.
//
// If `registry` is non-null, unknown node types are looked up there — so
// plugin-based primitives (Torus, Octahedron, Capsule, ...) can be restored
// as long as the corresponding plugin is registered. Without a registry,
// unknown types throw with a clear error message.
//
// If `base_dir` is non-empty, relative texture paths in the JSON are
// resolved against it before being passed to `load_image`. This pairs
// with the rewriting done by `serialize_scene` so a saved scene that
// references a sibling `textures/foo.png` file continues to work when
// the whole directory is moved.
SceneGraph deserialize_scene(const std::string& json_text,
                             const plugin::PluginRegistry* registry = nullptr,
                             const std::string& base_dir = {});

// Deep-clones an FRepNode subtree, assigning fresh ids derived from
// `new_id` (the root gets exactly `new_id`; descendants get
// `new_id` + "/" + their original relative id, keeping them unique
// and traceable). Implemented as a JSON round-trip through the same
// machinery as save/load, so every node type — including plugin
// primitives — is handled without each needing a bespoke clone()
// method. `registry` is required if the subtree contains plugin nodes.
//
// Used by the GUI's duplicate / copy-paste commands.
FRepNode::Ptr clone_node(const FRepNode& node,
                         const std::string& new_id,
                         const plugin::PluginRegistry* registry = nullptr);

// Convenience file wrappers. These derive `base_dir` from `path`
// automatically — `save_scene("/foo/bar/scene.frep", ...)` uses
// `/foo/bar` as the base directory, and `load_scene` does the same.
bool        save_scene(const SceneGraph& scene, const std::string& path);
SceneGraph  load_scene(const std::string& path,
                       const plugin::PluginRegistry* registry = nullptr);

} // namespace frep::io
