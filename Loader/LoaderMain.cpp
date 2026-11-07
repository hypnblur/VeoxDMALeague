// VeoxDMA Loader — KeyAuth-integrated loader.
//
// Addresses:
//   P1: Session creds encrypted in memory (per-run XOR key, never stored as hex)
//   P2: Short-lived sessions via KeyAuth heartbeat
//   P3: Encrypted bootstrap pipe (AES-256-CBC with ephemeral key)
//   P4: HMAC with loader-side nonce
//   P5: Product heartbeat — exits if loader dies
//
// Build: see build.bat
// Usage: PureLoader.exe [license-key]

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#include "KeyAuthAPI.h"
#include "SecureMem.h"
#include "Integrity.h"

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")

// ── Configuration ──
// Product exe name — looked for in the same directory as the loader, or override via env.
static const char* PRODUCT_EXE = "LeagueDMA.exe";
static const char* LICENSE_FILE = "license.dat";
static const int   HEARTBEAT_INTERVAL_MS = 30000;
static const int   PRODUCT_PING_INTERVAL_MS = 3000;
static const int   PRODUCT_PING_TIMEOUT_MISSES = 3;

// ── Globals ──
static SecureMem::RunKey g_runKey;
static SecureMem::EncBuf g_sessionKey;   // 32 bytes, encrypted
static SecureMem::EncBuf g_sessionId;    // 8 bytes, encrypted
static volatile bool g_running = true;
static HANDLE g_productProcess = NULL;
static char g_pipeName[256] = {};
static FILE* g_log = NULL;

static void Log(const char* fmt, ...) {
    if (!g_log) return;
    SYSTEMTIME s; GetLocalTime(&s);
    fprintf(g_log, "%02d:%02d:%02d.%03d ", s.wHour, s.wMinute, s.wSecond, s.wMilliseconds);
    va_list a; va_start(a, fmt); vfprintf(g_log, fmt, a); va_end(a);
    fputc('\n', g_log); fflush(g_log);
}

static void PrintStatus(const char* fmt, ...) {
    va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a);
    printf("\n");
    va_start(a, fmt);
    if (g_log) { vfprintf(g_log, fmt, a); fputc('\n', g_log); fflush(g_log); }
    va_end(a);
}

// ── License file I/O ──
static std::string LoadLicense() {
    char path[MAX_PATH]; GetModuleFileNameA(NULL, path, MAX_PATH);
    char* sl = strrchr(path, '\\'); if (sl) *(sl + 1) = 0;
    strcat_s(path, LICENSE_FILE);

    FILE* f = fopen(path, "r");
    if (!f) return "";
    char buf[256] = {}; fgets(buf, sizeof(buf), f); fclose(f);
    int len = (int)strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) buf[--len] = 0;
    return buf;
}

static void SaveLicense(const char* key) {
    char path[MAX_PATH]; GetModuleFileNameA(NULL, path, MAX_PATH);
    char* sl = strrchr(path, '\\'); if (sl) *(sl + 1) = 0;
    strcat_s(path, LICENSE_FILE);
    FILE* f = fopen(path, "w");
    if (f) { fprintf(f, "%s\n", key); fclose(f); }
}

// ── Product path resolution ──
static std::string FindProduct() {
    // Check env override first
    char envPath[MAX_PATH] = {};
    if (GetEnvironmentVariableA("PURE_PRODUCT_PATH", envPath, sizeof(envPath)) > 0) {
        if (GetFileAttributesA(envPath) != INVALID_FILE_ATTRIBUTES) return envPath;
    }
    // Same directory as loader
    char path[MAX_PATH]; GetModuleFileNameA(NULL, path, MAX_PATH);
    char* sl = strrchr(path, '\\'); if (sl) *(sl + 1) = 0;
    strcat_s(path, PRODUCT_EXE);
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) return path;
    return "";
}

