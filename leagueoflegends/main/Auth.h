#pragma once
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <string>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "psapi.lib")

namespace Auth {

// ══════════════════════════════════════════════════════
//  KeyAuth Configuration — set these from your dashboard
// ══════════════════════════════════════════════════════
static char g_appName[64]    = {};
static char g_ownerID[64]    = {};
static char g_appVersion[16] = {};
static char g_kaSessionID[64]= "";

// ══════════════════════════════════════════════════════
//  State
// ══════════════════════════════════════════════════════
static bool g_authenticated = false;
static char g_username[64]  = "";
static char g_expiry[32]    = "";
static volatile bool g_heartbeatRunning = false;

// ══════════════════════════════════════════════════════
//  Secure memory
// ══════════════════════════════════════════════════════
static void Wipe(void* p, size_t n) { SecureZeroMemory(p, n); }

// ══════════════════════════════════════════════════════
//  Integrity checks
// ══════════════════════════════════════════════════════
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

typedef LONG(NTAPI* pNtQIP)(HANDLE, ULONG, PVOID, ULONG, PULONG);
static bool NtDebugPortCheck() {
    auto fn = (pNtQIP)GetProcAddress(GetModuleHandleA(xorstr_("ntdll.dll")), xorstr_("NtQueryInformationProcess"));
    if (!fn) return false;
    DWORD_PTR port = 0;
    fn(GetCurrentProcess(), 7, &port, sizeof(port), NULL);
    return port != 0;
}

static bool HasAnalysisTools() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32 pe = {}; pe.dwSize = sizeof(pe);
    bool found = false;
    if (Process32First(snap, &pe)) {
        do {
            char lower[MAX_PATH] = {};
            for (int i = 0; pe.szExeFile[i] && i < MAX_PATH - 1; i++) {
                lower[i] = (pe.szExeFile[i] >= 'A' && pe.szExeFile[i] <= 'Z')
                           ? pe.szExeFile[i] + 32 : pe.szExeFile[i];
                lower[i + 1] = 0;
            }
            if (strstr(lower, xorstr_("x64dbg"))  || strstr(lower, xorstr_("x32dbg"))  ||
                strstr(lower, xorstr_("ollydbg"))  || strstr(lower, xorstr_("ida"))     ||
                strstr(lower, xorstr_("ghidra"))   || strstr(lower, xorstr_("cheatengine")) ||
                strstr(lower, xorstr_("processhacker")) || strstr(lower, xorstr_("wireshark")) ||
                strstr(lower, xorstr_("fiddler"))  || strstr(lower, xorstr_("apimonitor")))
                found = true;
        } while (!found && Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

static bool RunAll() {
    if (IsDebuggerAttached())    { printf(xorstr_("[!] Debugger detected.\n")); return false; }
    if (HasHardwareBreakpoints()){ printf(xorstr_("[!] Hardware breakpoints detected.\n")); return false; }
    if (NtDebugPortCheck())      { printf(xorstr_("[!] Debug port active.\n")); return false; }
    if (HasAnalysisTools())      { printf(xorstr_("[!] Analysis tools detected.\n")); return false; }
    return true;
}

static bool RuntimeCheck() {
    return !IsDebuggerAttached() && !NtDebugPortCheck();
}

} // namespace Integrity

// ══════════════════════════════════════════════════════
//  HTTP helpers (WinHTTP)
// ══════════════════════════════════════════════════════
static std::string UrlEncode(const char* s) {
    std::string out;
    for (; *s; s++) {
        unsigned char c = *s;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
            out += (char)c;
        else { char buf[8]; snprintf(buf, sizeof(buf), "%%%02X", c); out += buf; }
    }
    return out;
}

static std::string GetHWID() {
    char buf[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD sz = sizeof(buf);
    GetComputerNameA(buf, &sz);
    HW_PROFILE_INFOA hw = {};
    GetCurrentHwProfileA(&hw);
    return std::string(buf) + "-" + hw.szHwProfileGuid;
}

static bool HttpPost(const std::string& body, std::string& out) {
    HINTERNET ses = WinHttpOpen(L"Mozilla/5.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) return false;
    HINTERNET con = WinHttpConnect(ses, L"keyauth.win", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!con) { WinHttpCloseHandle(ses); return false; }
    HINTERNET req = WinHttpOpenRequest(con, L"POST", L"/api/1.2/", NULL,
                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       WINHTTP_FLAG_SECURE);
    if (!req) { WinHttpCloseHandle(con); WinHttpCloseHandle(ses); return false; }

    const wchar_t* hdrs = L"Content-Type: application/x-www-form-urlencoded\r\n";
    BOOL ok = WinHttpSendRequest(req, hdrs, (DWORD)-1,
                                 (LPVOID)body.c_str(), (DWORD)body.size(),
                                 (DWORD)body.size(), 0);
    if (!ok || !WinHttpReceiveResponse(req, NULL)) {
        WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
        return false;
    }

    out.clear();
    char rbuf[4096]; DWORD rd = 0;
    while (WinHttpReadData(req, rbuf, sizeof(rbuf) - 1, &rd) && rd > 0) {
        rbuf[rd] = 0; out += rbuf; rd = 0;
    }
    WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
    return true;
}

// Minimal JSON string extractor
static const char* JsonStr(const std::string& json, const char* key, char* out, int outsz) {
    char pat[128]; snprintf(pat, sizeof(pat), "\"%s\"", key);
    auto pos = json.find(pat);
    if (pos == std::string::npos) { out[0] = 0; return out; }
    pos = json.find('"', json.find(':', pos + strlen(pat)) + 1);
    if (pos == std::string::npos) { out[0] = 0; return out; }
    pos++;
    int i = 0;
    while (pos < json.size() && json[pos] != '"' && i < outsz - 1)
        out[i++] = json[pos++];
    out[i] = 0;
    return out;
}

static bool JsonBool(const std::string& json, const char* key) {
    char pat[128]; snprintf(pat, sizeof(pat), "\"%s\"", key);
    auto pos = json.find(pat);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos + strlen(pat));
    if (pos == std::string::npos) return false;
    auto rest = json.substr(pos + 1);
    for (char c : rest) { if (c == ' ') continue; return (c == 't'); }
    return false;
}

// ══════════════════════════════════════════════════════
//  KeyAuth API
// ══════════════════════════════════════════════════════
static bool KA_Init() {
    std::string body = "type=init";
    body += "&ver=" + UrlEncode(g_appVersion);
    body += "&name=" + UrlEncode(g_appName);
    body += "&ownerid=" + UrlEncode(g_ownerID);

    std::string raw;
    if (!HttpPost(body, raw)) return false;
    if (!JsonBool(raw, "success")) return false;
    JsonStr(raw, "sessionid", g_kaSessionID, sizeof(g_kaSessionID));
    return g_kaSessionID[0] != 0;
}

struct LicenseResult {
    bool success;
    char message[256];
    char username[64];
    char expiry[32];
};

static LicenseResult KA_License(const char* key) {
    LicenseResult r = {};
    std::string hwid = GetHWID();
    std::string body = "type=license";
    body += "&key=" + UrlEncode(key);
    body += "&sessionid=" + UrlEncode(g_kaSessionID);
    body += "&name=" + UrlEncode(g_appName);
    body += "&ownerid=" + UrlEncode(g_ownerID);
    body += "&hwid=" + UrlEncode(hwid.c_str());

    std::string raw;
    if (!HttpPost(body, raw)) {
        strncpy(r.message, "Network error during license check", sizeof(r.message) - 1);
        return r;
    }
    r.success = JsonBool(raw, "success");
    JsonStr(raw, "message", r.message, sizeof(r.message));
    JsonStr(raw, "username", r.username, sizeof(r.username));
    JsonStr(raw, "expiry", r.expiry, sizeof(r.expiry));
    return r;
}

static bool KA_CheckSession() {
    std::string body = "type=check";
    body += "&sessionid=" + UrlEncode(g_kaSessionID);
    body += "&name=" + UrlEncode(g_appName);
    body += "&ownerid=" + UrlEncode(g_ownerID);

    std::string raw;
    if (!HttpPost(body, raw)) return false;
    return JsonBool(raw, "success");
}

// ══════════════════════════════════════════════════════
//  Heartbeat thread (KeyAuth session keepalive)
// ══════════════════════════════════════════════════════
static DWORD WINAPI HeartbeatThread(LPVOID) {
    while (g_heartbeatRunning) {
        Sleep(30000);
        if (!g_heartbeatRunning) break;
        if (!KA_CheckSession()) {
            printf(xorstr_("[!] KeyAuth session expired or revoked.\n"));
            g_heartbeatRunning = false;
            g_authenticated = false;
        }
    }
    return 0;
}

// ══════════════════════════════════════════════════════
//  License file I/O
// ══════════════════════════════════════════════════════
static char LICENSE_PATH[MAX_PATH];

static std::string LoadKeyFromFile() {
    FILE* f = fopen(LICENSE_PATH, "r");
    if (!f) return "";
    char buf[256] = {};
    fgets(buf, sizeof(buf), f);
    fclose(f);
    int len = (int)strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' || buf[len - 1] == ' '))
        buf[--len] = 0;
    return buf;
}

static bool SaveKeyToFile(const char* key) {
    std::filesystem::create_directories(xorstr_("C:\\VeoxDMA"));
    FILE* f = fopen(LICENSE_PATH, "w");
    if (!f) return false;
    fprintf(f, "%s\n", key);
    fclose(f);
    return true;
}

// ══════════════════════════════════════════════════════
//  Main startup — call from WinMain before anything else
//  Returns true if authenticated, false to exit.
// ══════════════════════════════════════════════════════
static bool Startup() {
    strncpy(g_appName, xorstr_("VeoxDMA"), sizeof(g_appName) - 1);
    strncpy(g_ownerID, xorstr_("CHANGE_ME"), sizeof(g_ownerID) - 1);
    strncpy(g_appVersion, xorstr_("1.0"), sizeof(g_appVersion) - 1);
    strncpy(LICENSE_PATH, xorstr_("C:\\VeoxDMA\\license.key"), sizeof(LICENSE_PATH) - 1);

    // Integrity
    printf(xorstr_("[*] Running integrity checks...\n"));
    if (!Integrity::RunAll()) {
        printf(xorstr_("[!] Integrity check failed. Exiting.\n"));
        return false;
    }
    printf(xorstr_("[+] Integrity OK\n"));

    printf(xorstr_("[*] Connecting to auth server...\n"));
    if (!KA_Init()) {
        printf(xorstr_("[!] Cannot reach auth server. Check internet and try again.\n"));
        return false;
    }
    printf(xorstr_("[+] Connected\n"));

    std::string key = LoadKeyFromFile();
    if (key.empty()) {
        printf(xorstr_("[!] No license file found.\n"));
        printf(xorstr_("Enter license key: "));
        char input[256] = {};
        fgets(input, sizeof(input), stdin);
        int len = (int)strlen(input);
        while (len > 0 && (input[len - 1] == '\n' || input[len - 1] == '\r'))
            input[--len] = 0;
        key = input;
    }

    if (key.empty()) {
        printf(xorstr_("[!] No license key provided.\n"));
        return false;
    }

    printf(xorstr_("[*] Validating license...\n"));
    auto r = KA_License(key.c_str());
    if (!r.success) {
        printf(xorstr_("[!] License failed: %s\n"), r.message);
        printf(xorstr_("Enter license key: "));
        char input[256] = {};
        fgets(input, sizeof(input), stdin);
        int len = (int)strlen(input);
        while (len > 0 && (input[len - 1] == '\n' || input[len - 1] == '\r'))
            input[--len] = 0;
        if (input[0]) {
            key = input;
            r = KA_License(key.c_str());
        }
        if (!r.success) {
            printf(xorstr_("[!] License validation failed: %s\n"), r.message);
            return false;
        }
    }

    SaveKeyToFile(key.c_str());
    strncpy(g_username, r.username, sizeof(g_username) - 1);
    strncpy(g_expiry, r.expiry, sizeof(g_expiry) - 1);
    g_authenticated = true;

    printf(xorstr_("[+] License valid — welcome, %s\n"), g_username[0] ? g_username : xorstr_("user"));
    if (g_expiry[0]) {
        time_t exp = (time_t)atoll(g_expiry);
        if (exp > 0) {
            struct tm t; localtime_s(&t, &exp);
            char tbuf[64]; strftime(tbuf, sizeof(tbuf), xorstr_("%Y-%m-%d %H:%M"), &t);
            printf(xorstr_("[+] Expires: %s\n"), tbuf);
        }
    }

    // Start heartbeat
    g_heartbeatRunning = true;
    HANDLE ht = CreateThread(NULL, 0, HeartbeatThread, NULL, 0, NULL);
    if (ht) CloseHandle(ht);

    return true;
}

// Periodic runtime check — call from main loop every ~10s
static bool RuntimeIntegrityCheck() {
    return Integrity::RuntimeCheck() && g_authenticated;
}

} // namespace Auth
