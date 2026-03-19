// SPDX-License-Identifier: GPL-3.0-or-later
// UEOAL - DLL entry point and XAudio2 export shims
//
// Two deployment modes depending on how the game loads XAudio2:
//
// MODE A - System XAudio2 (UE5 / most modern UE4)
//   Place XAudio2_9.dll + OpenAL32.dll next to the game .exe.
//   Windows DLL search finds ours before System32.
//
// MODE B - XAudio2 Redistributable (older UE4 games)
//   The game loads e.g.:
//     Engine\Binaries\ThirdParty\Windows\XAudio2_9\x64\xaudio2_9redist.dll
//   Steps:
//     1. Rename the original  xaudio2_9redist.dll  ->  xaudio2_9redist_real.dll
//     2. Place UEOAL's DLL as  xaudio2_9redist.dll  in the same folder.
//     3. Place OpenAL32.dll    in the same folder.
//   UEOAL detects the redist variant at runtime and loads
//   xaudio2_9redist_real.dll from its own directory as the passthrough DLL.
//
// Set UEOAL_LOG_PATH=C:\path\to\ueoal.log to enable verbose logging.
// All path-resolution steps also emit via OutputDebugStringW so they are
// visible in Sysinternals DebugView even without UEOAL_LOG_PATH configured.

#include <windows.h>
#include <xaudio2.h>
#include <strsafe.h>

#include "logger.h"
#include "audio/openal_backend.h"
#include "hooks/x3daudio_hook.h"
#include "proxy/xaudio2_proxy.h"
#include "version.h"

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------
namespace {

HMODULE g_realXAudio2 = nullptr;

// Emit a message to both the UEOAL log file (if configured) and
// OutputDebugStringW (always) so path resolution issues are visible in
// DebugView / VS output window without needing UEOAL_LOG_PATH.
void DbgLog(const wchar_t* msg)
{
    wchar_t buf[512];
    StringCchPrintfW(buf, 512, L"[UEOAL] %s\n", msg);
    OutputDebugStringW(buf);
}

// ---------------------------------------------------------------------------
//  GetSelfPath – the ONLY reliable way to get your own DLL's path.
//
//  GetModuleHandleExW with GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS uses the
//  virtual address of a function inside *this* DLL to locate its module.
//  This works regardless of how the DLL was loaded (full path, relative path,
//  LoadLibraryEx flags, etc.) and does NOT increment the reference count
//  (UNCHANGED_REFCOUNT), so no matching FreeLibrary is needed.
// ---------------------------------------------------------------------------
bool GetSelfPath(wchar_t* outPath, DWORD outLen)
{
    HMODULE hSelf = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&GetSelfPath),   // any address inside this DLL
            &hSelf) || !hSelf)
    {
        DbgLog(L"GetModuleHandleExW(FROM_ADDRESS) failed");
        return false;
    }
    DWORD n = GetModuleFileNameW(hSelf, outPath, outLen);
    if (n == 0 || n >= outLen) {
        DbgLog(L"GetModuleFileNameW failed or path too long");
        return false;
    }
    return true;
}

// Returns the directory containing our own DLL (with trailing backslash).
bool GetSelfDir(wchar_t* outDir, DWORD outLen)
{
    if (!GetSelfPath(outDir, outLen)) return false;
    wchar_t* lastSlash = wcsrchr(outDir, L'\\');
    if (!lastSlash) return false;
    *(lastSlash + 1) = L'\0';
    return true;
}

// Case-insensitive check whether our own filename contains needle.
bool SelfNameContains(const wchar_t* needle)
{
    wchar_t path[MAX_PATH]{};
    if (!GetSelfPath(path, MAX_PATH)) return false;
    CharLowerW(path);
    wchar_t low[64]{};
    StringCchCopyW(low, 64, needle);
    CharLowerW(low);
    return wcsstr(path, low) != nullptr;
}

