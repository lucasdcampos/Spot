#pragma once

#include <spot.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Spot::Editor {

class EditorProject;

// Small pictures of a project's assets, as the asset browser shows them: textures as they are, materials on a
// sphere, and meshes and models seen from above and to the side. Pictures are drawn a few per frame, when asked for,
// and again when the assets change; the old one shows meanwhile.
class ThumbnailCache {
public:
    // The size of the pictures drawn, in pixels.
    static constexpr std::uint32_t Size = 128;
    static constexpr std::size_t DrawnPerFrame = 4;

    explicit ThumbnailCache(GraphicsDevice& device);
    ~ThumbnailCache();

    ThumbnailCache(const ThumbnailCache&) = delete;
    ThumbnailCache& operator=(const ThumbnailCache&) = delete;

    // The picture of the asset; null until it is drawn, and for types that have none, such as scenes. Asking for a
    // picture that is missing or out of date queues it to be drawn.
    const Texture* Get(EditorProject& project, AssetId id, AssetType type);
    // Draws pictures asked for, up to DrawnPerFrame.
    void Render(RenderContext& context, EditorProject& project);
    // Forgets every picture, as when the project closes, since they hold on to its assets.
    void Clear();

private:
    struct Entry {
        AssetType type = AssetType::Texture;
        std::unique_ptr<Texture> picture;
        // Textures show themselves.
        AssetRef<Texture> texture;
        std::uint64_t version = 0;
        bool queued = false;
    };

    // Draws the meshes with their materials, framed, into the entry's picture.
    void Draw(RenderContext& context, Entry& entry, const std::vector<std::pair<const Mesh*, const Material*>>& parts);

    GraphicsDevice& m_device;
    std::unique_ptr<Renderer3D> m_renderer;
    std::unique_ptr<Texture> m_samples;
    std::unique_ptr<Texture> m_depth;
    std::uint32_t m_sampleCount = 1;
    std::unordered_map<AssetId, Entry> m_entries;
    std::vector<AssetId> m_queue;
};

} // namespace Spot::Editor
