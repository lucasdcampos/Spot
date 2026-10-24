#include "interface_settings.h"
#include "player_movement.h"
#include "prop_spawner.h"
#include "runtime_interface.h"
#include "spot/scene/physics/physics_world.h"
#include "view_model.h"

#include <spot.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <map>
#include <numeric>
#include <string_view>
#include <vector>

namespace {

struct Options {
    int props = 500;
    int lamps = 50;
    int warmup = 240;
    int frames = 600;
    int quality = 2;
    // Walks the player through the level, seen through its own eyes with what it holds, as a game plays.
    int walk = 0;
};

Spot::ApplicationConfig Config() {
    const std::filesystem::path imported(SANDBOX_IMPORTED_DIR);
    return {.game = Spot::GameSettings::LoadFromFile(imported.parent_path() / Spot::GameSettings::FileName),
            .window = {.title = "Spot Sandbox Benchmark", .width = 1600, .height = 900},
            .graphics = {.vsync = false, .debug = false},
            .assets = {.importDirectory = imported},
            .logFile = std::filesystem::path(SPOT_SANDBOX_BENCHMARK_DATA_DIR) / "benchmark.log",
            .userDataDirectory = std::filesystem::path(SPOT_SANDBOX_BENCHMARK_DATA_DIR) / "user"};
}

class Benchmark final : public Spot::Application {
public:
    explicit Benchmark(Options options) : Application(Config()), m_options(options) {
        m_times.reserve(static_cast<std::size_t>(options.frames));
    }
    int GetResult() const { return m_result; }

protected:
    void OnStart() override {
        Spot::Profiler::SetEnabled(true);
        const auto start = std::chrono::steady_clock::now();
        const auto* game = GetGameSettings();
        auto scene = game ? Spot::InstantiateScene(GetAssets(), game->startupScene) : nullptr;
        m_sceneLoadMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (!scene) {
            Quit();
            return;
        }
        LoadScene(std::move(scene));
    }

