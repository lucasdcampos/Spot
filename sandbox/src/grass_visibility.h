#pragma once

#include <spot.h>

#include <vector>

namespace GrassChunks {

// Keeps the original triangles and shading, grouping nearby geometry into independently culled meshes.
std::vector<Spot::MeshData> Partition(const Spot::MeshData& source, float cellSize);

} // namespace GrassChunks

// Splits an authored grass renderer once, then submits only chunks near the player to all rendering passes.
class GrassVisibility final : public Spot::Component {
public:
    Spot::EntityId player = Spot::EntityId::Invalid;
    float drawDistance = 25.0f;
    float cellSize = 8.0f;

protected:
    void OnLateUpdate(float deltaTime) override;
    void OnDisable() override;
    void Serialize(Spot::Archive& archive) override;

private:
    struct Chunk {
        Spot::EntityId entity;
        Spot::BoundingSphere bounds;
        bool visible = false;
    };

    bool BuildChunks();

    std::vector<Chunk> m_chunks;
    bool m_built = false;
};
