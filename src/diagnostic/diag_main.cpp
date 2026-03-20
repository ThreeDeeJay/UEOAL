// SPDX-License-Identifier: GPL-3.0-or-later
// UEOAL Diagnostic Passthrough - xaudio2_9redist_diag.dll
//
// USAGE:
//   1. Rename xaudio2_9redist.dll -> xaudio2_9redist_real.dll
//   2. Rename xaudio2_9redist_diag.dll -> xaudio2_9redist.dll
//   3. Launch game. Log is written to:
//        - %UEOAL_LOG_PATH%  (if env var set), OR
//        - <same folder as the DLL>\ueoal_diag.log  (always, as fallback)
//      Also emitted via OutputDebugString (visible in Sysinternals DebugView).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#include <strsafe.h>

typedef UINT32 XA2Processor;

// ---------------------------------------------------------------------------
//  Logging
// ---------------------------------------------------------------------------
static HANDLE g_logFile = INVALID_HANDLE_VALUE;

static void LogOpen()
{
    // Determine our own path via FROM_ADDRESS
    HMODULE hSelf = nullptr;
    wchar_t selfPath[MAX_PATH]{};
    if (GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&LogOpen), &hSelf) && hSelf)
        GetModuleFileNameW(hSelf, selfPath, MAX_PATH);

    // 1. Try UEOAL_LOG_PATH env var
    wchar_t logPath[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"UEOAL_LOG_PATH", logPath, MAX_PATH) == 0) {
        // 2. Fall back to ueoal_diag.log next to the DLL
        StringCchCopyW(logPath, MAX_PATH, selfPath);
        wchar_t* slash = wcsrchr(logPath, L'\\');
        if (slash) *(slash + 1) = L'\0';
        else logPath[0] = L'\0';
        StringCchCatW(logPath, MAX_PATH, L"ueoal_diag.log");
    }

    g_logFile = CreateFileW(logPath, GENERIC_WRITE, FILE_SHARE_READ,
                            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    // Log where we're writing (to DebugView at minimum)
    wchar_t msg[MAX_PATH + 64];
    if (g_logFile != INVALID_HANDLE_VALUE)
        StringCchPrintfW(msg, ARRAYSIZE(msg), L"[UEOAL-DIAG] Log opened: %s", logPath);
    else
        StringCchPrintfW(msg, ARRAYSIZE(msg),
            L"[UEOAL-DIAG] WARNING: Could not open log at %s (err=%lu)",
            logPath, GetLastError());
    OutputDebugStringW(msg);
}

static void Log(const wchar_t* fmt, ...)
{
    wchar_t wbuf[1024];
    va_list va;
    va_start(va, fmt);
    StringCchVPrintfW(wbuf, 1024, fmt, va);
    va_end(va);

    wchar_t tagged[1088];
    StringCchPrintfW(tagged, 1088, L"[UEOAL-DIAG] %s\n", wbuf);
    OutputDebugStringW(tagged);

    if (g_logFile != INVALID_HANDLE_VALUE) {
        char abuf[1088];
        int n = WideCharToMultiByte(CP_UTF8, 0, tagged, -1, abuf, 1088, nullptr, nullptr);
        if (n > 1) { DWORD w; WriteFile(g_logFile, abuf, n - 1, &w, nullptr); }
    }
}

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------
static HMODULE g_real = nullptr;

static bool GetSelfPath(wchar_t* out, DWORD len)
{
    HMODULE hSelf = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&GetSelfPath), &hSelf) || !hSelf) {
        Log(L"GetSelfPath: GetModuleHandleExW failed err=%lu", GetLastError());
        return false;
    }
    DWORD n = GetModuleFileNameW(hSelf, out, len);
    if (n == 0) {
        Log(L"GetSelfPath: GetModuleFileNameW failed err=%lu", GetLastError());
        return false;
    }
    Log(L"GetSelfPath: %s", out);
    return true;
}

static void LogEnvVars()
{
    // Log relevant env vars so we can confirm UEOAL_LOG_PATH / PATH etc.
    const wchar_t* vars[] = {
        L"UEOAL_LOG_PATH", L"SystemRoot", L"PATH", nullptr
    };
    for (int i = 0; vars[i]; ++i) {
        wchar_t val[512]{};
        DWORD r = GetEnvironmentVariableW(vars[i], val, 512);
        if (r > 0)
            Log(L"  ENV %s = %s", vars[i], val);
        else
            Log(L"  ENV %s = <not set> (err=%lu)", vars[i], GetLastError());
    }
}

