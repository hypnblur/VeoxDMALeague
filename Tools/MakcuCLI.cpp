#define NOMINMAX
#include <Windows.h>
#include <setupapi.h>
#include <devguid.h>
#pragma comment(lib, "setupapi.lib")

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>

static HANDLE g_serial = INVALID_HANDLE_VALUE;
static int g_baud = 115200;

// ── Port detection ──────────────────────────────────────
static std::vector<std::string> ListPorts() {
    std::vector<std::string> ports;
    HDEVINFO info = SetupDiGetClassDevsA(&GUID_DEVCLASS_PORTS, 0, 0, DIGCF_PRESENT);
    if (info == INVALID_HANDLE_VALUE) return ports;
    SP_DEVINFO_DATA d{}; d.cbSize = sizeof(d);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(info, i, &d); ++i) {
        char buf[512]; DWORD n = 0;
        if (SetupDiGetDeviceRegistryPropertyA(info, &d, SPDRP_FRIENDLYNAME,
                NULL, (PBYTE)buf, sizeof(buf), &n) && n > 0) {
            buf[std::min<DWORD>(sizeof(buf) - 1, n)] = '\0';
            ports.emplace_back(buf);
        }
    }
    SetupDiDestroyDeviceInfoList(info);
    return ports;
}

static std::string ExtractCOM(const std::string& friendly) {
    auto p = friendly.find("COM");
    if (p == std::string::npos) return "";
    size_t numStart = p + 3;
    while (numStart < friendly.size() && friendly[numStart] == ' ') numStart++;
    size_t numEnd = numStart;
    while (numEnd < friendly.size() && isdigit((unsigned char)friendly[numEnd])) numEnd++;
    if (numEnd > numStart) return "COM" + friendly.substr(numStart, numEnd - numStart);
    return "";
}

static std::string FindMakcu() {
    const char* hints[] = {"CH343", "CH340", "CP210", "USB Serial"};
    auto ports = ListPorts();
    for (auto& h : hints)
        for (auto& p : ports)
            if (p.find(h) != std::string::npos) return ExtractCOM(p);
    return "";
}

// ── Serial I/O ──────────────────────────────────────────
static bool SerialOpen(const char* port, int baud) {
    std::string path = std::string("\\\\.\\") + port;
    g_serial = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_serial == INVALID_HANDLE_VALUE) return false;

    DCB dcb{}; dcb.DCBlength = sizeof(dcb);
    GetCommState(g_serial, &dcb);
    dcb.BaudRate = baud; dcb.ByteSize = 8; dcb.Parity = NOPARITY; dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE; dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE; dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE; dcb.fDsrSensitivity = FALSE;
    dcb.fOutX = FALSE; dcb.fInX = FALSE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE; dcb.fAbortOnError = FALSE;
    SetCommState(g_serial, &dcb);

    COMMTIMEOUTS to{}; to.ReadIntervalTimeout = MAXDWORD;
    to.WriteTotalTimeoutConstant = 50;
    SetCommTimeouts(g_serial, &to);
    SetupComm(g_serial, 4096, 256);
    PurgeComm(g_serial, PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);
    g_baud = baud;
    return true;
}

static void SerialClose() {
    if (g_serial != INVALID_HANDLE_VALUE) {
        CloseHandle(g_serial);
        g_serial = INVALID_HANDLE_VALUE;
    }
}

static bool SerialWrite(const char* data, size_t len) {
    DWORD written = 0;
    return WriteFile(g_serial, data, (DWORD)len, &written, nullptr) && written == len;
}

static int SerialRead(char* buf, int maxlen, int timeoutMs = 100) {
    int total = 0;
    auto start = std::chrono::steady_clock::now();
    while (total < maxlen) {
        DWORD errors = 0; COMSTAT status{};
        ClearCommError(g_serial, &errors, &status);
        if (status.cbInQue > 0) {
            DWORD toRead = std::min<DWORD>(status.cbInQue, maxlen - total);
            DWORD got = 0;
            if (ReadFile(g_serial, buf + total, toRead, &got, nullptr) && got > 0)
                total += got;
        }
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= timeoutMs) break;
        Sleep(1);
    }
    return total;
}

