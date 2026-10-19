#pragma once

#include "nectar_net/connection_code.h"
#include "nectar_net/transport.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace nectar::net {

// Quarantine in front of a freshly connected transport (ADR-0002). A GNS
// "Connected" event only means the channel is encrypted; nothing reaches
// gameplay until the guest has presented the join secret of the NECTAR1 code
// and the host has accepted it. Accepted residual risk: an active attacker on
// the network path during the handshake (no peer identity is verified).
//
// Both gates are transport-independent and driven by explicit millisecond
// timestamps, so they can be tested without sockets or a hidden clock.

constexpr std::uint64_t kJoinTimeoutMs = 5000;

struct JoinCredentials {
    std::array<std::uint8_t, 16> sessionNonce{};
    std::array<std::uint8_t, 16> joinSecret{};
};

JoinCredentials joinCredentialsFromCode(const ConnectionCode& code);

enum class JoinState {
    Idle,
    AwaitingJoin,   // host: connected, waiting for the guest's secret
    AwaitingAccept, // guest: secret sent, waiting for the host's verdict
    Authenticated,
    Rejected,
};

enum class JoinError {
    None,
    Timeout,
    UnexpectedMessage,
    Malformed,
    ProtocolMismatch,
    Refused, // wrong secret or a code from another room; deliberately not split
    RateLimited, // host: origin temporarily banned after repeated failures
    Incompatible, // authenticated, but game/region/build/content differ
};

enum class JoinAction {
    None,
    Send,          // send `output` on the reliable channel
    SendAndClose,  // send `output` reliably, then close the transport
    Forward,       // authenticated: hand the message to the session layer
    Close,         // close the transport without replying
};

class HostJoinGate {
public:
    explicit HostJoinGate(const JoinCredentials& credentials, std::uint64_t timeoutMs = kJoinTimeoutMs);
    ~HostJoinGate();

    HostJoinGate(const HostJoinGate&) = delete;
    HostJoinGate& operator=(const HostJoinGate&) = delete;

    void onConnected(std::uint64_t nowMs);
    void onDisconnected();
    JoinAction onMessage(Delivery delivery, const std::uint8_t* data, std::size_t size,
                         std::vector<std::uint8_t>& output);
    JoinAction tick(std::uint64_t nowMs);

    JoinState state() const { return mState; }
    JoinError error() const { return mError; }

private:
    JoinAction reject(JoinError error);

    JoinCredentials mCredentials;
    std::uint64_t mTimeoutMs;
    std::uint64_t mDeadlineMs = 0;
    JoinState mState = JoinState::Idle;
    JoinError mError = JoinError::None;
};

class GuestJoinGate {
public:
    explicit GuestJoinGate(const JoinCredentials& credentials, std::uint64_t timeoutMs = kJoinTimeoutMs);
    ~GuestJoinGate();

    GuestJoinGate(const GuestJoinGate&) = delete;
    GuestJoinGate& operator=(const GuestJoinGate&) = delete;

    // `output` carries the join secret: wipe it with secureWipe once sent.
    JoinAction onConnected(std::uint64_t nowMs, std::vector<std::uint8_t>& output);
    void onDisconnected();
    JoinAction onMessage(Delivery delivery, const std::uint8_t* data, std::size_t size);
    JoinAction tick(std::uint64_t nowMs);

    JoinState state() const { return mState; }
    JoinError error() const { return mError; }

private:
    JoinAction reject(JoinError error);

    JoinCredentials mCredentials;
    std::uint64_t mTimeoutMs;
    std::uint64_t mDeadlineMs = 0;
    JoinState mState = JoinState::Idle;
    JoinError mError = JoinError::None;
};

// Stable, secret-free strings suitable for logs and UI mapping.
const char* joinErrorString(JoinError error);

} // namespace nectar::net
