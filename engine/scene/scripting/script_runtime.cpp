#include "scene/scripting/script_runtime.h"

#include "core/log.h"

#ifdef SPOT_ENABLE_LUAU
#include "scene/scripting/luau_runtime.h"
#endif
#ifdef SPOT_ENABLE_VICESCRIPT
#include "scene/scripting/vice_runtime.h"
#endif

#include <algorithm>
#include <map>
#include <mutex>
#include <tuple>
#include <unordered_map>

namespace Spot {
namespace {

struct Language {
    std::vector<std::string> extensions;
    ScriptRuntimeFactory factory;
    ScriptExportReader exports;
    ScriptSyntaxChecker syntax;
};
struct Registry {
    std::mutex mutex;
    std::unordered_map<std::string, Language> languages;
    std::unordered_map<std::string, ScriptLibraryFactory> libraries;
};

std::string Normalize(std::string_view value) {
    std::string result(value);
    for (char& character : result) {
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character - 'A' + 'a');
    }
    return result;
}

Registry& Languages() {
    static Registry registry;
    static const bool initialized = [] {
#ifdef SPOT_ENABLE_LUAU
        registry.languages.emplace(
            "luau", Language{{".luau"}, Detail::CreateLuauRuntime, Detail::ReadLuauExports, Detail::CheckLuauSyntax});
#endif
#ifdef SPOT_ENABLE_VICESCRIPT
        registry.languages.emplace(
            "vice", Language{{".vice"}, Detail::CreateViceRuntime, Detail::ReadViceExports, Detail::CheckViceSyntax});
#endif
        return true;
    }();
    static_cast<void>(initialized);
    return registry;
}

bool IsIdentifier(std::string_view value) {
    return !value.empty() && std::ranges::all_of(value, [](char character) {
        return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_' ||
               character == '-';
    });
}

} // namespace

void RegisterScriptLanguage(std::string name, std::vector<std::string> extensions, ScriptRuntimeFactory factory) {
    name = Normalize(name);
    if (!IsIdentifier(name) || !factory)
        throw std::invalid_argument("A script language needs a valid name and runtime factory");
    for (auto& extension : extensions) {
        extension = Normalize(extension);
        if (!extension.starts_with('.') || !IsIdentifier(std::string_view(extension).substr(1)))
            throw std::invalid_argument("Script extensions must start with a dot followed by an identifier");
    }
    std::ranges::sort(extensions);
    extensions.erase(std::unique(extensions.begin(), extensions.end()), extensions.end());

    auto& registry = Languages();
    const std::lock_guard lock(registry.mutex);
    for (const auto& [otherName, language] : registry.languages) {
        if (otherName == name)
            continue;
        for (const auto& extension : extensions) {
            if (std::ranges::find(language.extensions, extension) != language.extensions.end())
                throw std::invalid_argument("Script extension '" + extension + "' is already registered");
        }
    }
    registry.languages.insert_or_assign(std::move(name), Language{std::move(extensions), std::move(factory)});
}

void UnregisterScriptLanguage(std::string_view name) {
    auto& registry = Languages();
    const std::lock_guard lock(registry.mutex);
    registry.languages.erase(Normalize(name));
}

void SetScriptExportReader(std::string_view language, ScriptExportReader reader) {
    auto& registry = Languages();
    const std::lock_guard lock(registry.mutex);
    const auto found = registry.languages.find(Normalize(language));
    if (found == registry.languages.end())
        throw std::runtime_error("Script language '" + std::string(language) + "' is not registered");
    found->second.exports = std::move(reader);
}

void SetScriptSyntaxChecker(std::string_view language, ScriptSyntaxChecker checker) {
    auto& registry = Languages();
    const std::lock_guard lock(registry.mutex);
    const auto found = registry.languages.find(Normalize(language));
    if (found == registry.languages.end())
        throw std::runtime_error("Script language '" + std::string(language) + "' is not registered");
    found->second.syntax = std::move(checker);
}

