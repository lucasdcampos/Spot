#pragma once

#include "scene/scene_document.h"

#include <spot.h>

#include <span>
#include <vector>

// What can be done to the entities of a scene document from menus, shortcuts and windows, each as one undoable step.
namespace Spot::Editor {

enum class EntityTemplate {
    Empty,
    Cube,
    Sphere,
    Plane,
    DirectionalLight,
    PointLight,
    SpotLight,
    AmbientLight,
    Sky,
};

struct EntityTemplateInfo {
    EntityTemplate kind;
    const char* name;
    const char* icon;
    // The submenu it is listed in, such as "3D Object"; null for the top of the menu.
    const char* category;
};

std::span<const EntityTemplateInfo> GetEntityTemplates();

// The icon that tells what an entity is, from its components.
const char* GetEntityIcon(const Entity& entity);

// Creates an entity from the template under the parent, or at the root when it is invalid, at the local position,
// and selects it. Returns whether it did.
bool CreateEntity(SceneDocument& document, EntityTemplate kind, EntityId parent = EntityId::Invalid,
                  Vector3 position = {});

// The selected entities that exist, leaving out those below other selected ones, in hierarchy order.
std::vector<Entity*> GetSelectedEntities(SceneDocument& document);

// Puts the selected entities, with their descendants, on the OS clipboard, so they can be pasted into any scene.
bool CopySelection(SceneDocument& document);
bool CutSelection(SceneDocument& document);
// Pastes entities from the clipboard after the selected entity, or as its children, or at the root when nothing is
// selected, and selects them.
bool PasteClipboard(SceneDocument& document, bool asChildren = false);
// Copies each selected entity right after itself, and selects the copies.
bool DuplicateSelection(SceneDocument& document);
bool DeleteSelection(SceneDocument& document);
void SelectAll(SceneDocument& document);

// Handles Ctrl+C, Ctrl+X, Ctrl+V, Ctrl+D, Delete, Ctrl+A and Escape for the window being drawn, while it has focus.
void HandleEntityShortcuts(SceneDocument& document);

} // namespace Spot::Editor
