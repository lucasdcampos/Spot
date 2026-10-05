#include "imgui/imgui_renderer.h"

#include "imgui.frag.h"
#include "imgui.vert.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Spot::Editor {

namespace {

static_assert(sizeof(ImDrawIdx) == sizeof(std::uint16_t), "The renderer draws 16-bit ImGui indices");

struct FragmentSettings {
    float encodeSrgb = 0.0f;
    float padding[3] = {};
};

ImGuiRenderer& GetRenderer() {
    return *static_cast<ImGuiRenderer*>(ImGui::GetIO().BackendRendererUserData);
}

// Grows buffers to the next power of two, so a frame slightly larger than the last does not recreate them.
std::uint32_t GetGrownSize(std::size_t needed) {
    return std::bit_ceil(static_cast<std::uint32_t>(std::max<std::size_t>(needed, 4096)));
}

} // namespace

ImGuiRenderer::ImGuiRenderer(GraphicsDevice& device, TextureFormat targetFormat) : m_device(device) {
    ImGuiIO& io = ImGui::GetIO();
    IM_ASSERT(io.BackendRendererUserData == nullptr && "A renderer backend is already set");
    io.BackendRendererUserData = this;
    io.BackendRendererName = "spot";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;

    ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
    // The callbacks only identify requests; the draw loop below carries them out.
    platformIo.DrawCallback_ResetRenderState = [](const ImDrawList*, const ImDrawCmd*) {};
    platformIo.DrawCallback_SetSamplerLinear = [](const ImDrawList*, const ImDrawCmd*) {
        GetRenderer().m_currentSampler = GetRenderer().m_linearSampler.get();
    };
    platformIo.DrawCallback_SetSamplerNearest = [](const ImDrawList*, const ImDrawCmd*) {
        GetRenderer().m_currentSampler = GetRenderer().m_nearestSampler.get();
    };

    const auto vertexShader = device.CreateShader(Shaders::ImguiVert);
    const auto fragmentShader = device.CreateShader(Shaders::ImguiFrag);
    m_linearSampler = device.CreateSampler({.filter = TextureFilter::Linear});
    m_nearestSampler = device.CreateSampler({.filter = TextureFilter::Nearest});
    if (!vertexShader || !fragmentShader || !m_linearSampler || !m_nearestSampler) {
        return;
    }
    m_pipeline = device.CreatePipeline({
        .vertexShader = vertexShader.get(),
        .fragmentShader = fragmentShader.get(),
        .vertexBuffers = {{
            .stride = sizeof(ImDrawVert),
            .attributes = {{0, VertexFormat::Float2, offsetof(ImDrawVert, pos)},
                           {1, VertexFormat::Float2, offsetof(ImDrawVert, uv)},
                           {2, VertexFormat::UByte4Normalized, offsetof(ImDrawVert, col)}},
        }},
        .blendMode = BlendMode::Alpha,
        .cullMode = CullMode::None,
        .targetFormat = targetFormat,
    });
}

ImGuiRenderer::~ImGuiRenderer() {
    for (ImTextureData* texture : ImGui::GetPlatformIO().Textures) {
        if (texture->RefCount == 1) {
            DestroyTexture(*texture);
        }
    }
    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererUserData = nullptr;
    io.BackendRendererName = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
    ImGui::GetPlatformIO().ClearRendererHandlers();
}

void ImGuiRenderer::Render(RenderPass& pass, const ImDrawData& drawData) {
    const float framebufferWidth = drawData.DisplaySize.x * drawData.FramebufferScale.x;
    const float framebufferHeight = drawData.DisplaySize.y * drawData.FramebufferScale.y;
    if (!m_pipeline || framebufferWidth <= 0.0f || framebufferHeight <= 0.0f) {
        return;
    }

    if (drawData.Textures) {
        for (ImTextureData* texture : *drawData.Textures) {
            if (texture->Status != ImTextureStatus_OK) {
                UpdateTexture(*texture);
            }
        }
    }
    if (drawData.TotalVtxCount <= 0) {
        return;
    }
    UploadGeometry(drawData);
    SetUpRenderState(pass, drawData);

    const ImVec2 clipOffset = drawData.DisplayPos;
    const ImVec2 clipScale = drawData.FramebufferScale;
    const ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
    std::uint32_t globalIndexOffset = 0;
    std::int32_t globalVertexOffset = 0;
    std::optional<bool> encodingSrgb;
    for (const ImDrawList* list : drawData.CmdLists) {
        for (const ImDrawCmd& command : list->CmdBuffer) {
            if (command.UserCallback) {
                if (command.UserCallback == platformIo.DrawCallback_ResetRenderState) {
                    SetUpRenderState(pass, drawData);
                    encodingSrgb.reset();
                } else {
                    command.UserCallback(list, &command);
                }
                continue;
            }

            // Scissor rectangles must lie inside the target.
            const float minX = std::max((command.ClipRect.x - clipOffset.x) * clipScale.x, 0.0f);
            const float minY = std::max((command.ClipRect.y - clipOffset.y) * clipScale.y, 0.0f);
            const float maxX = std::min((command.ClipRect.z - clipOffset.x) * clipScale.x, framebufferWidth);
            const float maxY = std::min((command.ClipRect.w - clipOffset.y) * clipScale.y, framebufferHeight);
            if (maxX <= minX || maxY <= minY) {
                continue;
            }
            pass.SetScissor({static_cast<std::int32_t>(minX), static_cast<std::int32_t>(minY),
                             static_cast<std::int32_t>(maxX - minX), static_cast<std::int32_t>(maxY - minY)});

            const auto* texture = reinterpret_cast<const Texture*>(command.GetTexID());
            if (!texture) {
                continue;
            }
            const bool encodeSrgb = IsSrgbFormat(texture->GetFormat());
            if (encodingSrgb != encodeSrgb) {
                pass.PushFragmentUniforms(0, FragmentSettings{.encodeSrgb = encodeSrgb ? 1.0f : 0.0f});
                encodingSrgb = encodeSrgb;
            }
            pass.BindFragmentTexture(0, *texture, *m_currentSampler);
            pass.DrawIndexed(command.ElemCount, 1, command.IdxOffset + globalIndexOffset,
                             static_cast<std::int32_t>(command.VtxOffset) + globalVertexOffset);
        }
        globalIndexOffset += static_cast<std::uint32_t>(list->IdxBuffer.Size);
        globalVertexOffset += list->VtxBuffer.Size;
    }

    // Later draws of the pass should not be clipped to the last ImGui rectangle.
    pass.SetScissor({0, 0, static_cast<std::int32_t>(framebufferWidth), static_cast<std::int32_t>(framebufferHeight)});
}

