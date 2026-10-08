// Tests of what runs in the shared engine library, as the editor and game modules use it.
#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace {

const std::filesystem::path TestModule = SPOT_TEST_MODULE;

constexpr const char* SpinnerText = R"({"type":"TestSpinner","enabled":true,"data":{"speed":2.0}})";

} // namespace

TEST_CASE("Shared libraries load and give their functions", "[module]") {
    const auto library = Spot::SharedLibrary::Load(TestModule);
    REQUIRE(library);
    CHECK(library->GetFunction("SpotLoadGameModule") != nullptr);
    CHECK(library->GetFunction("NoSuchFunction") == nullptr);
    CHECK_FALSE(Spot::SharedLibrary::Load(TestModule.parent_path() / "no_such_library.dll"));
}

TEST_CASE("Game modules register their components while loaded, and can be loaded again", "[module]") {
    std::string saved;
    {
        const auto module = Spot::GameModule::Load(TestModule);
        REQUIRE(module);
        CHECK(std::vector(module->GetComponentTypes().begin(), module->GetComponentTypes().end()) ==
              std::vector<std::string>{"TestPusher", "TestSpinner"});
        // Components marked with SPOT_COMPONENT register under their own names.
        CHECK(Spot::CreateComponent("TestPusher"));

        Spot::Scene scene;
        Spot::Entity& entity = scene.CreateEntity("Spinner");
        Spot::Component* spinner = Spot::AddComponentFromString(entity, SpinnerText);
        REQUIRE(spinner);
        CHECK(Spot::GetComponentTypeName(*spinner) == "TestSpinner");
        scene.Update(0.5f);
        CHECK(entity.GetTransform().GetRotation() != Spot::Quaternion::Identity());
        saved = scene.SaveToString();
        // The scene, with the module's components, goes before the module.
    }
    CHECK_FALSE(Spot::CreateComponent("TestSpinner"));
    CHECK_FALSE(Spot::CreateComponent("TestPusher"));

    // Without the module, its components are kept as they were saved.
    {
        Spot::Scene scene;
        REQUIRE(scene.LoadFromString(saved));
        const auto* missing = scene.FindEntity("Spinner")->GetComponent<Spot::MissingComponent>();
        REQUIRE(missing);
        CHECK(missing->GetTypeName() == "TestSpinner");
    }

    // Loaded again, as after rebuilding it, the module's components come back.
    const auto again = Spot::GameModule::Load(TestModule);
    REQUIRE(again);
    CHECK(again->GetComponentTypes().size() == 2);
    {
        Spot::Scene scene;
        REQUIRE(scene.LoadFromString(saved));
        const Spot::Entity* entity = scene.FindEntity("Spinner");
        CHECK_FALSE(entity->GetComponent<Spot::MissingComponent>());
        CHECK(entity->GetComponents<Spot::Component>().size() == 1);
    }
}

TEST_CASE("Processes run programs and give back what they wrote and their exit code", "[module]") {
    const std::array echo{std::string(SPOT_CMAKE_COMMAND), std::string("-E"), std::string("echo"),
                          std::string("hello from a process")};
    const auto process = Spot::Process::Start(echo);
    REQUIRE(process);
    CHECK(process->Wait() == 0);
    CHECK_FALSE(process->IsRunning());
    CHECK(process->TakeOutput() == std::vector<std::string>{"hello from a process"});
    CHECK(process->TakeOutput().empty());

    const std::array fail{std::string(SPOT_CMAKE_COMMAND), std::string("-E"), std::string("false")};
    const auto failing = Spot::Process::Start(fail);
    REQUIRE(failing);
    CHECK(failing->Wait() != 0);
    CHECK(failing->GetExitCode() == failing->Wait());

    const std::array missing{std::string("spot-no-such-program")};
    CHECK_FALSE(Spot::Process::Start(missing));
}
