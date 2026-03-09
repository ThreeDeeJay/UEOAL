// SPDX-License-Identifier: GPL-3.0-or-later
// UEOAL – X3DAudio hook: captures 3D emitter/listener data before XAudio2 sees it
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <x3daudio.h>
#include <mutex>

// ─────────────────────────────────────────────────────────────────────────────
//  Plain-data structs to ferry X3DAudio data out of the hook
// ─────────────────────────────────────────────────────────────────────────────
struct CapturedEmitter {
    float posX  = 0.f, posY  = 0.f, posZ  = 0.f;
    float velX  = 0.f, velY  = 0.f, velZ  = 0.f;
    float fwdX  = 0.f, fwdY  = 0.f, fwdZ  = -1.f;
    float upX   = 0.f, upY   = 1.f, upZ   = 0.f;
    float innerRadius = 0.f;
    float outerRadius = 0.f;
};

struct CapturedListener {
    float posX  = 0.f, posY  = 0.f, posZ  = 0.f;
    float velX  = 0.f, velY  = 0.f, velZ  = 0.f;
    float fwdX  = 0.f, fwdY  = 0.f, fwdZ  = -1.f;
    float upX   = 0.f, upY   = 1.f, upZ   = 0.f;
};

// ─────────────────────────────────────────────────────────────────────────────
class X3DAudioHook {
public:
    /// Install the detour on X3DAudioCalculate (idempotent).
    static bool Install();

    /// Remove the detour; safe to call even if Install() was never called.
    static void Uninstall();

    /// Returns true + fills *out* with the most recent emitter data captured
    /// on the CALLING thread (thread-local, so audio thread safe).
    static bool TakeLastEmitter(CapturedEmitter& out);

    /// Current global listener (updated on every X3DAudioCalculate call).
    static CapturedListener GetListener();

private:
    using Fn = void (WINAPI*)(const X3DAUDIO_HANDLE,
                              const X3DAUDIO_LISTENER*,
                              const X3DAUDIO_EMITTER*,
                              UINT32,
                              X3DAUDIO_DSP_SETTINGS*);

    static void WINAPI Hooked(const X3DAUDIO_HANDLE   inst,
                              const X3DAUDIO_LISTENER* pL,
                              const X3DAUDIO_EMITTER*  pE,
                              UINT32                   flags,
                              X3DAUDIO_DSP_SETTINGS*   pDSP);

    static Fn    s_original;
    static bool  s_installed;

    // Thread-local "last emitter" set inside the hook, consumed by the proxy
    static thread_local CapturedEmitter s_tlsEmitter;
    static thread_local bool            s_tlsHasEmitter;

    // Global listener (protected by mutex)
    static CapturedListener s_listener;
    static std::mutex       s_listenerMutex;
};
