#include "grass_visibility.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>
#include <utility>

SPOT_COMPONENT(GrassVisibility);

std::vector<Spot::MeshData> GrassChunks::Partition(const Spot::MeshData& source, float cellSize) {
    if (!std::isfinite(cellSize) || cellSize <= 0.0f) {
        return {};
    }
    struct Cell {
        Spot::MeshData mesh;
        std::unordered_map<std::uint32_t, std::uint32_t> vertices;
    };
    std::map<std::pair<int, int>, Cell> cells;
    for (std::size_t i = 0; i + 2 < source.indices.size(); i += 3) {
        const Spot::Vector3 center =
            (source.vertices[source.indices[i]].position + source.vertices[source.indices[i + 1]].position +
             source.vertices[source.indices[i + 2]].position) /
            3.0f;
        Cell& cell = cells[{static_cast<int>(std::floor(center.x / cellSize)),
                            static_cast<int>(std::floor(center.z / cellSize))}];
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const std::uint32_t index = source.indices[i + corner];
            const auto [found, inserted] =
                cell.vertices.try_emplace(index, static_cast<std::uint32_t>(cell.mesh.vertices.size()));
            if (inserted) {
                cell.mesh.vertices.push_back(source.vertices[index]);
            }
            cell.mesh.indices.push_back(found->second);
        }
    }
    std::vector<Spot::MeshData> result;
    result.reserve(cells.size());
    for (auto& [key, cell] : cells) {
        result.push_back(std::move(cell.mesh));
    }
    return result;
}

bool GrassVisibility::BuildChunks() {
    auto* application = Spot::Application::TryGet();
    auto* renderer = GetEntity().GetComponent<Spot::MeshRenderer>();
    if (!application || !application->HasGraphicsDevice() || !renderer || !renderer->IsEnabled()) {
        return false;
    }
    const auto source = Spot::Assets::Load<Spot::MeshData>(renderer->GetMesh().GetId());
    if (!source) {
        return false;
    }
    const auto cells = GrassChunks::Partition(*source, cellSize);
    if (cells.empty()) {
        return false;
    }
    std::vector<Spot::AssetRef<Spot::Mesh>> meshes;
    meshes.reserve(cells.size());
    for (const auto& cell : cells) {
        auto mesh = Spot::Mesh::Create(application->GetGraphicsDevice(), cell);
        if (!mesh) {
            return false;
        }
        meshes.emplace_back(std::move(mesh));
    }
    m_chunks.reserve(meshes.size());
    for (auto& mesh : meshes) {
        Spot::Entity& child = GetEntity().CreateChild("Grass Chunk");
        child.SetLayer(GetEntity().GetLayer());
        child.SetActive(false);
        auto& chunkRenderer = child.AddComponent<Spot::MeshRenderer>(mesh, renderer->GetMaterial());
        chunkRenderer.SetCastingShadows(renderer->IsCastingShadows());
        chunkRenderer.SetTextureSpace(renderer->GetTextureSpace());
        m_chunks.push_back({child.GetId(), mesh->GetBounds(), false});
    }
    renderer->SetEnabled(false);
    m_built = true;
    return true;
}

void GrassVisibility::OnLateUpdate(float /*deltaTime*/) {
    if (!m_built && !BuildChunks()) {
        return;
    }
    const auto* observer = GetScene().GetEntity(player);
    const auto* camera = observer ? nullptr : Spot::FindMainCamera(GetScene());
    const bool hasObserver = observer || camera;
    const Spot::Vector3 position = observer ? observer->GetTransform().GetWorldPosition()
                                   : camera ? camera->GetEntity().GetTransform().GetWorldPosition()
                                            : Spot::Vector3{};
    const Spot::Matrix4& world = GetEntity().GetTransform().GetWorldMatrix();
    const auto axisLength = [&](std::size_t axis) {
        const auto& column = world.columns[axis];
        return Spot::Length(Spot::Vector3{column.x, column.y, column.z});
    };
    const float scale = std::max({axisLength(0), axisLength(1), axisLength(2)});
    for (Chunk& chunk : m_chunks) {
        const auto local = chunk.bounds.center;
        const auto center = world * Spot::Vector4{local.x, local.y, local.z, 1.0f};
        // A little hysteresis avoids repeatedly adding and removing a chunk at the range boundary.
        const float range = std::max(drawDistance, 0.0f) + chunk.bounds.radius * scale + (chunk.visible ? 2.0f : 0.0f);
        const bool visible =
            hasObserver && Spot::LengthSquared(Spot::Vector3{center.x, center.y, center.z} - position) <= range * range;
        if (visible != chunk.visible) {
            if (auto* entity = GetScene().GetEntity(chunk.entity)) {
                entity->SetActive(visible);
            }
            chunk.visible = visible;
        }
    }
}

void GrassVisibility::OnDisable() {
    if (!m_built) {
        return;
    }
    for (const auto& chunk : m_chunks) {
        if (auto* entity = GetScene().GetEntity(chunk.entity)) {
            entity->Destroy();
        }
    }
    m_chunks.clear();
    m_built = false;
    if (auto* renderer = GetEntity().GetComponent<Spot::MeshRenderer>()) {
        renderer->SetEnabled(true);
    }
}

void GrassVisibility::Serialize(Spot::Archive& archive) {
    archive.Field("player", player);
    archive.Field("drawDistance", drawDistance, {.min = 0.0f, .max = 1000.0f});
    archive.Field("cellSize", cellSize, {.min = 1.0f, .max = 64.0f});
}
