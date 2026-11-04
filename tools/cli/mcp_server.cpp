#include "mcp_server.h"

#include <spot.h>
#include <spot_tools.h>

#include "project/project.h"
#include "spot/core/debug/log.h"
#include "spot/core/debug/log_sinks.h"
#include "spot/core/ids/asset_id.h"
#include "spot/core/math/angles.h"
#include "spot/core/math/quaternion.h"
#include "spot/core/math/vector.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace Spot {

namespace {

using nlohmann::json;

class StderrSink final : public LogSink {
public:
    void Write(const LogRecord& record) override {
        const std::string line = FormatLogRecord(record) + '\n';
        std::fwrite(line.data(), 1, line.size(), stderr);
        std::fflush(stderr);
    }
};

std::array<float, 4> EulerDegreesToQuaternion(float pitch, float yaw, float roll) {
    const Vector3 radians{ToRadians(pitch), ToRadians(yaw), ToRadians(roll)};
    const Quaternion q = Quaternion::FromEuler(radians);
    return {q.x, q.y, q.z, q.w};
}

std::array<float, 3> QuaternionToEulerDegrees(float x, float y, float z, float w) {
    const Quaternion q{x, y, z, w};
    const Vector3 euler = ToEuler(q);
    return {ToDegrees(euler.x), ToDegrees(euler.y), ToDegrees(euler.z)};
}

std::optional<std::filesystem::path> LocateProjectFile(const std::optional<std::filesystem::path>& explicitPath) {
    if (explicitPath) {
        if (std::filesystem::is_regular_file(*explicitPath)) {
            return *explicitPath;
        }
        if (std::filesystem::is_directory(*explicitPath)) {
            for (const auto& entry : std::filesystem::directory_iterator(*explicitPath)) {
                if (entry.path().extension() == Project::FileExtension) {
                    return entry.path();
                }
            }
        }
    }

    std::filesystem::path dir = std::filesystem::current_path();
    for (int i = 0; i < 4; ++i) {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (!ec && entry.path().extension() == Project::FileExtension) {
                return entry.path();
            }
        }
        if (!dir.has_parent_path() || dir == dir.parent_path()) {
            break;
        }
        dir = dir.parent_path();
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> FindStartupSceneFile(const Project& project) {
    const auto assetsDir = project.GetAssetsDirectory();
    std::error_code ec;
    if (!std::filesystem::exists(assetsDir, ec)) {
        return std::nullopt;
    }

    const std::string targetId = project.GetSettings().startupScene.ToString();
    for (const auto& entry : std::filesystem::recursive_directory_iterator(assetsDir, ec)) {
        if (entry.path().extension() == ".meta") {
            const auto stem = entry.path().stem();
            if (stem.extension() == ".scene") {
                std::ifstream stream(entry.path());
                if (stream) {
                    json meta = json::parse(stream, nullptr, false);
                    if (meta.is_object() && meta.value("id", "") == targetId) {
                        return entry.path().parent_path() / stem;
                    }
                }
            }
        }
    }

    const auto defaultMain = assetsDir / "scenes" / "Main.scene";
    if (std::filesystem::exists(defaultMain, ec)) {
        return defaultMain;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(assetsDir, ec)) {
        if (entry.path().extension() == ".scene") {
            return entry.path();
        }
    }
    return std::nullopt;
}

std::filesystem::path ResolveScenePath(const Project& project, const std::string& inputPath) {
    if (inputPath.empty() || inputPath == "default") {
        if (const auto found = FindStartupSceneFile(project)) {
            return *found;
        }
        return project.GetAssetsDirectory() / "scenes" / "Main.scene";
    }

    std::filesystem::path path(inputPath);
    if (path.is_absolute()) {
        return path;
    }
    return project.GetDirectory() / path;
}

json MakeToolResult(const std::string& text, bool isError = false) {
    return {{"content", json::array({json{{"type", "text"}, {"text", text}}})}, {"isError", isError}};
}

json GetToolDefinitions() {
    return json::array({
        json{
            {"name", "spot_get_project_info"},
            {"description",
             "Returns information about the Spot project (name, version, startup scene, assets directory)."},
            {"inputSchema", json{{"type", "object"}, {"properties", json::object()}}},
        },
        json{
            {"name", "spot_list_assets"},
            {"description", "Lists source assets in the project (scenes, scripts, prefabs, models, audio, textures)."},
            {"inputSchema",
             json{{"type", "object"},
                  {"properties",
                   json{{"type", json{{"type", "string"},
                                      {"description", "Filter by type ('all', 'scene', 'script', 'prefab', 'model'). "
                                                      "Defaults to 'all'."}}}}},
                  {"required", json::array()}}},
        },
        json{
            {"name", "spot_inspect_scene"},
            {"description", "Inspects a scene file and returns a concise hierarchy of entities, their positions, Euler "
                            "rotations (in degrees), and components."},
            {"inputSchema",
             json{
                 {"type", "object"},
                 {"properties",
                  json{{"scene_path",
                        json{{"type", "string"},
                             {"description",
                              "Path to .scene file relative to project root (e.g. 'assets/scenes/Main.scene'). If "
                              "omitted, inspects the startup scene."}}}}},
                 {"required", json::array()},
             }},
        },
        json{
            {"name", "spot_add_entity"},
            {"description", "Adds a new entity to a scene. Supports Euler rotation (in degrees) which is converted to "
                            "quaternions automatically, scale, parent hierarchy, and components."},
            {"inputSchema",
             json{
                 {"type", "object"},
                 {"properties",
                  json{
                      {"scene_path",
                       json{{"type", "string"},
                            {"description", "Path to .scene file. Defaults to startup scene if omitted."}}},
                      {"name", json{{"type", "string"}, {"description", "Name of the entity."}}},
                      {"position", json{{"type", "array"},
                                        {"items", json{{"type", "number"}}},
                                        {"description", "[x, y, z] position. Defaults to [0, 0, 0]."}}},
                      {"rotation_euler",
                       json{{"type", "array"},
                            {"items", json{{"type", "number"}}},
                            {"description",
                             "[pitch, yaw, roll] rotation in degrees. Converted to quaternion automatically."}}},
                      {"scale", json{{"type", "array"},
                                     {"items", json{{"type", "number"}}},
                                     {"description", "[x, y, z] scale. Defaults to [1, 1, 1]."}}},
                      {"parent",
                       json{{"type", "string"}, {"description", "Optional name or integer ID of parent entity."}}},
                      {"components",
                       json{{"type", "array"},
                            {"items", json{{"type", "object"}}},
                            {"description", "List of components to add, e.g. [{\"type\": \"MeshRenderer\"}, "
                                            "{\"type\": \"BoxCollider\", \"data\": {\"size\": [2, 0.5, 2]}}]"}}},
                  }},
                 {"required", json::array({"name"})},
             }},
        },
        json{
            {"name", "spot_modify_entity"},
            {"description", "Modifies transform or properties of an existing entity in a scene by name or integer ID."},
            {"inputSchema",
             json{
                 {"type", "object"},
                 {"properties",
                  json{
                      {"scene_path", json{{"type", "string"}, {"description", "Path to .scene file."}}},
                      {"name_or_id",
                       json{{"type", "string"}, {"description", "Name or integer ID of the entity to modify."}}},
                      {"name", json{{"type", "string"}, {"description", "New name for the entity."}}},
                      {"position", json{{"type", "array"},
                                        {"items", json{{"type", "number"}}},
                                        {"description", "New [x, y, z] position."}}},
                      {"rotation_euler", json{{"type", "array"},
                                              {"items", json{{"type", "number"}}},
                                              {"description", "New [pitch, yaw, roll] rotation in degrees."}}},
                      {"scale", json{{"type", "array"},
                                     {"items", json{{"type", "number"}}},
                                     {"description", "New [x, y, z] scale."}}},
                      {"active", json{{"type", "boolean"}, {"description", "Active state."}}},
                  }},
                 {"required", json::array({"name_or_id"})},
             }},
        },
        json{
            {"name", "spot_remove_entity"},
            {"description", "Removes an entity and all its descendants from a scene by name or integer ID."},
            {"inputSchema",
             json{
                 {"type", "object"},
                 {"properties",
                  json{
                      {"scene_path", json{{"type", "string"}, {"description", "Path to .scene file."}}},
                      {"name_or_id",
                       json{{"type", "string"}, {"description", "Name or integer ID of the entity to remove."}}},
                  }},
                 {"required", json::array({"name_or_id"})},
             }},
        },
        json{
            {"name", "spot_create_scene"},
            {"description", "Creates a new .scene file and its corresponding .scene.meta asset metadata."},
            {"inputSchema",
             json{
                 {"type", "object"},
                 {"properties",
                  json{
                      {"scene_path", json{{"type", "string"},
                                          {"description", "Path to new scene relative to project root (e.g. "
                                                          "'assets/scenes/Level2.scene')."}}},
                      {"template",
                       json{{"type", "string"},
                            {"description", "Template type: 'empty' or '3d' (includes Camera and Light). Defaults to "
                                            "'3d'."}}},
                  }},
                 {"required", json::array({"scene_path"})},
             }},
        },
    });
}

// Handlers
json HandleGetProjectInfo(const Project& project) {
    std::ostringstream ss;
    ss << "Project: " << project.GetSettings().name << "\n";
    ss << "Version: " << project.GetSettings().version << "\n";
    ss << "Directory: " << project.GetDirectory().string() << "\n";
    ss << "Assets: " << project.GetAssetsDirectory().string() << "\n";
    ss << "Startup Scene ID: " << project.GetSettings().startupScene.ToString() << "\n";
    if (const auto scenePath = FindStartupSceneFile(project)) {
        ss << "Startup Scene Path: " << std::filesystem::relative(*scenePath, project.GetDirectory()).generic_string()
           << "\n";
    }
    return MakeToolResult(ss.str());
}

json HandleListAssets(const Project& project, const json& args) {
    const std::string filter = args.value("type", "all");
    const auto assetsDir = project.GetAssetsDirectory();
    std::error_code ec;
    if (!std::filesystem::exists(assetsDir, ec)) {
        return MakeToolResult("Assets directory does not exist.", true);
    }

    std::ostringstream ss;
    ss << "Assets in project (" << filter << "):\n";
    int count = 0;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(assetsDir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() != ".meta") {
            const auto metaPath = std::filesystem::path(entry.path().string() + ".meta");
            std::string assetType = "Unknown";
            std::string assetId = "";

            if (std::filesystem::exists(metaPath, ec)) {
                std::ifstream metaStream(metaPath);
                if (metaStream) {
                    json metaJson = json::parse(metaStream, nullptr, false);
                    if (metaJson.is_object()) {
                        assetType = metaJson.value("type", "Unknown");
                        assetId = metaJson.value("id", "");
                    }
                }
            }

            const std::string ext = entry.path().extension().string();
            if (assetType == "Unknown") {
                if (ext == ".scene")
                    assetType = "Scene";
                else if (ext == ".luau")
                    assetType = "Script";
                else if (ext == ".prefab")
                    assetType = "Prefab";
                else if (ext == ".glb" || ext == ".gltf")
                    assetType = "Model";
                else if (ext == ".png" || ext == ".jpg")
                    assetType = "Texture";
                else if (ext == ".wav" || ext == ".ogg")
                    assetType = "Audio";
            }

            bool include = false;
            if (filter == "all")
                include = true;
            else if (filter == "scene" && assetType == "Scene")
                include = true;
            else if (filter == "script" && assetType == "Script")
                include = true;
            else if (filter == "prefab" && assetType == "Prefab")
                include = true;
            else if (filter == "model" && assetType == "Model")
                include = true;

            if (include) {
                const auto rel = std::filesystem::relative(entry.path(), project.GetDirectory()).generic_string();
                ss << "- [" << assetType << "] " << rel;
                if (!assetId.empty()) {
                    ss << " (id: " << assetId << ")";
                }
                ss << "\n";
                ++count;
            }
        }
    }

    if (count == 0) {
        ss << "(No matching assets found)\n";
    }
    return MakeToolResult(ss.str());
}

json HandleInspectScene(const Project& project, const json& args) {
    const std::string sceneArg = args.value("scene_path", "");
    const auto scenePath = ResolveScenePath(project, sceneArg);

    std::ifstream stream(scenePath);
    if (!stream) {
        return MakeToolResult(std::format("Scene file not found: {}", scenePath.generic_string()), true);
    }

    json doc = json::parse(stream, nullptr, false);
    if (doc.is_discarded() || !doc.is_object() || !doc.contains("entities")) {
        return MakeToolResult(std::format("Invalid scene JSON in: {}", scenePath.generic_string()), true);
    }

    std::ostringstream ss;
    const auto relPath = std::filesystem::relative(scenePath, project.GetDirectory()).generic_string();
    const auto& entities = doc["entities"];
    ss << "Scene: " << relPath << " (" << entities.size() << " entities)\n\n";

    for (const auto& entity : entities) {
        const uint64_t id = entity.value("id", 0ULL);
        const std::string name = entity.value("name", "Unnamed");
        const bool active = entity.value("active", true);

        const auto pos = entity.value("position", std::vector<float>{0.f, 0.f, 0.f});
        const auto rot = entity.value("rotation", std::vector<float>{0.f, 0.f, 0.f, 1.f});
        const auto scale = entity.value("scale", std::vector<float>{1.f, 1.f, 1.f});

        std::array<float, 3> euler = {0.f, 0.f, 0.f};
        if (rot.size() == 4) {
            euler = QuaternionToEulerDegrees(rot[0], rot[1], rot[2], rot[3]);
        }

        ss << "- [" << id << "] \"" << name << "\"" << (active ? "" : " (disabled)");
        if (entity.contains("parent")) {
            ss << " (parent: " << entity["parent"] << ")";
        }
        ss << "\n";

        if (pos.size() == 3) {
            ss << std::format("    Pos: [{:.2f}, {:.2f}, {:.2f}] | Rot(Euler): [{:.1f}deg, {:.1f}deg, {:.1f}deg] | "
                              "Scale: [{:.2f}, {:.2f}, {:.2f}]\n",
                              pos[0], pos[1], pos[2], euler[0], euler[1], euler[2], scale[0], scale[1], scale[2]);
        }

        if (entity.contains("components") && entity["components"].is_array()) {
            ss << "    Components: ";
            bool firstComp = true;
            for (const auto& comp : entity["components"]) {
                if (!firstComp)
                    ss << ", ";
                firstComp = false;
                ss << comp.value("type", "Unknown");
                if (comp.contains("data") && comp["data"].is_object()) {
                    const auto& data = comp["data"];
                    if (data.contains("script"))
                        ss << "(script: " << data["script"] << ")";
                    if (data.contains("mesh"))
                        ss << "(mesh: " << data["mesh"] << ")";
                    if (data.contains("size"))
                        ss << "(size: " << data["size"].dump() << ")";
                }
            }
            ss << "\n";
        }
    }

    return MakeToolResult(ss.str());
}

json HandleAddEntity(const Project& project, const json& args) {
    const std::string sceneArg = args.value("scene_path", "");
    const auto scenePath = ResolveScenePath(project, sceneArg);

    std::ifstream in(scenePath);
    if (!in) {
        return MakeToolResult(std::format("Scene file not found: {}", scenePath.generic_string()), true);
    }
    json doc = json::parse(in, nullptr, false);
    if (doc.is_discarded() || !doc.is_object() || !doc.contains("entities")) {
        return MakeToolResult(std::format("Invalid scene JSON in: {}", scenePath.generic_string()), true);
    }

    auto& entities = doc["entities"];
    uint64_t maxId = 0;
    for (const auto& e : entities) {
        maxId = std::max(maxId, e.value("id", 0ULL));
    }
    const uint64_t newId = maxId + 1;
    const std::string name = args.value("name", "Entity");

    std::vector<float> pos = {0.f, 0.f, 0.f};
    if (args.contains("position") && args["position"].is_array() && args["position"].size() == 3) {
        pos = args["position"].get<std::vector<float>>();
    }

    std::array<float, 4> rotQuat = {0.f, 0.f, 0.f, 1.f};
    if (args.contains("rotation_euler") && args["rotation_euler"].is_array() && args["rotation_euler"].size() == 3) {
        const auto euler = args["rotation_euler"].get<std::vector<float>>();
        rotQuat = EulerDegreesToQuaternion(euler[0], euler[1], euler[2]);
    }

    std::vector<float> scale = {1.f, 1.f, 1.f};
    if (args.contains("scale") && args["scale"].is_array() && args["scale"].size() == 3) {
        scale = args["scale"].get<std::vector<float>>();
    }

    json newEntity = {
        {"id", newId},         {"name", name},   {"active", true},        {"position", pos},
        {"rotation", rotQuat}, {"scale", scale}, {"tags", json::array()}, {"components", json::array()},
    };

    if (args.contains("parent")) {
        if (args["parent"].is_number_unsigned()) {
            newEntity["parent"] = args["parent"].get<uint64_t>();
        } else if (args["parent"].is_string()) {
            const std::string parentName = args["parent"].get<std::string>();
            for (const auto& e : entities) {
                if (e.value("name", "") == parentName) {
                    newEntity["parent"] = e["id"];
                    break;
                }
            }
        }
    }

    if (args.contains("components") && args["components"].is_array()) {
        for (auto comp : args["components"]) {
            if (comp.is_object()) {
                if (!comp.contains("id") || comp["id"].is_null()) {
                    comp["id"] = AssetId::Generate().ToString();
                }
                if (!comp.contains("enabled")) {
                    comp["enabled"] = true;
                }
                if (!comp.contains("data") || !comp["data"].is_object()) {
                    comp["data"] = json::object();
                }
                newEntity["components"].push_back(comp);
            }
        }
    }

    entities.push_back(std::move(newEntity));

    std::ofstream out(scenePath);
    if (!out) {
        return MakeToolResult(std::format("Failed to write to scene file: {}", scenePath.generic_string()), true);
    }
    out << doc.dump(2) << '\n';

    return MakeToolResult(
        std::format("Successfully added entity '{}' (id: {}) to scene '{}'.", name, newId, scenePath.generic_string()));
}

json HandleModifyEntity(const Project& project, const json& args) {
    const std::string sceneArg = args.value("scene_path", "");
    const auto scenePath = ResolveScenePath(project, sceneArg);

    std::ifstream in(scenePath);
    if (!in) {
        return MakeToolResult(std::format("Scene file not found: {}", scenePath.generic_string()), true);
    }
    json doc = json::parse(in, nullptr, false);
    if (doc.is_discarded() || !doc.is_object() || !doc.contains("entities")) {
        return MakeToolResult(std::format("Invalid scene JSON in: {}", scenePath.generic_string()), true);
    }

    const std::string target = args.value("name_or_id", "");
    auto& entities = doc["entities"];
    json* found = nullptr;

    for (auto& e : entities) {
        if (std::to_string(e.value("id", 0ULL)) == target || e.value("name", "") == target) {
            found = &e;
            break;
        }
    }

    if (!found) {
        return MakeToolResult(std::format("Entity '{}' not found in scene '{}'.", target, scenePath.generic_string()),
                              true);
    }

    if (args.contains("name")) {
        (*found)["name"] = args["name"].get<std::string>();
    }
    if (args.contains("active")) {
        (*found)["active"] = args["active"].get<bool>();
    }
    if (args.contains("position") && args["position"].is_array() && args["position"].size() == 3) {
        (*found)["position"] = args["position"];
    }
    if (args.contains("rotation_euler") && args["rotation_euler"].is_array() && args["rotation_euler"].size() == 3) {
        const auto euler = args["rotation_euler"].get<std::vector<float>>();
        (*found)["rotation"] = EulerDegreesToQuaternion(euler[0], euler[1], euler[2]);
    }
    if (args.contains("scale") && args["scale"].is_array() && args["scale"].size() == 3) {
        (*found)["scale"] = args["scale"];
    }

    std::ofstream out(scenePath);
    if (!out) {
        return MakeToolResult(std::format("Failed to save scene file: {}", scenePath.generic_string()), true);
    }
    out << doc.dump(2) << '\n';

    return MakeToolResult(
        std::format("Successfully modified entity '{}' in scene '{}'.", target, scenePath.generic_string()));
}

json HandleRemoveEntity(const Project& project, const json& args) {
    const std::string sceneArg = args.value("scene_path", "");
    const auto scenePath = ResolveScenePath(project, sceneArg);

    std::ifstream in(scenePath);
    if (!in) {
        return MakeToolResult(std::format("Scene file not found: {}", scenePath.generic_string()), true);
    }
    json doc = json::parse(in, nullptr, false);
    if (doc.is_discarded() || !doc.is_object() || !doc.contains("entities")) {
        return MakeToolResult(std::format("Invalid scene JSON in: {}", scenePath.generic_string()), true);
    }

    const std::string target = args.value("name_or_id", "");
    auto& entities = doc["entities"];
    std::vector<uint64_t> idsToRemove;

    for (const auto& e : entities) {
        if (std::to_string(e.value("id", 0ULL)) == target || e.value("name", "") == target) {
            idsToRemove.push_back(e["id"].get<uint64_t>());
            break;
        }
    }

    if (idsToRemove.empty()) {
        return MakeToolResult(std::format("Entity '{}' not found in scene '{}'.", target, scenePath.generic_string()),
                              true);
    }

    // Collect child entities recursively
    bool added = true;
    while (added) {
        added = false;
        for (const auto& e : entities) {
            if (e.contains("parent")) {
                const uint64_t parentId = e["parent"].get<uint64_t>();
                if (std::ranges::find(idsToRemove, parentId) != idsToRemove.end()) {
                    const uint64_t childId = e["id"].get<uint64_t>();
                    if (std::ranges::find(idsToRemove, childId) == idsToRemove.end()) {
                        idsToRemove.push_back(childId);
                        added = true;
                    }
                }
            }
        }
    }

    json newEntities = json::array();
    for (auto& e : entities) {
        const uint64_t id = e.value("id", 0ULL);
        if (std::ranges::find(idsToRemove, id) == idsToRemove.end()) {
            newEntities.push_back(std::move(e));
        }
    }
    doc["entities"] = std::move(newEntities);

    std::ofstream out(scenePath);
    if (!out) {
        return MakeToolResult(std::format("Failed to save scene file: {}", scenePath.generic_string()), true);
    }
    out << doc.dump(2) << '\n';

    return MakeToolResult(std::format("Removed entity '{}' and {} descendant(s) from scene '{}'.", target,
                                      idsToRemove.size() - 1, scenePath.generic_string()));
}

json HandleCreateScene(const Project& project, const json& args) {
    const std::string sceneArg = args.value("scene_path", "");
    if (sceneArg.empty()) {
        return MakeToolResult("scene_path cannot be empty.", true);
    }
    const auto scenePath = ResolveScenePath(project, sceneArg);
    const std::string templateType = args.value("template", "3d");

    std::error_code ec;
    std::filesystem::create_directories(scenePath.parent_path(), ec);

    json entities = json::array();
    if (templateType == "3d") {
        entities.push_back({
            {"id", 1},
            {"name", "Camera"},
            {"active", true},
            {"position", {0.f, 2.f, 6.f}},
            {"rotation", {-0.122183f, 0.f, 0.f, 0.992508f}},
            {"scale", {1.f, 1.f, 1.f}},
            {"tags", json::array()},
            {"components",
             json::array({json{{"id", AssetId::Generate().ToString()},
                               {"type", "Camera"},
                               {"enabled", true},
                               {"data", {{"fieldOfView", 1.047198f}, {"nearPlane", 0.1f}, {"farPlane", 1000.f}}}}})},
        });
        entities.push_back({
            {"id", 2},
            {"name", "Directional Light"},
            {"active", true},
            {"position", {0.f, 10.f, 0.f}},
            {"rotation", {-0.382683f, 0.f, 0.f, 0.92388f}},
            {"scale", {1.f, 1.f, 1.f}},
            {"tags", json::array()},
            {"components", json::array({json{{"id", AssetId::Generate().ToString()},
                                             {"type", "DirectionalLight"},
                                             {"enabled", true},
                                             {"data", {{"color", {1.f, 1.f, 1.f, 1.f}}, {"intensity", 1.f}}}}})},
        });
    }

    json doc = {
        {"format", "spot-scene"},
        {"version", 2},
        {"entities", std::move(entities)},
    };

    std::ofstream outScene(scenePath);
    if (!outScene) {
        return MakeToolResult(std::format("Failed to create scene file: {}", scenePath.generic_string()), true);
    }
    outScene << doc.dump(2) << '\n';

    const auto metaPath = std::filesystem::path(scenePath.string() + ".meta");
    json meta = {
        {"id", AssetId::Generate().ToString()},
        {"type", "Scene"},
    };
    std::ofstream outMeta(metaPath);
    if (outMeta) {
        outMeta << meta.dump(2) << '\n';
    }

    return MakeToolResult(
        std::format("Created scene '{}' and metadata '{}'.", scenePath.generic_string(), metaPath.generic_string()));
}

} // namespace

