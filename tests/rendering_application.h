#pragma once

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <utility>

namespace Spot::Tests {

// Runs OnRender() for a few frames, giving up after a bounded number of updates in case no frame can be drawn.
class RenderingApplication final : public Spot::Application {
public:
    explicit RenderingApplication(std::function<void(Spot::Application&, Spot::RenderContext&)> render,
                                  Spot::AssetConfig assets = {}, Spot::GraphicsConfig graphics = {})
        : Application({.window = {.title = "Spot GPU Tests", .width = 320, .height = 240},
                       .graphics = graphics,
                       .assets = std::move(assets)}),
          m_render(std::move(render)) {}

    int GetRenderedFrames() const { return m_renderedFrames; }

protected:
    void OnUpdate(float) override {
        if (++m_updates > 200) {
            Quit();
        }
    }

    void OnRender(Spot::RenderContext& context) override {
        m_render(*this, context);
        if (++m_renderedFrames == 3) {
            Quit();
        }
    }

private:
    std::function<void(Spot::Application&, Spot::RenderContext&)> m_render;
    int m_updates = 0;
    int m_renderedFrames = 0;
};

inline void RequireGpu() {
    if (!Spot::GraphicsDevice::IsSupported()) {
        SKIP("No GPU backend is available on this machine");
    }
}

} // namespace Spot::Tests
