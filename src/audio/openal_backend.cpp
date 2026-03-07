// SPDX-License-Identifier: GPL-3.0-or-later
// UEOAL – OpenAL Soft backend implementation
#include "openal_backend.h"
#include "../logger.h"

#include <AL/alext.h>
#include <algorithm>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────────────
OpenALBackend& OpenALBackend::Get() {
    static OpenALBackend inst;
    return inst;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Coordinate system conversion
//  UE4/5:  X = forward, Y = right,  Z = up   (centimetres)
//  OpenAL: X = right,   Y = up,     Z = back  (metres)
// ─────────────────────────────────────────────────────────────────────────────
void OpenALBackend::UEtoAL(float ueX, float ueY, float ueZ,
                            float& alX, float& alY, float& alZ) noexcept {
    constexpr float CM_TO_M = 0.01f;
    alX =  ueY * CM_TO_M;   // UE right  → AL X
    alY =  ueZ * CM_TO_M;   // UE up     → AL Y
    alZ = -ueX * CM_TO_M;   // UE fwd    → AL -Z  (OpenAL is right-handed)
}

// ─────────────────────────────────────────────────────────────────────────────
bool OpenALBackend::Initialize() {
    LOG_INFO("OpenALBackend::Initialize – opening default device");

    m_device = alcOpenDevice(nullptr);
    if (!m_device) {
        LOG_ERROR("alcOpenDevice failed");
        return false;
    }

    const char* devName = alcGetString(m_device, ALC_DEVICE_SPECIFIER);
    LOG_INFO("AL device : %s", devName ? devName : "(unknown)");

    // ── Request HRTF ────────────────────────────────────────────────────────
    const bool supportsHRTF = alcIsExtensionPresent(m_device, "ALC_SOFT_HRTF") == ALC_TRUE;
    LOG_INFO("ALC_SOFT_HRTF extension: %s", supportsHRTF ? "present" : "absent");

    ALCint attrs[8]{};
    int    ai = 0;
    if (supportsHRTF) {
        attrs[ai++] = ALC_HRTF_SOFT;
        attrs[ai++] = ALC_TRUE;
    }
    attrs[ai] = 0;

    m_context = alcCreateContext(m_device, ai > 0 ? attrs : nullptr);
    if (!m_context) {
        LOG_ERROR("alcCreateContext failed (ALC error 0x%04X)", alcGetError(m_device));
        alcCloseDevice(m_device);
        m_device = nullptr;
        return false;
    }

    alcMakeContextCurrent(m_context);

    // ── Verify HRTF status ──────────────────────────────────────────────────
    if (supportsHRTF) {
        ALCint hrtfStatus = 0;
        alcGetIntegerv(m_device, ALC_HRTF_STATUS_SOFT, 1, &hrtfStatus);
        m_hrtfEnabled = (hrtfStatus == ALC_HRTF_ENABLED_SOFT);
        LOG_INFO("HRTF status : %s (code=%d)",
                 m_hrtfEnabled ? "ENABLED" : "disabled / unavailable", hrtfStatus);

        if (m_hrtfEnabled) {
            const ALchar* hrtfName = alcGetString(m_device, ALC_HRTF_SPECIFIER_SOFT);
            LOG_INFO("HRTF filter : %s", hrtfName ? hrtfName : "(default)");
        }
    }

    // ── Global AL state ─────────────────────────────────────────────────────
    alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);
    alListenerf(AL_GAIN, 1.0f);

    // Default listener orientation: facing -Z (UE forward = +X → AL -Z)
    const float ori[6]{ 0.f, 0.f, -1.f,   0.f, 1.f, 0.f };
    alListenerfv(AL_ORIENTATION, ori);

    LOG_INFO("OpenAL Soft ready – vendor=%s version=%s renderer=%s",
             alGetString(AL_VENDOR), alGetString(AL_VERSION), alGetString(AL_RENDERER));

    m_initialized = true;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
void OpenALBackend::Shutdown() {
    if (!m_initialized) return;
    LOG_INFO("OpenALBackend::Shutdown – destroying %zu sources", m_sources.size());

    std::lock_guard<std::mutex> lk(m_mutex);

    for (auto& [id, st] : m_sources) {
        alSourceStop(st.alSource);
        // Unqueue & delete all pending buffers
        ALint queued = 0;
        alGetSourcei(st.alSource, AL_BUFFERS_QUEUED, &queued);
        if (queued > 0) {
            std::vector<ALuint> tmp(queued);
            alSourceUnqueueBuffers(st.alSource, queued, tmp.data());
            alDeleteBuffers(queued, tmp.data());
        }
        alDeleteSources(1, &st.alSource);
    }
    m_sources.clear();

    alcMakeContextCurrent(nullptr);
    alcDestroyContext(m_context);
    alcCloseDevice(m_device);
    m_context     = nullptr;
    m_device      = nullptr;
    m_initialized = false;
    m_hrtfEnabled = false;

    LOG_INFO("OpenALBackend shutdown complete");
}

// ─────────────────────────────────────────────────────────────────────────────
ALuint OpenALBackend::CreateSource() {
    if (!m_initialized) return 0;

    ALuint src = 0;
    alGenSources(1, &src);
    if (alGetError() != AL_NO_ERROR || src == 0) {
        LOG_ERROR("alGenSources failed");
        return 0;
    }

    // Sensible defaults (UE distances are in centimetres)
    alSourcef(src, AL_GAIN,               1.0f);
    alSourcef(src, AL_PITCH,              1.0f);
    alSourcef(src, AL_REFERENCE_DISTANCE, 100.f);   // 1 m reference
    alSourcef(src, AL_MAX_DISTANCE,       100000.f); // 1 km max
    alSourcef(src, AL_ROLLOFF_FACTOR,     1.0f);
    alSourcei(src, AL_SOURCE_RELATIVE,    AL_FALSE); // world-space by default
    alSource3f(src, AL_POSITION,          0.f, 0.f, 0.f);
    alSource3f(src, AL_VELOCITY,          0.f, 0.f, 0.f);

    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_sources[src] = ALSourceState{};
        m_sources[src].alSource = src;
    }

    LOG_DEBUG("CreateSource → AL source %u", src);
    return src;
}

