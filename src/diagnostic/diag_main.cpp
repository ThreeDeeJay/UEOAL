// SPDX-License-Identifier: GPL-3.0-or-later
// UEOAL Diagnostic Passthrough - xaudio2_9redist.dll
//
// Pure forwarding proxy with exhaustive logging. No OpenAL, no proxy objects.
// Every call is forwarded verbatim to xaudio2_9redist_real.dll.
//
// PURPOSE: Confirm the real DLL loads, enumerate its actual exports, and
//          record exactly what the game calls so we can hook it correctly.
//
// USAGE:
//   1. Rename original  xaudio2_9redist.dll -> xaudio2_9redist_real.dll
//   2. Drop this DLL as xaudio2_9redist.dll in the same folder
//   3. Set UEOAL_LOG_PATH=C:\ueoal_diag.log  (or read OutputDebugString in DebugView)
//   4. Launch the game, reproduce the error, examine the log

#include <windows.h>
#include <strsafe.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
//  Minimal file logger - no external dependencies
// ---------------------------------------------------------------------------
static HANDLE  g_logFile  = INVALID_HANDLE_VALUE;
static HMODULE g_real     = nullptr;

static void Log(const wchar_t* fmt, ...)
{
    wchar_t wbuf[1024];
    va_list va;
    va_start(va, fmt);
    StringCchVPrintfW(wbuf, 1024, fmt, va);
    va_end(va);

    // Always emit to debugger
    wchar_t tagged[1088];
    StringCchPrintfW(tagged, 1088, L"[UEOAL-DIAG] %s\n", wbuf);
    OutputDebugStringW(tagged);

    // File log if UEOAL_LOG_PATH is set
    if (g_logFile == INVALID_HANDLE_VALUE) {
        wchar_t path[MAX_PATH]{};
        if (GetEnvironmentVariableW(L"UEOAL_LOG_PATH", path, MAX_PATH) > 0) {
            g_logFile = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ,
                                    nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (g_logFile != INVALID_HANDLE_VALUE)
                SetFilePointer(g_logFile, 0, nullptr, FILE_END);
        }
    }
    if (g_logFile != INVALID_HANDLE_VALUE) {
        char abuf[1088];
        int n = WideCharToMultiByte(CP_UTF8, 0, tagged, -1, abuf, 1088, nullptr, nullptr);
        if (n > 1) {
            DWORD written;
            WriteFile(g_logFile, abuf, n - 1, &written, nullptr);
        }
    }
}

// ---------------------------------------------------------------------------
//  GetSelfPath - uses FROM_ADDRESS so it works regardless of load path
// ---------------------------------------------------------------------------
static bool GetSelfPath(wchar_t* out, DWORD len)
{
    HMODULE hSelf = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&GetSelfPath), &hSelf) || !hSelf)
        return false;
    return GetModuleFileNameW(hSelf, out, len) > 0;
}

// ---------------------------------------------------------------------------
//  EnumerateExports - log every export of a loaded DLL
// ---------------------------------------------------------------------------
static void EnumerateExports(HMODULE hMod, const wchar_t* label)
{
    auto base = reinterpret_cast<const BYTE*>(hMod);
    auto dos  = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) { Log(L"  %s: bad DOS header", label); return; }

    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) { Log(L"  %s: bad NT header", label); return; }

    DWORD exportRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    if (!exportRva) { Log(L"  %s: no export directory", label); return; }

    auto exp  = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + exportRva);
    auto names = reinterpret_cast<const DWORD*>(base + exp->AddressOfNames);
    auto ords  = reinterpret_cast<const WORD* >(base + exp->AddressOfNameOrdinals);
    auto funcs = reinterpret_cast<const DWORD*>(base + exp->AddressOfFunctions);

    Log(L"  %s exports (%lu named):", label, exp->NumberOfNames);
    for (DWORD i = 0; i < exp->NumberOfNames; ++i) {
        const char* name = reinterpret_cast<const char*>(base + names[i]);
        DWORD rva = funcs[ords[i]];
        Log(L"    [%3lu] @%u  %S", i, exp->Base + ords[i], name);
        (void)rva;
    }
}

// ---------------------------------------------------------------------------
//  Load the real DLL from the same directory as ourselves
// ---------------------------------------------------------------------------
static HMODULE LoadReal()
{
    wchar_t selfPath[MAX_PATH]{};
    if (!GetSelfPath(selfPath, MAX_PATH)) {
        Log(L"LoadReal: GetSelfPath failed (err=%lu)", GetLastError());
        return nullptr;
    }
    Log(L"LoadReal: we are loaded as: %s", selfPath);

    // Build path to xaudio2_9redist_real.dll beside ourselves
    wchar_t realPath[MAX_PATH];
    StringCchCopyW(realPath, MAX_PATH, selfPath);
    wchar_t* slash = wcsrchr(realPath, L'\\');
    if (!slash) { Log(L"LoadReal: no backslash in self path"); return nullptr; }
    *(slash + 1) = L'\0';
    StringCchCatW(realPath, MAX_PATH, L"xaudio2_9redist_real.dll");

    Log(L"LoadReal: attempting LoadLibraryW(\"%s\")", realPath);
    HMODULE h = LoadLibraryW(realPath);
    if (!h) {
        Log(L"LoadReal: FAILED  err=%lu", GetLastError());
        return nullptr;
    }
    Log(L"LoadReal: SUCCESS  hModule=%p", (void*)h);
    EnumerateExports(h, L"xaudio2_9redist_real");
    return h;
}

