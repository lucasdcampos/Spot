#include <vice/vice.h>

#include <array>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::filesystem::path Path(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}
std::optional<Vice::ModuleSource> Load(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return std::nullopt;
    std::string source{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (source.starts_with("\xef\xbb\xbf"))
        source.erase(0, 3);
    const auto canonical = std::filesystem::canonical(path).generic_u8string();
    return Vice::ModuleSource{std::move(source), std::string(canonical.begin(), canonical.end())};
}

// The interactive debugger of `vice --debug`: commands are read from standard input at each stop.
class Debugger {
public:
    explicit Debugger(Vice::Instance& instance, std::string sourceName)
        : m_instance(instance), m_sourceName(std::move(sourceName)) {}

    // Reads commands until `run`, setting breakpoints before the script starts.
    bool Prepare() {
        std::cout << "Vice debugger. Commands: break <line>, delete <line>, run, quit; when stopped also continue, "
                     "step, next, out, locals, stack.\n";
        for (;;) {
            const auto words = Prompt();
            if (words.empty() || words[0] == "quit" || words[0] == "q")
                return false;
            if (words[0] == "run" || words[0] == "r")
                return true;
            if (!Breakpoint(words))
                std::cout << "Unknown command\n";
        }
    }

    Vice::DebugAction Stop(const Vice::DebugEvent& event) {
        const auto& frame = event.frames.front();
        std::cout << (event.reason == Vice::DebugReason::Breakpoint ? "Breakpoint" : "Step") << " at " << frame.function
                  << " (" << frame.sourceName << ":" << frame.location.line << ")\n";
        for (;;) {
            const auto words = Prompt();
            if (words.empty() || words[0] == "continue" || words[0] == "c")
                return Vice::DebugAction::Continue;
            if (words[0] == "step" || words[0] == "s")
                return Vice::DebugAction::StepIn;
            if (words[0] == "next" || words[0] == "n")
                return Vice::DebugAction::StepOver;
            if (words[0] == "out" || words[0] == "o")
                return Vice::DebugAction::StepOut;
            if (words[0] == "quit" || words[0] == "q")
                std::exit(1);
            if (words[0] == "locals" || words[0] == "l") {
                for (const auto& [name, value] : frame.locals)
                    std::cout << "  " << name << " = " << Vice::ToString(value) << '\n';
            } else if (words[0] == "stack" || words[0] == "bt") {
                for (const auto& entry : event.frames)
                    std::cout << "  at " << entry.function << " (" << entry.sourceName << ":" << entry.location.line
                              << ")\n";
            } else if (!Breakpoint(words)) {
                std::cout << "Unknown command\n";
            }
        }
    }

private:
    std::vector<std::string> Prompt() {
        std::cout << "(vice) " << std::flush;
        std::string line;
        if (!std::getline(std::cin, line))
            return {};
        std::istringstream stream(line);
        std::vector<std::string> words;
        for (std::string word; stream >> word;)
            words.push_back(word);
        return words.empty() ? std::vector<std::string>{"continue"} : words;
    }
    bool Breakpoint(const std::vector<std::string>& words) {
        const bool add = words[0] == "break" || words[0] == "b";
        if ((!add && words[0] != "delete" && words[0] != "d") || words.size() != 2)
            return false;
        std::size_t line = 0;
        const auto result = std::from_chars(words[1].data(), words[1].data() + words[1].size(), line);
        if (result.ec != std::errc() || line == 0) {
            std::cout << "Expected a line number\n";
            return true;
        }
        if (add)
            m_lines.push_back(line);
        else
            std::erase(m_lines, line);
        m_instance.SetBreakpoints(m_sourceName, m_lines);
        std::cout << (add ? "Breakpoint at line " : "Removed breakpoint at line ") << line << '\n';
        return true;
    }

    Vice::Instance& m_instance;
    std::string m_sourceName;
    std::vector<std::size_t> m_lines;
};

} // namespace

int main(int argc, char** argv) {
    const bool debug = argc > 1 && std::string_view(argv[1]) == "--debug";
    if (debug) {
        --argc;
        ++argv;
    }
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: vice [--debug] <script.vice> [update-count]\n";
        return 2;
    }
    try {
        int updates = 1;
        if (argc == 3) {
            const std::string_view text(argv[2]);
            const auto result = std::from_chars(text.data(), text.data() + text.size(), updates);
            if (result.ec != std::errc() || result.ptr != text.data() + text.size() || updates < 0) {
                throw std::runtime_error("Update count must be a nonnegative integer");
            }
        }
        auto source = Load(Path(argv[1]));
        if (!source)
            throw std::runtime_error("Could not open script");
        Vice::Instance instance(Vice::Program::Parse(source->source, source->sourceName));
        instance.SetModuleLoader([](std::string_view request, std::string_view importer) {
            auto path = Path(request);
            if (!path.has_extension())
                path += ".vice";
            return Load(Path(importer).parent_path() / path);
        });
        instance.Bind("print", [](std::span<const Vice::Value> values) -> Vice::Value {
            bool first = true;
            for (const auto& value : values) {
                if (!first)
                    std::cout << ' ';
                std::cout << Vice::ToString(value);
                first = false;
            }
            std::cout << '\n';
            return {};
        });
        if (instance.HasFunction("start") && instance.GetParameterCount("start") != 0) {
            throw std::runtime_error("start must have no parameters");
        }
        if (instance.HasFunction("update") && instance.GetParameterCount("update") != 1) {
            throw std::runtime_error("update must have one deltaTime parameter");
        }
        std::optional<Debugger> debugger;
        if (debug) {
            debugger.emplace(instance, source->sourceName);
            if (!debugger->Prepare())
                return 0;
            instance.SetDebugHandler([&](const Vice::DebugEvent& event) { return debugger->Stop(event); });
        }
        instance.Initialize();
        if (instance.HasFunction("start"))
            instance.Call("start");
        const std::array<Vice::Value, 1> arguments{1.0 / 60.0};
        if (instance.HasFunction("update")) {
            for (int i = 0; i < updates; ++i)
                instance.Call("update", arguments);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
