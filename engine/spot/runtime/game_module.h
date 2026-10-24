#pragma once

#include "spot/platform/shared_library.h"
#include "spot/runtime/module.h"

#include <concepts>
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

// Marks the game's code, once, in one of its source files, naming its module: a Module the application runs, whose
// OnStart() registers the game's components. Without a type, it is a GameCodeModule, which registers the components
// marked with SPOT_COMPONENT(). Built into the game's executable, main() adds it with Spot::CreateGameModule(); built
// into a shared library, it makes a game library, which the editor loads, starting its module to register the
// components and stopping it when it unloads.
//
//     class Game final : public Spot::GameCodeModule {
//     protected:
//         void OnStart() override {
//             GameCodeModule::OnStart();
//             Spot::RegisterComponent<Player>("Hero");
//         }
//     };
//     SPOT_GAME_MODULE(Game)
//
// Game libraries link the shared engine library, spot_shared, that the program loading them links, so they share its
// state; they must be built against the same build of it.
#define SPOT_GAME_MODULE(...)                                                                                          \
    SPOT_MODULE_LOCAL ::Spot::Detail::ModuleRegistration* ::Spot::Detail::moduleRegistrations = nullptr;               \
    void ::Spot::RegisterGameComponents() {                                                                            \
        ::Spot::Detail::RunModuleRegistrations(::Spot::Detail::moduleRegistrations);                                   \
    }                                                                                                                  \
    std::unique_ptr<::Spot::Module> Spot::CreateGameModule() {                                                         \
        return std::make_unique<::Spot::Detail::GameModuleType<__VA_ARGS__>::Type>();                                  \
    }                                                                                                                  \
    SPOT_MODULE_EXPORT ::Spot::Module* SpotCreateGameModule() {                                                        \
        return ::Spot::CreateGameModule().release();                                                                   \
    }

// Registers the component type, under its name as written, when the game's module starts (see
// RegisterGameComponents()), so it needs no RegisterComponent() call. Written once, in a source file of the game's
// code, outside any function:
//
//     SPOT_COMPONENT(Player);
#define SPOT_COMPONENT(type)                                                                                           \
    static const ::Spot::Detail::ModuleRegistration SPOT_MODULE_CONCAT(spotComponentRegistration, __LINE__)(           \
        [] { ::Spot::RegisterComponent<type>(#type); })

namespace Spot {

// Registers the components of the game's code marked with SPOT_COMPONENT(). Defined by SPOT_GAME_MODULE, in the
// game's code only.
SPOT_MODULE_LOCAL void RegisterGameComponents();
// Makes the module SPOT_GAME_MODULE names, for the main() of a game built with its code.
std::unique_ptr<Module> CreateGameModule();

// The module of game code that registers the components marked with SPOT_COMPONENT() when it starts. Games with more
// to do derive from it, calling its OnStart().
class GameCodeModule : public Module {
protected:
    void OnStart() override { RegisterGameComponents(); }
};

namespace Detail {

// What the game's code registers when its module starts, in a list of the code's own, which SPOT_GAME_MODULE defines.
struct ModuleRegistration;
extern SPOT_MODULE_LOCAL ModuleRegistration* moduleRegistrations;

struct ModuleRegistration {
    using Function = void (*)();

    // Registrations are made as the code's static objects are, before it runs, so they only join the list then.
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

template <typename T = GameCodeModule>
struct GameModuleType {
    static_assert(std::derived_from<T, Module>, "SPOT_GAME_MODULE names a Module");
    using Type = T;
};

// Starts and stops a module outside an application, as game libraries and the tests of game code do.
void StartModule(Module& module);
void StopModule(Module& module);

// Registers what the game's code built into the program does, by starting its module, as tests running the game's
// code without an application do.
inline void LoadGameCode() {
    const std::unique_ptr<Module> module = CreateGameModule();
    StartModule(*module);
    StopModule(*module);
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

    // Loads the library and starts the module its SPOT_GAME_MODULE names. Null, after logging why, when it cannot be
    // loaded or is not a game module.
    static std::unique_ptr<GameModule> Load(const std::filesystem::path& path);
    // Stops its module, unregisters the types it registered and unloads it. No component of those types may be left, as
    // their code goes with the module.
    ~GameModule();

    GameModule(const GameModule&) = delete;
    GameModule& operator=(const GameModule&) = delete;

    // The names of the component types it registered, in order.
    std::span<const std::string> GetComponentTypes() const { return m_componentTypes; }
    const std::filesystem::path& GetPath() const { return m_library->GetPath(); }

private:
    GameModule(std::unique_ptr<SharedLibrary> library, std::unique_ptr<Module> module,
               std::vector<std::string> componentTypes)
        : m_library(std::move(library)), m_module(std::move(module)), m_componentTypes(std::move(componentTypes)) {}

    std::unique_ptr<SharedLibrary> m_library;
    // Made by the library's code, so destroyed before it unloads.
    std::unique_ptr<Module> m_module;
    std::vector<std::string> m_componentTypes;
};

} // namespace Spot
