#include "core/game_module.h"

#include "core/log.h"
#include "core/paths.h"
#include "scene/component_registry.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace Spot {

namespace {

Detail::GameCode::Function s_gameCode = nullptr;

} // namespace

Detail::GameCode::GameCode(Function load) : m_load(load) {
    s_gameCode = load;
}

Detail::GameCode::~GameCode() {
    // A module that unloads takes its code with it.
    if (s_gameCode == m_load) {
        s_gameCode = nullptr;
    }
}

void Detail::GameCode::Load() {
    if (s_gameCode) {
        s_gameCode();
    }
}

std::unique_ptr<GameModule> GameModule::Load(const std::filesystem::path& path) {
    auto library = SharedLibrary::Load(path);
    if (!library) {
        return nullptr;
    }
    using LoadFunction = void (*)();
    const auto load = reinterpret_cast<LoadFunction>(library->GetFunction("SpotLoadGameModule"));
    if (!load) {
        Log::GetEngineLogger().Error("'{}' is not a game module: it has no SPOT_GAME_MODULE", PathToUtf8(path));
        return nullptr;
    }
    // What the module registers is what it adds to the types known before.
    const std::vector<std::string> before = GetComponentTypeNames();
    load();
    const std::vector<std::string> after = GetComponentTypeNames();
    std::vector<std::string> added;
    std::ranges::set_difference(after, before, std::back_inserter(added));
    Log::GetEngineLogger().Info("Loaded the game module '{}' with {} component types", PathToUtf8(path.filename()),
                                added.size());
    return std::unique_ptr<GameModule>(new GameModule(std::move(library), std::move(added)));
}

GameModule::~GameModule() {
    for (const std::string& type : m_componentTypes) {
        UnregisterComponent(type);
    }
}

} // namespace Spot