// ---------------------------------------------------------------------------
//  LoadRealXAudio2
// ---------------------------------------------------------------------------
HMODULE LoadRealXAudio2()
{
    wchar_t msg[MAX_PATH + 64]{};
    wchar_t path[MAX_PATH]{};

    // --- Log our own path so we can verify mode detection ---
    wchar_t selfPath[MAX_PATH]{};
    if (GetSelfPath(selfPath, MAX_PATH)) {
        StringCchPrintfW(msg, ARRAYSIZE(msg), L"UEOAL loaded as: %s", selfPath);
        DbgLog(msg);
        LOG_INFO("UEOAL loaded as: %ls", selfPath);
    }

    if (SelfNameContains(L"xaudio2_9redist")) {
        // ----------------------------------------------------------------
        // MODE B: we are the redist placeholder.
        // Load xaudio2_9redist_real.dll from our own directory.
        // ----------------------------------------------------------------
        wchar_t dir[MAX_PATH]{};
        if (!GetSelfDir(dir, MAX_PATH)) {
            DbgLog(L"MODE B: GetSelfDir failed - cannot locate xaudio2_9redist_real.dll");
            LOG_ERROR("MODE B: GetSelfDir failed");
            return nullptr;
        }

        StringCchPrintfW(path, MAX_PATH, L"%sxaudio2_9redist_real.dll", dir);
        StringCchPrintfW(msg, ARRAYSIZE(msg), L"MODE B: trying to load: %s", path);
        DbgLog(msg);
        LOG_INFO("MODE B: trying to load: %ls", path);

        // Use plain LoadLibraryW (no LOAD_WITH_ALTERED_SEARCH_PATH).
        // LOAD_WITH_ALTERED_SEARCH_PATH changes search behaviour relative to
        // the path argument and can cause DLL dependency resolution to fail
        // for the redist DLL's own imports.
        HMODULE h = LoadLibraryW(path);
        if (h) {
            DbgLog(L"MODE B: loaded xaudio2_9redist_real.dll OK");
            LOG_INFO("MODE B: loaded xaudio2_9redist_real.dll OK");
            return h;
        }

        DWORD err = GetLastError();
        StringCchPrintfW(msg, ARRAYSIZE(msg),
            L"MODE B: LoadLibraryW failed  err=%lu  path=%s", err, path);
        DbgLog(msg);
        LOG_ERROR("MODE B: LoadLibraryW failed  err=%lu  path=%ls", err, path);
        DbgLog(L"MODE B: ensure xaudio2_9redist.dll was renamed to xaudio2_9redist_real.dll");
        LOG_ERROR("MODE B: ensure xaudio2_9redist.dll was renamed to xaudio2_9redist_real.dll");
        return nullptr;
    }

    // ----------------------------------------------------------------
    // MODE A: we are XAudio2_9.dll - load the system copy from System32.
    // ----------------------------------------------------------------
    wchar_t sysDir[MAX_PATH]{};
    GetSystemDirectoryW(sysDir, MAX_PATH);
    StringCchPrintfW(path, MAX_PATH, L"%s\\XAudio2_9.dll", sysDir);

    StringCchPrintfW(msg, ARRAYSIZE(msg), L"MODE A: trying to load: %s", path);
    DbgLog(msg);
    LOG_INFO("MODE A: trying to load: %ls", path);

    // LOAD_LIBRARY_SEARCH_SYSTEM32 is the safest flag for system DLLs -
    // it prevents DLL planting and does not alter the loaded DLL's own
    // dependency search path.
    HMODULE h = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!h) {
        // Fallback: some older Windows builds may not support SEARCH_SYSTEM32
        h = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    }

    if (h) {
        DbgLog(L"MODE A: loaded system XAudio2_9.dll OK");
        LOG_INFO("MODE A: loaded system XAudio2_9.dll OK");
    } else {
        DWORD err = GetLastError();
        StringCchPrintfW(msg, ARRAYSIZE(msg),
            L"MODE A: LoadLibraryExW failed  err=%lu  path=%s", err, path);
        DbgLog(msg);
        LOG_ERROR("MODE A: LoadLibraryExW failed  err=%lu  path=%ls", err, path);
    }
    return h;
}

HMODULE GetRealXAudio2()
{
    if (!g_realXAudio2)
        g_realXAudio2 = LoadRealXAudio2();
    return g_realXAudio2;
}

} // anonymous namespace

// Public accessor so DllMain can pass the real module to X3DAudioHook.
static HMODULE GetRealXAudio2Module() { return GetRealXAudio2(); }