// ── Generate session credentials (raw bytes, never hex) ──
static bool GenerateSession() {
    g_sessionKey.Alloc(32);
    g_sessionId.Alloc(8);
    if (!SecureMem::RandomBytes(g_sessionKey.data, 32)) return false;
    if (!SecureMem::RandomBytes(g_sessionId.data, 8)) return false;
    // Immediately encrypt in memory
    g_sessionKey.Encrypt(g_runKey);
    g_sessionId.Encrypt(g_runKey);
    return true;
}

// ── Create encrypted bootstrap pipe ──
// Layout: [16-byte IV][AES-256-CBC(ephemeral_key, session_id(8) || session_key(32))]
// Ephemeral AES key is passed via a second anonymous pipe.
static bool CreateBootstrapPipes(HANDLE* hCredRead, HANDLE* hKeyRead) {
    // Decrypt session creds to stack
    uint8_t sk[32], sid[8];
    g_sessionKey.DecryptTo(g_runKey, sk, 32);
    g_sessionId.DecryptTo(g_runKey, sid, 8);

    // Generate ephemeral AES key + IV
    uint8_t aesKey[32], iv[16];
    SecureMem::RandomBytes(aesKey, 32);
    SecureMem::RandomBytes(iv, 16);

    // Plaintext = session_id(8) || session_key(32)
    uint8_t plain[40];
    memcpy(plain, sid, 8);
    memcpy(plain + 8, sk, 32);

    // Encrypt
    uint8_t cipher[64];
    DWORD cipherLen = sizeof(cipher);
    bool ok = SecureMem::AesEncrypt(aesKey, iv, plain, 40, cipher, &cipherLen);
    SecureMem::Wipe(plain, sizeof(plain));
    SecureMem::Wipe(sk, sizeof(sk));
    SecureMem::Wipe(sid, sizeof(sid));

    if (!ok) {
        SecureMem::Wipe(aesKey, sizeof(aesKey));
        return false;
    }

    // Pipe 1: encrypted creds = [IV(16)] || [ciphertext]
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE hCredW = NULL;
    if (!CreatePipe(hCredRead, &hCredW, &sa, 0)) {
        SecureMem::Wipe(aesKey, sizeof(aesKey));
        return false;
    }
    SetHandleInformation(hCredW, HANDLE_FLAG_INHERIT, 0);
    DWORD w = 0;
    WriteFile(hCredW, iv, 16, &w, NULL);
    WriteFile(hCredW, cipher, cipherLen, &w, NULL);
    CloseHandle(hCredW);
    SecureMem::Wipe(cipher, sizeof(cipher));

    // Pipe 2: ephemeral AES key (32 raw bytes)
    HANDLE hKeyW = NULL;
    if (!CreatePipe(hKeyRead, &hKeyW, &sa, 0)) {
        SecureMem::Wipe(aesKey, sizeof(aesKey));
        CloseHandle(*hCredRead);
        return false;
    }
    SetHandleInformation(hKeyW, HANDLE_FLAG_INHERIT, 0);
    WriteFile(hKeyW, aesKey, 32, &w, NULL);
    CloseHandle(hKeyW);
    SecureMem::Wipe(aesKey, sizeof(aesKey));
    SecureMem::Wipe(iv, sizeof(iv));

    Log("bootstrap pipes created (encrypted, AES-256-CBC)");
    return true;
}