std::optional<ScriptDiagnostic> CheckScriptSyntax(std::string_view language, std::string_view source,
                                                  std::string_view sourceName) {
    ScriptSyntaxChecker checker;
    {
        auto& registry = Languages();
        const std::lock_guard lock(registry.mutex);
        const auto found = registry.languages.find(Normalize(language));
        if (found == registry.languages.end())
            return std::nullopt;
        checker = found->second.syntax;
    }
    return checker ? checker(source, sourceName) : std::nullopt;
}

std::vector<ScriptExport> GetScriptExports(std::string_view language, std::string_view source,
                                           std::string_view sourceName) {
    ScriptExportReader reader;
    {
        auto& registry = Languages();
        const std::lock_guard lock(registry.mutex);
        const auto found = registry.languages.find(Normalize(language));
        if (found == registry.languages.end())
            return {};
        reader = found->second.exports;
    }
    return reader ? reader(source, sourceName) : std::vector<ScriptExport>{};
}

std::string GetScriptLanguageForSource(std::string_view sourceName) {
    const auto separator = sourceName.find_last_of("/\\");
    const auto dot = sourceName.find_last_of('.');
    const std::string extension =
        dot != std::string_view::npos && (separator == std::string_view::npos || dot > separator)
            ? Normalize(sourceName.substr(dot))
            : std::string();
    auto& registry = Languages();
    const std::lock_guard lock(registry.mutex);
    for (const auto& [name, language] : registry.languages) {
        if (std::ranges::find(language.extensions, extension) != language.extensions.end())
            return name;
    }
    throw std::runtime_error("No script language is registered for extension '" + extension + "'");
}

std::unique_ptr<ScriptRuntime> CreateScriptRuntime(std::string_view language, std::string_view source,
                                                   std::string_view sourceName) {
    ScriptRuntimeFactory factory;
    {
        auto& registry = Languages();
        const std::lock_guard lock(registry.mutex);
        const auto found = registry.languages.find(Normalize(language));
        if (found == registry.languages.end())
            throw std::runtime_error("Script language '" + std::string(language) + "' is not registered");
        factory = found->second.factory;
    }
    // Factories may use engine services, including the registry; invoke them outside its lock.
    auto runtime = factory(source, sourceName);
    if (!runtime)
        throw std::runtime_error("Script runtime factory returned null");
    return runtime;
}

namespace {
struct ProfileRegistry {
    std::mutex mutex;
    bool enabled = false;
    std::map<std::tuple<std::string, std::string, std::size_t>, ScriptFunctionProfile> functions;
};
ProfileRegistry& Profiles() {
    static ProfileRegistry registry;
    return registry;
}
} // namespace

namespace {
struct DebugRegistry {
    std::mutex mutex;
    std::uint64_t revision = 0;
    std::map<std::string, std::vector<std::size_t>> breakpoints;
    ScriptBreakHandler handler;
};
DebugRegistry& Debugging() {
    static DebugRegistry registry;
    return registry;
}
} // namespace

void SetScriptBreakpoints(std::string sourceName, std::vector<std::size_t> lines) {
    auto& registry = Debugging();
    const std::lock_guard lock(registry.mutex);
    std::ranges::sort(lines);
    lines.erase(std::unique(lines.begin(), lines.end()), lines.end());
    if (lines.empty())
        registry.breakpoints.erase(sourceName);
    else
        registry.breakpoints.insert_or_assign(std::move(sourceName), std::move(lines));
    ++registry.revision;
}
std::vector<std::size_t> GetScriptBreakpoints(std::string_view sourceName) {
    auto& registry = Debugging();
    const std::lock_guard lock(registry.mutex);
    const auto found = registry.breakpoints.find(std::string(sourceName));
    return found != registry.breakpoints.end() ? found->second : std::vector<std::size_t>{};
}
void SetScriptBreakHandler(ScriptBreakHandler handler) {
    auto& registry = Debugging();
    const std::lock_guard lock(registry.mutex);
    registry.handler = std::move(handler);
}
std::uint64_t Detail::GetScriptDebugRevision() {
    auto& registry = Debugging();
    const std::lock_guard lock(registry.mutex);
    return registry.revision;
}
std::map<std::string, std::vector<std::size_t>> Detail::GetAllScriptBreakpoints() {
    auto& registry = Debugging();
    const std::lock_guard lock(registry.mutex);
    return registry.breakpoints;
}
void Detail::ReportScriptBreak(const ScriptBreak& stop) {
    ScriptBreakHandler handler;
    {
        auto& registry = Debugging();
        const std::lock_guard lock(registry.mutex);
        handler = registry.handler;
    }
    if (handler)
        handler(stop);
}