// ---------------------------------------------------------------------------
//  DLL lifecycle
// ---------------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*reserved*/)
{
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        Logger::Get().Initialize();
        LOG_INFO("UEOAL " UEOAL_VERSION_STRING
                 " initialising (build " UEOAL_BUILD_DATE " " UEOAL_BUILD_TIME ")");
        LOG_INFO("UEOAL project URL: " UEOAL_PROJECT_URL);

        if (!OpenALBackend::Get().Initialize())
            LOG_ERROR("OpenAL backend init failed - spatial audio will be disabled");

        // Load the real DLL eagerly so its handle is available for
        // the X3DAudio hook search (needed in Mode B / redist).
        HMODULE hReal = GetRealXAudio2Module();
        if (!hReal)
            LOG_WARN("Real XAudio2 DLL not loaded yet at attach - Mode B hook may be limited");

        if (!X3DAudioHook::Install(hReal))
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
    if (!real) {
        DbgLog(L"XAudio2Create: GetRealXAudio2() returned null - returning error to caller");
        return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    }

    // xaudio2_9redist.dll does NOT export XAudio2Create - it only exports
    // XAudio2CreateWithVersionInfo.  Try the plain name first (system DLL),
    // then fall back to the versioned name (redist DLL).
    using Fn     = HRESULT(WINAPI*)(IXAudio2**, UINT32, XAUDIO2_PROCESSOR);
    using FnVer  = HRESULT(WINAPI*)(IXAudio2**, UINT32, XAUDIO2_PROCESSOR, DWORD);

    auto fn    = reinterpret_cast<Fn>   (GetProcAddress(real, "XAudio2Create"));
    auto fnVer = reinterpret_cast<FnVer>(GetProcAddress(real, "XAudio2CreateWithVersionInfo"));

    if (!fn && !fnVer) {
        DbgLog(L"XAudio2Create: neither XAudio2Create nor XAudio2CreateWithVersionInfo"
               L" found in real DLL");
        LOG_ERROR("XAudio2Create: no usable entry point in real DLL");
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }

    if (fn) {
        DbgLog(L"XAudio2Create: calling XAudio2Create in real DLL");
    } else {
        DbgLog(L"XAudio2Create: XAudio2Create not found, falling back to"
               L" XAudio2CreateWithVersionInfo (redist DLL)");
    }

    IXAudio2* realObj = nullptr;
    // NTDDI_WIN10 (0x0A000000) is the version the redist DLL expects;
    // the system DLL ignores this parameter entirely.
    HRESULT hr = fn ? fn(&realObj, Flags, XAudio2Processor)
                    : fnVer(&realObj, Flags, XAudio2Processor, 0x0A000000);
    if (FAILED(hr) || !realObj) {
        wchar_t msg[64];
        StringCchPrintfW(msg, 64, L"XAudio2Create: real DLL returned 0x%08X", hr);
        DbgLog(msg);
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

HRESULT WINAPI UEOAL_CreateAudioVolumeMeter(IUnknown** ppApo)
{
    HMODULE real = GetRealXAudio2();
    if (!real) return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    using Fn = HRESULT(WINAPI*)(IUnknown**);
    auto fn = reinterpret_cast<Fn>(GetProcAddress(real, "CreateAudioVolumeMeter"));
    return fn ? fn(ppApo) : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
}

HRESULT WINAPI UEOAL_CreateAudioReverb(IUnknown** ppApo)
{
    HMODULE real = GetRealXAudio2();
    if (!real) return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    using Fn = HRESULT(WINAPI*)(IUnknown**);
    auto fn = reinterpret_cast<Fn>(GetProcAddress(real, "CreateAudioReverb"));
    return fn ? fn(ppApo) : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
}

// ---------------------------------------------------------------------------
//  Redist-mode additional exports
//  The xaudio2_9redist.dll exports X3DAudioCalculate, X3DAudioInitialize,
//  and CreateFX in addition to the XAudio2 API. Forward them to the real DLL.
//  X3DAudioCalculate is also hooked by MinHook on the real DLL, so the hook
//  fires when we call through - no duplicate capture logic needed here.
// ---------------------------------------------------------------------------

// X3DAUDIO_HANDLE is BYTE[20]; decays to pointer at call boundary. void* is
// ABI-compatible for both input (const) and output usages.
VOID WINAPI UEOAL_X3DAudioCalculate(
    const void* pInstance,
    const void* pListener,
    const void* pEmitter,
    UINT32      Flags,
    void*       pDSPSettings)
{
    HMODULE real = GetRealXAudio2();
    if (!real) return;
    using Fn = VOID(WINAPI*)(const void*, const void*, const void*, UINT32, void*);
    auto fn = reinterpret_cast<Fn>(GetProcAddress(real, "X3DAudioCalculate"));
    if (fn) fn(pInstance, pListener, pEmitter, Flags, pDSPSettings);
}

// X3DAudioInitialize writes into the X3DAUDIO_HANDLE output buffer (20 bytes).
HRESULT WINAPI UEOAL_X3DAudioInitialize(
    UINT32 SpeakerChannelMask,
    float  SpeedOfSound,
    void*  pInstance)          // X3DAUDIO_HANDLE* - output
{
    HMODULE real = GetRealXAudio2();
    if (!real) return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    using Fn = HRESULT(WINAPI*)(UINT32, float, void*);
    auto fn = reinterpret_cast<Fn>(GetProcAddress(real, "X3DAudioInitialize"));
    return fn ? fn(SpeakerChannelMask, SpeedOfSound, pInstance)
              : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
}

// CreateFX(REFCLSID, IUnknown**, const void*, UINT32)
HRESULT WINAPI UEOAL_CreateFX(
    const void* clsid,
    void**      ppEffect,
    const void* pInitData,
    UINT32      InitDataByteSize)
{
    HMODULE real = GetRealXAudio2();
    if (!real) return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    using Fn = HRESULT(WINAPI*)(const void*, void**, const void*, UINT32);
    auto fn = reinterpret_cast<Fn>(GetProcAddress(real, "CreateFX"));
    return fn ? fn(clsid, ppEffect, pInitData, InitDataByteSize)
              : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
}