// ─────────────────────────────────────────────────────────────────────────────
void OpenALBackend::DestroySource(ALuint source) {
    if (!source) return;

    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_sources.find(source);
    if (it == m_sources.end()) return;

    alSourceStop(source);

    ALint queued = 0;
    alGetSourcei(source, AL_BUFFERS_QUEUED, &queued);
    if (queued > 0) {
        std::vector<ALuint> tmp(queued);
        alSourceUnqueueBuffers(source, queued, tmp.data());
        alDeleteBuffers(queued, tmp.data());
    }
    alDeleteSources(1, &source);
    m_sources.erase(it);

    LOG_DEBUG("DestroySource AL source %u", source);
}

// ─────────────────────────────────────────────────────────────────────────────
void OpenALBackend::SetSourcePosition(ALuint source, float ueX, float ueY, float ueZ) {
    float ax, ay, az;
    UEtoAL(ueX, ueY, ueZ, ax, ay, az);
    alSource3f(source, AL_POSITION, ax, ay, az);

    std::lock_guard<std::mutex> lk(m_mutex);
    if (auto it = m_sources.find(source); it != m_sources.end()) {
        it->second.posX = ueX;
        it->second.posY = ueY;
        it->second.posZ = ueZ;
    }
}

void OpenALBackend::SetSourceVelocity(ALuint source, float ueX, float ueY, float ueZ) {
    float ax, ay, az;
    UEtoAL(ueX, ueY, ueZ, ax, ay, az);
    alSource3f(source, AL_VELOCITY, ax, ay, az);
}

void OpenALBackend::SetSource3D(ALuint source, bool is3D) {
    alSourcei(source, AL_SOURCE_RELATIVE, is3D ? AL_FALSE : AL_TRUE);
    std::lock_guard<std::mutex> lk(m_mutex);
    if (auto it = m_sources.find(source); it != m_sources.end())
        it->second.is3D = is3D;
}

// ─────────────────────────────────────────────────────────────────────────────
void OpenALBackend::SetListenerPosition(float ueX, float ueY, float ueZ) {
    float ax, ay, az;
    UEtoAL(ueX, ueY, ueZ, ax, ay, az);
    alListener3f(AL_POSITION, ax, ay, az);
    LOG_DEBUG("Listener pos UE=(%.1f,%.1f,%.1f) AL=(%.3f,%.3f,%.3f)",
              ueX, ueY, ueZ, ax, ay, az);
}

void OpenALBackend::SetListenerVelocity(float ueX, float ueY, float ueZ) {
    float ax, ay, az;
    UEtoAL(ueX, ueY, ueZ, ax, ay, az);
    alListener3f(AL_VELOCITY, ax, ay, az);
}

void OpenALBackend::SetListenerOrientation(const float* fwd, const float* up) {
    // Convert UE forward/up vectors (X=fwd, Y=right, Z=up) to AL (-Z=fwd, Y=up)
    float ori[6]{
        /* AL forward */ fwd[1], fwd[2], -fwd[0],
        /* AL up      */  up[1],  up[2],  -up[0]
    };
    alListenerfv(AL_ORIENTATION, ori);
}

// ─────────────────────────────────────────────────────────────────────────────
void OpenALBackend::StartSource(ALuint source) {
    ALint state = AL_STOPPED;
    alGetSourcei(source, AL_SOURCE_STATE, &state);
    if (state != AL_PLAYING) alSourcePlay(source);
}

void OpenALBackend::StopSource(ALuint source) {
    alSourceStop(source);
}

