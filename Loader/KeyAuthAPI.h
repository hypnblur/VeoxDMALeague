#pragma once
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <cstdio>
#pragma comment(lib, "winhttp.lib")

namespace KeyAuth {

// ── Configure these from your KeyAuth dashboard ──
static const wchar_t* API_HOST = L"keyauth.win";
static const wchar_t* API_PATH = L"/api/1.2/";

// Set these before calling Init()
static char g_appName[64]  = "VeoxDMA";
static char g_ownerID[64]  = "CHANGE_ME";
static char g_appVersion[16] = "1.0";
static char g_sessionID[64] = "";

struct Response {
    bool success = false;
    char message[256] = {};
    char sessionID[64] = {};
    char username[64] = {};
    char expiry[32] = {};
    char hwid[128] = {};
};

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
    std::string id = buf;
    id += "-";
    id += hw.szHwProfileGuid;
    return id;
}

static bool HttpPost(const std::string& body, std::string& out) {
    HINTERNET ses = WinHttpOpen(L"VeoxDMA/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) return false;

    HINTERNET con = WinHttpConnect(ses, API_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!con) { WinHttpCloseHandle(ses); return false; }

    HINTERNET req = WinHttpOpenRequest(con, L"POST", API_PATH, NULL,
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
    char buf[4096]; DWORD rd = 0;
    while (WinHttpReadData(req, buf, sizeof(buf) - 1, &rd) && rd > 0) {
        buf[rd] = 0; out += buf; rd = 0;
    }

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return true;
}

static const char* JsonStr(const std::string& json, const char* key, char* out, int outsz) {
    char pat[128]; snprintf(pat, sizeof(pat), "\"%s\"", key);
    auto pos = json.find(pat);
    if (pos == std::string::npos) { out[0] = 0; return out; }
    pos = json.find(':', pos + strlen(pat));
    if (pos == std::string::npos) { out[0] = 0; return out; }
    pos = json.find('"', pos + 1);
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
    while (pos < json.size() && json[pos] == ' ') pos++;
    return (json.find("true", pos) == pos + 1 || json.find("true", pos) == pos);
}

static Response ParseResponse(const std::string& json) {
    Response r;
    r.success = JsonBool(json, "success");
    JsonStr(json, "message", r.message, sizeof(r.message));
    JsonStr(json, "sessionid", r.sessionID, sizeof(r.sessionID));
    return r;
}

static Response Init() {
    std::string hwid = GetHWID();
    std::string body = "type=init";
    body += "&ver=" + UrlEncode(g_appVersion);
    body += "&name=" + UrlEncode(g_appName);
    body += "&ownerid=" + UrlEncode(g_ownerID);

    std::string raw;
    Response r;
    if (!HttpPost(body, raw)) {
        snprintf(r.message, sizeof(r.message), "Network error: cannot reach KeyAuth server");
        return r;
    }
    r = ParseResponse(raw);
    if (r.success && r.sessionID[0])
        strncpy(g_sessionID, r.sessionID, sizeof(g_sessionID) - 1);
    return r;
}

static Response License(const char* key) {
    std::string hwid = GetHWID();
    std::string body = "type=license";
    body += "&key=" + UrlEncode(key);
    body += "&sessionid=" + UrlEncode(g_sessionID);
    body += "&name=" + UrlEncode(g_appName);
    body += "&ownerid=" + UrlEncode(g_ownerID);
    body += "&hwid=" + UrlEncode(hwid.c_str());

    std::string raw;
    Response r;
    if (!HttpPost(body, raw)) {
        snprintf(r.message, sizeof(r.message), "Network error during license check");
        return r;
    }
    r = ParseResponse(raw);
    JsonStr(raw, "username", r.username, sizeof(r.username));
    JsonStr(raw, "expiry", r.expiry, sizeof(r.expiry));
    return r;
}

static Response CheckSession() {
    std::string body = "type=check";
    body += "&sessionid=" + UrlEncode(g_sessionID);
    body += "&name=" + UrlEncode(g_appName);
    body += "&ownerid=" + UrlEncode(g_ownerID);

    std::string raw;
    Response r;
    if (!HttpPost(body, raw)) {
        snprintf(r.message, sizeof(r.message), "Heartbeat network error");
        return r;
    }
    return ParseResponse(raw);
}

} // namespace KeyAuth
