// The engine as an application made in code: Application runs the loop, the modules bring the systems, and the
// scene is built in OnStart() from components, with no project, no assets to import and no game.json.
#include <spot/assets.h>
#include <spot/modules.h>
#include <spot/render.h>
#include <spot/runtime.h>
#include <spot/scene.h>

#include <cstdlib>
#include <memory>
#include <string_view>

namespace {

class Spinner final : public Spot::Component {
public:
    float speed = 1.0f;

protected:
    void OnUpdate(float deltaTime) override {
        GetTransform().Rotate(Spot::Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, speed * deltaTime));
    }
};

class Example final : public Spot::Application {
public:
    explicit Example(int frames)
        : Application({.window = {.title = "Application", .width = 640, .height = 360}}), m_frames(frames) {
        AddModule<Spot::RenderModule>();
    }

protected:
    void OnStart() override {
        Spot::RegisterComponent<Spinner>("Spinner");
        auto scene = std::make_unique<Spot::Scene>();

        auto& camera = scene->CreateEntity("Camera");
        camera.GetTransform().SetPosition({0.0f, 1.5f, 4.0f});
        camera.GetTransform().SetRotation(Spot::Quaternion::LookRotation({0.0f, -1.5f, -4.0f}));
        camera.AddComponent<Spot::Camera>();

        auto& sun = scene->CreateEntity("Sun");
        sun.GetTransform().SetRotation(Spot::Quaternion::LookRotation({-0.5f, -1.0f, -0.3f}));
        sun.AddComponent<Spot::DirectionalLight>(Spot::Color::White, 3.0f);
        scene->CreateEntity("Sky").AddComponent<Spot::Sky>();

        auto& cube = scene->CreateEntity("Cube");
        cube.AddComponent<Spot::MeshRenderer>(Spot::Assets::Load<Spot::Mesh>(Spot::BuiltInAssets::CubeMesh),
                                              Spot::Material{.baseColor = Spot::Color{0.9f, 0.4f, 0.1f, 1.0f}});
        cube.AddComponent<Spinner>().speed = 1.5f;

        LoadScene(std::move(scene));
    }

    void OnUpdate(float) override {
        if (m_frames > 0 && --m_frames == 0) {
            Quit();
        }
    }

private:
    int m_frames;
};

} // namespace

int main(int argc, char** argv) {
    // The tests run the examples for a few frames: "--frames <count>".
    const int frames = argc == 3 && std::string_view(argv[1]) == "--frames" ? std::atoi(argv[2]) : 0;
    Example example(frames);
    return example.Run();
}