void OpenALBackend::SetSourceFrequencyRatio(ALuint source, float ratio) {
    alSourcef(source, AL_PITCH, ratio);
}

void OpenALBackend::SetSourceGain(ALuint source, float gain) {
    alSourcef(source, AL_GAIN, gain);
}

// ─────────────────────────────────────────────────────────────────────────────
ALenum OpenALBackend::ChooseALFormat(int ch, int bits, bool isFloat) const {
    if (isFloat) {
        if (ch == 1) return AL_FORMAT_MONO_FLOAT32;
        if (ch == 2) return AL_FORMAT_STEREO_FLOAT32;
        if (ch == 4) { ALenum e = alGetEnumValue("AL_FORMAT_QUAD32");   if (e) return e; }
        if (ch == 6) { ALenum e = alGetEnumValue("AL_FORMAT_51CHN32");  if (e) return e; }
        if (ch == 7) { ALenum e = alGetEnumValue("AL_FORMAT_61CHN32");  if (e) return e; }
        if (ch == 8) { ALenum e = alGetEnumValue("AL_FORMAT_71CHN32");  if (e) return e; }
    } else if (bits == 16) {
        if (ch == 1) return AL_FORMAT_MONO16;
        if (ch == 2) return AL_FORMAT_STEREO16;
        if (ch == 4) { ALenum e = alGetEnumValue("AL_FORMAT_QUAD16");   if (e) return e; }
        if (ch == 6) { ALenum e = alGetEnumValue("AL_FORMAT_51CHN16");  if (e) return e; }
        if (ch == 8) { ALenum e = alGetEnumValue("AL_FORMAT_71CHN16");  if (e) return e; }
    } else if (bits == 8) {
        if (ch == 1) return AL_FORMAT_MONO8;
        if (ch == 2) return AL_FORMAT_STEREO8;
    }
    LOG_WARN("ChooseALFormat: unhandled ch=%d bits=%d float=%d – falling back to MONO16", ch, bits, isFloat);
    return AL_FORMAT_MONO16;
}

// ─────────────────────────────────────────────────────────────────────────────
bool OpenALBackend::SubmitAudioBuffer(ALuint      source,
                                      const void* pcmData,
                                      uint32_t    byteCount,
                                      int         sampleRate,
                                      int         channels,
                                      int         bitsPerSample,
                                      bool        isFloat) {
    if (!m_initialized || !source || !pcmData || byteCount == 0)
        return false;

    // First prune any already-consumed buffers to avoid leaking AL objects
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (auto it = m_sources.find(source); it != m_sources.end())
            PruneSource(source, it->second);
    }

    ALenum fmt = ChooseALFormat(channels, bitsPerSample, isFloat);

    ALuint buf = 0;
    alGenBuffers(1, &buf);
    if (alGetError() != AL_NO_ERROR) {
        LOG_ERROR("alGenBuffers failed for source %u", source);
        return false;
    }

    alBufferData(buf, fmt, pcmData, static_cast<ALsizei>(byteCount), sampleRate);
    if (ALenum err = alGetError(); err != AL_NO_ERROR) {
        LOG_ERROR("alBufferData error 0x%04X (src=%u fmt=0x%X size=%u rate=%d)",
                  err, source, fmt, byteCount, sampleRate);
        alDeleteBuffers(1, &buf);
        return false;
    }

    alSourceQueueBuffers(source, 1, &buf);

    // Auto-play if not already running
    ALint state = AL_STOPPED;
    alGetSourcei(source, AL_SOURCE_STATE, &state);
    if (state != AL_PLAYING) alSourcePlay(source);

    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (auto it = m_sources.find(source); it != m_sources.end()) {
            it->second.queuedBuffers.push_back(buf);
            it->second.sampleRate    = sampleRate;
            it->second.channels      = channels;
            it->second.bitsPerSample = bitsPerSample;
            it->second.isFloat       = isFloat;
        }
    }

    LOG_DEBUG("SubmitBuffer src=%u buf=%u sz=%u rate=%d ch=%d bits=%d float=%d",
              source, buf, byteCount, sampleRate, channels, bitsPerSample, isFloat);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
void OpenALBackend::PruneSource(ALuint source, ALSourceState& st) {
    ALint processed = 0;
    alGetSourcei(source, AL_BUFFERS_PROCESSED, &processed);
    while (processed-- > 0) {
        ALuint buf = 0;
        alSourceUnqueueBuffers(source, 1, &buf);
        alDeleteBuffers(1, &buf);
        auto& v = st.queuedBuffers;
        v.erase(std::remove(v.begin(), v.end(), buf), v.end());
    }
}

void OpenALBackend::PruneProcessedBuffers() {
    if (!m_initialized) return;
    std::lock_guard<std::mutex> lk(m_mutex);
    for (auto& [id, st] : m_sources)
        PruneSource(id, st);
}
