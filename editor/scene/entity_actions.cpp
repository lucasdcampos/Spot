#include "scene/entity_actions.h"

#include "imgui/icons.h"
#include "project/editor_project.h"
#include "scene/scene_commands.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <format>
#include <memory>
#include <string>
#include <utility>

namespace Spot::Editor {

namespace {

constexpr std::array Templates{
    EntityTemplateInfo{EntityTemplate::Empty, "Empty", ICON_FA_CIRCLE_NODES, nullptr},
    EntityTemplateInfo{EntityTemplate::Cube, "Cube", ICON_FA_CUBE, "3D Object"},
    EntityTemplateInfo{EntityTemplate::Sphere, "Sphere", ICON_FA_CIRCLE, "3D Object"},
    EntityTemplateInfo{EntityTemplate::Plane, "Plane", ICON_FA_SQUARE, "3D Object"},
    EntityTemplateInfo{EntityTemplate::DirectionalLight, "Directional Light", ICON_FA_SUN, "Light"},
    EntityTemplateInfo{EntityTemplate::PointLight, "Point Light", ICON_FA_LIGHTBULB, "Light"},
    EntityTemplateInfo{EntityTemplate::SpotLight, "Spot Light", ICON_FA_LIGHTBULB, "Light"},
    EntityTemplateInfo{EntityTemplate::AmbientLight, "Ambient Light", ICON_FA_CIRCLE_HALF_STROKE, "Light"},
    EntityTemplateInfo{EntityTemplate::Sky, "Sky", ICON_FA_CLOUD_SUN, nullptr},
    EntityTemplateInfo{EntityTemplate::Sprite, "Sprite", ICON_FA_IMAGE, "2D Object"},
    EntityTemplateInfo{EntityTemplate::Camera, "Camera", ICON_FA_VIDEO, nullptr},
};

// How many of a texture's pixels a sprite made from it shows per world unit.
constexpr float PixelsPerUnit = 100.0f;

// Builds the template's entity in a scratch scene, saved as text, which the create command adds to the document.
std::string MakeEntityText(EntityTemplate kind, Vector3 position) {
    Scene scratch;
    const auto found = std::ranges::find(Templates, kind, &EntityTemplateInfo::kind);
    Entity& entity = scratch.CreateEntity(found->name);
    entity.GetTransform().SetPosition(position);
    switch (kind) {
    case EntityTemplate::Empty:
        break;
    case EntityTemplate::Cube:
        entity.AddComponent<MeshRenderer>(AssetRef<Mesh>(BuiltInAssets::CubeMesh, nullptr));
        break;
    case EntityTemplate::Sphere:
        entity.AddComponent<MeshRenderer>(AssetRef<Mesh>(BuiltInAssets::SphereMesh, nullptr));
        break;
    case EntityTemplate::Plane:
        entity.AddComponent<MeshRenderer>(AssetRef<Mesh>(BuiltInAssets::PlaneMesh, nullptr));
        break;
    case EntityTemplate::DirectionalLight:
        entity.AddComponent<DirectionalLight>();
        entity.GetTransform().SetRotation(Quaternion::LookRotation({-0.4f, -1.0f, -0.6f}));
        break;
    case EntityTemplate::PointLight:
        entity.AddComponent<PointLight>();
        break;
    case EntityTemplate::SpotLight:
        entity.AddComponent<SpotLight>();
        entity.GetTransform().SetRotation(Quaternion::LookRotation({0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}));
        break;
    case EntityTemplate::AmbientLight:
        entity.AddComponent<AmbientLight>();
        break;
    case EntityTemplate::Sky:
        entity.AddComponent<Sky>();
        break;
    case EntityTemplate::Sprite:
        entity.AddComponent<SpriteRenderer>();
        break;
    case EntityTemplate::Camera:
        entity.AddComponent<Camera>();
        break;
    }
    return scratch.SaveToString();
}

// The sibling after the entity, which a copy goes before so it lands right after the entity.
EntityId GetNextSibling(const Entity& entity) {
    const std::vector<Entity*> siblings =
        entity.GetParent() ? entity.GetParent()->GetChildren() : entity.GetScene().GetRootEntities();
    const auto found = std::ranges::find(siblings, &entity);
    return found != siblings.end() && found + 1 != siblings.end() ? (*(found + 1))->GetId() : EntityId::Invalid;
}

std::vector<std::size_t> GetHierarchyPath(const Entity& entity) {
    std::vector<std::size_t> path;
    for (const Entity* node = &entity; node; node = node->GetParent()) {
        path.push_back(node->GetSiblingIndex());
    }
    std::ranges::reverse(path);
    return path;
}

std::vector<EntityId> GetIds(const std::vector<Entity*>& entities) {
    std::vector<EntityId> ids;
    for (const Entity* entity : entities) {
        ids.push_back(entity->GetId());
    }
    return ids;
}

} // namespace

std::span<const EntityTemplateInfo> GetEntityTemplates() {
    return Templates;
}

const char* GetEntityIcon(const Entity& entity) {
    if (entity.GetComponent<Camera>()) {
        return ICON_FA_VIDEO;
    }
    if (entity.GetComponent<SpriteRenderer>()) {
        return ICON_FA_IMAGE;
    }
    if (entity.GetComponent<DirectionalLight>()) {
        return ICON_FA_SUN;
    }
    if (entity.GetComponent<PointLight>() || entity.GetComponent<SpotLight>()) {
        return ICON_FA_LIGHTBULB;
    }
    if (entity.GetComponent<AmbientLight>()) {
        return ICON_FA_CIRCLE_HALF_STROKE;
    }
    if (entity.GetComponent<Sky>()) {
        return ICON_FA_CLOUD_SUN;
    }
    if (entity.GetComponent<MeshRenderer>()) {
        return ICON_FA_CUBE;
    }
    if (entity.GetComponent<Rigidbody>() || entity.GetComponent<Collider>()) {
        return ICON_FA_BOX_OPEN;
    }
    return ICON_FA_CIRCLE_NODES;
}

const char* GetComponentIcon(std::string_view typeName) {
    static constexpr std::array<std::pair<std::string_view, const char*>, 12> Icons{{
        {"Camera", ICON_FA_VIDEO},
        {"SpriteRenderer", ICON_FA_IMAGE},
        {"MeshRenderer", ICON_FA_CUBE},
        {"Rigidbody", ICON_FA_WEIGHT_HANGING},
        {"BoxCollider", ICON_FA_VECTOR_SQUARE},
        {"SphereCollider", ICON_FA_CIRCLE_DOT},
        {"CapsuleCollider", ICON_FA_CAPSULES},
        {"AmbientLight", ICON_FA_CIRCLE_HALF_STROKE},
        {"DirectionalLight", ICON_FA_SUN},
        {"PointLight", ICON_FA_LIGHTBULB},
        {"SpotLight", ICON_FA_LIGHTBULB},
        {"Sky", ICON_FA_CLOUD_SUN},
    }};
    const auto found = std::ranges::find(Icons, typeName, &std::pair<std::string_view, const char*>::first);
    return found != Icons.end() ? found->second : ICON_FA_PUZZLE_PIECE;
}

bool CreateEntityFromAsset(SceneDocument& document, EditorProject& project, AssetId asset, EntityId parent,
                           Vector3 position) {
    const AssetInfo* info = project.GetAssetDatabase().Find(asset);
    if (!info ||
        (info->type != AssetType::Mesh && info->type != AssetType::Model && info->type != AssetType::Texture)) {
        return false;
    }
    std::string name = info->parent.IsValid() ? info->name : PathToUtf8(Utf8ToPath(info->path).stem());
    Scene scratch;
    Entity& root = scratch.CreateEntity(name);
    root.GetTransform().SetPosition(position);
    if (info->type == AssetType::Mesh) {
        root.AddComponent<MeshRenderer>(AssetRef<Mesh>(asset, nullptr));
    } else if (info->type == AssetType::Texture) {
        // As large as the texture's pixels make it, when it loads.
        Vector2 size{1.0f, 1.0f};
        if (const AssetRef<Texture> texture = project.GetAssets().Load<Texture>(asset)) {
            size = {static_cast<float>(texture->GetWidth()) / PixelsPerUnit,
                    static_cast<float>(texture->GetHeight()) / PixelsPerUnit};
        }
        root.AddComponent<SpriteRenderer>(AssetRef<Texture>(asset, nullptr), size);
    } else {
        const AssetRef<Model> model = project.GetAssets().Load<Model>(asset);
        if (!model || model->GetParts().empty()) {
            return false;
        }
        // References by id alone: the scene loads the assets when it takes the entities.
        const auto addPart = [](Entity& entity, const ModelPart& part) {
            entity.AddComponent<MeshRenderer>(AssetRef<Mesh>(part.mesh.GetId(), nullptr),
                                              AssetRef<Material>(part.material.GetId(), nullptr));
        };
        const std::span<const ModelPart> parts = model->GetParts();
        if (parts.size() == 1) {
            addPart(root, parts.front());
        } else {
            for (std::size_t i = 0; i < parts.size(); ++i) {
                addPart(root.CreateChild(parts[i].name.empty() ? std::format("Part {}", i + 1) : parts[i].name),
                        parts[i]);
            }
        }
    }
    return document.Execute(
        std::make_unique<CreateEntitiesCommand>(document, "Add " + name, root.SaveToString(), parent));
}

bool ApplyMaterial(SceneDocument& document, EntityId entity, AssetId material) {
    Entity* target = document.GetScene().GetEntity(entity);
    if (!target) {
        return false;
    }
    auto group = std::make_unique<SceneCommandGroup>(document, "Apply Material");
    for (MeshRenderer* renderer : target->GetComponents<MeshRenderer>()) {
        // The new state is saved with the material's id; the command loads it.
        const std::string before = SaveComponentToString(*renderer);
        const AssetRef<Material> previous = renderer->GetMaterial();
        renderer->SetMaterial(AssetRef<Material>(material, nullptr));
        std::string after = SaveComponentToString(*renderer);
        renderer->SetMaterial(previous);
        group->Add(std::make_unique<EditComponentCommand>(document, entity, target->GetComponentIndex(*renderer),
                                                          before, std::move(after)));
    }
    return !group->IsEmpty() && document.Execute(std::move(group));
}

bool CreateEntity(SceneDocument& document, EntityTemplate kind, EntityId parent, Vector3 position) {
    const auto found = std::ranges::find(Templates, kind, &EntityTemplateInfo::kind);
    return document.Execute(std::make_unique<CreateEntitiesCommand>(document, std::string("Create ") + found->name,
                                                                    MakeEntityText(kind, position), parent));
}

std::vector<Entity*> GetSelectedEntities(SceneDocument& document) {
    Scene& scene = document.GetScene();
    std::vector<Entity*> entities;
    for (const EntityId id : document.GetSelection().GetEntities()) {
        if (Entity* entity = scene.GetEntity(id)) {
            entities.push_back(entity);
        }
    }
    std::erase_if(entities, [&](const Entity* entity) {
        return std::ranges::any_of(
            entities, [&](const Entity* other) { return other != entity && entity->IsDescendantOf(*other); });
    });
    std::ranges::sort(entities, {}, [](const Entity* entity) { return GetHierarchyPath(*entity); });
    return entities;
}

bool CopySelection(SceneDocument& document) {
    const std::vector<Entity*> selected = GetSelectedEntities(document);
    if (selected.empty()) {
        return false;
    }
    return Clipboard::SetText(document.GetScene().SaveEntitiesToString(selected));
}

bool CutSelection(SceneDocument& document) {
    if (!CopySelection(document)) {
        return false;
    }
    return document.Execute(std::make_unique<DeleteEntitiesCommand>(document, GetIds(GetSelectedEntities(document))));
}

bool PasteClipboard(SceneDocument& document, bool asChildren) {
    const std::string text = Clipboard::GetText();
    if (text.empty()) {
        return false;
    }
    Scene& scene = document.GetScene();
    const Entity* selected = scene.GetEntity(document.GetSelection().GetPrimary());
    EntityId parent = EntityId::Invalid;
    EntityId before = EntityId::Invalid;
    if (selected && asChildren) {
        parent = selected->GetId();
    } else if (selected) {
        parent = selected->GetParent() ? selected->GetParent()->GetId() : EntityId::Invalid;
        before = GetNextSibling(*selected);
    }
    return document.Execute(std::make_unique<CreateEntitiesCommand>(document, "Paste", text, parent, before));
}

bool DuplicateSelection(SceneDocument& document) {
    const std::vector<Entity*> selected = GetSelectedEntities(document);
    if (selected.empty()) {
        return false;
    }
    std::vector<CreateEntitiesCommand::Placement> placements;
    for (Entity* entity : selected) {
        Entity* const saved[] = {entity};
        placements.push_back({.text = document.GetScene().SaveEntitiesToString(saved),
                              .parent = entity->GetParent() ? entity->GetParent()->GetId() : EntityId::Invalid,
                              .before = GetNextSibling(*entity)});
    }
    return document.Execute(std::make_unique<CreateEntitiesCommand>(document, "Duplicate", std::move(placements)));
}

bool DeleteSelection(SceneDocument& document) {
    const std::vector<Entity*> selected = GetSelectedEntities(document);
    if (selected.empty()) {
        return false;
    }
    return document.Execute(std::make_unique<DeleteEntitiesCommand>(document, GetIds(selected)));
}

void SelectAll(SceneDocument& document) {
    std::vector<EntityId> all;
    document.GetScene().ForEachEntity([&](Entity& entity) { all.push_back(entity.GetId()); });
    document.GetSelection().Set(all);
}

void HandleEntityShortcuts(SceneDocument& document) {
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO().WantTextInput) {
        return;
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C)) {
        CopySelection(document);
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_X)) {
        CutSelection(document);
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_V)) {
        PasteClipboard(document);
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_D)) {
        DuplicateSelection(document);
    }
    if (ImGui::Shortcut(ImGuiKey_Delete)) {
        DeleteSelection(document);
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_A)) {
        SelectAll(document);
    }
    if (ImGui::Shortcut(ImGuiKey_Escape)) {
        document.GetSelection().Clear();
    }
}

} // namespace Spot::Editor
