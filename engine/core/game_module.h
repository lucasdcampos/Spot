#pragma once

#include "core/shared_library.h"

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#define SPOT_MODULE_EXPORT extern "C" __declspec(dllexport)
#else
#define SPOT_MODULE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// Makes a shared library a game module, which the editor and the player load: the function registers the game's
// components with RegisterComponent() when the module loads, and the module unregisters them when it unloads.
//
//     void RegisterGame() {
//         Spot::RegisterComponent<Player>("Player");
//     }
//     SPOT_GAME_MODULE(RegisterGame)
//
// Modules link the shared engine library, spot_shared, that the program loading them links, so they share its
// state; they must be built against the same build of it.
#define SPOT_GAME_MODULE(function)                                                                                     \
    SPOT_MODULE_EXPORT void SpotLoadGameModule() {                                                                     \
        function();                                                                                                    \
    }

namespace Spot {

// A game module loaded into the program, with the component types it registered.
class GameModule {
public:
    // The extension CMake gives modules: .dll on Windows, and .so everywhere else, macOS included.
#if defined(_WIN32)
    static constexpr std::string_view FileExtension = ".dll";
#else
    static constexpr std::string_view FileExtension = ".so";
#endif

    // Loads the library and runs its SPOT_GAME_MODULE function. Null, after logging why, when it cannot be loaded or
    // is not a game module.
    static std::unique_ptr<GameModule> Load(const std::filesystem::path& path);
    // Unregisters the module's types and unloads it. No component of those types may be left, as their code goes
    // with the module.
    ~GameModule();

    GameModule(const GameModule&) = delete;
    GameModule& operator=(const GameModule&) = delete;

    // The names of the component types it registered, in order.
    std::span<const std::string> GetComponentTypes() const { return m_componentTypes; }
    const std::filesystem::path& GetPath() const { return m_library->GetPath(); }

private:
    GameModule(std::unique_ptr<SharedLibrary> library, std::vector<std::string> componentTypes)
        : m_library(std::move(library)), m_componentTypes(std::move(componentTypes)) {}

    std::unique_ptr<SharedLibrary> m_library;
    std::vector<std::string> m_componentTypes;
};

} // namespace Spot