// ── ALSO support legacy plaintext bootstrap for backward compat ──
// If PURE_LEGACY_BOOTSTRAP=1, fall back to the old 80-byte hex format.
static bool CreateLegacyBootstrapPipe(HANDLE* hCredRead) {
    uint8_t sk[32], sid[8];
    g_sessionKey.DecryptTo(g_runKey, sk, 32);
    g_sessionId.DecryptTo(g_runKey, sid, 8);

    // Convert to hex for legacy protocol (report P1 says stop doing this — phase out)
    char hexBuf[96];
    int pos = 0;
    for (int i = 0; i < 8; i++) pos += snprintf(hexBuf + pos, sizeof(hexBuf) - pos, "%02x", sid[i]);
    for (int i = 0; i < 32; i++) pos += snprintf(hexBuf + pos, sizeof(hexBuf) - pos, "%02x", sk[i]);

    SecureMem::Wipe(sk, sizeof(sk));
    SecureMem::Wipe(sid, sizeof(sid));

    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE hWrite = NULL;
    if (!CreatePipe(hCredRead, &hWrite, &sa, 80)) return false;
    SetHandleInformation(hWrite, HANDLE_FLAG_INHERIT, 0);
    DWORD w = 0;
    WriteFile(hWrite, hexBuf, 80, &w, NULL);
    CloseHandle(hWrite);
    SecureMem::Wipe(hexBuf, sizeof(hexBuf));
    Log("legacy bootstrap pipe created (80-byte hex — schedule migration to encrypted pipe)");
    return true;
}

