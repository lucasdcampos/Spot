#pragma once

#include "spot/platform/shared_library.h"

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#define SPOT_MODULE_EXPORT extern "C" __declspec(dllexport)
#define SPOT_MODULE_LOCAL
#else
#define SPOT_MODULE_EXPORT extern "C" __attribute__((visibility("default")))
// Each module keeps its own, even where shared libraries share the symbols they all define.
#define SPOT_MODULE_LOCAL __attribute__((visibility("hidden")))
#endif

#define SPOT_MODULE_CONCAT_INNER(a, b) a##b
#define SPOT_MODULE_CONCAT(a, b) SPOT_MODULE_CONCAT_INNER(a, b)

// Marks the game's code, once, in one of its source files. Built into a shared library, it makes it a game module,
// which the editor loads: when the module loads, it registers the components marked with SPOT_COMPONENT(), then runs
// the function, if one is given, for anything else, such as components registered under names of their own; it
// unregisters them all when it unloads. Built into the game's executable, the same happens when Application::Run()
// starts, so the game's main() needs nothing more than running its application.
//
//     void RegisterGame() {
//         Spot::RegisterComponent<Player>("Hero");
//     }
//     SPOT_GAME_MODULE(RegisterGame)
//
// Modules link the shared engine library, spot_shared, that the program loading them links, so they share its
// state; they must be built against the same build of it.
#define SPOT_GAME_MODULE(...)                                                                                          \
    SPOT_MODULE_LOCAL ::Spot::Detail::ModuleRegistration* ::Spot::Detail::moduleRegistrations = nullptr;               \
    SPOT_MODULE_EXPORT void SpotLoadGameModule();                                                                      \
    static const ::Spot::Detail::GameCode spotGameCode(&SpotLoadGameModule);                                           \
    SPOT_MODULE_EXPORT void SpotLoadGameModule() {                                                                     \
        ::Spot::Detail::RunModuleRegistrations(::Spot::Detail::moduleRegistrations);                                   \
        ::Spot::Detail::CallIfGiven(__VA_ARGS__);                                                                      \
    }

// Registers the component type, under its name as written, when its game module loads, so it needs no
// RegisterComponent() call. Written once, in a source file of the module, outside any function:
//
//     SPOT_COMPONENT(Player);
#define SPOT_COMPONENT(type)                                                                                           \
    static const ::Spot::Detail::ModuleRegistration SPOT_MODULE_CONCAT(spotComponentRegistration, __LINE__)(           \
        [] { ::Spot::RegisterComponent<type>(#type); })

namespace Spot {

namespace Detail {

// What a game module registers when it loads, in a list of the module's own, which SPOT_GAME_MODULE defines.
struct ModuleRegistration;
extern SPOT_MODULE_LOCAL ModuleRegistration* moduleRegistrations;

struct ModuleRegistration {
    using Function = void (*)();

    // Registrations are made as the module's static objects are, before it loads, so they only join the list then.
    explicit ModuleRegistration(Function registration) : function(registration), next(moduleRegistrations) {
        moduleRegistrations = this;
    }

    Function function;
    const ModuleRegistration* next;
};

inline void RunModuleRegistrations(const ModuleRegistration* registration) {
    for (; registration; registration = registration->next) {
        registration->function();
    }
}

// The game's code built into the program, which applications register when they run. Modules have one too, from
// when they load until they unload, which is harmless: what they register is what their loading did already.
class GameCode {
public:
    using Function = void (*)();

    explicit GameCode(Function load);
    ~GameCode();

    GameCode(const GameCode&) = delete;
    GameCode& operator=(const GameCode&) = delete;

    // Registers what the last game code made registers; nothing without one.
    static void Load();

private:
    Function m_load;
};

inline void CallIfGiven() {
}

inline void CallIfGiven(void (*function)()) {
    function();
}

} // namespace Detail

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
