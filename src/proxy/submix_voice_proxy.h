// SPDX-License-Identifier: GPL-3.0-or-later
// UEOAL – IXAudio2SubmixVoice proxy (thin pass-through)
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xaudio2.h>

class UEOALSubmixVoice final : public IXAudio2SubmixVoice {
public:
    explicit UEOALSubmixVoice(IXAudio2SubmixVoice* real) : m_real(real) {}

    IXAudio2SubmixVoice* GetReal() const noexcept { return m_real; }

    // IXAudio2Voice
    void    STDMETHODCALLTYPE GetVoiceDetails(XAUDIO2_VOICE_DETAILS* p) override { m_real->GetVoiceDetails(p); }
    HRESULT STDMETHODCALLTYPE SetOutputVoices(const XAUDIO2_VOICE_SENDS* p) override { return m_real->SetOutputVoices(p); }
    HRESULT STDMETHODCALLTYPE SetEffectChain(const XAUDIO2_EFFECT_CHAIN* p) override { return m_real->SetEffectChain(p); }
    HRESULT STDMETHODCALLTYPE EnableEffect(UINT32 i, UINT32 op) override { return m_real->EnableEffect(i, op); }
    HRESULT STDMETHODCALLTYPE DisableEffect(UINT32 i, UINT32 op) override { return m_real->DisableEffect(i, op); }
    void    STDMETHODCALLTYPE GetEffectState(UINT32 i, BOOL* p) override { m_real->GetEffectState(i, p); }
    HRESULT STDMETHODCALLTYPE SetEffectParameters(UINT32 i, const void* p, UINT32 sz, UINT32 op) override { return m_real->SetEffectParameters(i, p, sz, op); }
    HRESULT STDMETHODCALLTYPE GetEffectParameters(UINT32 i, void* p, UINT32 sz) override { return m_real->GetEffectParameters(i, p, sz); }
    HRESULT STDMETHODCALLTYPE SetFilterParameters(const XAUDIO2_FILTER_PARAMETERS* p, UINT32 op) override { return m_real->SetFilterParameters(p, op); }
    void    STDMETHODCALLTYPE GetFilterParameters(XAUDIO2_FILTER_PARAMETERS* p) override { m_real->GetFilterParameters(p); }
    HRESULT STDMETHODCALLTYPE SetOutputFilterParameters(IXAudio2Voice* d, const XAUDIO2_FILTER_PARAMETERS* p, UINT32 op) override { return m_real->SetOutputFilterParameters(d, p, op); }
    void    STDMETHODCALLTYPE GetOutputFilterParameters(IXAudio2Voice* d, XAUDIO2_FILTER_PARAMETERS* p) override { m_real->GetOutputFilterParameters(d, p); }
    HRESULT STDMETHODCALLTYPE SetVolume(float v, UINT32 op) override { return m_real->SetVolume(v, op); }
    void    STDMETHODCALLTYPE GetVolume(float* p) override { m_real->GetVolume(p); }
    HRESULT STDMETHODCALLTYPE SetChannelVolumes(UINT32 c, const float* v, UINT32 op) override { return m_real->SetChannelVolumes(c, v, op); }
    void    STDMETHODCALLTYPE GetChannelVolumes(UINT32 c, float* v) override { m_real->GetChannelVolumes(c, v); }
    HRESULT STDMETHODCALLTYPE SetOutputMatrix(IXAudio2Voice* d, UINT32 s, UINT32 ds, const float* m, UINT32 op) override { return m_real->SetOutputMatrix(d, s, ds, m, op); }
    void    STDMETHODCALLTYPE GetOutputMatrix(IXAudio2Voice* d, UINT32 s, UINT32 ds, float* m) override { m_real->GetOutputMatrix(d, s, ds, m); }
    void    STDMETHODCALLTYPE DestroyVoice() override { m_real->DestroyVoice(); delete this; }

private:
    IXAudio2SubmixVoice* m_real = nullptr;
};