// ── Named-pipe HMAC handshake responder ──
static DWORD WINAPI PipeWorker(LPVOID param) {
    int id = (int)(intptr_t)param;
    HANDLE h = CreateNamedPipeA(g_pipeName, PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES, 65536, 65536, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { Log("[%d] pipe create err %lu", id, GetLastError()); return 1; }

    if (!ConnectNamedPipe(h, NULL) && GetLastError() != ERROR_PIPE_CONNECTED) {
        Log("[%d] pipe connect err %lu", id, GetLastError());
        CloseHandle(h); return 1;
    }

    ULONG clientPid = 0;
    GetNamedPipeClientProcessId(h, &clientPid);
    Log("[%d] CONNECTED client_pid=%lu", id, clientPid);

    // Decrypt session creds to stack for HMAC
    uint8_t sk[32], sid[8];
    g_sessionKey.DecryptTo(g_runKey, sk, 32);
    g_sessionId.DecryptTo(g_runKey, sid, 8);

    int missedPings = 0;
    ULONGLONG lastPing = GetTickCount64();

    while (g_running) {
        uint8_t buf[512]; DWORD nr = 0;
        if (!ReadFile(h, buf, sizeof(buf), &nr, NULL) || nr == 0) {
            Log("[%d] read end err=%lu", id, GetLastError());
            break;
        }

        if (nr >= 5 && buf[0] == 1) {
            // tag=1: challenge
            DWORD clen = *(DWORD*)(buf + 1);
            if (clen == 32 && nr >= 37) {
                uint8_t* challenge = buf + 5;

                // HMAC(session_key, session_id || challenge)
                uint8_t hmacData[40];
                memcpy(hmacData, sid, 8);
                memcpy(hmacData + 8, challenge, 32);
                uint8_t mac[32];
                SecureMem::HmacSha256(sk, 32, hmacData, 40, mac);
                SecureMem::Wipe(hmacData, sizeof(hmacData));

                uint8_t resp[37];
                resp[0] = 2; *(DWORD*)(resp + 1) = 32;
                memcpy(resp + 5, mac, 32);
                DWORD w = 0;
                WriteFile(h, resp, 37, &w, NULL);
                SecureMem::Wipe(mac, sizeof(mac));
                Log("[%d] HMAC response sent", id);
            }
        } else if (nr >= 1 && buf[0] == 3) {
            Log("[%d] tag=3 HMAC verified by product", id);
        } else if (nr >= 1 && buf[0] == 8) {
            Log("[%d] tag=8 session validated — product launching", id);
        } else if (nr >= 1 && buf[0] == 7) {
            char msg[256] = {};
            DWORD mlen = (nr >= 5) ? *(DWORD*)(buf + 1) : 0;
            if (mlen > 0 && mlen < sizeof(msg) && nr >= 5 + mlen)
                memcpy(msg, buf + 5, mlen);
            Log("[%d] tag=7 ERROR: %s", id, msg);
            PrintStatus("[!] Session rejected: %s", msg);
        } else if (nr >= 1 && buf[0] == 10) {
            // tag=10: product heartbeat pong
            missedPings = 0;
            lastPing = GetTickCount64();
        } else {
            Log("[%d] tag=%u (unhandled)", id, buf[0]);
        }

        // Send heartbeat ping to product (tag=9)
        if (g_running && (GetTickCount64() - lastPing) > (ULONGLONG)PRODUCT_PING_INTERVAL_MS) {
            uint8_t ping[5] = { 9, 0, 0, 0, 0 };
            DWORD w = 0;
            WriteFile(h, ping, 5, &w, NULL);
            missedPings++;
            if (missedPings > PRODUCT_PING_TIMEOUT_MISSES) {
                Log("[%d] product missed %d heartbeats — terminating", id, missedPings);
                break;
            }
        }
    }

    SecureMem::Wipe(sk, sizeof(sk));
    SecureMem::Wipe(sid, sizeof(sid));
    CloseHandle(h);
    return 0;
}

// ── KeyAuth heartbeat thread ──
static DWORD WINAPI HeartbeatThread(LPVOID) {
    while (g_running) {
        Sleep(HEARTBEAT_INTERVAL_MS);
        if (!g_running) break;

        auto r = KeyAuth::CheckSession();
        if (!r.success) {
            Log("KeyAuth heartbeat failed: %s", r.message);
            PrintStatus("[!] Session expired or revoked. Shutting down.");
            g_running = false;
            if (g_productProcess) TerminateProcess(g_productProcess, 1);
            break;
        }
        Log("KeyAuth heartbeat OK");
    }
    return 0;
}

// ── Product process monitor ──
static DWORD WINAPI ProductMonitor(LPVOID) {
    if (!g_productProcess) return 0;
    WaitForSingleObject(g_productProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(g_productProcess, &exitCode);
    Log("Product exited with code %lu", exitCode);
    g_running = false;
    return 0;
}

// ── Launch product ──
static bool LaunchProduct(const char* productPath) {
    // Generate unique pipe name
    DWORD tick = GetTickCount();
    uint8_t rnd[4];
    SecureMem::RandomBytes(rnd, 4);
    snprintf(g_pipeName, sizeof(g_pipeName), "\\\\.\\pipe\\PD_%08x%02x%02x%02x%02x",
             tick, rnd[0], rnd[1], rnd[2], rnd[3]);
    Log("pipe name: %s", g_pipeName);

    // Start pipe listener threads (before product launch)
    for (int i = 0; i < 4; i++)
        CreateThread(NULL, 0, PipeWorker, (LPVOID)(intptr_t)i, 0, NULL);
    Sleep(200);

    // Create bootstrap pipe(s)
    HANDLE hCredRead = NULL, hKeyRead = NULL;
    bool useLegacy = false;
    {
        char env[8] = {};
        useLegacy = (GetEnvironmentVariableA("PURE_LEGACY_BOOTSTRAP", env, sizeof(env)) > 0
                     && env[0] == '1');
    }

    if (useLegacy) {
        if (!CreateLegacyBootstrapPipe(&hCredRead)) {
            PrintStatus("[!] Failed to create bootstrap pipe");
            return false;
        }
    } else {
        if (!CreateBootstrapPipes(&hCredRead, &hKeyRead)) {
            PrintStatus("[!] Failed to create encrypted bootstrap pipes");
            return false;
        }
    }

    // Build environment block
    std::string env;
    auto addEnv = [&](const char* k, const char* v) {
        env += k; env += "="; env += v; env.push_back('\0');
    };

    addEnv("PD_PIPE_NAME", g_pipeName);

    char handleStr[32];
    snprintf(handleStr, sizeof(handleStr), "%lld", (long long)(intptr_t)hCredRead);
    addEnv("PD_BOOTSTRAP_HANDLE", handleStr);

    if (hKeyRead) {
        snprintf(handleStr, sizeof(handleStr), "%lld", (long long)(intptr_t)hKeyRead);
        addEnv("PD_BOOTSTRAP_KEY_HANDLE", handleStr);
    }

    // Inherit parent environment
    LPCH sysEnv = GetEnvironmentStringsA();
    if (sysEnv) {
        LPCH p = sysEnv;
        while (*p) {
            std::string e = p;
            // Don't pass through any PD_ vars from our own env (scrub)
            if (e.substr(0, 3) != "PD_" && e.substr(0, 6) != "PURE_" &&
                e.substr(0, 6) != "PROOF_")
                { env += e; env.push_back('\0'); }
            p += e.size() + 1;
        }
        FreeEnvironmentStringsA(sysEnv);
    }
    env.push_back('\0');

    // Launch product
    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOW;
    PROCESS_INFORMATION pi = {};

    char cmd[MAX_PATH * 2];
    snprintf(cmd, sizeof(cmd), "\"%s\"", productPath);

    if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NEW_CONSOLE,
                        (LPVOID)env.c_str(), NULL, &si, &pi)) {
        Log("CreateProcess failed %lu", GetLastError());
        PrintStatus("[!] Failed to launch product: error %lu", GetLastError());
        return false;
    }

    g_productProcess = pi.hProcess;
    CloseHandle(pi.hThread);
    Log("Product launched pid=%lu", pi.dwProcessId);
    PrintStatus("[+] Product launched (PID %lu)", pi.dwProcessId);

    // Scrub bootstrap env vars from OUR process immediately
    SetEnvironmentVariableA("PD_PIPE_NAME", NULL);
    SetEnvironmentVariableA("PD_BOOTSTRAP_HANDLE", NULL);
    SetEnvironmentVariableA("PD_BOOTSTRAP_KEY_HANDLE", NULL);

    return true;
}