static void EnumerateExports(HMODULE hMod, const wchar_t* label)
{
    auto base  = reinterpret_cast<const BYTE*>(hMod);
    auto dos   = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) { Log(L"  %s: bad DOS sig", label); return; }
    auto nt    = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)  { Log(L"  %s: bad NT sig",  label); return; }
    DWORD rva  = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    if (!rva) { Log(L"  %s: no export dir", label); return; }
    auto exp   = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + rva);
    auto names = reinterpret_cast<const DWORD*>(base + exp->AddressOfNames);
    auto ords  = reinterpret_cast<const WORD* >(base + exp->AddressOfNameOrdinals);
    Log(L"  %s: %lu named exports:", label, exp->NumberOfNames);
    for (DWORD i = 0; i < exp->NumberOfNames; ++i)
        Log(L"    [%3lu] ord=%u  %S", i, exp->Base + ords[i],
            reinterpret_cast<const char*>(base + names[i]));
}

static void LogLoadedModules()
{
    // Snapshot the first 64 loaded modules so we can see what else is present
    HMODULE mods[64]{};
    DWORD needed = 0;
    if (!EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed))
        return;
    DWORD count = (needed / (DWORD)sizeof(HMODULE) < 64) ? needed / (DWORD)sizeof(HMODULE) : 64;
    Log(L"Loaded modules (%lu shown, %lu total):", count, needed / (DWORD)sizeof(HMODULE));
    for (DWORD i = 0; i < count; ++i) {
        wchar_t name[MAX_PATH]{};
        GetModuleFileNameW(mods[i], name, MAX_PATH);
        Log(L"  %p  %s", static_cast<void*>(mods[i]), name);
    }
}

static HMODULE LoadReal()
{
    wchar_t selfPath[MAX_PATH]{};
    if (!GetSelfPath(selfPath, MAX_PATH)) return nullptr;

    wchar_t realPath[MAX_PATH];
    StringCchCopyW(realPath, MAX_PATH, selfPath);
    wchar_t* slash = wcsrchr(realPath, L'\\');
    if (!slash) { Log(L"LoadReal: no backslash in self path"); return nullptr; }
    *(slash + 1) = L'\0';
    StringCchCatW(realPath, MAX_PATH, L"xaudio2_9redist_real.dll");

    Log(L"LoadReal: trying LoadLibraryW(\"%s\")", realPath);

    // Check file existence first for a cleaner error message
    DWORD attr = GetFileAttributesW(realPath);
    if (attr == INVALID_FILE_ATTRIBUTES)
        Log(L"LoadReal: file does not exist (err=%lu) - did you rename xaudio2_9redist.dll?",
            GetLastError());
    else
        Log(L"LoadReal: file exists (attr=0x%08X)", attr);

    HMODULE h = LoadLibraryW(realPath);
    if (!h) {
        Log(L"LoadReal: LoadLibraryW FAILED err=%lu", GetLastError());
        // Also dump loaded modules to help diagnose missing dependencies
        LogLoadedModules();
        return nullptr;
    }
    Log(L"LoadReal: SUCCESS hModule=%p", static_cast<void*>(h));
    EnumerateExports(h, L"xaudio2_9redist_real");
    return h;
}

static void* Resolve(const char* name)
{
    if (!g_real) { Log(L"Resolve('%S'): g_real is null", name); return nullptr; }
    void* p = reinterpret_cast<void*>(GetProcAddress(g_real, name));
    Log(p ? L"Resolve('%S'): %p" : L"Resolve('%S'): NOT FOUND", name, p);
    return p;
}

// ---------------------------------------------------------------------------
//  DllMain
// ---------------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        LogOpen();
        Log(L"===== UEOAL Diagnostic Passthrough attached =====");
        Log(L"hModule=%p", static_cast<void*>(hModule));
        LogEnvVars();
        g_real = LoadReal();
        Log(g_real ? L"DllMain: real DLL loaded OK, passthrough active"
                   : L"DllMain: real DLL FAILED to load - all XAudio2 calls will fail");
    } else if (reason == DLL_PROCESS_DETACH) {
        Log(L"===== UEOAL Diagnostic Passthrough detaching =====");
        if (g_real) { FreeLibrary(g_real); g_real = nullptr; }
        if (g_logFile != INVALID_HANDLE_VALUE) { CloseHandle(g_logFile); g_logFile = INVALID_HANDLE_VALUE; }
    }
    return TRUE;
}