void ImGuiRenderer::UpdateTexture(ImTextureData& texture) {
    if (texture.Status == ImTextureStatus_WantDestroy) {
        // Draws submitted in earlier frames may still use it until it has gone unused for a frame.
        if (texture.UnusedFrames > 0) {
            DestroyTexture(texture);
        }
        return;
    }

    IM_ASSERT(texture.Format == ImTextureFormat_RGBA32);
    if (texture.Status == ImTextureStatus_WantCreate) {
        auto created = m_device.CreateTexture({.width = static_cast<std::uint32_t>(texture.Width),
                                               .height = static_cast<std::uint32_t>(texture.Height),
                                               .format = TextureFormat::Rgba8});
        if (!created) {
            return;
        }
        texture.SetTexID(ToImTextureId(*created));
        texture.BackendUserData = created.release();
    }

    // Updates only ever touch parts of the atlas, but the engine replaces whole textures; atlases change rarely.
    auto* native = static_cast<Texture*>(texture.BackendUserData);
    const auto* pixels = static_cast<const std::byte*>(texture.GetPixels());
    m_device.UploadToTexture(*native, std::span(pixels, static_cast<std::size_t>(texture.GetSizeInBytes())));
    texture.SetStatus(ImTextureStatus_OK);
}

void ImGuiRenderer::DestroyTexture(ImTextureData& texture) {
    delete static_cast<Texture*>(texture.BackendUserData);
    texture.BackendUserData = nullptr;
    texture.SetTexID(ImTextureID_Invalid);
    texture.SetStatus(ImTextureStatus_Destroyed);
}

void ImGuiRenderer::UploadGeometry(const ImDrawData& drawData) {
    m_vertices.clear();
    m_indices.clear();
    m_vertices.reserve(static_cast<std::size_t>(drawData.TotalVtxCount));
    m_indices.reserve(static_cast<std::size_t>(drawData.TotalIdxCount));
    for (const ImDrawList* list : drawData.CmdLists) {
        m_vertices.insert(m_vertices.end(), list->VtxBuffer.begin(), list->VtxBuffer.end());
        m_indices.insert(m_indices.end(), list->IdxBuffer.begin(), list->IdxBuffer.end());
    }

    const std::size_t vertexBytes = m_vertices.size() * sizeof(ImDrawVert);
    const std::size_t indexBytes = m_indices.size() * sizeof(ImDrawIdx);
    if (!m_vertexBuffer || m_vertexBuffer->GetSize() < vertexBytes) {
        m_vertexBuffer = m_device.CreateBuffer({.usage = BufferUsage::Vertex, .size = GetGrownSize(vertexBytes)});
    }
    if (!m_indexBuffer || m_indexBuffer->GetSize() < indexBytes) {
        m_indexBuffer = m_device.CreateBuffer({.usage = BufferUsage::Index, .size = GetGrownSize(indexBytes)});
    }
    m_device.UploadToBuffer(*m_vertexBuffer, m_vertices, 0, UploadMode::Discard);
    m_device.UploadToBuffer(*m_indexBuffer, m_indices, 0, UploadMode::Discard);
}

void ImGuiRenderer::SetUpRenderState(RenderPass& pass, const ImDrawData& drawData) {
    m_currentSampler = m_linearSampler.get();
    pass.BindPipeline(*m_pipeline);
    pass.BindVertexBuffer(0, *m_vertexBuffer);
    pass.BindIndexBuffer(*m_indexBuffer, IndexType::UInt16);
    pass.SetViewport({.width = drawData.DisplaySize.x * drawData.FramebufferScale.x,
                      .height = drawData.DisplaySize.y * drawData.FramebufferScale.y});
    // Maps the display rectangle to clip space, with y going down as it does in ImGui.
    const ImVec2 topLeft = drawData.DisplayPos;
    const ImVec2 bottomRight = {topLeft.x + drawData.DisplaySize.x, topLeft.y + drawData.DisplaySize.y};
    pass.PushVertexUniforms(0, Matrix4::Orthographic(topLeft.x, bottomRight.x, bottomRight.y, topLeft.y, -1.0f, 1.0f));
}

} // namespace Spot::Editor
