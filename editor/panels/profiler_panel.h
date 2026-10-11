#pragma once

#include "core/profiler_model.h"
#include "panels/panel.h"

#include <functional>

namespace Spot::Editor {

class ProfilerPanel final : public Panel {
public:
    using RemoteFrames = std::function<std::vector<const ProfileFrame*>()>;
    explicit ProfilerPanel(RemoteFrames remoteFrames = {});
    ProfilerModel& GetModel() { return m_model; }

protected:
    void OnDraw() override;

private:
    void DrawFrames();
    void DrawTimeline(const ProfileFrame& frame);
    void DrawAverages();
    RemoteFrames m_remoteFrames;
    ProfilerModel m_model;
    bool m_remote = false;
    std::uint64_t m_selected = 0;
};

} // namespace Spot::Editor
