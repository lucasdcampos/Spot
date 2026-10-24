#include "spot/runtime/game_library.h"

#include "spot/core/debug/log.h"
#include "spot/core/io/paths.h"
#include "spot/runtime/game_module.h"
#include "spot/scene/component_registry.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace Spot {

std::unique_ptr<GameLibrary> GameLibrary::Load(const std::filesystem::path& path) {
    auto library = SharedLibrary::Load(path);
    if (!library) {
        return nullptr;
    }
    using CreateFunction = Module* (*)();
    const auto create = reinterpret_cast<CreateFunction>(library->GetFunction("SpotCreateGameModule"));
    if (!create) {
        Log::GetEngineLogger().Error("'{}' is not a game library: it has no SPOT_GAME_MODULE", PathToUtf8(path));
        return nullptr;
    }
    // What the module registers is what it adds to the types known before.
    const std::vector<std::string> before = GetComponentTypeNames();
    std::unique_ptr<Module> module(create());
    Detail::StartModule(*module);
    const std::vector<std::string> after = GetComponentTypeNames();
    std::vector<std::string> added;
    std::ranges::set_difference(after, before, std::back_inserter(added));
    Log::GetEngineLogger().Info("Loaded the game library '{}' with {} component types", PathToUtf8(path.filename()),
                                added.size());
    return std::unique_ptr<GameLibrary>(new GameLibrary(std::move(library), std::move(module), std::move(added)));
}

GameLibrary::~GameLibrary() {
    Detail::StopModule(*m_module);
    m_module.reset();
    for (const std::string& type : m_componentTypes) {
        UnregisterComponent(type);
    }
}

} // namespace Spot
