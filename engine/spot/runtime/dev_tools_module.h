#pragma once

#include "spot/core/ids/asset_id.h"
#include "spot/core/ids/asset_ref.h"
#include "spot/runtime/console.h"
#include "spot/runtime/module.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace Spot {

class AssetManager;
class Component;
class ConsoleInput;
class Font;
class FrameCapture;
class Renderer2D;
class Texture;
struct RuntimeStats;

struct DevToolsConfig {
    // The Grave key opens an event-driven developer console. Tools with their own console turn it off.
    bool consoleOverlay = true;
    // Connects to RenderDoc, when its optional runtime is there, before the application creates its graphics device.
    bool frameCapture = true;
};

// The tools of developers for an application, which development builds and the editor add, first so the console
// sees events before the game and draws over everything: the console overlay, the statistics overlay, screenshots,
// RenderDoc captures and profile streaming, with their console commands (stats, screenshot, capture,
// profiler.stream), and the reloads of assets edited while the game runs.
class DevToolsModule final : public Module {
public:
    // Connects to RenderDoc here, as it must be before the graphics device is made.
    explicit DevToolsModule(DevToolsConfig config = {});
    ~DevToolsModule() override;

    // Makes the next Load() of the changed assets read their imported files again, as after they were imported again
    // (see AssetDatabase::GetChanged()), along with the loaded assets that refer to them, such as the materials of a
    // changed texture. What refers to the old ones keeps them until it lets them go. Returns every asset that will
    // load anew, sorted, for references to them to be loaded again (see ReloadAssetReferences()).
    static std::vector<AssetId> ReloadAssets(AssetManager& assets, std::span<const AssetId> changed);
    // Loads the component's references to these assets again, through the asset manager, or the running
    // application's when it is null, so it gets their new versions after ReloadAssets(). Its other fields are left
    // alone.
    static void ReloadAssetReferences(Component& component, std::span<const AssetId> assets,
                                      AssetManager* manager = nullptr);

    // A game's imported UI font; keep the asset reference so it stays loaded for the overlays.
    void SetFont(AssetRef<Font> font) { m_font = std::move(font); }
    // Null when the console overlay is off, or while the application is not running.
    ConsoleInput* GetConsoleInput() const { return m_consoleInput.get(); }
    bool IsStatsVisible() const { return m_statsVisible; }
    void SetStatsVisible(bool visible) { m_statsVisible = visible; }
    // Sends each profiled frame to the log as it completes, for tools reading the game's log; also turned on by the
    // SPOT_PROFILE_STREAM environment variable set to 1.
    bool IsProfileStreamEnabled() const { return m_profileStreamEnabled; }
    void SetProfileStreamEnabled(bool enabled) { m_profileStreamEnabled = enabled; }
    // Queues the next complete frame as a PNG in the user's screenshots folder; rejects paths and pending requests.
    bool RequestScreenshot(std::string_view fileName = {});
    // Requests a RenderDoc frame when its optional runtime connected before graphics creation.
    bool RequestFrameCapture(std::string_view fileName = {});

protected:
    void OnStart() override;
    void OnStop() override;
    bool OnEvent(const Event& event) override;
    void OnUpdate(float deltaTime) override;
    void OnBeginRender(RenderContext& context) override;
    void OnEndRender(RenderContext& context) override;

private:
    std::filesystem::path GetCaptureDirectory();
    void StreamProfileFrame();
    void SaveScreenshot();
    void RenderOverlay(RenderContext& context);

    DevToolsConfig m_config;
    std::unique_ptr<FrameCapture> m_frameCapture;
    std::unique_ptr<ConsoleInput> m_consoleInput;
    std::vector<ConsoleRegistration> m_console;
    AssetRef<Font> m_font;
    std::unique_ptr<Renderer2D> m_renderer;
    bool m_statsVisible = false;
    bool m_profileStreamEnabled = false;
    std::optional<std::uint64_t> m_lastStreamedFrame;
    std::unique_ptr<Texture> m_screenshotTexture;
    std::filesystem::path m_screenshotPath;
    // The screenshot's frame was submitted, so it can be read back.
    bool m_screenshotDrawn = false;
};

} // namespace Spot
