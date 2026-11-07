#define NOMINMAX
#include "MakcuTransport.h"
#include "MakcuProtocol.h"

#include <Windows.h>
#include <setupapi.h>
#include <devguid.h>
#pragma comment(lib, "setupapi.lib")

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdarg>
#include <cctype>
#include <mutex>
#include <thread>
#include <string>

// Inline port detection (replaces kmbox::suggest_serial_hardware_port)
static std::string FindPort(const std::string& targetDescription)
{
    HDEVINFO hDevInfo = SetupDiGetClassDevsA(&GUID_DEVCLASS_PORTS, 0, 0, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) return "";
    SP_DEVINFO_DATA deviceInfoData = {};
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &deviceInfoData); ++i) {
        char buf[512]; DWORD nSize = 0;
        if (SetupDiGetDeviceRegistryPropertyA(hDevInfo, &deviceInfoData, SPDRP_FRIENDLYNAME,
                NULL, (PBYTE)buf, sizeof(buf), &nSize) && nSize > 0) {
            if (nSize >= sizeof(buf)) nSize = sizeof(buf) - 1;
            buf[nSize] = '\0';
            std::string desc = buf;
            size_t comPos = desc.find("COM");
            size_t endPos = desc.find(")", comPos);
            if (comPos != std::string::npos && endPos != std::string::npos && desc.find(targetDescription) != std::string::npos) {
                SetupDiDestroyDeviceInfoList(hDevInfo);
                std::string raw = desc.substr(comPos, endPos - comPos);
                size_t numStart = 3;
                while (numStart < raw.size() && raw[numStart] == ' ') numStart++;
                size_t numEnd = numStart;
                while (numEnd < raw.size() && std::isdigit((unsigned char)raw[numEnd])) numEnd++;
                if (numEnd > numStart) return "COM" + raw.substr(numStart, numEnd - numStart);
                return raw;
            }
        }
    }
    SetupDiDestroyDeviceInfoList(hDevInfo);
    return "";
}

static std::string SuggestMakcuPort()
{
    std::string p = FindPort("USB-Enhanced-SERIAL CH343");
    if (!p.empty()) return p;
    p = FindPort("CH343");
    if (!p.empty()) return p;
    p = FindPort("USB-SERIAL CH340");
    if (!p.empty()) return p;
    p = FindPort("CP210");
    if (!p.empty()) return p;
    p = FindPort("USB Serial Device");
    if (!p.empty()) return p;
    return "";
}

