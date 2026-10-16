// The scene benchmark: how much a large scene costs the CPU by itself, without rendering. It builds a scene of 100,000
// entities, a fifth of them with a component that updates every frame and the rest with components that never do,
// and each frame updates the scene, spawns and destroys entities, and runs the queries games make. Run its Release
// build when the scene changes, and compare with the times recorded in its commit messages:
//
//     spot_scene_benchmark [--frames <count>] [--entities <count>]
#include <spot.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <numeric>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

class Spinner final : public Spot::Component {
protected:
    void OnUpdate(float deltaTime) override {
        GetEntity().GetTransform().Rotate(Spot::Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, deltaTime));
    }
};

// Data a system reads, with no hooks of its own, as most components of a large scene are.
class Health final : public Spot::Component {
public:
    float value = 100.0f;
};

class Loot final : public Spot::Component {};

class Marker final : public Spot::Component {};

struct Timings {
    std::vector<double> update;
    std::vector<double> churn;
    std::vector<double> queries;
};

double Milliseconds(Clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
}

void Print(std::string_view name, std::vector<double> values) {
    std::ranges::sort(values);
    const double mean = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
    std::printf("%-8.*s mean %7.3f ms   median %7.3f ms   p95 %7.3f ms\n", static_cast<int>(name.size()), name.data(),
                mean, values[values.size() / 2], values[values.size() * 95 / 100]);
}

} // namespace

int main(int argc, char** argv) {
    int frames = 300;
    int entityCount = 100'000;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string_view option = argv[i];
        if (option == "--frames") {
            frames = std::atoi(argv[i + 1]);
        } else if (option == "--entities") {
            entityCount = std::atoi(argv[i + 1]);
        }
    }

    Spot::Scene scene;
    std::vector<Spot::EntityId> ids;
    ids.reserve(static_cast<std::size_t>(entityCount));
    const auto build = Clock::now();
    for (int i = 0; i < entityCount; ++i) {
        Spot::Entity& entity = scene.CreateEntity();
        entity.GetTransform().SetPosition({static_cast<float>(i % 300), 0.0f, static_cast<float>(i / 300)});
        entity.AddComponent<Health>();
        if (i % 5 == 0) {
            entity.AddComponent<Spinner>();
        }
        if (i % 10 == 0) {
            entity.CreateChild().AddComponent<Health>();
        }
        ids.push_back(entity.GetId());
    }
    scene.CreateEntity("Rare").AddComponent<Marker>();
    std::printf("Built %zu entities in %.1f ms\n", scene.GetEntityCount(), Milliseconds(Clock::now() - build));

    std::deque<Spot::EntityId> spawned;
    Timings timings;
    std::size_t checksum = 0;
    for (int frame = 0; frame < frames; ++frame) {
        auto start = Clock::now();
        scene.Update(1.0f / 60.0f);
        timings.update.push_back(Milliseconds(Clock::now() - start));

        // A hundred spawns and despawns a frame, as projectiles, effects and pickups come and go.
        start = Clock::now();
        for (int i = 0; i < 100; ++i) {
            Spot::Entity& entity = scene.CreateEntity();
            entity.AddComponent<Health>();
            entity.AddComponent<Loot>();
            spawned.push_back(entity.GetId());
        }
        while (spawned.size() > 1000) {
            if (Spot::Entity* entity = scene.GetEntity(spawned.front())) {
                entity->Destroy();
            }
            spawned.pop_front();
        }
        timings.churn.push_back(Milliseconds(Clock::now() - start));

        // What gameplay and rendering ask each frame: every component of a type, components of given entities, and
        // the one component a scene has of a rare type.
        start = Clock::now();
        scene.ForEachComponent<Health>([&](Health& health) { checksum += health.value > 0.0f ? 1 : 0; });
        scene.ForEachComponent<Loot>([&](Loot&) { ++checksum; });
        for (std::size_t i = 0; i < 1000; ++i) {
            const Spot::Entity* entity = scene.GetEntity(ids[(i * 97 + static_cast<std::size_t>(frame)) % ids.size()]);
            checksum += entity && entity->GetComponent<Spinner>() ? 1 : 0;
        }
        checksum += scene.FindComponent<Marker>() ? 1 : 0;
        timings.queries.push_back(Milliseconds(Clock::now() - start));
    }

    std::printf("%d frames, %zu entities at the end (checksum %zu)\n", frames, scene.GetEntityCount(), checksum);
    Print("Update", timings.update);
    Print("Churn", timings.churn);
    Print("Queries", timings.queries);
    std::vector<double> total(timings.update.size());
    for (std::size_t i = 0; i < total.size(); ++i) {
        total[i] = timings.update[i] + timings.churn[i] + timings.queries[i];
    }
    Print("Frame", total);
    return EXIT_SUCCESS;
}
