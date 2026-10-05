#pragma once

#include <imgui.h>

#include <string>
#include <string_view>

namespace Spot::Editor {

// A dockable editor window. Its id stays the same whatever its title shows, so layouts saved with a title still
// apply when it changes.
class Panel {
public:
    // The id names the window in saved layouts; the title is what its tab shows, usually an icon and a name.
    Panel(std::string_view id, std::string_view title);
    virtual ~Panel() = default;

    Panel(const Panel&) = delete;
    Panel& operator=(const Panel&) = delete;

    const std::string& GetId() const { return m_id; }
    const std::string& GetTitle() const { return m_title; }
    // "<title>###<id>", as ImGui::Begin() takes it.
    const std::string& GetWindowName() const { return m_windowName; }

    bool IsOpen() const { return m_open; }
    void SetOpen(bool open) { m_open = open; }

    // Draws the window if it is open.
    void Draw();

protected:
    virtual void OnDraw() = 0;
    virtual ImGuiWindowFlags GetWindowFlags() const { return ImGuiWindowFlags_None; }

    // A centered, muted hint for panels with nothing to show, such as "Open a scene to see its entities".
    static void DrawEmptyState(const char* icon, std::string_view text);

private:
    std::string m_id;
    std::string m_title;
    std::string m_windowName;
    bool m_open = true;
};

} // namespace Spot::Editor