// ---------------------------------------------------------------------------
//  Forwarded exports  (aliased via diag_exports.def: Diag_* -> public name)
// ---------------------------------------------------------------------------

HRESULT WINAPI
Diag_XAudio2CreateWithVersionInfo(void** ppXAudio2, UINT32 Flags,
                                   XA2Processor Processor, DWORD NTDDIVersion)
{
    Log(L"XAudio2CreateWithVersionInfo(Flags=0x%08X Processor=0x%08X NTDDIVersion=0x%08X)",
        Flags, Processor, NTDDIVersion);
    using Fn  = HRESULT(WINAPI*)(void**, UINT32, XA2Processor, DWORD);
    using Fn2 = HRESULT(WINAPI*)(void**, UINT32, XA2Processor);
    auto fn = reinterpret_cast<Fn>(Resolve("XAudio2CreateWithVersionInfo"));
    if (!fn) {
        auto fn2 = reinterpret_cast<Fn2>(Resolve("XAudio2Create"));
        if (fn2) {
            HRESULT hr = fn2(ppXAudio2, Flags, Processor);
            Log(L"  <- XAudio2Create fallback: 0x%08X *pp=%p", hr, ppXAudio2 ? *ppXAudio2 : nullptr);
            return hr;
        }
        Log(L"  <- E_NOTIMPL (no entry point)");
        return E_NOTIMPL;
    }
    HRESULT hr = fn(ppXAudio2, Flags, Processor, NTDDIVersion);
    Log(L"  <- 0x%08X *pp=%p", hr, ppXAudio2 ? *ppXAudio2 : nullptr);
    return hr;
}

HRESULT WINAPI
Diag_XAudio2Create(void** ppXAudio2, UINT32 Flags, XA2Processor Processor)
{
    Log(L"XAudio2Create(Flags=0x%08X Processor=0x%08X)", Flags, Processor);
    return Diag_XAudio2CreateWithVersionInfo(ppXAudio2, Flags, Processor, 0x0A000000);
}

HRESULT WINAPI Diag_CreateAudioVolumeMeter(void** ppApo)
{
    Log(L"CreateAudioVolumeMeter()");
    using Fn = HRESULT(WINAPI*)(void**);
    auto fn = reinterpret_cast<Fn>(Resolve("CreateAudioVolumeMeter"));
    HRESULT hr = fn ? fn(ppApo) : E_NOTIMPL;
    Log(L"  <- 0x%08X", hr); return hr;
}

HRESULT WINAPI Diag_CreateAudioReverb(void** ppApo)
{
    Log(L"CreateAudioReverb()");
    using Fn = HRESULT(WINAPI*)(void**);
    auto fn = reinterpret_cast<Fn>(Resolve("CreateAudioReverb"));
    HRESULT hr = fn ? fn(ppApo) : E_NOTIMPL;
    Log(L"  <- 0x%08X", hr); return hr;
}

HRESULT WINAPI Diag_CreateFX(const void* clsid, void** ppEffect,
                              const void* pInit, UINT32 initSize)
{
    Log(L"CreateFX(initSize=%u)", initSize);
    using Fn = HRESULT(WINAPI*)(const void*, void**, const void*, UINT32);
    auto fn = reinterpret_cast<Fn>(Resolve("CreateFX"));
    HRESULT hr = fn ? fn(clsid, ppEffect, pInit, initSize) : E_NOTIMPL;
    Log(L"  <- 0x%08X", hr); return hr;
}

VOID WINAPI Diag_X3DAudioCalculate(const void* inst, const void* pL,
                                    const void* pE, UINT32 flags, void* pDSP)
{
    Log(L"X3DAudioCalculate(flags=0x%08X)", flags);
    using Fn = VOID(WINAPI*)(const void*, const void*, const void*, UINT32, void*);
    auto fn = reinterpret_cast<Fn>(Resolve("X3DAudioCalculate"));
    if (fn) fn(inst, pL, pE, flags, pDSP);
}

HRESULT WINAPI Diag_X3DAudioInitialize(UINT32 mask, float sos, void* pInst)
{
    Log(L"X3DAudioInitialize(SpeakerMask=0x%08X SpeedOfSound=%.1f)", mask, sos);
    using Fn = HRESULT(WINAPI*)(UINT32, float, void*);
    auto fn = reinterpret_cast<Fn>(Resolve("X3DAudioInitialize"));
    HRESULT hr = fn ? fn(mask, sos, pInst) : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    Log(L"  <- 0x%08X", hr); return hr;
}
