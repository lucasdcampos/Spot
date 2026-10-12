#pragma once

#include <spot.h>

#include <memory>
#include <vector>

// A bounded amount of public SceneRenderer work per frame, cached for the menu's life.
class MenuThumbnails {
public:
    void Add(Spot::EntityId image, Spot::AssetRef<Spot::Prefab> prefab);
    void Clear();
    void Render(const Spot::RenderStageContext& stage, Spot::Scene& interfaceScene, unsigned int budget = 2);
    std::size_t GetCompletedCount() const;

private:
    struct Entry {
        Spot::EntityId image;
        Spot::AssetRef<Spot::Prefab> prefab;
        std::unique_ptr<Spot::Scene> preview;
        Spot::AssetRef<Spot::Texture> texture;
        bool ready = false;
    };
    std::vector<Entry> m_entries;
    std::unique_ptr<Spot::SceneRenderer> m_renderer;
};