namespace Makcu {
namespace {
struct Transport {
    std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> generation{0};
    std::atomic<State> state{State::Disconnected};
    std::atomic<unsigned> buttons{0};
    std::atomic<AimState> aim{AimState::Disabled};
    std::atomic<unsigned> bindInfo{0};
    std::atomic<int> testResult{0};
    std::atomic<bool> testPending{false};
    std::atomic<bool> queueFault{false};
    std::atomic<uint64_t> axisLease[2]{};
    bool axesLocked = false;
    char port[64]{}, error[240]{};
    int baud = 115200;
    bool wantConnection = false;
    Outbox outbox;
    HANDLE handle = INVALID_HANDLE_VALUE;
    bool configured = false;
    Parser parser;
    uint64_t writeOk = 0, writeFail = 0, reports = 0, lastVersion = 0;
    int lastBaud = 115200;
    unsigned monitorRequests = 0;
    uint64_t heartbeat = 0;
    ~Transport() { Stop(); }
    void Stop() {
        stop = true; wake.notify_all();
        if (worker.joinable()) worker.join();
    }
    bool Current(uint64_t gen) { return !stop && generation.load() == gen; }
    bool Pause(uint64_t gen, unsigned ms) {
        std::unique_lock<std::mutex> lock(mutex);
        wake.wait_for(lock, std::chrono::milliseconds(ms), [&] { return !Current(gen); });
        return Current(gen);
    }
    void Log(const char* fmt, ...) {
        char body[512]{};
        va_list args; va_start(args, fmt);
        vsnprintf_s(body, _TRUNCATE, fmt, args); va_end(args);
        CreateDirectoryA("C:\\VeoxDMA", nullptr);
        HANDLE file = CreateFileA("C:\\VeoxDMA\\makcu.log", FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return;
        SYSTEMTIME time{}; GetLocalTime(&time);
        char line[640]{};
        const int n = snprintf(line, sizeof(line), "[%04u-%02u-%02u %02u:%02u:%02u] [Makcu] %s\r\n",
            time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,body);
        DWORD written = 0;
        if (n > 0) WriteFile(file, line, static_cast<DWORD>(n), &written, nullptr);
        CloseHandle(file);
    }
    void Close() {
        axisLease[0] = axisLease[1] = 0;
        axesLocked = false;
        if (handle != INVALID_HANDLE_VALUE) {
            static const char neutral[] = "km.left(0)\r\nkm.right(0)\r\nkm.middle(0)\r\nkm.ms1(0)\r\nkm.ms2(0)\r\nkm.buttons(0)\r\nkm.lock_mx(0)\r\nkm.lock_my(0)\r\n";
            DWORD n = 0;
            PurgeComm(handle, PURGE_TXABORT | PURGE_TXCLEAR);
            if (configured) WriteFile(handle, neutral, sizeof(neutral)-1, &n, nullptr);
            CloseHandle(handle); handle = INVALID_HANDLE_VALUE;
        }
        configured = false; parser = {}; buttons = 0;
    }
    void Fail(uint64_t gen, const char* reason) {
        const DWORD osError = GetLastError();
        { std::lock_guard<std::mutex> lock(mutex);
          if (!Current(gen)) return;
          snprintf(error, sizeof(error), "%s (Windows error %lu)", reason, osError);
          state = State::Failed; buttons = 0; outbox.Clear();
          if (testPending.exchange(false)) testResult = -1;
        }
        Log("link failed generation=%llu reason=%s osError=%lu writesOk=%llu writesFailed=%llu",
            gen, reason, osError, writeOk, writeFail);
    }
    bool Configure(int rate) {
        DCB dcb{}; dcb.DCBlength = sizeof(dcb);
        if (!GetCommState(handle, &dcb)) return false;
        dcb.BaudRate = rate; dcb.ByteSize = 8; dcb.Parity = NOPARITY; dcb.StopBits = ONESTOPBIT;
        dcb.fBinary = TRUE; dcb.fParity = FALSE;
        dcb.fOutxCtsFlow = FALSE; dcb.fOutxDsrFlow = FALSE;
        dcb.fDtrControl = DTR_CONTROL_DISABLE; dcb.fDsrSensitivity = FALSE;
        dcb.fTXContinueOnXoff = FALSE; dcb.fOutX = FALSE; dcb.fInX = FALSE;
        dcb.fErrorChar = FALSE; dcb.fNull = FALSE;
        dcb.fRtsControl = RTS_CONTROL_DISABLE; dcb.fAbortOnError = FALSE;
        return SetCommState(handle, &dcb) != FALSE;
    }
    bool Write(const void* data, size_t size) {
        DWORD count = 0;
        const bool wrote = WriteFile(handle, data, static_cast<DWORD>(size), &count, nullptr) != FALSE;
        const DWORD error = wrote ? (count == size ? ERROR_SUCCESS : ERROR_WRITE_FAULT) : GetLastError();
        const bool ok = wrote && count == size;
        if (ok) ++writeOk; else ++writeFail;
        if (!ok) {
            Log("serial write failed requested=%zu written=%lu osError=%lu", size, count, error);
            SetLastError(error);
        }
        return ok;
    }
    bool Read(uint64_t gen) {
        DWORD errors = 0; COMSTAT status{};
        if (!ClearCommError(handle, &errors, &status)) return false;
        if (errors & (CE_BREAK | CE_FRAME | CE_RXPARITY | CE_OVERRUN | CE_RXOVER)) {
            Log("serial receive error flags=0x%lX queuedRx=%lu queuedTx=%lu", errors, status.cbInQue, status.cbOutQue);
            buttons = 0; parser = {}; SetLastError(ERROR_INVALID_DATA); return false;
        }
        if (!status.cbInQue) return true;
        unsigned char buffer[512]; DWORD count = 0;
        if (!ReadFile(handle, buffer, (std::min)(status.cbInQue, DWORD(sizeof(buffer))), &count, nullptr))
            return false;
        for (DWORD i = 0; i < count; ++i) {
            const auto event = parser.Feed(buffer[i]);
            if (event == Parser::Event::Buttons) {
                bool firstReport = false;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if (!Current(gen)) continue;
                    unsigned previous = buttons.load();
                    firstReport = !(previous & 0x100u);
                    unsigned next;
                    do {
                        const unsigned pressed = parser.mask & ~(previous & 0x1Fu);
                        next = (previous & 0x1F0000u) | (pressed << 16) | 0x100u | parser.mask;
                    } while (!buttons.compare_exchange_weak(previous, next));
                    ++reports;
                }
                if (firstReport) Log("button monitoring verified generation=%llu mask=0x%02X", gen, parser.mask);
            } else if (event == Parser::Event::Version) lastVersion = GetTickCount64();
        }
        return true;
    }
    bool Probe(uint64_t gen) {
        parser = {}; lastVersion = 0;
        PurgeComm(handle, PURGE_RXCLEAR);
        static const char query[] = "km.version()\r\n";
        if (!Write(query, sizeof(query)-1)) return false;
        const auto start = GetTickCount64();
        while (Current(gen) && GetTickCount64() - start < 250) {
            if (!Read(gen)) return false;
            if (lastVersion) return true;
            if (!Pause(gen, 2)) return false;
        }
        SetLastError(WAIT_TIMEOUT); return false;
    }
    bool Open(uint64_t gen, std::string selected, int rate) {
        if (rate < 115200 || rate > 4000000) {
            SetLastError(ERROR_INVALID_PARAMETER);
            Fail(gen, "Makcu baud must be between 115200 and 4000000"); return false;
        }
        if (selected.empty() || selected == "COM0" || selected == "com0")
            selected = SuggestMakcuPort();
        for (char& c : selected) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
        if (selected.size() < 4 || selected.compare(0,3,"COM") != 0
            || selected.find_first_not_of("0123456789", 3) != std::string::npos) {
            SetLastError(ERROR_INVALID_PARAMETER); Fail(gen, "Select a valid Makcu COM port"); return false;
        }
        Log("connect generation=%llu port=%s requestedBaud=%d",gen,selected.c_str(),rate);
        handle = CreateFileA(("\\\\.\\" + selected).c_str(), GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) { Fail(gen,"Could not open Makcu port"); return false; }
        COMMTIMEOUTS timeouts{}; timeouts.ReadIntervalTimeout = MAXDWORD;
        timeouts.WriteTotalTimeoutConstant = 20;
        if (!SetupComm(handle, 4096, 256) || !SetCommTimeouts(handle,&timeouts)) {
            Fail(gen,"Could not configure Makcu port buffers/timeouts"); return false;
        }
        configured = true;
        PurgeComm(handle,PURGE_TXABORT|PURGE_RXABORT|PURGE_TXCLEAR|PURGE_RXCLEAR);
        const int rates[] = {rate, lastBaud, 115200, 4000000, 921600, 1500000, 2000000, 3000000};
        int found = 0;
        for (size_t i=0; i<sizeof(rates)/sizeof(rates[0]) && Current(gen); ++i) {
            bool duplicate = false;
            for (size_t j=0; j<i; ++j) duplicate |= rates[j] == rates[i];
            if (duplicate) continue;
            if (Configure(rates[i]) && Probe(gen)) {
                found = rates[i];
                Log("baud discovered generation=%llu baud=%d", gen, found);
                break;
            }
        }
        if (!Current(gen)) return false;
        if (!found) { Fail(gen,"No Makcu version reply at startup/reconnect baud rates"); return false; }
        if (found != rate) {
            const auto command = BaudCommand(static_cast<uint32_t>(rate));
            if (!Write(command.data(),command.size())) { Fail(gen,"Makcu baud command write failed"); return false; }
            const auto start = GetTickCount64();
            DWORD errors = 0; COMSTAT status{};
            do {
                if (!ClearCommError(handle,&errors,&status)) { Fail(gen,"Makcu baud drain failed"); return false; }
                if (!status.cbOutQue) break;
                if (!Pause(gen,2)) return false;
            } while (GetTickCount64()-start < 100);
            if (status.cbOutQue) { SetLastError(WAIT_TIMEOUT); Fail(gen,"Makcu baud drain timed out"); return false; }
            if (!Pause(gen,50)) return false;
            if (!Configure(rate) || !Probe(gen)) { Fail(gen,"Makcu did not confirm requested baud"); return false; }
        }
        lastBaud = rate;
        static const char init[] = "km.left(0)\r\nkm.right(0)\r\nkm.ms1(0)\r\nkm.ms2(0)\r\nkm.buttons(1)\r\nkm.lock_mx(0)\r\nkm.lock_my(0)\r\n";
        if (!Write(init,sizeof(init)-1)) { Fail(gen,"Makcu initialization write failed"); return false; }
        monitorRequests = 1;
        heartbeat = GetTickCount64();
        { std::lock_guard<std::mutex> lock(mutex);
          if (!Current(gen)) return false;
          error[0] = 0; state = State::Connected;
        }
        Log("connected generation=%llu port=%s baud=%d",gen,selected.c_str(),rate);
        return true;
    }
    bool WriteAxisLock(bool locked) {
        axesLocked = locked;
        const char* command = locked ? "km.lock_mx(1)\r\nkm.lock_my(1)\r\n"
                                     : "km.lock_mx(0)\r\nkm.lock_my(0)\r\n";
        return Write(command, std::strlen(command));
    }
    void Run() {
        uint64_t active = 0;
        while (!stop) {
            uint64_t gen; char selected[64]{}; int rate; bool wanted;
            { std::unique_lock<std::mutex> lock(mutex);
              if (state != State::Connected && generation.load() == active)
                  wake.wait(lock,[&] { return stop || generation.load()!=active; });
              else wake.wait_for(lock,std::chrono::milliseconds(2),[&] {
                  return stop || generation.load()!=active || outbox.count || outbox.hasMovement || outbox.hasRecoilMovement;
              });
              if (stop) break;
              gen = generation; rate = baud; wanted = wantConnection;
              memcpy(selected,port,sizeof(selected));
            }
            if (gen != active) {
                Close(); active = gen;
                if (!wanted) continue;
                try { if (!Open(gen,selected,rate)) { Close(); continue; } }
                catch (...) { SetLastError(ERROR_UNHANDLED_EXCEPTION); Fail(gen,"Makcu connection exception"); Close(); continue; }
            }
            if (!Current(gen) || state != State::Connected) continue;
            if (queueFault.exchange(false)) {
                SetLastError(ERROR_BUFFER_OVERFLOW); Fail(gen,"Makcu control queue overflow"); Close(); continue;
            }
            const auto leaseNow = GetTickCount64();
            const bool wantAxes = axisLease[0].load() > leaseNow || axisLease[1].load() > leaseNow;
            if (wantAxes != axesLocked && !WriteAxisLock(wantAxes)) {
                Fail(gen, "Makcu axis lock write failed"); Close(); continue;
            }
            Command command;
            bool have = false;
            { std::lock_guard<std::mutex> lock(mutex);
              if (Current(gen)) have = outbox.Pop(command,GetTickCount64());
            }
            if (have && Current(gen)) {
                const bool ok = Write(command.text.data(),command.size);
                if (command.test) {
                    std::lock_guard<std::mutex> lock(mutex);
                    if (Current(gen)) { testResult = ok ? 1 : -1; testPending = false; }
                }
                if (!ok) { Fail(gen,"Makcu serial write failed/short"); Close(); continue; }
            }
            if (!Read(gen)) { Fail(gen,"Makcu receive/link error"); Close(); continue; }
            const auto now = GetTickCount64();
            if (now - lastVersion > 15000) { SetLastError(WAIT_TIMEOUT); Fail(gen,"Makcu heartbeat reply timed out"); Close(); continue; }
            if (now - heartbeat >= 5000) {
                heartbeat = now;
                static const char query[] = "km.version()\r\n";
                if (!Write(query,sizeof(query)-1)) { Fail(gen,"Makcu heartbeat write failed"); Close(); continue; }
                if (!(buttons.load() & 0x100u) && monitorRequests < 3) {
                    static const char monitor[] = "km.buttons(1)\r\n";
                    if (!Write(monitor,sizeof(monitor)-1)) { Fail(gen,"Makcu button monitoring write failed"); Close(); continue; }
                    ++monitorRequests;
                }
            }
        }
        Close();
    }
};
Transport transport;
}
void Connect(const char* port, int baud) {
    std::lock_guard<std::mutex> lock(transport.mutex);
    transport.outbox.Clear(); transport.buttons = 0; transport.queueFault = false;
    if (transport.testPending.exchange(false)) transport.testResult = -1;
    snprintf(transport.port,sizeof(transport.port),"%s",port ? port : "");
    transport.baud = baud; transport.wantConnection = true;
    transport.error[0] = 0; transport.state = State::Connecting;
    ++transport.generation;
    if (!transport.worker.joinable()) {
        transport.stop = false;
        try { transport.worker = std::thread([] { transport.Run(); }); }
        catch (...) { transport.state = State::Failed; snprintf(transport.error,sizeof(transport.error),"Could not start Makcu worker"); }
    }
    transport.wake.notify_all();
}
void Disconnect() {
    std::lock_guard<std::mutex> lock(transport.mutex);
    transport.wantConnection = false; ++transport.generation;
    transport.state = State::Disconnected; transport.buttons = 0; transport.outbox.Clear();
    if (transport.testPending.exchange(false)) transport.testResult = -1;
    transport.wake.notify_all();
}
void Shutdown() { Disconnect(); transport.Stop(); }
State GetState() { return transport.state; }
void SetAxisLock(bool locked, unsigned source) {
    if (source > 1) return;
    transport.axisLease[source] = locked && GetState() == State::Connected
        ? GetTickCount64() + 150 : 0;
    transport.wake.notify_all();
}
void GetError(char* out, size_t size) {
    if (!out || !size) return;
    std::lock_guard<std::mutex> lock(transport.mutex);
    snprintf(out,size,"%s",transport.error);
}
bool Send(const char* text, size_t size, bool movement, bool test, bool recoil) {
    if (!text || !size || size >= Command{}.text.size() || GetState()!=State::Connected) return false;
    std::lock_guard<std::mutex> lock(transport.mutex);
    if (GetState()!=State::Connected || (test && transport.testPending)) return false;
    Command c; memcpy(c.text.data(),text,size); c.size=size; c.created=GetTickCount64(); c.test=test;
    if (!transport.outbox.Push(c,movement,recoil)) {
        transport.queueFault = true; transport.wake.notify_all(); return false;
    }
    if (test) { transport.testPending=true; transport.testResult=0; }
    transport.wake.notify_all(); return true;
}
bool MouseDown(int vk, bool& down) {
    if (GetState()!=State::Connected) return false;
    const unsigned value = transport.buttons;
    if (!(value & 0x100)) return false;
    const int bit = vk==VK_LBUTTON ? 0 : vk==VK_RBUTTON ? 1 : vk==VK_MBUTTON ? 2
        : vk==VK_XBUTTON1 ? 3 : vk==VK_XBUTTON2 ? 4 : -1;
    if (bit<0) return false;
    down = (value & (1u<<bit)) != 0; return true;
}
ButtonSample TakeButtonSample() {
    std::lock_guard<std::mutex> lock(transport.mutex);
    const unsigned value = transport.buttons.fetch_and(~0x1F0000u);
    if (GetState()!=State::Connected || !(value & 0x100u)) return {};
    return {value & 0x1Fu, (value >> 16) & 0x1Fu, true};
}
bool ButtonsReady() {
    return GetState()==State::Connected && (transport.buttons.load() & 0x100u)!=0;
}
void ReportAim(AimState state) {
    transport.aim=state;
    if (state != AimState::Queued && state != AimState::Delay) {
        std::lock_guard<std::mutex> lock(transport.mutex);
        transport.outbox.hasMovement = false;
    }
}
void ReportBind(bool dmaReady, bool hasMouseBind, bool hasKeyboardBind, int mode) {
    transport.bindInfo = (dmaReady ? 1u : 0u) | (hasMouseBind ? 2u : 0u)
        | (hasKeyboardBind ? 4u : 0u) | (static_cast<unsigned>(mode) << 3);
}
int TakeTestResult() { return transport.testResult.exchange(0); }
}
