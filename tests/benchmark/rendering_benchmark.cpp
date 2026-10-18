// The rendering benchmark: a scene of 10,000 meshes and 200 lights, under the sky and its shadowed sun, seen by a
// camera circling it, drawn as games draw scenes, without vsync. After warming up it times a fixed number of frames
// and prints how long they took. Run its Release build when rendering changes, and compare with the times in
// ROADMAP.md's M7 notes:
//
//     spot_benchmark [--frames <count>] [--warmup <count>] [--grid <meshes on a side, 100 by default>]
//
// It can time a project's scene instead, from its imported assets, with lamps added over it, as many as asked:
//
//     spot_benchmark --scene <scene file> --assets <imported folder> [--lamps <count>]
#include <spot.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <numeric>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr float Spacing = 2.0f;
constexpr int LightCount = 200;

struct Options {
    int frames = 600;
    int warmup = 120;
    std::filesystem::path scene;
    std::filesystem::path assets;
    int lamps = 48;
    // The meshes are a grid this many on a side.
    int grid = 100;
    // Spheres turn into coarser ones far away.
    bool lods = false;
};

class Benchmark final : public Spot::Application {
public:
    explicit Benchmark(Options options)
        : Application({.window = {.title = "Spot Rendering Benchmark", .width = 1600, .height = 900},
                       .graphics = {.vsync = false},
                       .assets = {.importDirectory = options.assets}}),
          m_frames(options.frames), m_warmup(options.warmup), m_options(std::move(options)) {}

    int GetResult() const { return m_result; }

protected:
    void OnStart() override {
        if (!m_options.scene.empty()) {
            StartWithScene();
            return;
        }
        auto& device = GetGraphicsDevice();
        auto& scene = LoadScene<Spot::Scene>();
        const Spot::AssetRef<Spot::Mesh> cube = Spot::Mesh::Create(device, Spot::MeshData::CreateCube());
        const Spot::AssetRef<Spot::Mesh> sphere = Spot::Mesh::Create(device, Spot::MeshData::CreateSphere(0.5f));
        const Spot::AssetRef<Spot::Mesh> floor = Spot::Mesh::Create(device, Spot::MeshData::CreatePlane(1.0f));
        // Spheres look as round from afar with far fewer triangles.
        const Spot::AssetRef<Spot::Mesh> farSphere =
            Spot::Mesh::Create(device, Spot::MeshData::CreateSphere(0.5f, 12, 6));
        if (!cube || !sphere || !floor || !farSphere) {
            Quit();
            return;
        }
        // Eight materials, from rough dielectrics to polished metals, shared as assets are.
        std::vector<Spot::AssetRef<Spot::Material>> materials;
        for (int i = 0; i < 8; ++i) {
            const float hue = static_cast<float>(i) / 8.0f;
            materials.push_back(std::make_shared<const Spot::Material>(
                Spot::Material{.baseColor = {0.3f + 0.6f * hue, 0.8f - 0.5f * hue, 0.4f + 0.3f * (i % 2), 1.0f},
                               .metallic = i >= 6 ? 1.0f : 0.0f,
                               .roughness = 0.2f + 0.1f * static_cast<float>(i)}));
        }
        const int GridSize = m_options.grid;
        m_meshCount = GridSize * GridSize;
        auto& ground = scene.CreateEntity("Ground");
        ground.GetTransform().SetScale({GridSize * Spacing * 1.2f, 1.0f, GridSize * Spacing * 1.2f});
        ground.AddComponent<Spot::MeshRenderer>(floor, Spot::Material{.baseColor = {0.4f, 0.4f, 0.42f, 1.0f}});

        for (int z = 0; z < GridSize; ++z) {
            for (int x = 0; x < GridSize; ++x) {
                auto& entity = scene.CreateEntity("Mesh");
                entity.GetTransform().SetPosition({(static_cast<float>(x) - GridSize * 0.5f) * Spacing, 0.5f,
                                                   (static_cast<float>(z) - GridSize * 0.5f) * Spacing});
                auto& renderer = entity.AddComponent<Spot::MeshRenderer>(
                    (x + z) % 2 == 0 ? cube : sphere, materials[static_cast<std::size_t>((x * 7 + z * 3) % 8)]);
                if (m_options.lods && (x + z) % 2 != 0) {
                    renderer.SetLods({{farSphere, 0.04f}});
                }
            }
        }

        // Lights over the grid, the first few casting shadows, as many as the shadow tiles hold.
        std::mt19937 random(42);
        std::uniform_real_distribution<float> across(-GridSize * Spacing * 0.5f, GridSize * Spacing * 0.5f);
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        for (int i = 0; i < LightCount; ++i) {
            auto& entity = scene.CreateEntity("Light");
            entity.GetTransform().SetPosition({across(random), 2.0f + unit(random) * 2.0f, across(random)});
            const Spot::Color color{0.5f + 0.5f * unit(random), 0.5f + 0.5f * unit(random), 0.5f + 0.5f * unit(random),
                                    1.0f};
            if (i % 50 == 0) {
                entity.GetTransform().SetRotation(Spot::Quaternion::LookRotation({0.0f, -1.0f, 0.2f}));
                entity.AddComponent<Spot::SpotLight>(color, 60.0f, 14.0f).SetCastingShadows(true);
            } else {
                entity.AddComponent<Spot::PointLight>(color, 20.0f, 7.0f).SetCastingShadows(i < 4);
            }
        }

        auto& sun = scene.CreateEntity("Sun");
        sun.GetTransform().SetRotation(Spot::Quaternion::LookRotation({-0.4f, -0.8f, -0.45f}));
        sun.AddComponent<Spot::DirectionalLight>(Spot::Color::White, 2.0f);
        scene.CreateEntity("Sky").AddComponent<Spot::Sky>();
        auto& camera = scene.CreateEntity("Camera");
        camera.AddComponent<Spot::Camera>().SetFarPlane(400.0f);
        m_camera = camera.GetId();
    }

