#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace Spot {
namespace Detail {

class SignalConnections {
public:
    virtual ~SignalConnections() = default;
    virtual void Disconnect(std::uint64_t id) = 0;
    virtual bool IsConnected(std::uint64_t id) const = 0;
};

} // namespace Detail

template <typename... Args>
class Signal;

class SignalConnection {
public:
    SignalConnection() = default;
    ~SignalConnection() { Disconnect(); }
    SignalConnection(const SignalConnection&) = delete;
    SignalConnection& operator=(const SignalConnection&) = delete;
    SignalConnection(SignalConnection&& other) noexcept
        : m_state(std::move(other.m_state)), m_id(std::exchange(other.m_id, 0)) {}
    SignalConnection& operator=(SignalConnection&& other) noexcept {
        if (this != &other) {
            Disconnect();
            m_state = std::move(other.m_state);
            m_id = std::exchange(other.m_id, 0);
        }
        return *this;
    }
    void Disconnect() {
        const auto id = std::exchange(m_id, 0);
        if (auto state = m_state.lock()) {
            state->Disconnect(id);
        }
        m_state.reset();
    }
    bool IsConnected() const {
        const auto state = m_state.lock();
        return state && state->IsConnected(m_id);
    }

private:
    template <typename... Args>
    friend class Signal;
    SignalConnection(const std::shared_ptr<Detail::SignalConnections>& state, std::uint64_t id)
        : m_state(state), m_id(id) {}
    std::weak_ptr<Detail::SignalConnections> m_state;
    std::uint64_t m_id = 0;
};

// Synchronous, ordered dispatch. Keep the returned connection for as long as the subscriber should receive events.
template <typename... Args>
class Signal {
public:
    Signal() : m_state(std::make_shared<State>()) {}
    ~Signal() { DisconnectAll(); }
    Signal(const Signal&) = delete;
    Signal& operator=(const Signal&) = delete;

    SignalConnection Connect(std::function<void(Args...)> callback) {
        if (!callback || m_state->nextId == 0) {
            return {};
        }
        const auto id = m_state->nextId++;
        m_state->slots.push_back({id, std::move(callback)});
        return SignalConnection(m_state, id);
    }
    void DisconnectAll() { m_state->slots.clear(); }
    void Emit(Args... args) const {
        // A callback may destroy the signal. Its destructor clears the shared state, stopping further dispatch.
        const auto state = m_state;
        const auto depth = state->dispatchDepth;
        if (depth == state->dispatchBuffers.size())
            state->dispatchBuffers.push_back(std::make_unique<std::vector<std::uint64_t>>());
        ++state->dispatchDepth;
        struct DispatchScope {
            State& state;
            ~DispatchScope() { --state.dispatchDepth; }
        } scope{*state};
        auto& pending = *state->dispatchBuffers[depth];
        pending.clear();
        pending.reserve(state->slots.size());
        for (const auto& slot : state->slots) {
            pending.push_back(slot.id);
        }
        for (const auto id : pending) {
            const auto found = std::ranges::find(state->slots, id, &Slot::id);
            if (found != state->slots.end()) {
                auto callback = found->callback;
                callback(args...);
            }
        }
    }

private:
    struct Slot {
        std::uint64_t id;
        std::function<void(Args...)> callback;
    };
    struct State final : Detail::SignalConnections {
        std::vector<Slot> slots;
        std::uint64_t nextId = 1;
        std::size_t dispatchDepth = 0;
        std::vector<std::unique_ptr<std::vector<std::uint64_t>>> dispatchBuffers;
        void Disconnect(std::uint64_t id) override {
            std::erase_if(slots, [id](const Slot& slot) { return slot.id == id; });
        }
        bool IsConnected(std::uint64_t id) const override {
            return std::ranges::any_of(slots, [id](const Slot& slot) { return slot.id == id; });
        }
    };
    std::shared_ptr<State> m_state;
};

} // namespace Spot