void SetScriptProfiling(bool enabled) {
    auto& registry = Profiles();
    const std::lock_guard lock(registry.mutex);
    registry.enabled = enabled;
}
bool IsScriptProfiling() {
    auto& registry = Profiles();
    const std::lock_guard lock(registry.mutex);
    return registry.enabled;
}
std::vector<ScriptFunctionProfile> GetScriptProfile() {
    auto& registry = Profiles();
    std::vector<ScriptFunctionProfile> profile;
    {
        const std::lock_guard lock(registry.mutex);
        for (const auto& [key, function] : registry.functions)
            profile.push_back(function);
    }
    std::ranges::sort(profile, [](const auto& a, const auto& b) { return a.time > b.time; });
    return profile;
}
void ResetScriptProfile() {
    auto& registry = Profiles();
    const std::lock_guard lock(registry.mutex);
    registry.functions.clear();
}
void Detail::AddScriptProfile(std::span<const ScriptFunctionProfile> profile) {
    auto& registry = Profiles();
    const std::lock_guard lock(registry.mutex);
    for (const auto& function : profile) {
        auto& total = registry.functions[{function.sourceName, function.name, function.line}];
        if (total.name.empty()) {
            total.name = function.name;
            total.sourceName = function.sourceName;
            total.line = function.line;
        }
        total.calls += function.calls;
        total.instructions += function.instructions;
        total.allocations += function.allocations;
        total.time += function.time;
    }
}

void RegisterScriptLibrary(std::string name, ScriptLibraryFactory factory) {
    if (name.empty() || name == "spot" || !factory)
        throw std::invalid_argument("A script library needs a non-reserved name and factory");
    auto& registry = Languages();
    const std::lock_guard lock(registry.mutex);
    registry.libraries.insert_or_assign(std::move(name), std::move(factory));
}
void UnregisterScriptLibrary(std::string_view name) {
    auto& registry = Languages();
    const std::lock_guard lock(registry.mutex);
    registry.libraries.erase(std::string(name));
}
void Detail::BindRegisteredScriptLibraries(ScriptRuntime& runtime) {
    std::vector<std::pair<std::string, ScriptLibraryFactory>> libraries;
    {
        auto& registry = Languages();
        const std::lock_guard lock(registry.mutex);
        for (const auto& entry : registry.libraries)
            libraries.push_back(entry);
    }
    for (const auto& [name, factory] : libraries) {
        auto library = factory();
        if (!library)
            throw std::runtime_error("Script library factory returned null");
        runtime.BindLibrary(name, library);
    }
}

std::string Detail::ResolveScriptModulePath(std::string_view request, std::string_view defaultExtension,
                                            bool allowDotSeparators) {
    std::string path(request);
    const std::string normalizedExtension = Normalize(defaultExtension);
    if (allowDotSeparators && path.find_first_of("/\\") == std::string::npos && !path.starts_with('.') &&
        !Normalize(path).ends_with(normalizedExtension)) {
        std::ranges::replace(path, '.', '/');
    }
    const auto separator = path.find_last_of("/\\");
    const auto extension = path.find_last_of('.');
    if (extension == std::string::npos || (separator != std::string::npos && extension < separator))
        path += defaultExtension;
    return path;
}

void Detail::WriteScriptLog(std::string_view loggerName, std::string_view message) {
    Logger logger{std::string(loggerName)};
    logger.WriteMessage(LogLevel::Info, message);
}

} // namespace Spot
