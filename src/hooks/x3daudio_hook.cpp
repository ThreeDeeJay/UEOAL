// SPDX-License-Identifier: GPL-3.0-or-later
// UEOAL – X3DAudio hook implementation
#include "x3daudio_hook.h"
#include "../logger.h"
#include "../audio/openal_backend.h"

#include <MinHook.h>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────────────
//  Static member definitions
// ─────────────────────────────────────────────────────────────────────────────
X3DAudioHook::Fn    X3DAudioHook::s_original    = nullptr;
bool                X3DAudioHook::s_installed   = false;

thread_local CapturedEmitter X3DAudioHook::s_tlsEmitter     {};
thread_local bool            X3DAudioHook::s_tlsHasEmitter  { false };

CapturedListener    X3DAudioHook::s_listener{};
std::mutex          X3DAudioHook::s_listenerMutex{};

// ─────────────────────────────────────────────────────────────────────────────
bool X3DAudioHook::Install(HMODULE hExtraModule) {
    if (s_installed) return true;

    MH_STATUS mhStatus = MH_Initialize();
    if (mhStatus != MH_OK && mhStatus != MH_ERROR_ALREADY_INITIALIZED) {
        LOG_ERROR("MH_Initialize failed: %s", MH_StatusToString(mhStatus));
        return false;
    }

    // Locate the DLL that owns X3DAudioCalculate.
    //
    // Mode A (system XAudio2): X3DAudio lives in X3DAudio1_7.dll, X3DAudio1_8.dll,
    //   or is re-exported from XAudio2_9.dll on modern Windows.
    // Mode B (redist): X3DAudioCalculate is an export of xaudio2_9redist_real.dll
    //   itself. hExtraModule is the already-loaded real redist handle.
    //
    // Search order: named system DLLs first, then hExtraModule, so Mode A always
    // wins if both are present (system DLL is the authoritative implementation).

    const char* x3dLibNames[] = {
        "X3DAudio1_7.dll",
        "X3DAudio1_8.dll",
        "XAudio2_9.dll",  // recent Windows - X3DAudioCalculate re-exported here
        nullptr
    };

    HMODULE hX3D = nullptr;
    for (int i = 0; x3dLibNames[i]; ++i) {
        hX3D = GetModuleHandleA(x3dLibNames[i]);
        if (!hX3D) hX3D = LoadLibraryA(x3dLibNames[i]);
        if (hX3D && GetProcAddress(hX3D, "X3DAudioCalculate")) {
            LOG_INFO("X3DAudio found in %s", x3dLibNames[i]);
            break;
        }
        hX3D = nullptr;
    }

    // Mode B fallback: use the extra module (real redist DLL) if provided
    if (!hX3D && hExtraModule && GetProcAddress(hExtraModule, "X3DAudioCalculate")) {
        hX3D = hExtraModule;
        LOG_INFO("X3DAudio found in hExtraModule (redist passthrough mode)");
    }

    if (!hX3D) {
        LOG_WARN("X3DAudio DLL not found - 3D position capture disabled");
        return false;
    }

    void* pTarget = GetProcAddress(hX3D, "X3DAudioCalculate");
    if (!pTarget) {
        LOG_WARN("X3DAudioCalculate not found in loaded DLL");
        return false;
    }

    mhStatus = MH_CreateHook(pTarget,
                             reinterpret_cast<void*>(&Hooked),
                             reinterpret_cast<void**>(&s_original));
    if (mhStatus != MH_OK) {
        LOG_ERROR("MH_CreateHook(X3DAudioCalculate) failed: %s",
                  MH_StatusToString(mhStatus));
        return false;
    }

    mhStatus = MH_EnableHook(pTarget);
    if (mhStatus != MH_OK) {
        LOG_ERROR("MH_EnableHook(X3DAudioCalculate) failed: %s",
                  MH_StatusToString(mhStatus));
        return false;
    }

    s_installed = true;
    LOG_INFO("X3DAudioCalculate hook installed successfully");
    return true;
}

