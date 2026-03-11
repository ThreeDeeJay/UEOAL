// SPDX-License-Identifier: GPL-3.0-or-later
// UEOAL - DLL entry point and XAudio2 export shims
//
// Two deployment modes depending on how the game loads XAudio2:
//
// MODE A - System XAudio2 (most UE5 / modern UE4 games)
//   Place XAudio2_9.dll + OpenAL32.dll next to the game .exe.
//   Windows DLL search finds ours before System32.
//
// MODE B - XAudio2 Redistributable (UE4 games with the redist)
//   The game loads e.g.:
//     Engine\Binaries\ThirdParty\Windows\XAudio2_9\x64\xaudio2_9redist.dll
//   Steps:
//     1. Rename the original  xaudio2_9redist.dll  ->  xaudio2_9redist_real.dll
//     2. Place UEOAL's DLL as  xaudio2_9redist.dll  in the same folder.
//     3. Place OpenAL32.dll    in the same folder.
//   UEOAL detects it is running as the redist variant at runtime and loads
//   xaudio2_9redist_real.dll from its own directory as the passthrough DLL.
//
// Set UEOAL_LOG_PATH=C:\path\to\ueoal.log to enable verbose logging.

#include <windows.h>
#include <xaudio2.h>

#include "logger.h"
#include "audio/openal_backend.h"
#include "hooks/x3daudio_hook.h"
#include "proxy/xaudio2_proxy.h"
#include "version.h"

