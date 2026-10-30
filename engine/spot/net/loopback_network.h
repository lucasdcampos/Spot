#pragma once

#include "spot/net/net_transport.h"

#include <memory>

namespace Spot {

namespace Detail {
struct LoopbackState;
} // namespace Detail

// A network inside the process: its transports and sockets reach one another by port, whatever the host of the
// address, and deliver what is sent at the receiver's next Poll() or Receive(), in order and without loss (see
// ConditionedNetTransport for lag and loss). Servers and clients of one test, or a game hosting and joining itself,
// talk through it without sockets. What it makes may outlive it.
class LoopbackNetwork {
public:
    LoopbackNetwork();
    ~LoopbackNetwork();

    LoopbackNetwork(const LoopbackNetwork&) = delete;
    LoopbackNetwork& operator=(const LoopbackNetwork&) = delete;

    std::unique_ptr<NetTransport> CreateTransport();
    std::unique_ptr<NetDatagramSocket> CreateDatagramSocket();

private:
    std::shared_ptr<Detail::LoopbackState> m_state;
};

} // namespace Spot
