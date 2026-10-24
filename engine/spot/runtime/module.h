#pragma once

namespace Spot {

class Application;
class Event;
class RenderContext;

// A part of an application that hooks into its frame, such as a system that draws the scene or plays its sounds.
// Applications add them before Run() (see Application::AddModule()), which starts them in the order they were added,
// after the window, the graphics device and the assets, and stops them in reverse. Each frame calls their hooks at
// fixed points, in the order they were added unless said otherwise:
//
// - OnEvent(), for each event, before the input state, the application and the scene see it;
// - OnUpdate(), after the application's OnUpdate() and the scene's update;
// - on frames that can be drawn, OnBeginRender() before anything is drawn, OnRender() after the scene is prepared and
//   before the application's OnRender(), and OnEndRender(), in reverse order, after it.
//
// What a module makes in OnStart() it releases in OnStop(), while the window, the graphics device and the assets
// still exist. Only Application knows modules: the systems' own APIs work without one.
class Module {
public:
    virtual ~Module() = default;

    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;

    // The application that runs the module, from when it was added.
    Application& GetApplication() const { return *m_application; }

protected:
    Module() = default;

    virtual void OnStart() {}
    virtual void OnStop() {}
    // Returning true consumes the event: modules after this one, the input state and the scene never see it; the
    // application's OnEvent() still does.
    virtual bool OnEvent(const Event& /*event*/) { return false; }
    virtual void OnUpdate(float /*deltaTime*/) {}
    virtual void OnBeginRender(RenderContext& /*context*/) {}
    virtual void OnRender(RenderContext& /*context*/) {}
    virtual void OnEndRender(RenderContext& /*context*/) {}

private:
    friend class Application;

    Application* m_application = nullptr;
};

} // namespace Spot