// ── Send command and print response ─────────────────────
static void SendCmd(const char* cmd, bool showHex = false) {
    std::string full = std::string(cmd) + "\r\n";
    printf("  > %s", cmd);
    if (!SerialWrite(full.c_str(), full.size())) {
        printf("  [WRITE FAILED]\n");
        return;
    }
    printf("\n");

    char buf[1024];
    int n = SerialRead(buf, sizeof(buf) - 1, 200);
    if (n > 0) {
        buf[n] = 0;
        if (showHex) {
            printf("  < hex[%d]: ", n);
            for (int i = 0; i < n; i++) printf("%02X ", (unsigned char)buf[i]);
            printf("\n");
        }
        // print printable lines
        for (int i = 0; i < n; ) {
            int start = i;
            while (i < n && buf[i] != '\r' && buf[i] != '\n') i++;
            if (i > start) printf("  < %.*s\n", i - start, buf + start);
            while (i < n && (buf[i] == '\r' || buf[i] == '\n')) i++;
        }
    }
}

// ── Latency test ────────────────────────────────────────
static void LatencyTest(int count) {
    printf("  Running %d round-trips (km.version)...\n", count);
    double total = 0; int ok = 0;
    for (int i = 0; i < count; i++) {
        PurgeComm(g_serial, PURGE_RXCLEAR);
        auto t0 = std::chrono::high_resolution_clock::now();
        const char cmd[] = "km.version()\r\n";
        if (!SerialWrite(cmd, sizeof(cmd) - 1)) { printf("  write failed at iteration %d\n", i); continue; }
        char buf[256]; int n = SerialRead(buf, sizeof(buf) - 1, 500);
        auto t1 = std::chrono::high_resolution_clock::now();
        if (n > 0) {
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            total += ms; ok++;
            if (i < 5 || i == count - 1) printf("  [%d] %.2f ms\n", i, ms);
            else if (i == 5) printf("  ...\n");
        } else {
            printf("  [%d] NO RESPONSE\n", i);
        }
        Sleep(5);
    }
    if (ok > 0) printf("  avg: %.2f ms over %d successful trips\n", total / ok, ok);
}

// ── Movement sequence test ──────────────────────────────
static void MoveTest(int dx, int dy, int steps, int delayMs) {
    printf("  Moving (%d,%d) x%d with %dms delay...\n", dx, dy, steps, delayMs);
    for (int i = 0; i < steps; i++) {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "km.move(%d,%d)", dx, dy);
        SendCmd(cmd);
        if (delayMs > 0) Sleep(delayMs);
    }
    printf("  Done.\n");
}

// ── Button monitor ──────────────────────────────────────
static void MonitorButtons(int durationSec) {
    printf("  Monitoring buttons for %d seconds (press Ctrl+C to stop early)...\n", durationSec);
    SendCmd("km.buttons(1)");
    auto start = std::chrono::steady_clock::now();
    unsigned char lastMask = 0xFF;
    while (true) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= durationSec) break;

        char buf[256]; int n = SerialRead(buf, sizeof(buf), 50);
        for (int i = 0; i < n; i++) {
            // button report: after "km." prefix, byte < 32 is the mask
            if (i >= 3 && buf[i-3] == 'k' && buf[i-2] == 'm' && buf[i-1] == '.'
                && (unsigned char)buf[i] < 32) {
                unsigned char mask = (unsigned char)buf[i];
                if (mask != lastMask) {
                    printf("  [%.1fs] buttons: L=%d R=%d M=%d X1=%d X2=%d (0x%02X)\n",
                        (double)elapsed, mask & 1, (mask >> 1) & 1, (mask >> 2) & 1,
                        (mask >> 3) & 1, (mask >> 4) & 1, mask);
                    lastMask = mask;
                }
            }
        }
    }
    printf("  Monitor done.\n");
}

// ── Help ────────────────────────────────────────────────
static void PrintHelp() {
    printf("\n");
    printf("  ┌─── Makcu CLI Tester ────────────────────────────┐\n");
    printf("  │                                                  │\n");
    printf("  │  Raw commands (sent directly to device):         │\n");
    printf("  │    km.version()     - query firmware version     │\n");
    printf("  │    km.move(x,y)     - relative mouse move        │\n");
    printf("  │    km.left(1/0)     - left button down/up        │\n");
    printf("  │    km.right(1/0)    - right button down/up       │\n");
    printf("  │    km.middle(1/0)   - middle button down/up      │\n");
    printf("  │    km.press(hid)    - key down (HID code)        │\n");
    printf("  │    km.release(hid)  - key up (HID code)          │\n");
    printf("  │    km.buttons(1/0)  - enable/disable btn reports │\n");
    printf("  │    km.lock_mx(1/0)  - lock X axis                │\n");
    printf("  │    km.lock_my(1/0)  - lock Y axis                │\n");
    printf("  │                                                  │\n");
    printf("  │  Shortcuts:                                      │\n");
    printf("  │    move X Y         - km.move(X,Y)               │\n");
    printf("  │    left / right     - click (down + 10ms + up)   │\n");
    printf("  │    ldown / lup      - left down / up             │\n");
    printf("  │    rdown / rup      - right down / up            │\n");
    printf("  │    key HID          - press + 10ms + release     │\n");
    printf("  │    version          - km.version()               │\n");
    printf("  │                                                  │\n");
    printf("  │  Tests:                                          │\n");
    printf("  │    latency [N]      - round-trip timing (def 20) │\n");
    printf("  │    movetest X Y N D - move sequence test         │\n");
    printf("  │    monitor [secs]   - watch button states (def 30)│\n");
    printf("  │    circle R steps   - circular mouse movement    │\n");
    printf("  │                                                  │\n");
    printf("  │  hex                - toggle hex dump of replies  │\n");
    printf("  │  ports              - list serial ports          │\n");
    printf("  │  reconnect [PORT]   - reconnect to device        │\n");
    printf("  │  baud N             - change baud rate           │\n");
    printf("  │  help / quit                                     │\n");
    printf("  └──────────────────────────────────────────────────┘\n\n");
}