// ---------------------------------------------------------------------------
//  Helpers to resolve and call exports in the real DLL
// ---------------------------------------------------------------------------
static void* Resolve(const char* name)
{
    if (!g_real) return nullptr;
    void* p = reinterpret_cast<void*>(GetProcAddress(g_real, name));
    if (!p) Log(L"Resolve: '%S' NOT FOUND in real DLL", name);
    return p;
}

// ---------------------------------------------------------------------------
//  DllMain
// ---------------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        Log(L"===== UEOAL Diagnostic Passthrough attached =====");
        g_real = LoadReal();
        if (!g_real)
            Log(L"DllMain: real DLL failed to load - all XAudio2 calls will fail");
        else
            Log(L"DllMain: real DLL loaded OK, passthrough active");
    }
    else if (reason == DLL_PROCESS_DETACH) {
        Log(L"===== UEOAL Diagnostic Passthrough detaching =====");
        if (g_real) { FreeLibrary(g_real); g_real = nullptr; }
        if (g_logFile != INVALID_HANDLE_VALUE) { CloseHandle(g_logFile); }
    }
    return TRUE;
}

// ---------------------------------------------------------------------------
//  Forwarded exports
//  We forward every export the redist is known to have, logging each call.
//  If the real DLL has additional exports, they will appear in the startup
//  enumeration log - add forwarders here as needed.
// ---------------------------------------------------------------------------

extern "C" HRESULT WINAPI
XAudio2CreateWithVersionInfo(IXAudio2** ppXAudio2, UINT32 Flags,
                              XAUDIO2_PROCESSOR Processor, DWORD NTDDIVersion)
{
    Log(L"XAudio2CreateWithVersionInfo(Flags=0x%08X, Processor=0x%08X, NTDDIVersion=0x%08X)",
        Flags, Processor, NTDDIVersion);

    using Fn = HRESULT(WINAPI*)(IXAudio2**, UINT32, XAUDIO2_PROCESSOR, DWORD);
    auto fn = reinterpret_cast<Fn>(Resolve("XAudio2CreateWithVersionInfo"));
    if (!fn) {
        // Also try plain XAudio2Create as fallback
        using Fn2 = HRESULT(WINAPI*)(IXAudio2**, UINT32, XAUDIO2_PROCESSOR);
        auto fn2 = reinterpret_cast<Fn2>(Resolve("XAudio2Create"));
        if (fn2) {
            Log(L"  -> falling back to XAudio2Create");
            HRESULT hr = fn2(ppXAudio2, Flags, Processor);
            Log(L"  <- XAudio2Create returned 0x%08X  *ppXAudio2=%p", hr, ppXAudio2 ? *ppXAudio2 : nullptr);
            return hr;
        }
        Log(L"  -> NO ENTRY POINT FOUND - returning E_NOTIMPL");
        return E_NOTIMPL;
    }
    HRESULT hr = fn(ppXAudio2, Flags, Processor, NTDDIVersion);
    Log(L"  <- returned 0x%08X  *ppXAudio2=%p", hr, ppXAudio2 ? *ppXAudio2 : nullptr);
    return hr;
}

// The game may also call XAudio2Create (the non-versioned form) via
// GetProcAddress or direct import - forward it too just in case.
extern "C" HRESULT WINAPI
XAudio2Create(IXAudio2** ppXAudio2, UINT32 Flags, XAUDIO2_PROCESSOR Processor)
{
    Log(L"XAudio2Create(Flags=0x%08X, Processor=0x%08X) -> routing to XAudio2CreateWithVersionInfo",
        Flags, Processor);
    return XAudio2CreateWithVersionInfo(ppXAudio2, Flags, Processor, 0x0A000000);
}

extern "C" HRESULT WINAPI
CreateAudioVolumeMeter(IUnknown** ppApo)
{
    Log(L"CreateAudioVolumeMeter()");
    using Fn = HRESULT(WINAPI*)(IUnknown**);
    auto fn = reinterpret_cast<Fn>(Resolve("CreateAudioVolumeMeter"));
    HRESULT hr = fn ? fn(ppApo) : E_NOTIMPL;
    Log(L"  <- 0x%08X", hr);
    return hr;
}

extern "C" HRESULT WINAPI
CreateAudioReverb(IUnknown** ppApo)
{
    Log(L"CreateAudioReverb()");
    using Fn = HRESULT(WINAPI*)(IUnknown**);
    auto fn = reinterpret_cast<Fn>(Resolve("CreateAudioReverb"));
    HRESULT hr = fn ? fn(ppApo) : E_NOTIMPL;
    Log(L"  <- 0x%08X", hr);
    return hr;
}
