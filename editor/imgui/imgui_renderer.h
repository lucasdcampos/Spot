#pragma once

#include <spot.h>

#include <imgui.h>

#include <memory>
#include <vector>

namespace Spot::Editor {

// The ImTextureID that shows a texture in ImGui, such as with ImGui::Image(). sRGB textures, such as scenes rendered
// into sRGB targets, are shown as they look in the window.
inline ImTextureID ToImTextureId(const Texture& texture) {
    return reinterpret_cast<ImTextureID>(&texture);
}

// Draws the current ImGui context's frames with the engine's graphics API, and owns the textures ImGui asks for,
// such as its font atlas.
class ImGuiRenderer {
public:
    // targetFormat is the format of the passes the frames are drawn in.
    ImGuiRenderer(GraphicsDevice& device, TextureFormat targetFormat);
    ~ImGuiRenderer();

    ImGuiRenderer(const ImGuiRenderer&) = delete;
    ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;

    // False, after logging why, when the pipeline could not be created.
    bool IsValid() const { return m_pipeline != nullptr; }

    // Draws the frame into the pass, whose targets must be the size of the frame's display in pixels.
    void Render(RenderPass& pass, const ImDrawData& drawData);

private:
    void UpdateTexture(ImTextureData& texture);
    void DestroyTexture(ImTextureData& texture);
    void UploadGeometry(const ImDrawData& drawData);
    void SetUpRenderState(RenderPass& pass, const ImDrawData& drawData);

    GraphicsDevice& m_device;
    std::unique_ptr<Pipeline> m_pipeline;
    std::unique_ptr<Sampler> m_linearSampler;
    std::unique_ptr<Sampler> m_nearestSampler;
    const Sampler* m_currentSampler = nullptr;
    std::unique_ptr<Buffer> m_vertexBuffer;
    std::unique_ptr<Buffer> m_indexBuffer;
    std::vector<ImDrawVert> m_vertices;
    std::vector<ImDrawIdx> m_indices;
};

} // namespace Spot::Editor
