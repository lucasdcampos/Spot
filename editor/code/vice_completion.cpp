#include "code/vice_completion.h"

#include "scene/scripting/script_api.h"

#include <spot.h>

#include <algorithm>
#include <cctype>
#include <optional>

namespace Spot::Editor {

namespace {

// Types beyond ScriptApi's: the script's own class, and component references, which add what their type's bindings
// describe to what every component has.
constexpr std::string_view SelfType = "self";
constexpr std::string_view ComponentPrefix = "Component:";

bool IsIdentifierCharacter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

bool IsIdentifier(std::string_view text) {
    return !text.empty() && !(text.front() >= '0' && text.front() <= '9') &&
           std::ranges::all_of(text, IsIdentifierCharacter);
}

std::string Lower(std::string_view text) {
    std::string lower(text);
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

// One step of the receiver: a name, or a call with what its parentheses hold.
struct Segment {
    std::string_view name;
    bool call = false;
    std::string_view arguments;
};

// Reads the chain before the dot just before `position`, backwards: "entity.GetComponent("Camera")." gives entity,
// then GetComponent("Camera"). Null when it is not one.
std::optional<std::vector<Segment>> ReadReceiver(std::string_view source, std::size_t position) {
    if (position == 0 || position > source.size() || source[position - 1] != '.')
        return std::nullopt;
    std::vector<Segment> segments;
    std::size_t end = position - 1;
    for (;;) {
        Segment segment;
        if (end > 0 && source[end - 1] == ')') {
            // The matching parenthesis, past any nested ones and strings.
            int depth = 0;
            bool inString = false;
            std::size_t open = end - 1;
            for (std::size_t i = end; i-- > 0;) {
                const char c = source[i];
                if (c == '"' && (i == 0 || source[i - 1] != '\\')) {
                    inString = !inString;
                } else if (!inString && c == ')') {
                    ++depth;
                } else if (!inString && c == '(' && --depth == 0) {
                    open = i;
                    break;
                }
                if (i == 0)
                    return std::nullopt;
            }
            segment.call = true;
            segment.arguments = source.substr(open + 1, end - 1 - open - 1);
            end = open;
        }
        std::size_t start = end;
        while (start > 0 && IsIdentifierCharacter(source[start - 1]))
            --start;
        segment.name = source.substr(start, end - start);
        if (!IsIdentifier(segment.name))
            return std::nullopt;
        segments.push_back(segment);
        if (start == 0 || source[start - 1] != '.')
            break;
        end = start - 1;
    }
    std::ranges::reverse(segments);
    return segments;
}

// The first argument of a call when it is a string literal, as the type name GetComponent("Camera") takes.
std::string_view StringArgument(std::string_view arguments) {
    const std::size_t open = arguments.find_first_not_of(" \t");
    if (open == std::string_view::npos || arguments[open] != '"')
        return {};
    const std::size_t close = arguments.find('"', open + 1);
    return close == std::string_view::npos ? std::string_view{} : arguments.substr(open + 1, close - open - 1);
}

// What a component script's source declares, read with braces counted: its class's fields and methods, how it
// imports spot, and the function that `position` is in, for its hook's parameter.
struct Outline {
    std::vector<std::string_view> fields;
    std::vector<std::string_view> methods;
    std::vector<std::string_view> moduleNames;
    std::string_view function;
    std::string_view parameters;
};

Outline ReadOutline(std::string_view source, std::size_t position) {
    Outline outline;
    struct Function {
        int depth = 0;
        std::string_view name;
        std::string_view parameters;
    };
    // The functions open at `position`, innermost last.
    std::vector<Function> open;
    std::optional<Function> declared;
    int depth = 0;
    // The depth of the body of the class being read, whose members are declared at it.
    int classDepth = -1;
    bool classDeclared = false;
    std::string_view previous;
    std::string_view beforePrevious;
    for (std::size_t i = 0; i < source.size(); ++i) {
        const char c = source[i];
        if (c == '/' && i + 1 < source.size() && source[i + 1] == '/') {
            while (i + 1 < source.size() && source[i + 1] != '\n')
                ++i;
            continue;
        }
        if (c == '"') {
            for (++i; i < source.size() && source[i] != '"'; ++i) {
                if (source[i] == '\\')
                    ++i;
            }
            continue;
        }
        if (c == '{') {
            ++depth;
            if (classDeclared && classDepth < 0)
                classDepth = depth;
            if (declared && i < position)
                open.push_back({depth, declared->name, declared->parameters});
            classDeclared = false;
            declared.reset();
            continue;
        }
        if (c == '}') {
            if (i < position) {
                while (!open.empty() && open.back().depth >= depth)
                    open.pop_back();
            }
            if (depth == classDepth)
                classDepth = -1;
            --depth;
            continue;
        }
        if (!IsIdentifierCharacter(c))
            continue;
        std::size_t end = i;
        while (end < source.size() && IsIdentifierCharacter(source[end]))
            ++end;
        const std::string_view word = source.substr(i, end - i);
        const bool member = classDepth >= 0 && depth == classDepth;
        if (previous == "class") {
            classDeclared = true;
        } else if (previous == "func") {
            const std::size_t parenthesis = source.find_first_not_of(" \t", end);
            if (parenthesis != std::string_view::npos && source[parenthesis] == '(') {
                const std::size_t close = source.find(')', parenthesis);
                declared =
                    Function{0, word,
                             close == std::string_view::npos ? std::string_view{}
                                                             : source.substr(parenthesis + 1, close - parenthesis - 1)};
                if (member)
                    outline.methods.push_back(word);
            }
        } else if (previous == "var" && member) {
            outline.fields.push_back(word);
        } else if (previous == "import" && word == ScriptApi::Module) {
            outline.moduleNames.push_back(word);
        } else if (previous == "as" && beforePrevious == ScriptApi::Module && !outline.moduleNames.empty()) {
            outline.moduleNames.back() = word;
        }
        beforePrevious = previous;
        previous = word;
        i = end - 1;
    }
    if (!open.empty()) {
        outline.function = open.back().name;
        outline.parameters = open.back().parameters;
    }
    return outline;
}

// The type of a hook's parameter named `name` in the function the outline is in.
std::string_view HookParameterType(const Outline& outline, std::string_view name) {
    for (const auto& hook : ScriptApi::Hooks) {
        if (hook.name != outline.function || hook.parameterType.empty())
            continue;
        std::string_view parameters = outline.parameters;
        const std::size_t first = parameters.find_first_not_of(" \t");
        if (first == std::string_view::npos)
            return {};
        parameters = parameters.substr(first);
        const std::size_t last = parameters.find_first_of(" \t,");
        if (parameters.substr(0, last) == name)
            return hook.parameterType;
    }
    return {};
}

// The type of `segment` read from a value of `type`; empty when unknown.
std::string Resolve(std::string_view type, const Segment& segment, const Outline& outline) {
    const auto kind = segment.call ? ScriptApi::MemberKind::Method : ScriptApi::MemberKind::Property;
    if (type == SelfType) {
        if (std::ranges::find(outline.fields, segment.name) != outline.fields.end() ||
            std::ranges::find(outline.methods, segment.name) != outline.methods.end())
            return {};
        return Resolve(ScriptApi::Component, segment, outline);
    }
    if (type.starts_with(ComponentPrefix) || type == ScriptApi::Component) {
        if (const auto* member = ScriptApi::FindMember(ScriptApi::Component, segment.name, kind))
            return std::string(member->type);
        if (type == ScriptApi::Component)
            return {};
        for (const auto& member : GetComponentScriptMembers(type.substr(ComponentPrefix.size()))) {
            if (member.name == segment.name && member.method == segment.call)
                return member.type;
        }
        return {};
    }
    if (type == "Entity" && segment.call && (segment.name == "GetComponent" || segment.name == "AddComponent")) {
        if (const auto name = StringArgument(segment.arguments); !name.empty())
            return std::string(ComponentPrefix) + std::string(name);
    }
    if (const auto* member = ScriptApi::FindMember(type, segment.name, kind))
        return std::string(member->type);
    // entity.Health is entity.GetComponent("Health").
    if (type == "Entity" && !segment.call && segment.name.front() >= 'A' && segment.name.front() <= 'Z')
        return std::string(ComponentPrefix) + std::string(segment.name);
    return {};
}

// The type of the first segment, from what a component script knows without being told.
std::string ResolveRoot(const Segment& root, const Outline& outline) {
    if (root.call && ScriptApi::IsValueConstructor(root.name))
        return std::string(root.name);
    if (!root.call) {
        if (root.name == "self")
            return std::string(SelfType);
        if (std::ranges::find(outline.moduleNames, root.name) != outline.moduleNames.end())
            return std::string(ScriptApi::Module);
        if (const auto parameter = HookParameterType(outline, root.name); !parameter.empty())
            return std::string(parameter);
    }
    // Members of the class itself and of Component need no self prefix.
    return Resolve(SelfType, root, outline);
}

void Add(std::vector<ViceCompletion>& completions, std::string_view name, bool method, std::string_view type) {
    if (std::ranges::none_of(completions, [&](const ViceCompletion& item) { return item.name == name; }))
        completions.push_back({std::string(name), method, std::string(type)});
}

void AddTable(std::vector<ViceCompletion>& completions, std::string_view type) {
    if (const auto* found = ScriptApi::FindType(type)) {
        for (const auto& member : found->members) {
            if (!member.hidden)
                Add(completions, member.name, member.kind == ScriptApi::MemberKind::Method, member.type);
        }
    }
}

std::vector<ViceCompletion> MembersOf(std::string_view type, const Outline& outline) {
    std::vector<ViceCompletion> members;
    if (type == SelfType) {
        for (const auto field : outline.fields)
            Add(members, field, false, {});
        for (const auto method : outline.methods)
            Add(members, method, true, {});
        AddTable(members, ScriptApi::Component);
    } else if (type.starts_with(ComponentPrefix)) {
        AddTable(members, ScriptApi::Component);
        for (const auto& member : GetComponentScriptMembers(type.substr(ComponentPrefix.size())))
            Add(members, member.name, member.method, member.type);
    } else {
        AddTable(members, type);
        // Each registered component is a property of entities, entity.Camera for entity.GetComponent("Camera").
        if (type == "Entity") {
            for (const auto& name : GetComponentTypeNames())
                Add(members, name, false, std::string(ComponentPrefix) + name);
        }
    }
    return members;
}

} // namespace

std::vector<ViceCompletion> GetViceMemberCompletions(std::string_view source, std::size_t position,
                                                     std::string_view prefix) {
    const auto receiver = ReadReceiver(source, position);
    if (!receiver)
        return {};
    const Outline outline = ReadOutline(source, position);
    std::string type = ResolveRoot(receiver->front(), outline);
    for (std::size_t i = 1; i < receiver->size() && !type.empty(); ++i)
        type = Resolve(type, (*receiver)[i], outline);
    if (type.empty())
        return {};

    std::vector<ViceCompletion> completions = MembersOf(type, outline);
    const std::string lowerPrefix = Lower(prefix);
    std::erase_if(completions, [&](const ViceCompletion& item) { return !Lower(item.name).starts_with(lowerPrefix); });
    std::ranges::sort(completions, [](const ViceCompletion& a, const ViceCompletion& b) {
        const std::string lowerA = Lower(a.name);
        const std::string lowerB = Lower(b.name);
        return lowerA != lowerB ? lowerA < lowerB : a.name < b.name;
    });
    return completions;
}

} // namespace Spot::Editor
