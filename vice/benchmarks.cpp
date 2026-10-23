#include <vice/vice.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

class Transform final : public Vice::Object {
public:
    Vice::Value Get(std::string_view name) const override {
        if (name == "position")
            return position;
        return Object::Get(name);
    }
    void Set(std::string_view name, const Vice::Value& value) override {
        if (name == "position")
            position = std::get<Vice::Vector3>(value);
        else
            Object::Set(name, value);
    }
    Vice::Value Call(std::string_view name, std::span<const Vice::Value> arguments) override {
        if (name == "Speed" && arguments.empty())
            return 3.0;
        return Object::Call(name, arguments);
    }
    Vice::Vector3 position{};
};

template <typename F>
void Measure(std::string_view name, int iterations, F&& update, int updatesPerIteration = 1) {
    for (int i = 0; i < std::max(1, 1000 / updatesPerIteration); ++i)
        update();
    std::array<double, 5> samples;
    for (auto& sample : samples) {
        const auto begin = std::chrono::steady_clock::now();
        for (int i = 0; i < iterations; ++i)
            update();
        sample = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begin).count() /
                 (static_cast<double>(iterations) * updatesPerIteration);
    }
    std::sort(samples.begin(), samples.end());
    // One thousand updates in milliseconds has the same numeric value as microseconds per update.
    std::cout << name << ',' << samples[2] << ',' << samples[2] << '\n';
}

} // namespace

int main(int argc, char** argv) {
    try {
        int iterations = 100000;
        if (argc > 2)
            throw std::runtime_error("Usage: vice_benchmarks [positive-iteration-count]");
        if (argc == 2) {
            const std::string_view argument(argv[1]);
            const auto result = std::from_chars(argument.data(), argument.data() + argument.size(), iterations);
            if (result.ec != std::errc{} || result.ptr != argument.data() + argument.size() || iterations <= 0)
                throw std::runtime_error("Iteration count must be a positive integer");
        }
        const auto program = Vice::Program::Parse(R"(
            class Actor {
                var position = Vector3(0, 0, 0)
                func Tick(dt) { position.x += 3 * dt; return position.x }
            }
            var actor = Actor()
            func GetActor() { return actor }
            func Arithmetic(dt) {
                var velocity = 3 * dt
                var distance = velocity * 2 + 1
                if distance > 1 { distance += velocity }
                return distance
            }
            func Scale(value, factor) { return value * factor }
            func Calls(dt) { return Scale(Scale(dt, 3), 2) }
            func Host(dt) { transform.position.x += transform.Speed() * dt; return transform.position.x }
        )",
                                                  "benchmark.vice");
        Vice::Instance instance(program);
        auto transform = std::make_shared<Transform>();
        instance.BindValue("transform", Vice::ObjectRef(transform));
        instance.Initialize();
        const auto actor = std::get<Vice::ObjectRef>(instance.Call("GetActor"));
        const std::array<Vice::Value, 1> arguments{1.0 / 60.0};
        double checksum = 0;
        std::cout << std::fixed << std::setprecision(3)
                  << "workload,median_us_per_update,projected_ms_per_1000_updates\n";
        Measure("locals_and_branch", iterations,
                [&] { checksum += std::get<double>(instance.Call("Arithmetic", arguments)); });
        Measure("script_calls", iterations, [&] { checksum += std::get<double>(instance.Call("Calls", arguments)); });
        Measure("component_method", iterations, [&] { checksum += std::get<double>(actor->Call("Tick", arguments)); });
        Measure("native_method_and_writeback", iterations,
                [&] { checksum += std::get<double>(instance.Call("Host", arguments)); });
        Vice::Instance collections(Vice::Program::Parse(R"(
            var positions = [Vector3(0, 0, 0), Vector3(1, 0, 0), Vector3(2, 0, 0)]
            var stats = {"distance": 0}
            func Tick(dt) {
                var sum = 0
                for index, position in positions {
                    positions[index].x += dt
                    sum += position.x
                }
                for index in range(4) { sum += index }
                stats["distance"] = sum
                return stats["distance"]
            }
        )"));
        collections.Initialize();
        Measure("arrays_maps_and_range_iteration", iterations,
                [&] { checksum += std::get<double>(collections.Call("Tick", arguments)); });
        std::vector<std::unique_ptr<Vice::Instance>> instances;
        std::vector<Vice::ObjectRef> actors;
        for (int i = 0; i < 1000; ++i) {
            auto independent = std::make_unique<Vice::Instance>(program);
            independent->Initialize();
            actors.push_back(std::get<Vice::ObjectRef>(independent->Call("GetActor")));
            instances.push_back(std::move(independent));
        }
        Measure(
            "component_methods_1000_instances", std::max(1, iterations / 1000),
            [&] {
                for (const auto& independent : actors)
                    checksum += std::get<double>(independent->Call("Tick", arguments));
            },
            1000);
        if (!std::isfinite(checksum) || checksum <= 0)
            throw std::runtime_error("Invalid benchmark result");
        std::cerr << "Checksum: " << checksum << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
