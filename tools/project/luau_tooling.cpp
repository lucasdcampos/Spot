#include "project/luau_tooling.h"

#include "tools_log.h"
#include <spot/scene/component_registry.h>
#include <spot/scene/scripting/script_api.h>
#include <spot/scene/scripting/script_bindings.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <system_error>

namespace Spot {

using nlohmann::json;

namespace {

constexpr std::string_view ModuleType = "SpotModule";

constexpr std::array<std::string_view, 21> ReservedWords{
    "and",   "break", "do",  "else", "elseif", "end",    "false", "for",  "function", "if",    "in",
    "local", "nil",   "not", "or",   "repeat", "return", "then",  "true", "until",    "while",
};

std::string GetTypeName(const ScriptApi::Type& type) {
    return type.name == ScriptApi::Module ? std::string(ModuleType) : std::string(type.name);
}

// The Luau type of a type ScriptApi names. Objects a method finds can be missing, so they come back optional.
std::string ToLuauType(std::string_view type, bool optional) {
    if (type == "bool")
        return "boolean";
    if (type == "number" || type == "string" || type == "any")
        return std::string(type);
    if (ScriptApi::FindType(type) && type != ScriptApi::Module)
        return std::string(type) + (optional ? "?" : "");
    return "any";
}

// A method's function type, from its parameters as ScriptApi writes them for people, such as "typeName[, index]"
// or "name, arguments...".
std::string MakeFunctionType(const ScriptApi::Type& owner, const ScriptApi::Member& method) {
    std::string parameters = "self: " + GetTypeName(owner);
    if (method.writable && !method.property.empty()) {
        const auto* property = ScriptApi::FindMember(owner.name, method.property, ScriptApi::MemberKind::Property);
        parameters += ", value: " + ToLuauType(property ? property->type : "any", false);
    } else {
        bool optional = false;
        std::string token;
        const auto finish = [&] {
            const auto end = std::ranges::find_if(token, [](char c) {
                return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_');
            });
            std::string name(token.begin(), end);
            // Parameters are named for people, who may call one after a keyword, as StartCoroutine(function).
            if (std::ranges::find(ReservedWords, name) != ReservedWords.end())
                name = name == "function" ? "callback" : name + "_";
            if (token.find("...") != std::string::npos)
                parameters += ", ...any";
            else if (!name.empty())
                parameters += ", " + name + (optional ? ": any?" : ": any");
            token.clear();
        };
        for (const char c : method.parameters) {
            if (c == '[' || c == ',' || c == ']') {
                finish();
                optional = optional || c == '[';
            } else if (c != ' ' || !token.empty()) {
                token += c;
            }
        }
        finish();
    }
    return "(" + parameters + ") -> " + (method.type.empty() ? "()" : ToLuauType(method.type, true));
}

std::optional<std::string> ReadText(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error))
        return std::nullopt;
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::string LowerCase(std::string_view text) {
    std::string lower(text);
    for (char& c : lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower;
}

bool WriteText(const std::filesystem::path& path, std::string_view text) {
    if (ReadText(path) == text)
        return true;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream file(path, std::ios::binary);
    file << text;
    if (!file)
        GetToolsLogger().Error("Could not write '{}'", PathToUtf8(path));
    return static_cast<bool>(file);
}

// How the reference writes a type: engine objects link to their section.
std::string ToReferenceType(std::string_view type) {
    if (type.empty())
        return "";
    if (ScriptApi::FindType(type) && type != ScriptApi::Module)
        return "[`" + std::string(type) + "`](#" + LowerCase(type) + ")";
    return "`" + std::string(type) + "`";
}

std::string MakeReferenceRow(std::string_view member, std::string_view type, std::string_view notes) {
    return "| `" + std::string(member) + "` | " + ToReferenceType(type) + " | " + std::string(notes) + " |\n";
}

constexpr std::string_view TableHeader = "| Member | Type | |\n|---|---|---|\n";

void AppendMemberTable(std::string& out, const ScriptApi::Type& type) {
    out += TableHeader;
    for (const auto& member : type.members) {
        // Getters and setters do what their property does; the property stands for them.
        if (member.hidden || !member.property.empty())
            continue;
        if (member.kind == ScriptApi::MemberKind::Property)
            out += MakeReferenceRow(member.name, member.type, member.writable ? "" : "read only");
        else
            out += MakeReferenceRow(std::string(member.name) + "(" + std::string(member.parameters) + ")", member.type,
                                    "method");
    }
}

// Whether a component's method only reads or writes one of its properties, as GetColor() and SetColor() do color.
bool IsAccessor(const ScriptMemberInfo& method, std::span<const ScriptMemberInfo> members) {
    for (const std::string_view prefix : {"Get", "Set", "Is"}) {
        if (method.name.size() <= prefix.size() || !method.name.starts_with(prefix))
            continue;
        std::string property = method.name.substr(prefix.size());
        property[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(property[0])));
        if (std::ranges::any_of(members, [&](const auto& other) { return !other.method && other.name == property; }))
            return true;
    }
    return false;
}

} // namespace

