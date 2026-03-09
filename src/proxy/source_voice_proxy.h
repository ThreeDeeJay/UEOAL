// SPDX-License-Identifier: GPL-3.0-or-later
// UEOAL – IXAudio2SourceVoice proxy: intercepts audio submission + 3D positioning
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <xaudio2.h>

#include <atomic>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
class UEOALSourceVoice final : public IXAudio2SourceVoice {
public:
    UEOALSourceVoice(IXAudio2SourceVoice* real,
                     const WAVEFORMATEX*  pFmt);
    ~UEOALSourceVoice();

    // ── IUnknown (not a real COM object but UE calls QueryInterface) ─────────
    // XAudio2 voices don't inherit IUnknown; no-op body needed to satisfy vtable.

    // ── IXAudio2Voice ────────────────────────────────────────────────────────
    void STDMETHODCALLTYPE GetVoiceDetails(XAUDIO2_VOICE_DETAILS* pVoiceDetails) override;
    HRESULT STDMETHODCALLTYPE SetOutputVoices(const XAUDIO2_VOICE_SENDS* pSendList) override;
    HRESULT STDMETHODCALLTYPE SetEffectChain(const XAUDIO2_EFFECT_CHAIN* pEffectChain) override;
    HRESULT STDMETHODCALLTYPE EnableEffect(UINT32 EffectIndex, UINT32 OperationSet) override;
    HRESULT STDMETHODCALLTYPE DisableEffect(UINT32 EffectIndex, UINT32 OperationSet) override;
    void    STDMETHODCALLTYPE GetEffectState(UINT32 EffectIndex, BOOL* pEnabled) override;
    HRESULT STDMETHODCALLTYPE SetEffectParameters(UINT32 EffectIndex, const void* pParameters,
                                                  UINT32 ParametersByteSize,
                                                  UINT32 OperationSet) override;
    HRESULT STDMETHODCALLTYPE GetEffectParameters(UINT32 EffectIndex, void* pParameters,
                                                  UINT32 ParametersByteSize) override;
    HRESULT STDMETHODCALLTYPE SetFilterParameters(const XAUDIO2_FILTER_PARAMETERS* pParameters,
                                                  UINT32 OperationSet) override;
    void    STDMETHODCALLTYPE GetFilterParameters(XAUDIO2_FILTER_PARAMETERS* pParameters) override;
    HRESULT STDMETHODCALLTYPE SetOutputFilterParameters(IXAudio2Voice* pDestinationVoice,
                                                        const XAUDIO2_FILTER_PARAMETERS* pParameters,
                                                        UINT32 OperationSet) override;
    void    STDMETHODCALLTYPE GetOutputFilterParameters(IXAudio2Voice* pDestinationVoice,
                                                        XAUDIO2_FILTER_PARAMETERS* pParameters) override;
    HRESULT STDMETHODCALLTYPE SetVolume(float Volume, UINT32 OperationSet) override;
    void    STDMETHODCALLTYPE GetVolume(float* pVolume) override;
    HRESULT STDMETHODCALLTYPE SetChannelVolumes(UINT32 Channels, const float* pVolumes,
                                                UINT32 OperationSet) override;
    void    STDMETHODCALLTYPE GetChannelVolumes(UINT32 Channels, float* pVolumes) override;
    HRESULT STDMETHODCALLTYPE SetOutputMatrix(IXAudio2Voice* pDestinationVoice,
                                              UINT32 SourceChannels,
                                              UINT32 DestinationChannels,
                                              const float* pLevelMatrix,
                                              UINT32 OperationSet) override;
    void    STDMETHODCALLTYPE GetOutputMatrix(IXAudio2Voice* pDestinationVoice,
                                              UINT32 SourceChannels,
                                              UINT32 DestinationChannels,
                                              float* pLevelMatrix) override;
    void    STDMETHODCALLTYPE DestroyVoice() override;

    // ── IXAudio2SourceVoice ──────────────────────────────────────────────────
    HRESULT STDMETHODCALLTYPE Start(UINT32 Flags, UINT32 OperationSet) override;
    HRESULT STDMETHODCALLTYPE Stop(UINT32 Flags, UINT32 OperationSet) override;
    HRESULT STDMETHODCALLTYPE SubmitSourceBuffer(const XAUDIO2_BUFFER* pBuffer,
                                                 const XAUDIO2_BUFFER_WMA* pBufferWMA) override;
    HRESULT STDMETHODCALLTYPE FlushSourceBuffers() override;
    HRESULT STDMETHODCALLTYPE Discontinuity() override;
    HRESULT STDMETHODCALLTYPE ExitLoop(UINT32 OperationSet) override;
    void    STDMETHODCALLTYPE GetState(XAUDIO2_VOICE_STATE* pVoiceState,
                                       UINT32 Flags) override;
    HRESULT STDMETHODCALLTYPE SetFrequencyRatio(float Ratio, UINT32 OperationSet) override;
    void    STDMETHODCALLTYPE GetFrequencyRatio(float* pRatio) override;
    HRESULT STDMETHODCALLTYPE SetSourceSampleRate(UINT32 NewSourceSampleRate) override;

    // ── UEOAL internals ──────────────────────────────────────────────────────
    IXAudio2SourceVoice* GetReal() const noexcept { return m_real; }
    uint32_t             GetALSource() const noexcept { return m_alSource; }
    bool                 Is3D()        const noexcept { return m_is3D; }

    /// Called by the XAudio2 proxy after an X3DAudioCalculate association.
    void SetEmitterPosition(float ueX, float ueY, float ueZ,
                            float velX, float velY, float velZ);

private:
    IXAudio2SourceVoice* m_real      = nullptr;
    uint32_t             m_alSource  = 0;
    bool                 m_is3D      = false;

    // Format info from creation
    int  m_sampleRate   = 48000;
    int  m_channels     = 1;
    int  m_bitsPerSample= 16;
    bool m_isFloat      = false;

    float m_volume      = 1.0f;
    float m_pitchRatio  = 1.0f;

    /// True once SetOutputMatrix has been called with a non-trivial matrix
    /// (heuristic: if not all channels are equal → spatial source).
    bool m_matrixSeen   = false;

    void EnsureALSource();
};
