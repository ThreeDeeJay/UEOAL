// SPDX-License-Identifier: GPL-3.0-or-later
// UEOAL – IXAudio2 proxy: wraps the real XAudio2_9.dll instance
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xaudio2.h>
#include <atomic>

class UEOALXAudio2 final : public IXAudio2 {
public:
    explicit UEOALXAudio2(IXAudio2* real);
    ~UEOALXAudio2();

    // ── IUnknown ──────────────────────────────────────────────────────────────
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
    ULONG   STDMETHODCALLTYPE AddRef()  override;
    ULONG   STDMETHODCALLTYPE Release() override;

    // ── IXAudio2 ──────────────────────────────────────────────────────────────
    HRESULT STDMETHODCALLTYPE RegisterForCallbacks(IXAudio2EngineCallback* pCallback) override;
    void    STDMETHODCALLTYPE UnregisterForCallbacks(IXAudio2EngineCallback* pCallback) override;

    HRESULT STDMETHODCALLTYPE CreateSourceVoice(
        IXAudio2SourceVoice**       ppSourceVoice,
        const WAVEFORMATEX*         pSourceFormat,
        UINT32                      Flags,
        float                       MaxFrequencyRatio,
        IXAudio2VoiceCallback*      pCallback,
        const XAUDIO2_VOICE_SENDS*  pSendList,
        const XAUDIO2_EFFECT_CHAIN* pEffectChain) override;

    HRESULT STDMETHODCALLTYPE CreateSubmixVoice(
        IXAudio2SubmixVoice**       ppSubmixVoice,
        UINT32                      InputChannels,
        UINT32                      InputSampleRate,
        UINT32                      Flags,
        UINT32                      ProcessingStage,
        const XAUDIO2_VOICE_SENDS*  pSendList,
        const XAUDIO2_EFFECT_CHAIN* pEffectChain) override;

    HRESULT STDMETHODCALLTYPE CreateMasteringVoice(
        IXAudio2MasteringVoice**    ppMasteringVoice,
        UINT32                      InputChannels,
        UINT32                      InputSampleRate,
        UINT32                      Flags,
        LPCWSTR                     szDeviceId,
        const XAUDIO2_EFFECT_CHAIN* pEffectChain,
        AUDIO_STREAM_CATEGORY       StreamCategory) override;

    HRESULT STDMETHODCALLTYPE StartEngine() override;
    void    STDMETHODCALLTYPE StopEngine()  override;
    HRESULT STDMETHODCALLTYPE CommitChanges(UINT32 OperationSet) override;
    void    STDMETHODCALLTYPE GetPerformanceData(XAUDIO2_PERFORMANCE_DATA* pPerfData) override;
    void    STDMETHODCALLTYPE SetDebugConfiguration(
        const XAUDIO2_DEBUG_CONFIGURATION* pDebugConfiguration,
        void*                              pReserved) override;

    IXAudio2* GetReal() const noexcept { return m_real; }

private:
    IXAudio2*             m_real     = nullptr;
    std::atomic<ULONG>    m_refCount { 1 };
};
