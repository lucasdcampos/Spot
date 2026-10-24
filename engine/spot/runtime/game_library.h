#pragma once

#include "spot/platform/shared_library.h"
#include "spot/runtime/module.h"

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Spot {

// A game library loaded into the program, running its game module,, with the component types it registered.
class GameLibrary {
public:
    // The extension CMake gives game libraries: .dll on Windows, and .so everywhere else, macOS included.
#if defined(_WIN32)
    static constexpr std::string_view FileExtension = ".dll";
#else
    static constexpr std::string_view FileExtension = ".so";
#endif

    // Loads the library and starts the module its SPOT_GAME_MODULE names. Null, after logging why, when it cannot be
    // loaded or has no game module.
    static std::unique_ptr<GameLibrary> Load(const std::filesystem::path& path);
    // Stops its module, unregisters the types it registered and unloads it. No component of those types may be left, as
    // their code goes with the module.
    ~GameLibrary();

    GameLibrary(const GameLibrary&) = delete;
    GameLibrary& operator=(const GameLibrary&) = delete;

    // The names of the component types it registered, in order.
    std::span<const std::string> GetComponentTypes() const { return m_componentTypes; }
    const std::filesystem::path& GetPath() const { return m_library->GetPath(); }

private:
    GameLibrary(std::unique_ptr<SharedLibrary> library, std::unique_ptr<Module> module,
                std::vector<std::string> componentTypes)
        : m_library(std::move(library)), m_module(std::move(module)), m_componentTypes(std::move(componentTypes)) {}

    std::unique_ptr<SharedLibrary> m_library;
    // Made by the library's code, so destroyed before it unloads.
    std::unique_ptr<Module> m_module;
    std::vector<std::string> m_componentTypes;
};

} // namespace Spot
