#include "temp_directory.h"
#include <spot.h>
#include <spot/platform/frame_capture.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Optional frame capture tolerates an absent library", "[capture]") {
    TempDirectory directory;
    CHECK_FALSE(Spot::FrameCapture::Connect(directory.GetPath() / "missing_library"));
}

#if !defined(__APPLE__)
TEST_CASE("Frame capture loads the SDK ABI, sets the destination and requests a frame without launching UI",
          "[capture]") {
    TempDirectory directory;
    auto capture = Spot::FrameCapture::Connect(SPOT_CAPTURE_FIXTURE);
    REQUIRE(capture);
    auto library = Spot::SharedLibrary::Load(SPOT_CAPTURE_FIXTURE);
    REQUIRE(library);
    const auto count = reinterpret_cast<unsigned (*)()>(library->GetFunction("SpotGetCaptureCount"));
    const auto path = reinterpret_cast<const char* (*)()>(library->GetFunction("SpotGetCaptureTemplate"));
    REQUIRE(count);
    REQUIRE(path);
    const auto before = count();
    const auto file = directory.GetPath() / "captures" / "test";
    REQUIRE(capture->Request(file));
    CHECK(count() == before + 1);
    CHECK(std::string(path()) == Spot::PathToUtf8(file));
    CHECK(std::filesystem::is_directory(file.parent_path()));
    CHECK_FALSE(capture->Request({}));
}
#endif

TEST_CASE("Capture commands report unavailable graphics and reject extra arguments headlessly", "[capture][console]") {
    Spot::Console console;
    Spot::Application application({.graphics = {.enabled = false}, .audio = {.enabled = false}});
    Spot::EngineConsole commands(console, application);
    CHECK_FALSE(console.Execute("screenshot"));
    CHECK_FALSE(console.Execute("capture"));
    CHECK_FALSE(console.Execute("screenshot ../outside.png"));
    CHECK_FALSE(console.Execute("capture ../outside"));
    CHECK(console.Execute("screenshot one.png two.png").message == "Usage: screenshot [name.png]");
    CHECK(console.Execute("capture one two").message == "Usage: capture [name]");
}
