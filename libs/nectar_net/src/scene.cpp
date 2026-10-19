#include "nectar_net/scene.h"

#include "nectar_net/byte_io.h"

namespace nectar::net {

std::vector<std::uint8_t> encodeScenePrepare(const ScenePrepare& m)
{
    ByteWriter writer(16);
    writer.writeU32(m.epoch);
    writer.writeU32(m.sceneId);
    writer.writeU64(m.seed);
    return writer.ok() ? writer.bytes() : std::vector<std::uint8_t>{};
}

bool decodeScenePrepare(const std::uint8_t* data, std::size_t size, ScenePrepare& m)
{
    if (data == nullptr || size != 16) return false;
    ByteReader reader(data, size);
    return reader.readU32(m.epoch) && reader.readU32(m.sceneId) && reader.readU64(m.seed) && m.epoch != 0;
}

std::vector<std::uint8_t> encodeSceneReady(const SceneReady& m)
{
    ByteWriter writer(8);
    writer.writeU32(m.epoch);
    writer.writeU32(m.sceneId);
    return writer.ok() ? writer.bytes() : std::vector<std::uint8_t>{};
}

bool decodeSceneReady(const std::uint8_t* data, std::size_t size, SceneReady& m)
{
    if (data == nullptr || size != 8) return false;
    ByteReader reader(data, size);
    return reader.readU32(m.epoch) && reader.readU32(m.sceneId) && m.epoch != 0;
}

std::vector<std::uint8_t> encodeSceneStart(const SceneStart& m)
{
    ByteWriter writer(12);
    writer.writeU32(m.epoch);
    writer.writeU64(m.startTick);
    return writer.ok() ? writer.bytes() : std::vector<std::uint8_t>{};
}

bool decodeSceneStart(const std::uint8_t* data, std::size_t size, SceneStart& m)
{
    if (data == nullptr || size != 12) return false;
    ByteReader reader(data, size);
    return reader.readU32(m.epoch) && reader.readU64(m.startTick) && m.epoch != 0;
}

ScenePrepare HostSceneBarrier::prepare(std::uint32_t sceneId, std::uint64_t seed, std::uint64_t nowMs)
{
    ++mEpoch;
    if (mEpoch == 0) mEpoch = 1; // 0 means "no scene"
    mSceneId = sceneId;
    mDeadlineMs = nowMs + mTimeoutMs;
    mState = BarrierState::Preparing;
    return ScenePrepare{mEpoch, sceneId, seed};
}

bool HostSceneBarrier::onReady(const SceneReady& ready)
{
    if (mState != BarrierState::Preparing || ready.epoch != mEpoch || ready.sceneId != mSceneId) return false;
    mState = BarrierState::Ready;
    return true;
}

SceneStart HostSceneBarrier::start(std::uint64_t startTick)
{
    mState = BarrierState::Running;
    return SceneStart{mEpoch, startTick};
}

BarrierState HostSceneBarrier::tick(std::uint64_t nowMs)
{
    if (mState == BarrierState::Preparing && nowMs >= mDeadlineMs) mState = BarrierState::TimedOut;
    return mState;
}

bool GuestSceneBarrier::onPrepare(const ScenePrepare& prepare)
{
    if (prepare.epoch == 0 || prepare.epoch <= mEpoch) return false;
    mPrepare = prepare;
    mEpoch = prepare.epoch;
    mStartTick = 0;
    mState = BarrierState::Preparing;
    return true;
}

bool GuestSceneBarrier::loaded(SceneReady& ready)
{
    if (mState != BarrierState::Preparing) return false;
    mState = BarrierState::Ready;
    ready = SceneReady{mEpoch, mPrepare.sceneId};
    return true;
}

bool GuestSceneBarrier::onStart(const SceneStart& start)
{
    if (mState != BarrierState::Ready || start.epoch != mEpoch) return false;
    mStartTick = start.startTick;
    mState = BarrierState::Running;
    return true;
}

} // namespace nectar::net
