#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <cstdio>
#pragma comment(lib, "psapi.lib")

namespace Integrity {

static bool IsDebuggerAttached() {
    if (IsDebuggerPresent()) return true;
    BOOL remote = FALSE;
    CheckRemoteDebuggerPresent(GetCurrentProcess(), &remote);
    return remote != FALSE;
}

static bool HasHardwareBreakpoints() {
    CONTEXT ctx = {};
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (!GetThreadContext(GetCurrentThread(), &ctx)) return false;
    return (ctx.Dr0 || ctx.Dr1 || ctx.Dr2 || ctx.Dr3);
}

// NtQueryInformationProcess-based debug check (ProcessDebugPort = 7)
typedef LONG(NTAPI* pNtQIP)(HANDLE, ULONG, PVOID, ULONG, PULONG);
static bool NtDebugPortCheck() {
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) return false;
    auto fn = (pNtQIP)GetProcAddress(ntdll, "NtQueryInformationProcess");
    if (!fn) return false;
    DWORD_PTR port = 0;
    fn(GetCurrentProcess(), 7, &port, sizeof(port), NULL);
    return port != 0;
}

// Check for common analysis tool windows
static bool HasAnalysisTools() {
    const char* names[] = {
        "x64dbg", "x32dbg", "ollydbg", "ida", "ida64",
        "ghidra", "cheatengine", "processhacker", "wireshark",
        "fiddler", "apimonitor", NULL
    };
    HWND hwnd = NULL;
    for (int i = 0; names[i]; i++) {
        hwnd = FindWindowA(NULL, NULL);
        // Enumerate top-level windows
    }
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32 pe = {}; pe.dwSize = sizeof(pe);
    bool found = false;
    if (Process32First(snap, &pe)) {
        do {
            char lower[MAX_PATH];
            for (int i = 0; pe.szExeFile[i] && i < MAX_PATH - 1; i++) {
                lower[i] = (pe.szExeFile[i] >= 'A' && pe.szExeFile[i] <= 'Z')
                           ? pe.szExeFile[i] + 32 : pe.szExeFile[i];
                lower[i + 1] = 0;
            }
            for (int i = 0; names[i]; i++) {
                if (strstr(lower, names[i])) { found = true; break; }
            }
        } while (!found && Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

// Verify parent process is a shell (explorer, cmd, powershell) — not an injector
static bool IsParentLegitimate() {
    typedef LONG(NTAPI* pNtQIP)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) return true;
    auto fn = (pNtQIP)GetProcAddress(ntdll, "NtQueryInformationProcess");
    if (!fn) return true;

    struct { DWORD_PTR reserved; DWORD_PTR peb; DWORD_PTR reserved2[2]; DWORD_PTR upid; DWORD_PTR ppid; } pbi = {};
    fn(GetCurrentProcess(), 0, &pbi, sizeof(pbi), NULL);

    HANDLE parent = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pbi.ppid);
    if (!parent) return true;

    char path[MAX_PATH] = {};
    DWORD sz = MAX_PATH;
    QueryFullProcessImageNameA(parent, 0, path, &sz);
    CloseHandle(parent);

    _strlwr_s(path);
    return (strstr(path, "explorer.exe") || strstr(path, "cmd.exe") ||
            strstr(path, "powershell.exe") || strstr(path, "pwsh.exe") ||
            strstr(path, "windowsterminal.exe") || strstr(path, "conhost.exe") ||
            strstr(path, "schtasks.exe"));
}

struct CheckResult {
    bool debugger;
    bool hwBreakpoints;
    bool ntDebugPort;
    bool analysisTools;
    bool parentOk;
    bool passed;
};

static CheckResult RunAll() {
    CheckResult r;
    r.debugger = IsDebuggerAttached();
    r.hwBreakpoints = HasHardwareBreakpoints();
    r.ntDebugPort = NtDebugPortCheck();
    r.analysisTools = HasAnalysisTools();
    r.parentOk = IsParentLegitimate();
    r.passed = !r.debugger && !r.hwBreakpoints && !r.ntDebugPort &&
               !r.analysisTools && r.parentOk;
    return r;
}

} // namespace Integrity
