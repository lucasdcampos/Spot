#include "log_capture.h"
#include <spot/platform/platform.h>

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>

TEST_CASE("Platform initialization nests and can be repeated", "[platform]") {
    const auto capture = std::make_shared<Spot::Tests::CaptureSink>();
    const Spot::Tests::ScopedSinks sinks({capture});

    for (int round = 0; round < 2; ++round) {
        REQUIRE(Spot::Platform::Initialize());
        REQUIRE(Spot::Platform::Initialize());
        Spot::Platform::Shutdown();
        Spot::Platform::Shutdown();
    }

    for (const auto& record : capture->GetRecords()) {
        CHECK(record.level != Spot::LogLevel::Error);
    }
}
