// Deterministic fuzzing of every parser that sees untrusted bytes: the
// connection code, the message header, compatibility info and the join and
// compatibility state machines. Mutates valid inputs and feeds random ones;
// run under ASan/UBSan to catch memory errors. A fixed seed keeps failures
// reproducible; NECTAR_FUZZ_ITERATIONS raises the count for longer runs.
#include "nectar_net/clock.h"
#include "nectar_net/compatibility.h"
#include "nectar_net/input.h"
#include "nectar_net/recording.h"
#include "nectar_net/scene.h"
#include "nectar_net/world.h"
#include "nectar_net/connection_code.h"
#include "nectar_net/join_gate.h"
#include "nectar_net/limits.h"
#include "nectar_net/port_mapping.h"
#include "nectar_net/protocol.h"

#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

using namespace nectar::net;

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition) {
        std::printf("FAIL: %s\n", message);
        ++failures;
    }
}

struct Rng {
    std::uint64_t state = 0x9e3779b97f4a7c15ULL;
    std::uint32_t next()
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return static_cast<std::uint32_t>(state >> 16);
    }
    std::size_t below(std::size_t n) { return n == 0 ? 0 : next() % n; }
};

std::vector<std::uint8_t> mutate(Rng& rng, std::vector<std::uint8_t> data)
{
    const int edits = 1 + static_cast<int>(rng.below(4));
    for (int i = 0; i < edits; ++i) {
        switch (rng.below(5)) {
        case 0:
            if (!data.empty()) data[rng.below(data.size())] ^= static_cast<std::uint8_t>(1U << rng.below(8));
            break;
        case 1:
            if (!data.empty()) data[rng.below(data.size())] = static_cast<std::uint8_t>(rng.next());
            break;
        case 2:
            if (!data.empty()) data.resize(rng.below(data.size()));
            break;
        case 3:
            data.insert(data.begin() + static_cast<std::ptrdiff_t>(rng.below(data.size() + 1)),
                        static_cast<std::uint8_t>(rng.next()));
            break;
        default:
            for (std::size_t n = rng.below(16); n != 0; --n) data.push_back(static_cast<std::uint8_t>(rng.next()));
            break;
        }
    }
    return data;
}

std::vector<std::uint8_t> randomBytes(Rng& rng, std::size_t maxSize)
{
    std::vector<std::uint8_t> data(rng.below(maxSize + 1));
    for (auto& byte : data) byte = static_cast<std::uint8_t>(rng.next());
    return data;
}

JoinCredentials creds()
{
    JoinCredentials value;
    for (std::size_t i = 0; i < 16; ++i) {
        value.sessionNonce[i] = static_cast<std::uint8_t>(i + 1);
        value.joinSecret[i] = static_cast<std::uint8_t>(0x50U + i);
    }
    return value;
}

} // namespace

