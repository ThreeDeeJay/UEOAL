// SPDX-License-Identifier: GPL-3.0-or-later
// UEOAL – IXAudio2SourceVoice proxy implementation
#include "source_voice_proxy.h"
#include "../logger.h"
#include "../audio/openal_backend.h"
#include "../hooks/x3daudio_hook.h"

#include <mmreg.h>   // WAVEFORMATEXTENSIBLE, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT

// ─────────────────────────────────────────────────────────────────────────────
UEOALSourceVoice::UEOALSourceVoice(IXAudio2SourceVoice* real,
                                   const WAVEFORMATEX*  pFmt)
    : m_real(real)
{
    if (pFmt) {
        m_sampleRate    = static_cast<int>(pFmt->nSamplesPerSec);
        m_channels      = static_cast<int>(pFmt->nChannels);
        m_bitsPerSample = static_cast<int>(pFmt->wBitsPerSample);
        m_isFloat       = (pFmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);

        if (pFmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
            auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(pFmt);
            m_isFloat = (ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
            m_bitsPerSample = static_cast<int>(ext->Samples.wValidBitsPerSample
                                               ? ext->Samples.wValidBitsPerSample
                                               : pFmt->wBitsPerSample);
        }
    }

    EnsureALSource();

    LOG_INFO("SourceVoice created  alSrc=%u rate=%d ch=%d bits=%d float=%d",
             m_alSource, m_sampleRate, m_channels, m_bitsPerSample, m_isFloat);
}

UEOALSourceVoice::~UEOALSourceVoice() {
    if (m_alSource) {
        OpenALBackend::Get().DestroySource(m_alSource);
        m_alSource = 0;
    }
}

void UEOALSourceVoice::EnsureALSource() {
    if (m_alSource) return;
    m_alSource = OpenALBackend::Get().CreateSource();
}

// ─────────────────────────────────────────────────────────────────────────────
//  IXAudio2Voice – forwarded methods
// ─────────────────────────────────────────────────────────────────────────────
void STDMETHODCALLTYPE UEOALSourceVoice::GetVoiceDetails(XAUDIO2_VOICE_DETAILS* p) {
    m_real->GetVoiceDetails(p);
}
HRESULT STDMETHODCALLTYPE UEOALSourceVoice::SetOutputVoices(const XAUDIO2_VOICE_SENDS* p) {
    return m_real->SetOutputVoices(p);
}
HRESULT STDMETHODCALLTYPE UEOALSourceVoice::SetEffectChain(const XAUDIO2_EFFECT_CHAIN* p) {
    return m_real->SetEffectChain(p);
}
HRESULT STDMETHODCALLTYPE UEOALSourceVoice::EnableEffect(UINT32 idx, UINT32 op) {
    return m_real->EnableEffect(idx, op);
}
HRESULT STDMETHODCALLTYPE UEOALSourceVoice::DisableEffect(UINT32 idx, UINT32 op) {
    return m_real->DisableEffect(idx, op);
}
void STDMETHODCALLTYPE UEOALSourceVoice::GetEffectState(UINT32 idx, BOOL* p) {
    m_real->GetEffectState(idx, p);
}
HRESULT STDMETHODCALLTYPE UEOALSourceVoice::SetEffectParameters(UINT32 idx, const void* p,
                                                                 UINT32 sz, UINT32 op) {
    return m_real->SetEffectParameters(idx, p, sz, op);
}
HRESULT STDMETHODCALLTYPE UEOALSourceVoice::GetEffectParameters(UINT32 idx, void* p, UINT32 sz) {
    return m_real->GetEffectParameters(idx, p, sz);
}
HRESULT STDMETHODCALLTYPE UEOALSourceVoice::SetFilterParameters(const XAUDIO2_FILTER_PARAMETERS* p,
                                                                 UINT32 op) {
    return m_real->SetFilterParameters(p, op);
}
void STDMETHODCALLTYPE UEOALSourceVoice::GetFilterParameters(XAUDIO2_FILTER_PARAMETERS* p) {
    m_real->GetFilterParameters(p);
}
HRESULT STDMETHODCALLTYPE UEOALSourceVoice::SetOutputFilterParameters(IXAudio2Voice* dst,
                                                                       const XAUDIO2_FILTER_PARAMETERS* p,
                                                                       UINT32 op) {
    return m_real->SetOutputFilterParameters(dst, p, op);
}
void STDMETHODCALLTYPE UEOALSourceVoice::GetOutputFilterParameters(IXAudio2Voice* dst,
                                                                    XAUDIO2_FILTER_PARAMETERS* p) {
    m_real->GetOutputFilterParameters(dst, p);
}
HRESULT STDMETHODCALLTYPE UEOALSourceVoice::SetVolume(float v, UINT32 op) {
    m_volume = v;
    if (m_alSource) OpenALBackend::Get().SetSourceGain(m_alSource, v);
    return m_real->SetVolume(v, op);
}
void STDMETHODCALLTYPE UEOALSourceVoice::GetVolume(float* p) {
    m_real->GetVolume(p);
}
HRESULT STDMETHODCALLTYPE UEOALSourceVoice::SetChannelVolumes(UINT32 ch, const float* v, UINT32 op) {
    return m_real->SetChannelVolumes(ch, v, op);
}
void STDMETHODCALLTYPE UEOALSourceVoice::GetChannelVolumes(UINT32 ch, float* v) {
    m_real->GetChannelVolumes(ch, v);
}
void STDMETHODCALLTYPE UEOALSourceVoice::GetOutputMatrix(IXAudio2Voice* dst, UINT32 src, UINT32 dest,
                                                          float* mtx) {
    m_real->GetOutputMatrix(dst, src, dest, mtx);
}
void STDMETHODCALLTYPE UEOALSourceVoice::GetState(XAUDIO2_VOICE_STATE* p, UINT32 flags) {
    m_real->GetState(p, flags);
}
void STDMETHODCALLTYPE UEOALSourceVoice::GetFrequencyRatio(float* p) {
    m_real->GetFrequencyRatio(p);
}

// ─────────────────────────────────────────────────────────────────────────────
//  SetOutputMatrix – key interception point
//  Called by UE after X3DAudioCalculate; we associate the pending emitter with
//  this source voice so SubmitSourceBuffer can feed the right AL source.
// ─────────────────────────────────────────────────────────────────────────────
HRESULT STDMETHODCALLTYPE UEOALSourceVoice::SetOutputMatrix(IXAudio2Voice* dst,
                                                            UINT32 srcCh,
                                                            UINT32 dstCh,
                                                            const float* mtx,
                                                            UINT32 op) {
    // Try to claim a pending emitter from the X3DAudio hook
    CapturedEmitter em;
    if (X3DAudioHook::TakeLastEmitter(em)) {
        m_is3D = true;
        if (m_alSource) {
            OpenALBackend::Get().SetSource3D(m_alSource, true);
            OpenALBackend::Get().SetSourcePosition(m_alSource, em.posX, em.posY, em.posZ);
            OpenALBackend::Get().SetSourceVelocity(m_alSource, em.velX, em.velY, em.velZ);
        }
        LOG_DEBUG("SetOutputMatrix: associated emitter (%.1f,%.1f,%.1f) → alSrc=%u",
                  em.posX, em.posY, em.posZ, m_alSource);
    }

    m_matrixSeen = true;
    // Always forward to real voice so XAudio2 pipeline is intact
    return m_real->SetOutputMatrix(dst, srcCh, dstCh, mtx, op);
}

// ─────────────────────────────────────────────────────────────────────────────
//  SubmitSourceBuffer – feed decoded PCM to OpenAL
// ─────────────────────────────────────────────────────────────────────────────
HRESULT STDMETHODCALLTYPE UEOALSourceVoice::SubmitSourceBuffer(const XAUDIO2_BUFFER* pBuf,
                                                               const XAUDIO2_BUFFER_WMA* pWMA) {
    if (pBuf && m_alSource && pBuf->pAudioData && pBuf->AudioBytes > 0) {
        OpenALBackend::Get().SubmitAudioBuffer(
            m_alSource,
            pBuf->pAudioData,
            pBuf->AudioBytes,
            m_sampleRate,
            m_channels,
            m_bitsPerSample,
            m_isFloat);
    }
    // Always also forward to real XAudio2 so non-spatial sounds / SFX still play
    // through the game's normal pipeline (we effectively dual-feed).
    // For purely spatial sources, you may wish to silence the XAudio2 side by
    // calling SetVolume(0) on m_real.  Toggle with UEOAL_MUTE_XAUDIO2 in future.
    return m_real->SubmitSourceBuffer(pBuf, pWMA);
}

// ─────────────────────────────────────────────────────────────────────────────
HRESULT STDMETHODCALLTYPE UEOALSourceVoice::Start(UINT32 flags, UINT32 op) {
    if (m_alSource) OpenALBackend::Get().StartSource(m_alSource);
    return m_real->Start(flags, op);
}
HRESULT STDMETHODCALLTYPE UEOALSourceVoice::Stop(UINT32 flags, UINT32 op) {
    if (m_alSource) OpenALBackend::Get().StopSource(m_alSource);
    return m_real->Stop(flags, op);
}
HRESULT STDMETHODCALLTYPE UEOALSourceVoice::FlushSourceBuffers() {
    if (m_alSource) OpenALBackend::Get().StopSource(m_alSource);
    return m_real->FlushSourceBuffers();
}
HRESULT STDMETHODCALLTYPE UEOALSourceVoice::Discontinuity() {
    return m_real->Discontinuity();
}
HRESULT STDMETHODCALLTYPE UEOALSourceVoice::ExitLoop(UINT32 op) {
    return m_real->ExitLoop(op);
}
HRESULT STDMETHODCALLTYPE UEOALSourceVoice::SetFrequencyRatio(float ratio, UINT32 op) {
    m_pitchRatio = ratio;
    if (m_alSource) OpenALBackend::Get().SetSourceFrequencyRatio(m_alSource, ratio);
    return m_real->SetFrequencyRatio(ratio, op);
}
HRESULT STDMETHODCALLTYPE UEOALSourceVoice::SetSourceSampleRate(UINT32 rate) {
    m_sampleRate = static_cast<int>(rate);
    return m_real->SetSourceSampleRate(rate);
}

// ─────────────────────────────────────────────────────────────────────────────
void STDMETHODCALLTYPE UEOALSourceVoice::DestroyVoice() {
    LOG_INFO("SourceVoice::DestroyVoice alSrc=%u", m_alSource);
    if (m_alSource) {
        OpenALBackend::Get().DestroySource(m_alSource);
        m_alSource = 0;
    }
    m_real->DestroyVoice();
    delete this;
}

// ─────────────────────────────────────────────────────────────────────────────
void UEOALSourceVoice::SetEmitterPosition(float ueX, float ueY, float ueZ,
                                          float velX, float velY, float velZ) {
    m_is3D = true;
    if (m_alSource) {
        OpenALBackend::Get().SetSource3D(m_alSource, true);
        OpenALBackend::Get().SetSourcePosition(m_alSource, ueX, ueY, ueZ);
        OpenALBackend::Get().SetSourceVelocity(m_alSource, velX, velY, velZ);
    }
}
