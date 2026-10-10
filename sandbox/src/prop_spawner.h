#pragma once

#include <spot.h>

#include <string>
#include <vector>

// A prop the spawner makes, described as data until prefabs (M8) describe props.
struct SpawnableProp {
    enum class Shape { Box, Sphere, Capsule, ConvexHull };

    std::string name = "Prop";
    Spot::AssetRef<Spot::Mesh> mesh;
    Spot::AssetRef<Spot::Material> material;
    Shape shape = Shape::Box;
    // The box's size, the sphere's diameter in X, or the capsule's diameter and height in X and Y.
    Spot::Vector3 size = {1.0f, 1.0f, 1.0f};
    float mass = 10.0f;
    // Blows up when shot; see ExplosiveBarrel.
    bool explosive = false;

    void Serialize(Spot::Archive& archive);
};

// Spawn 1 to Spawn 8 spawn the props of the list, in its order, where the player looks, and Spawn the next one each
// time, each of which the UndoList can take back; Clean Up removes every one.
class PropSpawner final : public Spot::Component {
public:
    std::vector<SpawnableProp> props;
    float reach = 6.0f;

    // Spawns the prop of the list where the player looks; null when there is no such prop.
    Spot::Entity* Spawn(std::size_t index);
    void Cleanup();
    std::size_t GetSpawnedCount() const;

protected:
    void OnUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;

private:
    // What was spawned, oldest first, by id, since props may be destroyed by other means.
    std::vector<Spot::EntityId> m_spawned;
    // The prop Spawn makes next.
    std::size_t m_next = 0;
};
