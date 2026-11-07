#define NOMINMAX
#include "InputManager.h"
#include "Memory.h"

#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
#include <chrono>

namespace Keyboard {
    uint64_t gafAsyncKeyStateExport = 0;
    uint8_t  state_bitmap[64]{};
    uint8_t  previous_state_bitmap[256 / 8]{};
    uint64_t win32kbase = 0;
    int      win_logon_pid = 0;
}

namespace {

bool IsKernelVa(uint64_t va) { return va > 0x7FFFFFFFFFFF; }

template<typename T>
bool ReadExact(int pid, uint64_t va, T& value)
{
    DWORD cb = 0;
    return VMMDLL_MemReadEx(TargetProcess.vHandle, pid | VMMDLL_PID_PROCESS_WITH_KERNELMEMORY, va,
        (PBYTE)&value, sizeof(T), &cb, VMMDLL_FLAG_NOCACHE) && cb == sizeof(T);
}

uint8_t HexByte(const char* hex)
{
    char b[3] = { hex[0], hex[1], 0 };
    return (uint8_t)strtoul(b, nullptr, 16);
}

uint64_t ScanKernelModule(const char* signature, uint64_t start, uint64_t end, int pid)
{
    std::vector<int> pattern;
    for (const char* p = signature; *p;) {
        while (*p == ' ') ++p;
        if (!*p) break;
        if (*p == '?') { pattern.push_back(-1); while (*p == '?') ++p; }
        else { pattern.push_back(HexByte(p)); p += 2; }
    }
    auto matches = [&](const uint8_t* bytes) {
        for (size_t i = 0; i < pattern.size(); ++i)
            if (pattern[i] >= 0 && bytes[i] != (uint8_t)pattern[i])
                return false;
        return true;
    };

    constexpr uint64_t kChunk = 64 * 1024;
    const uint64_t overlap = pattern.size() - 1;
    std::vector<uint8_t> buffer((size_t)(kChunk + overlap));
    std::vector<uint8_t> verified(pattern.size());
    for (uint64_t chunk = start; chunk < end; chunk += kChunk) {
        const DWORD requested = (DWORD)(std::min)(end - chunk, kChunk + overlap);
        std::fill(buffer.begin(), buffer.end(), 0);
        DWORD cb = 0;
        if (VMMDLL_MemReadEx(TargetProcess.vHandle, pid, chunk, buffer.data(), requested, &cb,
            VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_ZEROPAD_ON_FAIL) && requested >= pattern.size()) {
            for (size_t off = 0; off + pattern.size() <= requested; ++off) {
                if (!matches(buffer.data() + off)) continue;
                DWORD vcb = 0;
                if (VMMDLL_MemReadEx(TargetProcess.vHandle, pid, chunk + off, verified.data(), (DWORD)verified.size(),
                    &vcb, VMMDLL_FLAG_NOCACHE) && vcb == verified.size() && matches(verified.data()))
                    return chunk + off;
            }
        }
    }
    return 0;
}

bool FindModule(int pid, const char* name, uint64_t& base, uint32_t& size)
{
    PVMMDLL_MAP_MODULEENTRY m = nullptr;
    if (!VMMDLL_Map_GetModuleFromNameU(TargetProcess.vHandle, pid, (LPSTR)name, &m, VMMDLL_MODULE_FLAG_NORMAL))
        return false;
    base = m->vaBase;
    size = m->cbImageSize;
    VMMDLL_MemFree(m);
    return true;
}

bool PrimeBitmap()
{
    for (int attempt = 0; attempt < 3; ++attempt) {
        DWORD cb = 0;
        if (VMMDLL_MemReadEx(TargetProcess.vHandle, Keyboard::win_logon_pid | VMMDLL_PID_PROCESS_WITH_KERNELMEMORY,
            Keyboard::gafAsyncKeyStateExport, Keyboard::state_bitmap, 64, &cb, VMMDLL_FLAG_NOCACHE) && cb == 64)
            return true;
        Sleep(5);
    }
    return false;
}

bool InitFromEat()
{
    PVMMDLL_MAP_EAT eat_map = NULL;
    if (VMMDLL_Map_GetEATU(TargetProcess.vHandle, Keyboard::win_logon_pid | VMMDLL_PID_PROCESS_WITH_KERNELMEMORY, (LPSTR)"win32kbase.sys", &eat_map)
        && eat_map->dwVersion == VMMDLL_MAP_EAT_VERSION) {
        for (DWORD i = 0; i < eat_map->cMap; i++) {
            if (strcmp(eat_map->pMap[i].uszFunction, "gafAsyncKeyState") == 0) {
                Keyboard::gafAsyncKeyStateExport = eat_map->pMap[i].vaFunction;
                break;
            }
        }
    }
    if (eat_map) VMMDLL_MemFree(eat_map);
    if (IsKernelVa(Keyboard::gafAsyncKeyStateExport) && PrimeBitmap())
        return true;
    printf("[Input] win32kbase.sys EAT: gafAsyncKeyState %s\n",
        IsKernelVa(Keyboard::gafAsyncKeyStateExport) ? "found but unreadable" : "not found");
    Keyboard::gafAsyncKeyStateExport = 0;
    return false;
}

bool InitFromSession(ULONG64 build)
{
    DWORD session = 0;
    {
        VMMDLL_PROCESS_INFORMATION info{};
        info.magic = VMMDLL_PROCESS_INFORMATION_MAGIC;
        info.wVersion = VMMDLL_PROCESS_INFORMATION_VERSION;
        SIZE_T cb = sizeof(info);
        if (VMMDLL_ProcessGetInformation(TargetProcess.vHandle, Keyboard::win_logon_pid, &info, &cb))
            session = info.win.dwSessionId;
    }

    const bool is24H2 = build >= 26100;
    const char* mods[2] = { is24H2 ? "win32k.sys" : "win32ksgd.sys", is24H2 ? "win32ksgd.sys" : "win32k.sys" };
    const char* failure = "no csrss.exe";

    for (int pid : TargetProcess.GetPidListFromName("csrss.exe")) {
        VMMDLL_PROCESS_INFORMATION ci{};
        ci.magic = VMMDLL_PROCESS_INFORMATION_MAGIC;
        ci.wVersion = VMMDLL_PROCESS_INFORMATION_VERSION;
        SIZE_T ccb = sizeof(ci);
        if (VMMDLL_ProcessGetInformation(TargetProcess.vHandle, pid, &ci, &ccb)) {}

        for (const char* mod : mods) {
            uint64_t modBase = 0; uint32_t modSize = 0;
            if (!FindModule(pid, mod, modBase, modSize)) {
                failure = "win32k module not mapped in csrss";
                continue;
            }

            struct SessionSig { const char* pat; bool decrementSession; };
            const SessionSig sigs[] = {
                { "48 8B 05 ? ? ? ? FF C9 48 8B 04 C8", true },
                { "48 8B 05 ? ? ? ? 48 8B 04 C8", false },
                { "48 8B 05 ? ? ? ? FF C9", true },
            };
            std::vector<std::pair<uint64_t, bool>> slotTables;
            for (int si = 0; si < 3; ++si) {
                const auto& s = sigs[si];
                const uint64_t insn = ScanKernelModule(s.pat, modBase, modBase + modSize, pid);
                int32_t rel = 0;
                if (insn && ReadExact(pid, insn + 3, rel)) {
                    slotTables.push_back({ insn + 7 + rel, s.decrementSession });
                    break;
                }
            }
            if (slotTables.empty() && is24H2 && strcmp(mod, "win32k.sys") == 0) {
                for (uint32_t rva : { 0x824F0u, 0x82538u }) {
                    const uint64_t site = modBase + rva;
                    uint8_t head[3]{}; int32_t rel = 0;
                    if (!ReadExact(pid, site, head)) continue;
                    if (head[0] == 0x48 && head[1] == 0x8B && head[2] == 0x05) {
                        if (ReadExact(pid, site + 3, rel))
                            slotTables.push_back({ site + 7 + rel, true });
                    } else {
                        slotTables.push_back({ site, true });
                    }
                }
            }

            std::vector<uint64_t> states;
            for (const auto& [slots, dec] : slotTables) {
                uint64_t slotPtr = 0;
                if (!IsKernelVa(slots) || !ReadExact(pid, slots, slotPtr) || !IsKernelVa(slotPtr))
                    continue;
                for (uint64_t i = 0; i < 8; ++i) {
                    uint64_t slotValue = 0, state = 0;
                    if (!ReadExact(pid, slotPtr + 8 * i, slotValue) || !IsKernelVa(slotValue))
                        continue;
                    if (ReadExact(pid, slotValue, state) && IsKernelVa(state)) {
                        if (std::find(states.begin(), states.end(), state) == states.end())
                            states.push_back(state);
                    } else {
                        if (std::find(states.begin(), states.end(), slotValue) == states.end())
                            states.push_back(slotValue);
                    }
                }
                if (states.empty())
                    states.push_back(slotPtr);
            }
            if (states.empty()) {
                failure = slotTables.empty() ? "session signature not found" : "no readable session slot";
                continue;
            }

            uint64_t kbBase = 0; uint32_t kbSize = 0;
            if (!FindModule(pid, "win32kbase.sys", kbBase, kbSize)) {
                failure = "win32kbase.sys not mapped in csrss";
                continue;
            }
            struct OffSig { const char* pat; int dispAt; };
            const OffSig offSigs[] = {
                { "48 8D 90 ? ? ? ? E8 ? ? ? ? 0F 57 C0", 3 },
                { "B9 00 80 FF FF ? 22 B4 ? ? ? ? ? 41", 9 },
                { "40 0F B6 ? 48 C1 E9 02 8A 94 08 ? ? ? ?", 11 },
            };
            uint32_t gafOffset = 0;
            for (const auto& s : offSigs) {
                const uint64_t hit = ScanKernelModule(s.pat, kbBase, kbBase + kbSize, pid);
                uint32_t off = 0;
                if (hit && ReadExact(pid, hit + s.dispAt, off) && off >= 0x2000 && off <= 0x8000) {
                    gafOffset = off;
                    break;
                }
            }
            if (!gafOffset) {
                failure = "gafAsyncKeyState offset signature not found";
                continue;
            }

            for (uint64_t state : states) {
                Keyboard::gafAsyncKeyStateExport = state + gafOffset;
                if (PrimeBitmap()) {
                    printf("[Input] gafAsyncKeyState 0x%llX (session state 0x%llX + 0x%X, %s)\n",
                        Keyboard::gafAsyncKeyStateExport, state, gafOffset, mod);
                    return true;
                }
            }
            Keyboard::gafAsyncKeyStateExport = 0;
            failure = "bitmap unreadable at every session candidate";
        }
    }
    printf("[Input] Win11 gafAsyncKeyState lookup failed: %s\n", failure);
    return false;
}

} // anon namespace