// ── Circle test ─────────────────────────────────────────
static void CircleTest(int radius, int steps) {
    printf("  Drawing circle: radius=%d steps=%d\n", radius, steps);
    const double pi2 = 6.283185307;
    for (int i = 0; i < steps; i++) {
        double a0 = pi2 * i / steps;
        double a1 = pi2 * (i + 1) / steps;
        int dx = (int)(radius * (cos(a1) - cos(a0)));
        int dy = (int)(radius * (sin(a1) - sin(a0)));
        if (dx == 0 && dy == 0) continue;
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "km.move(%d,%d)", std::max(-127, std::min(127, dx)),
            std::max(-127, std::min(127, dy)));
        std::string full = std::string(cmd) + "\r\n";
        SerialWrite(full.c_str(), full.size());
        Sleep(5);
    }
    printf("  Done.\n");
}

// ── Main ────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    printf("╔══════════════════════════════════════════╗\n");
    printf("║        VeoxDMA - Makcu CLI Tester        ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    // Find port
    std::string port;
    int baud = 115200;

    for (int i = 1; i < argc; i++) {
        if (_strnicmp(argv[i], "COM", 3) == 0) port = argv[i];
        else if (atoi(argv[i]) > 1000) baud = atoi(argv[i]);
    }

    if (port.empty()) {
        printf("  Scanning for Makcu device...\n");
        port = FindMakcu();
        if (port.empty()) {
            printf("  No Makcu found. Available ports:\n");
            auto ports = ListPorts();
            for (auto& p : ports) printf("    %s\n", p.c_str());
            printf("\n  Usage: MakcuCLI.exe [COMx] [baud]\n");
            printf("  Enter port: ");
            char buf[32]; fgets(buf, sizeof(buf), stdin);
            int len = (int)strlen(buf);
            while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) buf[--len] = 0;
            if (buf[0]) port = buf;
            else return 1;
        }
    }

    // Try requested baud first, then fall back
    const int bauds[] = { baud, (baud == 115200) ? 4000000 : 115200 };
    bool connected = false;
    for (int b : bauds) {
        printf("  Trying %s at %d baud... ", port.c_str(), b);
        if (!SerialOpen(port.c_str(), b)) {
            printf("FAILED (error %lu)\n", GetLastError());
            continue;
        }
        // probe for version reply
        std::string q = "km.version()\r\n";
        SerialWrite(q.c_str(), q.size());
        char probe[256]; int n = SerialRead(probe, sizeof(probe)-1, 300);
        if (n > 0 && (strstr(probe, "km.") || strstr(probe, "MAKCU") || strstr(probe, "makcu"))) {
            probe[n] = 0;
            printf("OK\n");
            // print version line
            for (int i = 0; i < n; ) {
                int s = i;
                while (i < n && probe[i] != '\r' && probe[i] != '\n') i++;
                if (i > s) printf("  < %.*s\n", i - s, probe + s);
                while (i < n && (probe[i] == '\r' || probe[i] == '\n')) i++;
            }
            connected = true;
            break;
        }
        printf("no reply\n");
        SerialClose();
    }
    if (!connected) {
        printf("  FAILED - no Makcu reply at any baud rate.\n");
        return 1;
    }

    printf("\n  Type 'help' for commands.\n\n");

    bool showHex = false;
    char line[512];
    while (true) {
        printf("makcu> ");
        if (!fgets(line, sizeof(line), stdin)) break;
        int len = (int)strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r' || line[len-1] == ' '))
            line[--len] = 0;
        if (len == 0) continue;

        // lowercase copy for matching
        char lower[512];
        for (int i = 0; i <= len; i++) lower[i] = (char)tolower((unsigned char)line[i]);

        if (strcmp(lower, "quit") == 0 || strcmp(lower, "exit") == 0 || strcmp(lower, "q") == 0) break;
        if (strcmp(lower, "help") == 0 || strcmp(lower, "?") == 0) { PrintHelp(); continue; }
        if (strcmp(lower, "hex") == 0) { showHex = !showHex; printf("  Hex dump: %s\n", showHex ? "ON" : "OFF"); continue; }
        if (strcmp(lower, "version") == 0) { SendCmd("km.version()", showHex); continue; }

        if (strcmp(lower, "ports") == 0) {
            auto ports = ListPorts();
            for (auto& p : ports) printf("  %s\n", p.c_str());
            continue;
        }

        if (strncmp(lower, "reconnect", 9) == 0) {
            SerialClose();
            char* arg = line + 9; while (*arg == ' ') arg++;
            if (*arg) port = arg;
            printf("  Reconnecting to %s at %d...\n", port.c_str(), g_baud);
            if (SerialOpen(port.c_str(), g_baud)) { printf("  OK\n"); SendCmd("km.version()", showHex); }
            else printf("  FAILED (error %lu)\n", GetLastError());
            continue;
        }

        if (strncmp(lower, "baud ", 5) == 0) {
            int b = atoi(line + 5);
            if (b >= 9600) {
                SerialClose();
                g_baud = b;
                printf("  Reconnecting at %d baud...\n", b);
                if (SerialOpen(port.c_str(), b)) { printf("  OK\n"); SendCmd("km.version()", showHex); }
                else printf("  FAILED\n");
            } else printf("  Invalid baud\n");
            continue;
        }

        // shortcuts
        if (strcmp(lower, "left") == 0) {
            SendCmd("km.left(1)", showHex); Sleep(10); SendCmd("km.left(0)", showHex); continue;
        }
        if (strcmp(lower, "right") == 0) {
            SendCmd("km.right(1)", showHex); Sleep(10); SendCmd("km.right(0)", showHex); continue;
        }
        if (strcmp(lower, "ldown") == 0) { SendCmd("km.left(1)", showHex); continue; }
        if (strcmp(lower, "lup") == 0) { SendCmd("km.left(0)", showHex); continue; }
        if (strcmp(lower, "rdown") == 0) { SendCmd("km.right(1)", showHex); continue; }
        if (strcmp(lower, "rup") == 0) { SendCmd("km.right(0)", showHex); continue; }

        if (strncmp(lower, "move ", 5) == 0) {
            int x = 0, y = 0;
            sscanf(line + 5, "%d %d", &x, &y);
            char cmd[64]; snprintf(cmd, sizeof(cmd), "km.move(%d,%d)", x, y);
            SendCmd(cmd, showHex); continue;
        }

        if (strncmp(lower, "key ", 4) == 0) {
            int hid = atoi(line + 4);
            char cmd[64];
            snprintf(cmd, sizeof(cmd), "km.press(%d)", hid);
            SendCmd(cmd, showHex);
            Sleep(10);
            snprintf(cmd, sizeof(cmd), "km.release(%d)", hid);
            SendCmd(cmd, showHex);
            continue;
        }

        if (strncmp(lower, "latency", 7) == 0) {
            int count = 20;
            if (lower[7] == ' ') count = atoi(line + 8);
            if (count < 1) count = 1;
            if (count > 1000) count = 1000;
            LatencyTest(count); continue;
        }

        if (strncmp(lower, "movetest ", 9) == 0) {
            int x = 0, y = 0, n = 10, d = 10;
            sscanf(line + 9, "%d %d %d %d", &x, &y, &n, &d);
            MoveTest(x, y, n, d); continue;
        }

        if (strncmp(lower, "monitor", 7) == 0) {
            int secs = 30;
            if (lower[7] == ' ') secs = atoi(line + 8);
            if (secs < 1) secs = 1;
            MonitorButtons(secs); continue;
        }

        if (strncmp(lower, "circle ", 7) == 0) {
            int r = 50, s = 36;
            sscanf(line + 7, "%d %d", &r, &s);
            if (s < 4) s = 4;
            CircleTest(r, s); continue;
        }

        // raw command passthrough — anything starting with km. or any other text
        SendCmd(line, showHex);
    }

    SerialClose();
    printf("  Disconnected.\n");
    return 0;
}