    void OnUpdate(float) override {
        auto* scene = GetScene();
        if (!scene) {
            Quit();
            return;
        }
        if (!m_ready) {
            if (++m_frame < 3) {
                return;
            }
            if (!Populate(*scene)) {
                Quit();
                return;
            }
            m_ready = true;
            m_frame = 0;
            m_last = std::chrono::steady_clock::now();
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (m_frame >= m_options.warmup) {
            m_times.push_back(std::chrono::duration<double, std::milli>(now - m_last).count());
            const auto frames = Spot::Profiler::GetFrames();
            if (!frames.empty()) {
                double scriptMs = 0;
                for (const auto& sample : frames.back()->samples) {
                    m_scopeTotals[sample.name] += std::chrono::duration<double, std::milli>(sample.duration).count();
                    if (sample.name.ends_with(".OnUpdate")) {
                        scriptMs += std::chrono::duration<double, std::milli>(sample.duration).count();
                        ++m_scriptCalls;
                    }
                }
                m_scriptTimes.push_back(scriptMs);
                m_droppedSamples += frames.back()->droppedSamples;
            }
        }
        m_last = now;
        if (m_options.walk) {
            Walk(*scene);
        } else {
            Orbit(*scene);
        }
        if (++m_frame >= m_options.warmup + m_options.frames) {
            Report(*scene);
            Quit();
        }
    }

private:
    void Orbit(Spot::Scene& scene) {
        auto* camera = scene.GetEntity(m_camera);
        const float angle = static_cast<float>(m_frame) * .003f;
        const Spot::Vector3 position{std::sin(angle) * 23, 9, std::cos(angle) * 23};
        camera->GetTransform().SetWorldPosition(position);
        camera->GetTransform().SetWorldRotation(Spot::Quaternion::LookRotation(Spot::Vector3{0, 1, 0} - position));
    }

    // From the plaza's south edge north into the workshop, again and again, holding Forward.
    void Walk(Spot::Scene& scene) {
        auto* movement = scene.FindComponent<PlayerMovement>();
        if (!movement) {
            return;
        }
        if (m_frame % 600 == 0) {
            movement->GetEntity().GetTransform().SetWorldPosition({19.0f, 1.0f, 16.0f});
        }
        if (m_frame == 0) {
            if (auto* input = Spot::Input::GetState()) {
                input->OnEvent(Spot::KeyPressedEvent{.key = Spot::Key::W, .repeat = false});
            }
        }
    }

    bool Populate(Spot::Scene& scene) {
        auto* spawner = scene.FindComponent<PropSpawner>();
        auto* ui = scene.FindComponent<RuntimeInterface>();
        if (!spawner || !ui) {
            return false;
        }
        auto settings = ui->GetSettings();
        settings.graphicsQuality = m_options.quality;
        settings.Apply(scene);
        const auto entries = spawner->GetSpawnables();
        const auto crate = std::ranges::find_if(entries, [](const auto& entry) { return entry.name == "Crate"; });
        if (crate == entries.end()) {
            return false;
        }
        const auto index = static_cast<std::size_t>(crate - entries.begin());
        const int side = std::max(1, static_cast<int>(std::ceil(std::cbrt(static_cast<float>(m_options.props)))));
        for (int i = 0; i < m_options.props; ++i) {
            const auto start = std::chrono::steady_clock::now();
            auto* prop = spawner->Spawn(index);
            m_spawnTimes.push_back(
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
            if (!prop) {
                return false;
            }
            prop->GetTransform().SetWorldPosition(
                {(static_cast<float>(i % side) - static_cast<float>(side - 1) * .5f) * 1.1f,
                 .6f + static_cast<float>(i / (side * side)) * 1.1f,
                 (static_cast<float>((i / side) % side) - static_cast<float>(side - 1) * .5f) * 1.1f});
        }
        for (int i = 0; i < m_options.lamps; ++i) {
            auto& lamp = scene.CreateEntity("Benchmark Lamp");
            const float x = (static_cast<float>(i % 10) - 4.5f) * 3;
            const float z = (static_cast<float>(i / 10) - 2) * 5;
            lamp.GetTransform().SetWorldPosition({x, 3, z});
            lamp.AddComponent<Spot::PointLight>(Spot::Color{.8f, .9f, 1, 1}, 6.0f, 7.0f).SetCastingShadows(i % 8 == 0);
        }
        if (!m_options.walk) {
            scene.ForEachComponent<Spot::Camera>([](Spot::Camera& camera) { camera.SetEnabled(false); });
            scene.ForEachComponent<PlayerMovement>([](PlayerMovement& movement) { movement.SetFlying(true); });
            scene.ForEachComponent<ViewModel>([](ViewModel& view) { view.SetEnabled(false); });
            auto& camera = scene.CreateEntity("Benchmark Camera");
            camera.AddComponent<Spot::Camera>();
            m_camera = camera.GetId();
        }
        Spot::Input::SetCursorLocked(false);
        Spot::Profiler::Clear();
        return true;
    }

    void Report(Spot::Scene& scene) {
        std::ranges::sort(m_times);
        const double mean = std::accumulate(m_times.begin(), m_times.end(), 0.0) / static_cast<double>(m_times.size());
        const auto percentile = [&](double share) {
            return m_times[std::min(m_times.size() - 1,
                                    static_cast<std::size_t>(share * static_cast<double>(m_times.size())))];
        };
        std::printf("Sandbox benchmark: %d spawned props, %d added lamps, %zu frames, quality %d, 1600x900, %s%s\n",
                    m_options.props, m_options.lamps, m_times.size(), m_options.quality,
                    std::string(GetGraphicsDevice().GetBackendName()).c_str(), m_options.walk ? ", walking" : "");
        std::printf("Frame: mean %.3f ms (%.1f FPS), median %.3f ms, p95 %.3f ms, worst %.3f ms\n", mean, 1000 / mean,
                    percentile(.5), percentile(.95), m_times.back());
        std::printf("Bodies: %zu, resident memory: %.1f MiB\n",
                    scene.GetOrAddSystem<Spot::PhysicsWorld>().GetBodyCount(),
                    static_cast<double>(GetRuntimeStats().memoryBytes) / (1024 * 1024));
        std::printf("Scene load: %.3f ms (startup InstantiateScene, assets initially uncached)\n", m_sceneLoadMs);
        const auto print = [](const char* label, std::vector<double> values) {
            if (values.empty()) {
                std::printf("%s: no samples\n", label);
                return;
            }
            std::ranges::sort(values);
            std::printf("%s: %zu samples, mean %.6f ms, median %.6f ms, p95 %.6f ms\n", label, values.size(),
                        std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size()),
                        values[values.size() / 2], values[values.size() * 95 / 100]);
        };
        // The first spawn may load the prefab; report it separately from the sorted distribution.
        if (!m_spawnTimes.empty()) {
            std::printf("First prefab spawn: %.6f ms\n", m_spawnTimes.front());
        }
        print("Prefab spawn (PropSpawner::Spawn, includes raycast and undo)", m_spawnTimes);
        print("Script OnUpdate (sum per measured frame)", m_scriptTimes);
        std::printf("Script OnUpdate calls: %zu, dropped profiler samples: %zu\n", m_scriptCalls, m_droppedSamples);
        std::vector<std::pair<std::string_view, double>> scopes(m_scopeTotals.begin(), m_scopeTotals.end());
        std::ranges::sort(scopes, [](const auto& a, const auto& b) { return a.second > b.second; });
        for (std::size_t i = 0; i < std::min<std::size_t>(scopes.size(), 40); ++i) {
            const auto& [name, total] = scopes[i];
            std::printf("%-40.*s %.3f ms\n", static_cast<int>(name.size()), name.data(),
                        total / static_cast<double>(std::max<std::size_t>(m_times.size(), 1)));
        }
        m_result = EXIT_SUCCESS;
    }

    Options m_options;
    int m_frame = 0;
    bool m_ready = false;
    int m_result = EXIT_FAILURE;
    Spot::EntityId m_camera = Spot::EntityId::Invalid;
    std::chrono::steady_clock::time_point m_last;
    std::vector<double> m_times;
    double m_sceneLoadMs = 0;
    std::vector<double> m_spawnTimes;
    std::vector<double> m_scriptTimes;
    std::map<std::string_view, double> m_scopeTotals;
    std::size_t m_scriptCalls = 0;
    std::size_t m_droppedSamples = 0;
};

} // namespace

int main(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc) {
            std::fprintf(stderr, "Expected a value after %s\n", argv[i]);
            return EXIT_FAILURE;
        }
        const std::string_view flag(argv[i]);
        int* value = flag == "--props"     ? &options.props
                     : flag == "--lamps"   ? &options.lamps
                     : flag == "--warmup"  ? &options.warmup
                     : flag == "--frames"  ? &options.frames
                     : flag == "--quality" ? &options.quality
                     : flag == "--walk"    ? &options.walk
                                           : nullptr;
        const std::string_view argument(argv[i + 1]);
        int parsed = 0;
        const auto result = std::from_chars(argument.data(), argument.data() + argument.size(), parsed);
        const int maximum = flag == "--quality"                      ? 2
                            : flag == "--walk"                       ? 1
                            : flag == "--props" || flag == "--lamps" ? 2000
                                                                     : 100000;
        if (!value || result.ec != std::errc{} || result.ptr != argument.data() + argument.size() || parsed < 0 ||
            parsed > maximum || (flag == "--frames" && parsed == 0)) {
            std::fprintf(stderr, "Invalid benchmark option: %s %s\n", argv[i], argv[i + 1]);
            return EXIT_FAILURE;
        }
        *value = parsed;
    }
    Benchmark benchmark(options);
    return benchmark.Run() == EXIT_SUCCESS ? benchmark.GetResult() : EXIT_FAILURE;
}
