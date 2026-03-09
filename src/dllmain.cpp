// SPDX-License-Identifier: GPL-3.0-or-later
// UEOAL – DLL entry point and XAudio2 export shims
//
// Drop XAudio2_9.dll into a UE4/5 game's root directory.
// Windows DLL search order will load this before the system copy.
//
// Set the environment variable:
//   UEOAL_LOG_PATH=C:\path\to\ueoal.log
// to enable verbose logging.

#include <windows.h>
#include <xaudio2.h>

#include "logger.h"
#include "audio/openal_backend.h"
#include "hooks/x3daudio_hook.h"
#include "proxy/xaudio2_proxy.h"
#include "version.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Real XAudio2_9.dll from System32 (loaded lazily)
// ─────────────────────────────────────────────────────────────────────────────
namespace {
    HMODULE g_realXAudio2 = nullptr;

    HMODULE GetRealXAudio2() {
        if (g_realXAudio2) return g_realXAudio2;

        wchar_t sysDir[MAX_PATH]{};
        GetSystemDirectoryW(sysDir, MAX_PATH);

        wchar_t dllPath[MAX_PATH]{};
        swprintf_s(dllPath, L"%s\\XAudio2_9.dll", sysDir);

        g_realXAudio2 = LoadLibraryExW(dllPath, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!g_realXAudio2) {
            LOG_ERROR("Failed to load system XAudio2_9.dll from %ls  (err=%lu)",
                      dllPath, GetLastError());
        } else {
            LOG_INFO("Loaded real XAudio2_9.dll from %ls", dllPath);
        }
        return g_realXAudio2;
    }
} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
//  DLL lifecycle
// ─────────────────────────────────────────────────────────────────────────────
BOOL APIENTRY DllMain(HMODULE /*hModule*/, DWORD reason, LPVOID /*reserved*/) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        // Logging first – so any subsequent failures are captured
        Logger::Get().Initialize();
        LOG_INFO("UEOAL " UEOAL_VERSION_STRING
                 " initialising (build " UEOAL_BUILD_DATE " " UEOAL_BUILD_TIME ")");
        LOG_INFO("UEOAL project URL: " UEOAL_PROJECT_URL);

        // OpenAL Soft backend
        if (!OpenALBackend::Get().Initialize()) {
            LOG_ERROR("OpenAL backend init failed – spatial audio will be disabled");
        }

        // X3DAudio hook for 3D coordinate capture
        if (!X3DAudioHook::Install()) {
            LOG_WARN("X3DAudio hook unavailable – listener/emitter tracking degraded");
        }
        break;

    case DLL_PROCESS_DETACH:
        LOG_INFO("UEOAL detaching…");
        X3DAudioHook::Uninstall();
        OpenALBackend::Get().Shutdown();
        if (g_realXAudio2) {
            FreeLibrary(g_realXAudio2);
            g_realXAudio2 = nullptr;
        }
        break;

    default:
        break;
    }
    return TRUE;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Exported XAudio2 API surface
//  These symbols match what XAudio2_9.dll exports so that any game loading
//  XAudio2_9.dll will transparently pick up our proxy.
// ─────────────────────────────────────────────────────────────────────────────

HRESULT WINAPI XAudio2Create(IXAudio2**           ppXAudio2,
                              UINT32               Flags,
                              XAUDIO2_PROCESSOR    XAudio2Processor)
{
    LOG_INFO("XAudio2Create(Flags=0x%08X, Processor=0x%08X)", Flags, XAudio2Processor);

    HMODULE real = GetRealXAudio2();
    if (!real) return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);

    using FnXAudio2Create = HRESULT (WINAPI*)(IXAudio2**, UINT32, XAUDIO2_PROCESSOR);
    auto fn = reinterpret_cast<FnXAudio2Create>(GetProcAddress(real, "XAudio2Create"));
    if (!fn) {
        LOG_ERROR("XAudio2Create not found in real DLL");
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }

    IXAudio2* realObj = nullptr;
    HRESULT hr = fn(&realObj, Flags, XAudio2Processor);
    if (FAILED(hr) || !realObj) {
        LOG_ERROR("Real XAudio2Create failed: 0x%08X", hr);
        return hr;
    }

    *ppXAudio2 = new UEOALXAudio2(realObj);
    LOG_INFO("XAudio2Create succeeded → proxy %p", static_cast<void*>(*ppXAudio2));
    return S_OK;
}

// ── XAudio2CreateWithVersionInfo (UE 4.27+ / Win 10 SDK) ────────────────────
HRESULT WINAPI XAudio2CreateWithVersionInfo(IXAudio2**        ppXAudio2,
                                             UINT32            Flags,
                                             XAUDIO2_PROCESSOR XAudio2Processor,
                                             DWORD             /*ntddiVersion*/)
{
    // Delegate to our XAudio2Create; version param only used for driver hints
    return XAudio2Create(ppXAudio2, Flags, XAudio2Processor);
}

// ── CreateAudioVolumeMeter / CreateAudioReverb – forward to real DLL ─────────
HRESULT WINAPI CreateAudioVolumeMeter(IUnknown** ppApo) {
    HMODULE real = GetRealXAudio2();
    if (!real) return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    using Fn = HRESULT(WINAPI*)(IUnknown**);
    auto fn = reinterpret_cast<Fn>(GetProcAddress(real, "CreateAudioVolumeMeter"));
    return fn ? fn(ppApo) : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
}

HRESULT WINAPI CreateAudioReverb(IUnknown** ppApo) {
    HMODULE real = GetRealXAudio2();
    if (!real) return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    using Fn = HRESULT(WINAPI*)(IUnknown**);
    auto fn = reinterpret_cast<Fn>(GetProcAddress(real, "CreateAudioReverb"));
    return fn ? fn(ppApo) : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
}