    // The scene, its own cameras off, with lamps spread over the middle of it and a camera circling above.
    void StartWithScene() {
        const auto loadStart = std::chrono::steady_clock::now();
        auto scene = std::make_unique<Spot::Scene>();
        if (!scene->LoadFromFile(m_options.scene, &GetAssets())) {
            Quit();
            return;
        }
        std::printf("Loaded the scene in %.1f ms\n",
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - loadStart).count());
        scene->ForEachComponent<Spot::Camera>([](Spot::Camera& camera) { camera.SetEnabled(false); });
        std::mt19937 random(7);
        std::uniform_real_distribution<float> across(-18.0f, 18.0f);
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        for (int i = 0; i < m_options.lamps; ++i) {
            auto& lamp = scene->CreateEntity("Lamp");
            lamp.GetTransform().SetPosition({across(random), 1.5f + unit(random) * 2.0f, across(random)});
            const Spot::Color color{0.6f + 0.4f * unit(random), 0.6f + 0.4f * unit(random), 0.6f + 0.4f * unit(random),
                                    1.0f};
            // A quarter are spots shining down, and every eighth lamp casts shadows, as players mix them.
            if (i % 4 == 0) {
                lamp.GetTransform().SetRotation(Spot::Quaternion::LookRotation({0.0f, -1.0f, 0.1f}));
                lamp.AddComponent<Spot::SpotLight>(color, 25.0f, 12.0f).SetCastingShadows(i % 8 == 0);
            } else {
                lamp.AddComponent<Spot::PointLight>(color, 6.0f, 7.0f).SetCastingShadows(i % 8 == 1);
            }
        }
        auto& camera = scene->CreateEntity("Benchmark Camera");
        camera.AddComponent<Spot::Camera>();
        m_camera = camera.GetId();
        m_orbitRadius = 28.0f;
        m_orbitHeight = 12.0f;
        m_lightCount = m_options.lamps;
        m_meshCount = 0;
        scene->ForEachComponent<Spot::MeshRenderer>([this](const Spot::MeshRenderer&) { ++m_meshCount; });
        LoadScene(std::move(scene));
    }

    void OnUpdate(float /*deltaTime*/) override {
        Spot::Scene* scene = GetScene();
        Spot::Entity* camera = scene ? scene->GetEntity(m_camera) : nullptr;
        if (!camera) {
            return;
        }
        // Circling the grid by frame, not by time, so every run sees the same frames.
        const float angle = static_cast<float>(m_frame) * 0.003f;
        const Spot::Vector3 position{std::sin(angle) * m_orbitRadius, m_orbitHeight, std::cos(angle) * m_orbitRadius};
        camera->GetTransform().SetPosition(position);
        camera->GetTransform().SetRotation(Spot::Quaternion::LookRotation(Spot::Vector3{} - position));

        const auto now = std::chrono::steady_clock::now();
        if (m_frame > m_warmup) {
            m_times.push_back(std::chrono::duration<double, std::milli>(now - m_last).count());
        }
        m_last = now;
        if (++m_frame > m_warmup + m_frames) {
            Report();
            Quit();
        }
    }