bool Keyboard::InitKeyboard()
{
    gafAsyncKeyStateExport = 0;

    ULONG64 build = 0;
    if (!VMMDLL_ConfigGet(TargetProcess.vHandle, VMMDLL_OPT_WIN_VERSION_BUILD, &build)) {
        printf("[Input] could not read the game PC Windows build\n");
        return false;
    }

    DWORD targetSession = 0;
    {
        VMMDLL_PROCESS_INFORMATION gi{};
        gi.magic = VMMDLL_PROCESS_INFORMATION_MAGIC;
        gi.wVersion = VMMDLL_PROCESS_INFORMATION_VERSION;
        SIZE_T gcb = sizeof(gi);
        if (VMMDLL_ProcessGetInformation(TargetProcess.vHandle, TargetProcess.current_process.PID, &gi, &gcb))
            targetSession = gi.win.dwSessionId;
    }

    win_logon_pid = 0;
    for (int pid : TargetProcess.GetPidListFromName("winlogon.exe")) {
        VMMDLL_PROCESS_INFORMATION info{};
        info.magic = VMMDLL_PROCESS_INFORMATION_MAGIC;
        info.wVersion = VMMDLL_PROCESS_INFORMATION_VERSION;
        SIZE_T cb = sizeof(info);
        if (VMMDLL_ProcessGetInformation(TargetProcess.vHandle, pid, &info, &cb)) {
            if (info.win.dwSessionId == targetSession && targetSession > 0) {
                win_logon_pid = pid;
                break;
            }
            if (info.win.dwSessionId > 0 && !win_logon_pid)
                win_logon_pid = pid;
        }
    }
    if (!win_logon_pid) {
        win_logon_pid = TargetProcess.current_process.PID;
        printf("[Input] no interactive winlogon, using game pid %d\n", win_logon_pid);
    }
    printf("[Input] game PC Windows build %llu, winlogon pid %d\n", build, win_logon_pid);
    if (!win_logon_pid) return false;

    if (build <= 22000 && InitFromEat()) return true;
    if (build < 22000) return false;
    if (InitFromSession(build)) return true;
    return InitFromEat();
}

