// SPDX-License-Identifier: GPL-3.0-or-later
// UEOAL – IXAudio2 proxy implementation
#include "xaudio2_proxy.h"
#include "source_voice_proxy.h"
#include "submix_voice_proxy.h"
#include "mastering_voice_proxy.h"
#include "../logger.h"
#include "../audio/openal_backend.h"

// ─────────────────────────────────────────────────────────────────────────────
UEOALXAudio2::UEOALXAudio2(IXAudio2* real) : m_real(real) {
    LOG_INFO("UEOALXAudio2 constructed (wrapping %p)", static_cast<void*>(real));
}
UEOALXAudio2::~UEOALXAudio2() {
    LOG_INFO("UEOALXAudio2 destroyed");
}

// ─────────────────────────────────────────────────────────────────────────────
//  IUnknown
// ─────────────────────────────────────────────────────────────────────────────
HRESULT STDMETHODCALLTYPE UEOALXAudio2::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) return E_POINTER;
    HRESULT hr = m_real->QueryInterface(riid, ppvObject);
    // If the QI succeeded on the real object, replace with ourselves for IXAudio2
    if (SUCCEEDED(hr) && riid == __uuidof(IXAudio2)) {
        m_real->Release();   // undo real AddRef
        *ppvObject = this;
        AddRef();
    }
    return hr;
}
ULONG STDMETHODCALLTYPE UEOALXAudio2::AddRef() {
    return ++m_refCount;
}
ULONG STDMETHODCALLTYPE UEOALXAudio2::Release() {
    ULONG ref = --m_refCount;
    if (ref == 0) {
        m_real->Release();
        delete this;
    }
    return ref;
}

// ─────────────────────────────────────────────────────────────────────────────
//  IXAudio2 – engine callbacks
// ─────────────────────────────────────────────────────────────────────────────
HRESULT STDMETHODCALLTYPE UEOALXAudio2::RegisterForCallbacks(IXAudio2EngineCallback* p) {
    return m_real->RegisterForCallbacks(p);
}
void STDMETHODCALLTYPE UEOALXAudio2::UnregisterForCallbacks(IXAudio2EngineCallback* p) {
    m_real->UnregisterForCallbacks(p);
}

// ─────────────────────────────────────────────────────────────────────────────
//  CreateSourceVoice – wrap in UEOALSourceVoice
// ─────────────────────────────────────────────────────────────────────────────
HRESULT STDMETHODCALLTYPE UEOALXAudio2::CreateSourceVoice(
    IXAudio2SourceVoice**       ppSourceVoice,
    const WAVEFORMATEX*         pSourceFormat,
    UINT32                      Flags,
    float                       MaxFrequencyRatio,
    IXAudio2VoiceCallback*      pCallback,
    const XAUDIO2_VOICE_SENDS*  pSendList,
    const XAUDIO2_EFFECT_CHAIN* pEffectChain)
{
    IXAudio2SourceVoice* real = nullptr;
    HRESULT hr = m_real->CreateSourceVoice(&real, pSourceFormat, Flags,
                                            MaxFrequencyRatio, pCallback,
                                            pSendList, pEffectChain);
    if (FAILED(hr) || !real) return hr;

    auto* proxy = new UEOALSourceVoice(real, pSourceFormat);
    *ppSourceVoice = proxy;

    LOG_INFO("CreateSourceVoice → proxy=%p  alSrc=%u  rate=%u ch=%u",
             static_cast<void*>(proxy), proxy->GetALSource(),
             pSourceFormat ? pSourceFormat->nSamplesPerSec : 0,
             pSourceFormat ? pSourceFormat->nChannels      : 0);
    return S_OK;
}

// ─────────────────────────────────────────────────────────────────────────────
//  CreateSubmixVoice – thin wrap
// ─────────────────────────────────────────────────────────────────────────────
HRESULT STDMETHODCALLTYPE UEOALXAudio2::CreateSubmixVoice(
    IXAudio2SubmixVoice**       ppSubmixVoice,
    UINT32                      InputChannels,
    UINT32                      InputSampleRate,
    UINT32                      Flags,
    UINT32                      ProcessingStage,
    const XAUDIO2_VOICE_SENDS*  pSendList,
    const XAUDIO2_EFFECT_CHAIN* pEffectChain)
{
    IXAudio2SubmixVoice* real = nullptr;
    HRESULT hr = m_real->CreateSubmixVoice(&real, InputChannels, InputSampleRate,
                                            Flags, ProcessingStage, pSendList, pEffectChain);
    if (FAILED(hr) || !real) return hr;
    *ppSubmixVoice = new UEOALSubmixVoice(real);
    return S_OK;
}

// ─────────────────────────────────────────────────────────────────────────────
//  CreateMasteringVoice – thin wrap
// ─────────────────────────────────────────────────────────────────────────────
HRESULT STDMETHODCALLTYPE UEOALXAudio2::CreateMasteringVoice(
    IXAudio2MasteringVoice**    ppMasteringVoice,
    UINT32                      InputChannels,
    UINT32                      InputSampleRate,
    UINT32                      Flags,
    LPCWSTR                     szDeviceId,
    const XAUDIO2_EFFECT_CHAIN* pEffectChain,
    AUDIO_STREAM_CATEGORY       StreamCategory)
{
    IXAudio2MasteringVoice* real = nullptr;
    HRESULT hr = m_real->CreateMasteringVoice(&real, InputChannels, InputSampleRate,
                                               Flags, szDeviceId, pEffectChain, StreamCategory);
    if (FAILED(hr) || !real) return hr;
    *ppMasteringVoice = new UEOALMasteringVoice(real);
    LOG_INFO("CreateMasteringVoice → proxy=%p  rate=%u  ch=%u",
             static_cast<void*>(*ppMasteringVoice), InputSampleRate, InputChannels);
    return S_OK;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Engine control
// ─────────────────────────────────────────────────────────────────────────────
HRESULT STDMETHODCALLTYPE UEOALXAudio2::StartEngine() {
    LOG_INFO("XAudio2 engine started");
    return m_real->StartEngine();
}
void STDMETHODCALLTYPE UEOALXAudio2::StopEngine() {
    LOG_INFO("XAudio2 engine stopped");
    m_real->StopEngine();
}
HRESULT STDMETHODCALLTYPE UEOALXAudio2::CommitChanges(UINT32 op) {
    // Housekeeping: prune stale OpenAL buffers on every commit
    OpenALBackend::Get().PruneProcessedBuffers();
    return m_real->CommitChanges(op);
}
void STDMETHODCALLTYPE UEOALXAudio2::GetPerformanceData(XAUDIO2_PERFORMANCE_DATA* p) {
    m_real->GetPerformanceData(p);
}
void STDMETHODCALLTYPE UEOALXAudio2::SetDebugConfiguration(
    const XAUDIO2_DEBUG_CONFIGURATION* p, void* r) {
    m_real->SetDebugConfiguration(p, r);
}