private:
    void Report() {
        std::vector<double> sorted = m_times;
        std::ranges::sort(sorted);
        const double mean = std::accumulate(sorted.begin(), sorted.end(), 0.0) / static_cast<double>(sorted.size());
        const auto at = [&](double share) {
            return sorted[std::min(sorted.size() - 1,
                                   static_cast<std::size_t>(share * static_cast<double>(sorted.size())))];
        };
        const auto frames = Spot::Profiler::GetFrames();
        const auto counter = [&](std::string_view name) {
            return frames.empty() ? 0.0 : Spot::Profiler::GetCounter(*frames.back(), name);
        };
        std::printf("Rendering benchmark: %d meshes, %d lights, %zu frames at 1600x900 on %s\n", m_meshCount,
                    m_lightCount, sorted.size(), std::string(GetGraphicsDevice().GetBackendName()).c_str());
        std::printf("  frame time: mean %.2f ms (%.0f FPS), median %.2f ms, 95th percentile %.2f ms, worst %.2f ms\n",
                    mean, 1000.0 / mean, at(0.5), at(0.95), sorted.back());
        std::printf("  last frame: %.0f visible meshes, %.0f culled, %.0f draw calls, %.0f shadow draw calls, %.0f "
                    "lights\n",
                    counter("Visible meshes"), counter("Culled meshes"), counter("Draw calls"),
                    counter("Shadow draw calls"), counter("Lights"));
        // The GPU's time, part by part, as the renderers time it.
        if (!frames.empty()) {
            std::printf("  GPU, last frame:");
            for (const Spot::ProfileCounter& gpu : frames.back()->counters) {
                if (gpu.name.starts_with("GPU ")) {
                    std::printf(" %.*s %.2f;", static_cast<int>(gpu.name.size() - 4), gpu.name.data() + 4, gpu.value);
                }
            }
            std::puts("");
        }
        // Where the time goes, from the profiler's scopes over the frames it kept, the costliest first.
        std::map<std::string_view, double> totals;
        for (const Spot::ProfileFrame* frame : frames) {
            for (const Spot::ProfileSample& sample : frame->samples) {
                {
                    totals[sample.name] += std::chrono::duration<double, std::milli>(sample.duration).count();
                }
            }
        }
        std::vector<std::pair<std::string_view, double>> scopes(totals.begin(), totals.end());
        std::ranges::sort(scopes, [](const auto& a, const auto& b) { return a.second > b.second; });
        std::printf("  costliest scopes, per frame:\n");
        const double frameCount = static_cast<double>(std::max<std::size_t>(frames.size(), 1));
        for (std::size_t i = 0; i < std::min<std::size_t>(scopes.size(), 16); ++i) {
            const auto& [name, total] = scopes[i];
            std::printf("    %-40.*s %6.2f ms\n", static_cast<int>(name.size()), name.data(), total / frameCount);
        }
        m_result = EXIT_SUCCESS;
    }

    int m_frames;
    int m_warmup;
    Options m_options;
    float m_orbitRadius = 90.0f;
    float m_orbitHeight = 30.0f;
    int m_meshCount = 0;
    int m_lightCount = LightCount;
    int m_frame = 0;
    int m_result = EXIT_FAILURE;
    Spot::EntityId m_camera = Spot::EntityId::Invalid;
    std::chrono::steady_clock::time_point m_last = std::chrono::steady_clock::now();
    std::vector<double> m_times;
};

} // namespace

int main(int argc, char** argv) {
    Options options;
    for (int i = 1; i + 1 < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument == "--frames") {
            options.frames = std::max(std::atoi(argv[++i]), 1);
        } else if (argument == "--warmup") {
            options.warmup = std::max(std::atoi(argv[++i]), 0);
        } else if (argument == "--scene") {
            options.scene = argv[++i];
        } else if (argument == "--assets") {
            options.assets = argv[++i];
        } else if (argument == "--lods") {
            options.lods = std::atoi(argv[++i]) != 0;
        } else if (argument == "--grid") {
            options.grid = std::max(std::atoi(argv[++i]), 1);
        } else if (argument == "--lamps") {
            options.lamps = std::max(std::atoi(argv[++i]), 0);
        }
    }
    Benchmark benchmark(std::move(options));
    if (benchmark.Run() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    return benchmark.GetResult();
}