void Keyboard::UpdateKeys()
{
    uint8_t previous_key_state_bitmap[64] = { 0 };
    memcpy(previous_key_state_bitmap, state_bitmap, 64);
    VMMDLL_MemReadEx(TargetProcess.vHandle, win_logon_pid | VMMDLL_PID_PROCESS_WITH_KERNELMEMORY,
        gafAsyncKeyStateExport, (PBYTE)&state_bitmap, 64, NULL, VMMDLL_FLAG_NOCACHE);
    for (int vk = 0; vk < 256; ++vk)
        if ((state_bitmap[(vk * 2 / 8)] & 1 << vk % 4 * 2) && !(previous_key_state_bitmap[(vk * 2 / 8)] & 1 << vk % 4 * 2))
            previous_state_bitmap[vk / 8] |= 1 << vk % 8;
}

static auto s_lastUpdate = std::chrono::system_clock::now();

bool Keyboard::IsKeyDown(uint32_t virtual_key_code)
{
    if (gafAsyncKeyStateExport < 0x7FFFFFFFFFFF) return false;
    if (std::chrono::system_clock::now() - s_lastUpdate > std::chrono::milliseconds(1)) {
        UpdateKeys();
        s_lastUpdate = std::chrono::system_clock::now();
    }
    return state_bitmap[(virtual_key_code * 2 / 8)] & 1 << virtual_key_code % 4 * 2;
}

bool Keyboard::IsReady()
{
    return gafAsyncKeyStateExport > 0x7FFFFFFFFFFF;
}