std::string MakeScriptReference() {
    std::string out = "# Scripting API\n\n"
                      "<!-- Generated by MakeScriptReference() (tools/project/luau_tooling.h) from the engine's script "
                      "API; do not edit. Run the tests with SPOT_UPDATE_DOCS=1 to write it again. -->\n\n"
                      "Everything the engine gives Luau scripts. [Scripting](../guides/scripting.md) explains how "
                      "scripts run; this page lists what they can reach. Properties marked *read only* cannot be "
                      "assigned. Every property also has the methods older scripts use, such as `GetName()` and "
                      "`SetName(value)` for `name`. Parameters in brackets are optional.\n\n";

    out += "## Hooks\n\nFunctions a script defines for the engine to call, with how many parameters they take.\n\n"
           "| Hook | Parameters |\n|---|---|\n";
    for (const auto& hook : ScriptApi::Hooks)
        out += "| `" + std::string(hook.name) + "` | " + std::to_string(hook.parameterCount) + " |\n";

    out += "\n## Globals\n\nValue types, built by functions of the same name.\n\n";
    for (const auto constructor : ScriptApi::ValueConstructors) {
        std::string parameters;
        for (const auto& member : ScriptApi::FindType(constructor)->members)
            parameters += (parameters.empty() ? "" : ", ") + std::string(member.name);
        out += "- [`" + std::string(constructor) + "(" + parameters + ")`](#" + LowerCase(constructor) + ")\n";
    }

    out += "\n## The `spot` module\n\nWhat `local spot = require(\"@spot\")` gives.\n\n";
    AppendMemberTable(out, *ScriptApi::FindType(ScriptApi::Module));

    out += "\n## Types\n";
    for (const auto& type : ScriptApi::Types) {
        if (type.name == ScriptApi::Module)
            continue;
        out += "\n### " + std::string(type.name) + "\n\n";
        AppendMemberTable(out, type);
    }

    out += "\n## Components\n\nWhat `entity:GetComponent(\"<type>\")` gives, besides the members of every "
           "[`Component`](#component). Components without members of their own are left out.\n";
    for (const auto& name : GetComponentTypeNames()) {
        const auto members = GetComponentScriptMembers(name);
        std::string rows;
        for (const auto& member : members) {
            if (!member.method)
                rows += MakeReferenceRow(member.name, member.type, member.writable ? "" : "read only");
            else if (!IsAccessor(member, members))
                rows += MakeReferenceRow(member.name + "()", member.type, "method");
        }
        if (!rows.empty())
            out += "\n### " + name + "\n\n" + std::string(TableHeader) + rows;
    }
    return out;
}

std::string MakeLuauModule() {
    std::string out = "-- What require(\"@spot\") gives: the engine's script API for Luau's language server, generated "
                      "by Spot's tools; do not edit.\n\n";
    for (const auto& type : ScriptApi::Types) {
        out += "export type " + GetTypeName(type) + " = {\n";
        for (const auto& member : type.members) {
            if (member.hidden)
                continue;
            out += "    " + std::string(member.name) + ": ";
            if (member.kind == ScriptApi::MemberKind::Property)
                out += ToLuauType(member.type, member.name == "parent");
            else
                out += MakeFunctionType(type, member);
            out += ",\n";
        }
        // Entities reach their components by type name, and components have the members their bindings describe.
        if (type.name == "Entity" || type.name == ScriptApi::Component)
            out += "    [string]: any,\n";
        out += "}\n\n";
    }
    out += "return (nil :: any) :: " + std::string(ModuleType) + "\n";
    return out;
}

std::string MakeLuauDefinitions() {
    std::string out = "-- The globals the engine gives Luau scripts, generated by Spot's tools; do not edit.\n\n";
    for (const auto constructor : ScriptApi::ValueConstructors) {
        // The same shape as the module's type of the value, which makes them interchangeable.
        std::string parameters;
        std::string shape;
        for (const auto& member : ScriptApi::FindType(constructor)->members) {
            parameters += (parameters.empty() ? "" : ", ") + std::string(member.name) + ": number";
            shape += (shape.empty() ? "" : ", ") + std::string(member.name) + ": number";
        }
        out += "declare function " + std::string(constructor) + "(" + parameters + "): { " + shape + " }\n";
    }
    return out;
}

bool WriteLuauTypes(const Project& project) {
    const std::filesystem::path directory = project.GetCacheDirectory() / "luau";
    const bool module = WriteText(directory / "spot.luau", MakeLuauModule());
    const bool definitions = WriteText(directory / "spot.d.luau", MakeLuauDefinitions());
    return module && definitions;
}

bool WriteLuauTooling(const Project& project) {
    if (!WriteLuauTypes(project))
        return false;
    const std::filesystem::path path = project.GetDirectory() / ".luaurc";
    std::string module =
        PathToUtf8(std::filesystem::relative(project.GetCacheDirectory() / "luau" / "spot", project.GetDirectory()));
    std::ranges::replace(module, '\\', '/');
    // Scripts' global functions are what the engine and other scripts call, so none is unused for being unused in
    // its own file.
    json config = {{"languageMode", "nonstrict"}, {"lint", {{"FunctionUnused", false}}}};
    if (const auto text = ReadText(path)) {
        config = json::parse(*text, nullptr, false);
        if (!config.is_object()) {
            GetToolsLogger().Warning("Left '{}' as it is, as it is not plain JSON; give it the alias \"spot\": \"{}\"",
                                     PathToUtf8(path), module);
            return true;
        }
        if (config.contains("aliases") && config["aliases"].is_object() && config["aliases"].contains("spot"))
            return true;
    }
    config["aliases"]["spot"] = module;
    return WriteText(path, config.dump(4) + "\n");
}

} // namespace Spot
