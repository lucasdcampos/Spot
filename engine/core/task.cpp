#include "core/task.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Spot {

Task::~Task() {
    Reset();
}
Task::Task(Task&& other) noexcept : m_handle(std::exchange(other.m_handle, {})) {
}
Task& Task::operator=(Task&& other) noexcept {
    if (this != &other) {
        Reset();
        m_handle = std::exchange(other.m_handle, {});
    }
    return *this;
}
void Task::Reset() {
    if (m_handle) {
        m_handle.destroy();
        m_handle = {};
    }
}
bool Task::IsFinished() const {
    return !m_handle || m_handle.done();
}
TaskAwaiter Task::operator co_await() && {
    return {std::move(*this), {}};
}
Task TaskPromise::get_return_object() {
    return Task(std::coroutine_handle<TaskPromise>::from_promise(*this));
}

void NextFrame::await_suspend(std::coroutine_handle<TaskPromise> handle) const noexcept {
    handle.promise().wait = TaskPromise::Wait::Frame;
}
void WaitForSeconds::await_suspend(std::coroutine_handle<TaskPromise> handle) const {
    if (!std::isfinite(seconds) || seconds < 0.0f) {
        throw std::invalid_argument("Coroutine wait duration must be finite and non-negative");
    }
    handle.promise().wait = TaskPromise::Wait::Seconds;
    handle.promise().secondsLeft = seconds;
}
void WaitUntil::await_suspend(std::coroutine_handle<TaskPromise> coroutine) {
    if (!condition) {
        throw std::invalid_argument("Coroutine wait condition must be callable");
    }
    handle = coroutine;
    handle.promise().wait = TaskPromise::Wait::Condition;
    handle.promise().condition = std::move(condition);
}
void WaitUntil::await_resume() const {
    if (const auto error = std::exchange(handle.promise().waitError, {})) {
        std::rethrow_exception(error);
    }
}

std::coroutine_handle<> TaskAwaiter::await_suspend(std::coroutine_handle<TaskPromise> handle) {
    parent = handle;
    parent.promise().wait = TaskPromise::Wait::Child;
    parent.promise().child = child.m_handle;
    return child.m_handle;
}
void TaskAwaiter::await_resume() {
    if (parent) {
        parent.promise().child = {};
        parent.promise().wait = TaskPromise::Wait::Frame;
    }
    const auto error = child.m_handle ? child.m_handle.promise().error : std::exception_ptr{};
    child.Reset();
    if (error) {
        std::rethrow_exception(error);
    }
}

void Task::Advance(std::coroutine_handle<TaskPromise> handle, float deltaTime, const std::function<bool()>& canResume) {
    if (!handle || handle.done() || (canResume && !canResume())) {
        return;
    }
    auto& promise = handle.promise();
    bool ready = true;
    switch (promise.wait) {
    case TaskPromise::Wait::Seconds:
        promise.secondsLeft -= deltaTime;
        ready = promise.secondsLeft <= 0.0;
        break;
    case TaskPromise::Wait::Condition:
        try {
            ready = promise.condition();
        } catch (...) {
            promise.waitError = std::current_exception();
            ready = true;
        }
        break;
    case TaskPromise::Wait::Child:
        if (promise.child && !promise.child.done()) {
            Advance(promise.child, deltaTime, canResume);
        }
        ready = !promise.child || promise.child.done();
        break;
    default:
        break;
    }
    if (ready && (!canResume || canResume())) {
        promise.condition = {};
        handle.resume();
    }
}

TaskId TaskScheduler::Start(Task task) {
    return Start(std::move(task), m_frame);
}
TaskId TaskScheduler::Start(Task task, std::uint64_t frame) {
    if (task.IsFinished() || m_nextId == 0) {
        return TaskId::Invalid;
    }
    const auto id = static_cast<TaskId>(m_nextId++);
    m_frame = frame;
    m_tasks.push_back({id, std::move(task), frame});
    Run(id, 0.0f, true, {});
    return id;
}

bool TaskScheduler::IsRunning(TaskId id) const {
    return std::ranges::any_of(m_tasks, [id](const Entry& entry) { return entry.id == id && !entry.cancelled; });
}
void TaskScheduler::RemoveCancelled() {
    std::erase_if(m_tasks, [&](const Entry& entry) {
        return entry.cancelled && std::ranges::find(m_executing, entry.id) == m_executing.end();
    });
}
bool TaskScheduler::Cancel(TaskId id) {
    const auto found = std::ranges::find(m_tasks, id, &Entry::id);
    if (found == m_tasks.end() || found->cancelled) {
        return false;
    }
    found->cancelled = true;
    RemoveCancelled();
    return true;
}
void TaskScheduler::CancelAll() {
    for (auto& entry : m_tasks) {
        entry.cancelled = true;
    }
    RemoveCancelled();
}

void TaskScheduler::Run(TaskId id, float deltaTime, bool start, const std::function<bool()>& canResume) {
    const auto found = std::ranges::find(m_tasks, id, &Entry::id);
    if (found == m_tasks.end() || found->cancelled) {
        return;
    }
    const auto handle = found->task.m_handle;
    m_executing.push_back(id);
    const auto guard = [&] { return IsRunning(id) && (!canResume || canResume()); };
    std::exception_ptr error;
    try {
        if (start) {
            handle.resume();
        } else {
            Task::Advance(handle, deltaTime, guard);
        }
        if (handle.done()) {
            error = handle.promise().error;
            Cancel(id);
        }
    } catch (...) {
        error = std::current_exception();
        Cancel(id);
    }
    m_executing.pop_back();
    RemoveCancelled();
    if (error) {
        std::rethrow_exception(error);
    }
}

void TaskScheduler::Update(float deltaTime) {
    Update(deltaTime, m_frame + 1);
}
void TaskScheduler::Update(float deltaTime, std::uint64_t frame, const std::function<bool()>& canResume) {
    if (m_updating || !std::isfinite(deltaTime) || deltaTime <= 0.0f) {
        return;
    }
    m_frame = frame;
    m_updating = true;
    struct Reset {
        bool& updating;
        ~Reset() { updating = false; }
    } reset{m_updating};
    std::vector<TaskId> pending;
    for (const auto& entry : m_tasks) {
        pending.push_back(entry.id);
    }
    for (const auto id : pending) {
        if (canResume && !canResume()) {
            break;
        }
        const auto found = std::ranges::find(m_tasks, id, &Entry::id);
        if (found == m_tasks.end() || found->cancelled || found->lastFrame >= frame ||
            std::ranges::find(m_executing, id) != m_executing.end()) {
            continue;
        }
        found->lastFrame = frame;
        Run(id, deltaTime, false, canResume);
    }
}

} // namespace Spot
