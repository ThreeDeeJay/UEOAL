// SPDX-License-Identifier: GPL-3.0-or-later
// UEOAL – OpenAL Soft backend with HRTF / binaural rendering
#pragma once

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Per-source bookkeeping
// ─────────────────────────────────────────────────────────────────────────────
struct ALSourceState {
    ALuint alSource     = 0;
    bool   is3D         = false;
    float  posX         = 0.f, posY = 0.f, posZ = 0.f;
    float  velX         = 0.f, velY = 0.f, velZ = 0.f;
    int    sampleRate   = 48000;
    int    channels     = 1;
    int    bitsPerSample= 16;
    bool   isFloat      = false;
    // Pending queued AL buffer handles (for lifecycle management)
    std::vector<ALuint> queuedBuffers;
};

// ─────────────────────────────────────────────────────────────────────────────
//  OpenALBackend – singleton, manages the AL device / context and all sources
// ─────────────────────────────────────────────────────────────────────────────
class OpenALBackend {
public:
    static OpenALBackend& Get();

    bool Initialize();
    void Shutdown();
    bool IsInitialized() const noexcept { return m_initialized; }
    bool IsHRTFEnabled()  const noexcept { return m_hrtfEnabled;  }

    // ── Source management ───────────────────────────────────────────────────
    ALuint CreateSource();
    void   DestroySource(ALuint source);

    // ── 3D positioning ──────────────────────────────────────────────────────
    /// UE world coordinates (cm, X=forward, Y=right, Z=up).
    /// Converted internally to OpenAL (metres, right-hand Y-up).
    void SetSourcePosition(ALuint source, float ueX, float ueY, float ueZ);
    void SetSourceVelocity(ALuint source, float ueX, float ueY, float ueZ);
    void SetSource3D      (ALuint source, bool is3D);

    // ── Listener ────────────────────────────────────────────────────────────
    void SetListenerPosition   (float ueX, float ueY, float ueZ);
    void SetListenerVelocity   (float ueX, float ueY, float ueZ);
    /// forward[3] and up[3] in UE world space.
    void SetListenerOrientation(const float* forward, const float* up);

    // ── Playback control ────────────────────────────────────────────────────
    void StartSource            (ALuint source);
    void StopSource             (ALuint source);
    void SetSourceFrequencyRatio(ALuint source, float ratio);
    void SetSourceGain          (ALuint source, float gain);

    // ── Audio submission ────────────────────────────────────────────────────
    /// Feed a decoded PCM block into the streaming queue of *source*.
    bool SubmitAudioBuffer(ALuint      source,
                           const void* pcmData,
                           uint32_t    byteCount,
                           int         sampleRate,
                           int         channels,
                           int         bitsPerSample,
                           bool        isFloat);

    // ── Maintenance ─────────────────────────────────────────────────────────
    /// Recycle OpenAL buffers that the driver has already consumed.
    void PruneProcessedBuffers();

private:
    OpenALBackend()                          = default;
    OpenALBackend(const OpenALBackend&)      = delete;
    OpenALBackend& operator=(const OpenALBackend&) = delete;

    static void UEtoAL(float ueX, float ueY, float ueZ,
                       float& alX, float& alY, float& alZ) noexcept;

    ALenum ChooseALFormat(int channels, int bitsPerSample, bool isFloat) const;

    void PruneSource(ALuint source, ALSourceState& st);

    ALCdevice*  m_device  = nullptr;
    ALCcontext* m_context = nullptr;
    bool        m_initialized = false;
    bool        m_hrtfEnabled = false;

    std::unordered_map<ALuint, ALSourceState> m_sources;
    mutable std::mutex m_mutex;
};