// ---------------------------------------------------------------------------
//  Runtime DLL resolution
//  Detects whether we were loaded as xaudio2_9redist.dll or XAudio2_9.dll
//  and locates the appropriate passthrough DLL accordingly.
// ---------------------------------------------------------------------------
namespace {

HMODULE g_self        = nullptr;  // our own HMODULE, captured in DllMain
HMODULE g_realXAudio2 = nullptr;

// Returns true if our own filename (case-insensitive) matches needle.
bool OurNameContains(const wchar_t* needle) {
    wchar_t selfPath[MAX_PATH]{};
    if (!GetModuleFileNameW(g_self, selfPath, MAX_PATH)) return false;

    // Lower-case both strings for comparison
    wchar_t lower[MAX_PATH]{};
    wcsncpy_s(lower, selfPath, MAX_PATH);
    CharLowerW(lower);

    wchar_t needleLow[64]{};
    wcsncpy_s(needleLow, needle, 64);
    CharLowerW(needleLow);

    return wcsstr(lower, needleLow) != nullptr;
}

// Returns the directory containing our own DLL, with trailing backslash.
bool GetOurDirectory(wchar_t* outDir, DWORD outLen) {
    wchar_t selfPath[MAX_PATH]{};
    if (!GetModuleFileNameW(g_self, selfPath, MAX_PATH)) return false;
    wcsncpy_s(outDir, outLen, selfPath, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(outDir, L'\\');
    if (lastSlash) *(lastSlash + 1) = L'\0';
    return true;
}

HMODULE LoadRealXAudio2() {
    wchar_t path[MAX_PATH]{};

    if (OurNameContains(L"xaudio2_9redist")) {
        // MODE B: we are the redist replacement.
        // Load xaudio2_9redist_real.dll from our own directory (the user
        // renamed the original before placing us here).
        wchar_t dir[MAX_PATH]{};
        if (GetOurDirectory(dir, MAX_PATH)) {
            swprintf_s(path, L"%sxaudio2_9redist_real.dll", dir);
            HMODULE h = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
            if (h) {
                LOG_INFO("Redist mode: loaded real DLL from %ls", path);
                return h;
            }
            LOG_ERROR("Redist mode: failed to load %ls (err=%lu)", path, GetLastError());
            LOG_ERROR("Did you rename xaudio2_9redist.dll -> xaudio2_9redist_real.dll?");
        }
        return nullptr;
    }

    // MODE A: we are XAudio2_9.dll - load the system copy from System32.
    wchar_t sysDir[MAX_PATH]{};
    GetSystemDirectoryW(sysDir, MAX_PATH);
    swprintf_s(path, L"%s\\XAudio2_9.dll", sysDir);
    HMODULE h = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (h) {
        LOG_INFO("System mode: loaded real DLL from %ls", path);
    } else {
        LOG_ERROR("System mode: failed to load %ls (err=%lu)", path, GetLastError());
    }
    return h;
}

HMODULE GetRealXAudio2() {
    if (!g_realXAudio2)
        g_realXAudio2 = LoadRealXAudio2();
    return g_realXAudio2;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
//  DLL lifecycle
// ---------------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*reserved*/) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        g_self = hModule;
        DisableThreadLibraryCalls(hModule);

        Logger::Get().Initialize();
        LOG_INFO("UEOAL " UEOAL_VERSION_STRING
                 " initialising (build " UEOAL_BUILD_DATE " " UEOAL_BUILD_TIME ")");
        LOG_INFO("UEOAL project URL: " UEOAL_PROJECT_URL);

        {
            wchar_t selfPath[MAX_PATH]{};
            GetModuleFileNameW(hModule, selfPath, MAX_PATH);
            LOG_INFO("Loaded as: %ls", selfPath);
        }

        if (!OpenALBackend::Get().Initialize())
            LOG_ERROR("OpenAL backend init failed - spatial audio will be disabled");

        if (!X3DAudioHook::Install())
            LOG_WARN("X3DAudio hook unavailable - listener/emitter tracking degraded");
        break;

    case DLL_PROCESS_DETACH:
        LOG_INFO("UEOAL detaching...");
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

// ---------------------------------------------------------------------------
//  Exported XAudio2 API surface  (symbols resolved via exports.def aliases)
// ---------------------------------------------------------------------------

HRESULT WINAPI UEOAL_XAudio2Create(IXAudio2**        ppXAudio2,
                                    UINT32            Flags,
                                    XAUDIO2_PROCESSOR XAudio2Processor)
{
    LOG_INFO("XAudio2Create(Flags=0x%08X, Processor=0x%08X)", Flags, XAudio2Processor);

    HMODULE real = GetRealXAudio2();
    if (!real) return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);

    using Fn = HRESULT(WINAPI*)(IXAudio2**, UINT32, XAUDIO2_PROCESSOR);
    auto fn = reinterpret_cast<Fn>(GetProcAddress(real, "XAudio2Create"));
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
    LOG_INFO("XAudio2Create succeeded -> proxy %p", static_cast<void*>(*ppXAudio2));
    return S_OK;
}

// UE 4.27+ calls XAudio2CreateWithVersionInfo when available.
HRESULT WINAPI UEOAL_XAudio2CreateWithVersionInfo(IXAudio2**        ppXAudio2,
                                                   UINT32            Flags,
                                                   XAUDIO2_PROCESSOR XAudio2Processor,
                                                   DWORD             /*ntddiVersion*/)
{
    return UEOAL_XAudio2Create(ppXAudio2, Flags, XAudio2Processor);
}

HRESULT WINAPI UEOAL_CreateAudioVolumeMeter(IUnknown** ppApo) {
    HMODULE real = GetRealXAudio2();
    if (!real) return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    using Fn = HRESULT(WINAPI*)(IUnknown**);
    auto fn = reinterpret_cast<Fn>(GetProcAddress(real, "CreateAudioVolumeMeter"));
    return fn ? fn(ppApo) : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
}

HRESULT WINAPI UEOAL_CreateAudioReverb(IUnknown** ppApo) {
    HMODULE real = GetRealXAudio2();
    if (!real) return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    using Fn = HRESULT(WINAPI*)(IUnknown**);
    auto fn = reinterpret_cast<Fn>(GetProcAddress(real, "CreateAudioReverb"));
    return fn ? fn(ppApo) : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
}