int main()
{
    long iterations = 200000;
    if (const char* env = std::getenv("NECTAR_FUZZ_ITERATIONS")) iterations = std::atol(env);
    Rng rng;

    // Seeds: a valid code, a valid hello, a valid compatibility offer.
    ConnectionCode code;
    code.protocolMajor = 1;
    code.expiresUnixSeconds = 1790000000U;
    code.sessionNonce = creds().sessionNonce;
    code.joinSecret = creds().joinSecret;
    DirectEndpoint v4;
    v4.address[0] = 203;
    v4.port = 48810;
    code.endpoints.push_back(v4);
    const std::string codeText = encodeConnectionCode(code);
    const std::vector<std::uint8_t> codeSeed(codeText.begin(), codeText.end());

    std::vector<std::uint8_t> helloSeed;
    {
        GuestJoinGate guest(creds());
        guest.onConnected(0, helloSeed);
    }
    CompatibilityInfo info;
    info.game = static_cast<std::uint8_t>(GameId::Pikmin1);
    std::vector<std::uint8_t> offerSeed, unused;
    {
        CompatibilityCheck guest(CompatibilityCheck::Role::Guest, info);
        guest.start(0, offerSeed);
    }

    // Gateway answers: a valid PCP MAP response to mutate.
    MappingRequest mapRequest;
    mapRequest.internalPort = 39999;
    mapRequest.lifetimeSeconds = 600;
    std::vector<std::uint8_t> pcpSeed = encodePcpMapRequest(mapRequest, {192, 168, 1, 2});
    pcpSeed[1] = 0x81;
    pcpSeed[42] = 0x9c;
    pcpSeed[43] = 0x40;
    pcpSeed[4] = 0;
    pcpSeed[7] = 0x58;
    pcpSeed[56] = 81;

    // Gameplay payload seeds.
    std::vector<NetInputFrame> inputFrames(6);
    for (std::size_t i = 0; i < inputFrames.size(); ++i) inputFrames[i].tick = 100 - i;
    const std::vector<std::uint8_t> inputSeed = encodeInputBundle(inputFrames);
    EntityRecord entity;
    entity.id = 0x0003020000000001ULL;
    entity.schema = 1;
    entity.data.assign(12, 7);
    const std::vector<std::uint8_t> worldSeed = encodeWorldDelta(0, {entity, entity});
    SessionRecorder seedRecorder;
    seedRecorder.record(1, RecordDirection::Inbound, Delivery::Unreliable, inputSeed.data(), inputSeed.size());
    const std::vector<std::uint8_t> recordingSeed = seedRecorder.bytes();

    long authenticatedByNoise = 0;
    long compatibleByNoise = 0;
    for (long i = 0; i < iterations; ++i) {
        const bool fromSeed = rng.below(4) != 0;

        // Connection code text.
        std::vector<std::uint8_t> text = fromSeed ? mutate(rng, codeSeed) : randomBytes(rng, 400);
        ConnectionCode decoded;
        decodeConnectionCode(std::string(text.begin(), text.end()), decoded);

        // Message header and payload.
        std::vector<std::uint8_t> wire = fromSeed ? mutate(rng, helloSeed) : randomBytes(rng, 128);
        Message message;
        decodeMessage(wire.data(), wire.size(), message, 64);

        // Compatibility info and state machine.
        std::vector<std::uint8_t> offer = fromSeed ? mutate(rng, offerSeed) : randomBytes(rng, 160);
        CompatibilityInfo parsed;
        decodeCompatibilityInfo(offer.data(), offer.size(), parsed);
        CompatibilityCheck host(CompatibilityCheck::Role::Host, info);
        host.start(0, unused);
        std::vector<std::uint8_t> reply;
        host.onMessage(rng.below(2) ? Delivery::ReliableOrdered : Delivery::Unreliable, offer.data(), offer.size(),
                       reply);
        if (offer != offerSeed && host.state() == CompatibilityState::Compatible) {
            // Only a byte-identical offer or one changing ignored fields may pass.
            CompatibilityInfo check;
            if (!decodeCompatibilityInfo(offer.data() + limits::kHeaderBytes,
                                         offer.size() > limits::kHeaderBytes ? offer.size() - limits::kHeaderBytes : 0,
                                         check)
                || compareCompatibility(info, check) != CompatibilityField::None) {
                ++compatibleByNoise;
            }
        }

        // PCP / NAT-PMP answers from the LAN gateway.
        std::vector<std::uint8_t> gatewayReply = fromSeed ? mutate(rng, pcpSeed) : randomBytes(rng, 1200);
        PortMapping mapping;
        Ipv4Address external{};
        decodePcpMapResponse(gatewayReply.data(), gatewayReply.size(), mapRequest, mapping);
        decodeNatPmpMapResponse(gatewayReply.data(), gatewayReply.size(), mapRequest, mapping);
        decodeNatPmpAddressResponse(gatewayReply.data(), gatewayReply.size(), external);

        // Gameplay payloads.
        std::vector<std::uint8_t> input = fromSeed ? mutate(rng, inputSeed) : randomBytes(rng, 160);
        std::vector<NetInputFrame> frames;
        if (decodeInputBundle(input.data(), input.size(), InputLimits{}, frames)) {
            InputReceiver receiver;
            receiver.receive(frames, 100);
        }
        std::vector<std::uint8_t> world = fromSeed ? mutate(rng, worldSeed) : randomBytes(rng, 200);
        std::uint8_t worldFlags = 0;
        std::vector<EntityRecord> records;
        decodeWorldDelta(world.data(), world.size(), 3, WorldDeltaLimits{}, worldFlags, records);
        std::vector<std::uint8_t> small = randomBytes(rng, 32);
        PongPayload pong;
        decodePong(small.data(), small.size(), pong);
        ScenePrepare prepare;
        decodeScenePrepare(small.data(), small.size(), prepare);
        StateHashPayload stateHash;
        decodeStateHash(small.data(), small.size(), stateHash);
        std::vector<std::uint8_t> recording = fromSeed ? mutate(rng, recordingSeed) : randomBytes(rng, 128);
        std::vector<RecordedMessage> recorded;
        parseRecording(recording.data(), recording.size(), recorded);

        // Join quarantine: random bytes must never authenticate.
        HostJoinGate gate(creds());
        gate.onConnected(0);
        std::vector<std::uint8_t> hello = fromSeed ? mutate(rng, helloSeed) : randomBytes(rng, 128);
        gate.onMessage(Delivery::ReliableOrdered, hello.data(), hello.size(), reply);
        if (gate.state() == JoinState::Authenticated) {
            // Header fields outside type/length are not authenticated, so a
            // mutation there may legitimately pass; the secret must not change.
            const bool secretIntact = hello.size() == helloSeed.size()
                && std::equal(hello.end() - 32, hello.end(), helloSeed.end() - 32);
            if (!secretIntact) ++authenticatedByNoise;
        }
    }
    check(authenticatedByNoise == 0, "no altered secret ever authenticates");
    check(compatibleByNoise == 0, "no incompatible offer is ever accepted");
    std::printf("nectar_net_decoder_fuzz_test: %ld iterations, %s\n", iterations,
                failures == 0 ? "all tests passed" : "FAILED");
    return failures == 0 ? 0 : 1;
}