void X3DAudioHook::Uninstall() {
    if (!s_installed) return;
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    s_installed = false;
    LOG_INFO("X3DAudioCalculate hook removed");
}

// ─────────────────────────────────────────────────────────────────────────────
//  The hooked replacement
// ─────────────────────────────────────────────────────────────────────────────
void WINAPI X3DAudioHook::Hooked(const X3DAUDIO_HANDLE    inst,
                                  const X3DAUDIO_LISTENER* pL,
                                  const X3DAUDIO_EMITTER*  pE,
                                  UINT32                   flags,
                                  X3DAUDIO_DSP_SETTINGS*   pDSP) {
    // ── Call the real function first ────────────────────────────────────────
    if (s_original) s_original(inst, pL, pE, flags, pDSP);

    // ── Capture emitter into thread-local storage ───────────────────────────
    if (pE) {
        s_tlsEmitter.posX = pE->Position.x;
        s_tlsEmitter.posY = pE->Position.y;
        s_tlsEmitter.posZ = pE->Position.z;

        s_tlsEmitter.velX = pE->Velocity.x;
        s_tlsEmitter.velY = pE->Velocity.y;
        s_tlsEmitter.velZ = pE->Velocity.z;

        if (pE->OrientFront.x != 0 || pE->OrientFront.y != 0 || pE->OrientFront.z != 0) {
            s_tlsEmitter.fwdX = pE->OrientFront.x;
            s_tlsEmitter.fwdY = pE->OrientFront.y;
            s_tlsEmitter.fwdZ = pE->OrientFront.z;
        }
        if (pE->OrientTop.x != 0 || pE->OrientTop.y != 0 || pE->OrientTop.z != 0) {
            s_tlsEmitter.upX = pE->OrientTop.x;
            s_tlsEmitter.upY = pE->OrientTop.y;
            s_tlsEmitter.upZ = pE->OrientTop.z;
        }

        s_tlsEmitter.innerRadius = pE->InnerRadius;
        s_tlsEmitter.outerRadius = 0.f; // InnerRadiusAngle is in radians, not useful here

        s_tlsHasEmitter = true;
    }

    // ── Capture listener into shared state ─────────────────────────────────
    if (pL) {
        std::lock_guard<std::mutex> lk(s_listenerMutex);
        s_listener.posX = pL->Position.x;
        s_listener.posY = pL->Position.y;
        s_listener.posZ = pL->Position.z;

        s_listener.velX = pL->Velocity.x;
        s_listener.velY = pL->Velocity.y;
        s_listener.velZ = pL->Velocity.z;

        s_listener.fwdX = pL->OrientFront.x;
        s_listener.fwdY = pL->OrientFront.y;
        s_listener.fwdZ = pL->OrientFront.z;

        s_listener.upX  = pL->OrientTop.x;
        s_listener.upY  = pL->OrientTop.y;
        s_listener.upZ  = pL->OrientTop.z;

        // Push immediately to OpenAL listener
        OpenALBackend& al = OpenALBackend::Get();
        if (al.IsInitialized()) {
            al.SetListenerPosition(pL->Position.x, pL->Position.y, pL->Position.z);
            al.SetListenerVelocity(pL->Velocity.x, pL->Velocity.y, pL->Velocity.z);
            const float fwd[3]{ pL->OrientFront.x, pL->OrientFront.y, pL->OrientFront.z };
            const float up [3]{ pL->OrientTop.x,   pL->OrientTop.y,   pL->OrientTop.z   };
            al.SetListenerOrientation(fwd, up);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
bool X3DAudioHook::TakeLastEmitter(CapturedEmitter& out) {
    if (!s_tlsHasEmitter) return false;
    out              = s_tlsEmitter;
    s_tlsHasEmitter  = false;
    return true;
}

CapturedListener X3DAudioHook::GetListener() {
    std::lock_guard<std::mutex> lk(s_listenerMutex);
    return s_listener;
}