int RunMcpServer(const std::optional<std::filesystem::path>& explicitProjectPath) {
    // Direct all engine logging to stderr so stdout is purely JSON-RPC messages
    Log::SetSinks({std::make_shared<StderrSink>()});

    const auto projectFile = LocateProjectFile(explicitProjectPath);
    std::optional<Project> project;
    if (projectFile) {
        project = Project::Load(*projectFile);
    }

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }

        json request = json::parse(line, nullptr, false);
        if (request.is_discarded() || !request.is_object()) {
            continue;
        }

        const std::string method = request.value("method", "");
        const auto id = request.contains("id") ? request["id"] : json(nullptr);

        if (method == "initialize") {
            json response = {
                {"jsonrpc", "2.0"},
                {"id", id},
                {"result",
                 {
                     {"protocolVersion", "2024-11-05"},
                     {"capabilities", {{"tools", json::object()}}},
                     {"serverInfo", {{"name", "spot"}, {"version", "0.1.0"}}},
                 }},
            };
            std::cout << response.dump() << '\n';
            std::cout.flush();
            continue;
        }

        if (method == "notifications/initialized") {
            // Notifications require no response
            continue;
        }

        if (method == "ping") {
            json response = {
                {"jsonrpc", "2.0"},
                {"id", id},
                {"result", json::object()},
            };
            std::cout << response.dump() << '\n';
            std::cout.flush();
            continue;
        }

        if (method == "tools/list") {
            json response = {
                {"jsonrpc", "2.0"},
                {"id", id},
                {"result", {{"tools", GetToolDefinitions()}}},
            };
            std::cout << response.dump() << '\n';
            std::cout.flush();
            continue;
        }

        if (method == "tools/call") {
            const auto& params = request.value("params", json::object());
            const std::string toolName = params.value("name", "");
            const json args = params.value("arguments", json::object());

            if (!project) {
                // Try to find project again in case directory changed
                if (const auto found = LocateProjectFile(explicitProjectPath)) {
                    project = Project::Load(*found);
                }
            }

            json toolResult;
            if (!project) {
                toolResult = MakeToolResult("No Spot project (.spot) found in current or parent directories.", true);
            } else if (toolName == "spot_get_project_info") {
                toolResult = HandleGetProjectInfo(*project);
            } else if (toolName == "spot_list_assets") {
                toolResult = HandleListAssets(*project, args);
            } else if (toolName == "spot_inspect_scene") {
                toolResult = HandleInspectScene(*project, args);
            } else if (toolName == "spot_add_entity") {
                toolResult = HandleAddEntity(*project, args);
            } else if (toolName == "spot_modify_entity") {
                toolResult = HandleModifyEntity(*project, args);
            } else if (toolName == "spot_remove_entity") {
                toolResult = HandleRemoveEntity(*project, args);
            } else if (toolName == "spot_create_scene") {
                toolResult = HandleCreateScene(*project, args);
            } else {
                toolResult = MakeToolResult(std::format("Unknown tool '{}'.", toolName), true);
            }

            json response = {
                {"jsonrpc", "2.0"},
                {"id", id},
                {"result", std::move(toolResult)},
            };
            std::cout << response.dump() << '\n';
            std::cout.flush();
            continue;
        }

        // Unknown method
        if (!id.is_null()) {
            json errorResponse = {
                {"jsonrpc", "2.0"},
                {"id", id},
                {"error", {{"code", -32601}, {"message", std::format("Method '{}' not found", method)}}},
            };
            std::cout << errorResponse.dump() << '\n';
            std::cout.flush();
        }
    }

    return EXIT_SUCCESS;
}

} // namespace Spot
