#pragma once

#include <coroutine>
#include <cstdint>
#include <exception>
#include <functional>
#include <utility>
#include <vector>

namespace Spot {

struct TaskPromise;
struct TaskAwaiter;

// A lazy, move-only coroutine. Start it through TaskScheduler or Component::StartCoroutine().
class Task {
public:
    using promise_type = TaskPromise;
    Task() = default;
    ~Task();
    Task(Task&& other) noexcept;
    Task& operator=(Task&& other) noexcept;
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    bool IsFinished() const;
    TaskAwaiter operator co_await() &&;

private:
    friend struct TaskPromise;
    friend struct TaskAwaiter;
    friend class TaskScheduler;
    explicit Task(std::coroutine_handle<TaskPromise> handle) : m_handle(handle) {}
    void Reset();
    static void Advance(std::coroutine_handle<TaskPromise> handle, float deltaTime,
                        const std::function<bool()>& canResume);
    std::coroutine_handle<TaskPromise> m_handle;
};

struct NextFrame {
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<TaskPromise> handle) const noexcept;
    void await_resume() const noexcept {}
};

struct WaitForSeconds {
    float seconds;
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<TaskPromise> handle) const;
    void await_resume() const noexcept {}
};

struct WaitUntil {
    std::function<bool()> condition;
    std::coroutine_handle<TaskPromise> handle = {};
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<TaskPromise> handle);
    void await_resume() const;
};

struct TaskPromise {
    enum class Wait { Frame, Seconds, Condition, Child };
    Task get_return_object();
    std::suspend_always initial_suspend() const noexcept { return {}; }
    std::suspend_always final_suspend() const noexcept { return {}; }
    void return_void() const noexcept {}
    void unhandled_exception() noexcept { error = std::current_exception(); }

    Wait wait = Wait::Frame;
    double secondsLeft = 0.0;
    std::function<bool()> condition;
    std::coroutine_handle<TaskPromise> child;
    std::exception_ptr error;
    std::exception_ptr waitError;
};

struct TaskAwaiter {
    Task child;
    std::coroutine_handle<TaskPromise> parent;
    bool await_ready() const { return child.IsFinished(); }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<TaskPromise> handle);
    void await_resume();
};

enum class TaskId : std::uint64_t { Invalid = 0 };

class TaskScheduler {
public:
    TaskId Start(Task task);
    bool Cancel(TaskId id);
    void CancelAll();
    bool IsRunning(TaskId id) const;
    void Update(float deltaTime);

    // Shared frame numbers let scenes start tasks in hooks without advancing them again in the same frame.
    TaskId Start(Task task, std::uint64_t frame);
    void Update(float deltaTime, std::uint64_t frame, const std::function<bool()>& canResume = {});

private:
    struct Entry {
        TaskId id;
        Task task;
        std::uint64_t lastFrame;
        bool cancelled = false;
    };
    void Run(TaskId id, float deltaTime, bool start, const std::function<bool()>& canResume);
    void RemoveCancelled();
    std::vector<Entry> m_tasks;
    std::vector<TaskId> m_executing;
    std::uint64_t m_nextId = 1;
    std::uint64_t m_frame = 0;
    bool m_updating = false;
};

} // namespace Spot
