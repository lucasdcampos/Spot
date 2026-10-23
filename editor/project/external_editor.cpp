#include "project/external_editor.h"

#include "core/editor_log.h"

namespace Spot::Editor {

namespace {

void ReplaceAll(std::string& text, std::string_view from, std::string_view to) {
    for (std::size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size()))
        text.replace(at, from.size(), to);
}

} // namespace

std::vector<std::string> MakeExternalEditorCommand(std::string_view command, const std::filesystem::path& project,
                                                   const std::filesystem::path& file, std::size_t line) {
    std::vector<std::string> words;
    std::string word;
    bool quoted = false;
    bool started = false;
    for (const char c : command) {
        if (c == '"') {
            quoted = !quoted;
            started = true;
        } else if ((c == ' ' || c == '\t') && !quoted) {
            if (started)
                words.push_back(std::move(word));
            word.clear();
            started = false;
        } else {
            word += c;
            started = true;
        }
    }
    if (started)
        words.push_back(std::move(word));
    if (words.empty())
        return words;

    const std::string folder = PathToUtf8(project);
    const std::string target = file.empty() ? folder : PathToUtf8(file);
    const bool placed =
        command.find("{project}") != std::string_view::npos || command.find("{file}") != std::string_view::npos;
    for (std::string& argument : words) {
        ReplaceAll(argument, "{project}", folder);
        ReplaceAll(argument, "{file}", target);
        ReplaceAll(argument, "{line}", std::to_string(line == 0 ? 1 : line));
    }
    if (!placed)
        words.push_back(target);
    return words;
}

bool OpenInExternalEditor(std::string_view command, const Project& project, const std::filesystem::path& file,
                          std::size_t line) {
    WriteLuauTooling(project);
    const std::vector<std::string> arguments = MakeExternalEditorCommand(command, project.GetDirectory(), file, line);
    if (arguments.empty())
        return Shell::Open(file.empty() ? project.GetDirectory() : file);
    GetEditorLogger().Info("Opening '{}' with '{}'", PathToUtf8(file.empty() ? project.GetDirectory() : file),
                           arguments.front());
    return Process::Launch(arguments, project.GetDirectory());
}

} // namespace Spot::Editor