// ── Console UI ──
static void PrintBanner() {
    printf("\n");
    printf("  ========================================\n");
    printf("            VeoxDMA Loader\n");
    printf("  ========================================\n");
    printf("\n");
}

int main(int argc, char** argv) {
    SetConsoleTitleA("VeoxDMA Loader");

    // Open log
    char logPath[MAX_PATH];
    GetModuleFileNameA(NULL, logPath, MAX_PATH);
    char* sl = strrchr(logPath, '\\'); if (sl) *(sl + 1) = 0;
    strcat_s(logPath, "loader.log");
    g_log = fopen(logPath, "a");
    Log("=== VeoxDMA Loader start ===");

    PrintBanner();

    // ── Step 1: Integrity checks ──
    PrintStatus("[*] Running integrity checks...");
    auto integrity = Integrity::RunAll();
    if (!integrity.passed) {
        if (integrity.debugger)      PrintStatus("[!] Debugger detected");
        if (integrity.hwBreakpoints) PrintStatus("[!] Hardware breakpoints detected");
        if (integrity.ntDebugPort)   PrintStatus("[!] Debug port active");
        if (integrity.analysisTools) PrintStatus("[!] Analysis tools detected");
        if (!integrity.parentOk)     PrintStatus("[!] Suspicious parent process");
        PrintStatus("[!] Integrity check failed. Exiting.");
        Log("Integrity FAILED");
        Sleep(3000);
        return 1;
    }
    PrintStatus("[+] Integrity OK");

    // ── Step 2: Initialize per-run encryption key ──
    g_runKey.Generate();
    if (!g_runKey.valid) {
        PrintStatus("[!] Failed to initialize encryption. Exiting.");
        return 1;
    }

    // ── Step 3: Find product ──
    std::string productPath = FindProduct();
    if (productPath.empty()) {
        PrintStatus("[!] Cannot find %s — place it in the same folder as the loader.", PRODUCT_EXE);
        Sleep(5000);
        return 1;
    }
    PrintStatus("[+] Product: %s", productPath.c_str());

    // ── Step 4: KeyAuth initialization ──
    PrintStatus("[*] Connecting to auth server...");
    auto initResp = KeyAuth::Init();
    if (!initResp.success) {
        PrintStatus("[!] Auth init failed: %s", initResp.message);
        PrintStatus("    Check your internet connection and try again.");
        Sleep(5000);
        return 1;
    }
    PrintStatus("[+] Connected to auth server");

    // ── Step 5: License validation ──
    std::string licenseKey;
    if (argc > 1) {
        licenseKey = argv[1];
    } else {
        licenseKey = LoadLicense();
    }

    if (licenseKey.empty()) {
        printf("\n  Enter license key: ");
        char buf[256] = {};
        if (fgets(buf, sizeof(buf), stdin)) {
            int len = (int)strlen(buf);
            while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) buf[--len] = 0;
            licenseKey = buf;
        }
    }

    if (licenseKey.empty()) {
        PrintStatus("[!] No license key provided. Exiting.");
        Sleep(3000);
        return 1;
    }

    PrintStatus("[*] Validating license...");
    auto licResp = KeyAuth::License(licenseKey.c_str());
    if (!licResp.success) {
        PrintStatus("[!] License validation failed: %s", licResp.message);
        Sleep(5000);
        return 1;
    }

    SaveLicense(licenseKey.c_str());
    PrintStatus("[+] License valid — welcome, %s",
                licResp.username[0] ? licResp.username : "user");
    if (licResp.expiry[0]) {
        time_t exp = (time_t)atoll(licResp.expiry);
        if (exp > 0) {
            struct tm t; localtime_s(&t, &exp);
            char tbuf[64]; strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M", &t);
            PrintStatus("[+] Expires: %s", tbuf);
        }
    }

    // ── Step 6: Generate session credentials ──
    if (!GenerateSession()) {
        PrintStatus("[!] Failed to generate session credentials. Exiting.");
        return 1;
    }
    Log("Session creds generated (encrypted in memory, raw bytes, no hex)");

    // ── Step 7: Launch product ──
    PrintStatus("[*] Launching product...");
    if (!LaunchProduct(productPath.c_str())) {
        return 1;
    }

    // ── Step 8: Start background threads ──
    CreateThread(NULL, 0, HeartbeatThread, NULL, 0, NULL);
    CreateThread(NULL, 0, ProductMonitor, NULL, 0, NULL);

    PrintStatus("[+] Loader active. Do not close this window.");
    PrintStatus("    Product heartbeat: every %d ms", PRODUCT_PING_INTERVAL_MS);
    PrintStatus("    KeyAuth heartbeat: every %d ms", HEARTBEAT_INTERVAL_MS);
    printf("\n");

    // ── Monitor loop ──
    while (g_running) {
        Sleep(1000);

        // Periodic integrity re-check
        static ULONGLONG lastCheck = 0;
        ULONGLONG now = GetTickCount64();
        if (now - lastCheck > 10000) {
            lastCheck = now;
            if (Integrity::IsDebuggerAttached() || Integrity::NtDebugPortCheck()) {
                Log("Runtime integrity FAILED — debugger attached");
                PrintStatus("[!] Debugger detected at runtime. Shutting down.");
                g_running = false;
                if (g_productProcess) TerminateProcess(g_productProcess, 1);
                break;
            }
        }
    }

    // Cleanup
    PrintStatus("[*] Shutting down...");
    if (g_productProcess) {
        WaitForSingleObject(g_productProcess, 5000);
        CloseHandle(g_productProcess);
    }
    g_sessionKey.Free();
    g_sessionId.Free();
    SecureMem::Wipe(&g_runKey, sizeof(g_runKey));
    if (g_log) fclose(g_log);

    Sleep(2000);
    return 0;
}
