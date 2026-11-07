#define NOMINMAX
#include "../Memory/Memory.h"

#include <d3d11.h>
#include <dxgi.h>
#include <cmath>
#include <intrin.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <gdiplus.h>
#include <urlmon.h>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "urlmon.lib")

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

#include "../imgui/imgui.h"
#include "../imgui/imgui_impl_dx11.h"
#include "../imgui/imgui_impl_win32.h"
#include "../Misc/InputDevice.h"
#include "../Memory/InputManager.h"
#include "xorstr.hpp"
#include "Auth.h"
#include "ChampionCombo.h"

// ── Compile-out logging ────────────────────────────────────────────
// VX_LOG_ENABLED=1 keeps printf diagnostics (debug builds).
// VX_LOG_ENABLED=0 (release) preprocesses every VX_LOG() away so the
// format-string literals never appear in the binary's .rdata section.
#ifndef VX_LOG_ENABLED
#  ifdef NDEBUG
#    define VX_LOG_ENABLED 0
#  else
#    define VX_LOG_ENABLED 1
#  endif
#endif

#if VX_LOG_ENABLED
#  define VX_LOG(fmt, ...)  printf(fmt, ##__VA_ARGS__)
#else
#  define VX_LOG(fmt, ...)  ((void)0)
#endif

// ── XorShift32 PRNG (seeded from RDTSC) ───────────────────────────
static uint32_t g_xorState = 0;
static uint32_t xorshift32() {
    if (g_xorState == 0) g_xorState = (uint32_t)__rdtsc();
    g_xorState ^= g_xorState << 13;
    g_xorState ^= g_xorState >> 17;
    g_xorState ^= g_xorState << 5;
    return g_xorState;
}
static float xorFloat(float lo, float hi) {
    return lo + (float)(xorshift32() & 0xFFFF) / 65535.f * (hi - lo);
}

// Offsets - updated 2026-11-04
namespace Offsets {
    constexpr uintptr_t ImageBase = 0x7FF69A1B0000;
    namespace Globals {
        constexpr uintptr_t LocalPlayer      = 0x1CFA7B0;
        constexpr uintptr_t HeroManager      = 0x1CB8550;
        constexpr uintptr_t MinionManager    = 0x1CBBA78;
        constexpr uintptr_t TurretManager    = 0x1CC2268;
        constexpr uintptr_t ObjectManager    = 0x1CB8400;
        constexpr uintptr_t GameTime         = 0x1CC5E38;
        constexpr uintptr_t MissileManager   = 0x1CBBA88;
        constexpr uintptr_t MissileContainer = 0x1CEE208;
        constexpr uintptr_t NavGrid          = 0x1CBB9D0;
        constexpr uintptr_t ViewPort         = 0x1CBBA50;
        constexpr uintptr_t MouseScreenVec2  = 0x1CBBA58;
        constexpr uintptr_t UnderMouseObj    = 0x1CBBC98;
        constexpr uintptr_t HudInstance      = 0x1CB85C0;
        constexpr uintptr_t ChatManager      = 0x1CCDED0;
        constexpr uintptr_t NetClient        = 0x1CB8420;
    }
    namespace GameObject {
        constexpr uintptr_t IsDead              = 0x160; // IsDeadEncrypted in dump
        constexpr uintptr_t Team                = 0x164;
        constexpr uintptr_t Position            = 0x168;
        constexpr uintptr_t Visible             = 0x218;
        constexpr uintptr_t Visible2            = 0x220;
        constexpr uintptr_t Mana                = 0x248;
        constexpr uintptr_t MaxMana             = 0x270;
        constexpr uintptr_t BoundingRadius      = 0x554;   // not in new dump, kept
        constexpr uintptr_t ResourceSize        = 0x860;
        constexpr uintptr_t Health              = 0x938;
        constexpr uintptr_t MaxHealth           = 0x960;
        constexpr uintptr_t Shield              = 0x9D0;   // not in new dump, kept
        constexpr uintptr_t MagicShield         = 0xA20;   // not in new dump, kept
        constexpr uintptr_t Block               = 0xD58;
        constexpr uintptr_t BonusAD             = 0xF00;
        constexpr uintptr_t AttackSpeedScale    = 0xF7C;
        constexpr uintptr_t AttackSpeedMod      = 0xFA0;
        constexpr uintptr_t BaseAD              = 0xFA4;
        constexpr uintptr_t BaseAP              = 0xFB4;
        constexpr uintptr_t Armor               = 0xFCC;
        constexpr uintptr_t MagicResist         = 0xFD4;
        constexpr uintptr_t MoveSpeed           = 0xFE4;
        constexpr uintptr_t AttackRange         = 0xFEC;
        constexpr uintptr_t IsTargetable        = 0x10E0;  // not in new dump, kept
        constexpr uintptr_t CharacterStats      = 0x1388;
        constexpr uintptr_t BuffManager         = 0x25F8;
        constexpr uintptr_t SpellBook           = 0x28B8;
        constexpr uintptr_t CharacterDataStack  = 0x3638;  // not in new dump, kept
        constexpr uintptr_t EncryptedWrapper    = 0x3788;
        constexpr uintptr_t Name                = 0x3890;
        constexpr uintptr_t Level               = 0x4060;
        constexpr uintptr_t MinionType          = 0x4B39;  // not in new dump, kept
        constexpr uintptr_t NetworkID           = 0x78;     // not in new dump, kept
    }
    namespace SpellBook {
        constexpr uintptr_t ActiveSpellCast = 0x38;
        constexpr uintptr_t SpellSlots      = 0xAE0;
    }
    namespace SpellSlot {
        constexpr uintptr_t SpellSlotData   = 0x140;
    }
    namespace SpellData {
        constexpr uintptr_t MissileSpeed    = 0x3F8;
        constexpr uintptr_t LineWidth       = 0x448;
        constexpr uintptr_t BaseCost        = 0x6D8;
        constexpr uintptr_t CooldownTime    = 0x708;
        constexpr uintptr_t CastRange       = 0x778;
        constexpr uintptr_t CastRadius      = 0x7CC;
    }
    namespace SpellCast {
        constexpr uintptr_t ReleaseTime     = 0x1B8;
        constexpr uintptr_t SpellCastData   = 0x1D0;
        constexpr uintptr_t SpellData       = 0x1D8;
        constexpr uintptr_t CasterHandle    = 0x278;
    }
    namespace Buff {
        constexpr uintptr_t EntryExpire     = 0x1C;
        constexpr uintptr_t ScriptName      = 0x60;
        constexpr uintptr_t Lifecycle       = 0x8C;
        constexpr uintptr_t EntryCount      = 0x94;
    }
    namespace Waypoint {
        constexpr uintptr_t WrapperToPC     = 0x10;
        constexpr uintptr_t CurrentWaypoint = 0x308;
        constexpr uintptr_t RouteStart      = 0x318;
        constexpr uintptr_t EndPos          = 0x324;
        constexpr uintptr_t WaypointArray   = 0x330;
        constexpr uintptr_t WaypointCount   = 0x338;
        constexpr uintptr_t ServerPos       = 0x45C;
    }
    namespace ViewPort {
        constexpr uintptr_t ViewMatrix      = 0x1E4;
        constexpr uintptr_t ProjMatrix      = 0x224;
        constexpr uintptr_t CamEntOffset    = 0x308;
        constexpr uintptr_t ViewportRect    = 0x310;
    }
    namespace Manager {
        constexpr uintptr_t List  = 0x8;
        constexpr uintptr_t Count = 0x10;
    }
    namespace FixedSpeed {
        constexpr uintptr_t Accelerating    = 0x18F4DC0;
        constexpr uintptr_t AcceleratingAlt = 0x18F4DF0;
        constexpr uintptr_t FixedTime       = 0x18F4E20;
        constexpr uintptr_t FixedSpeed      = 0x18F4E50;
    }
}

struct Vec3 { float x, y, z; };
struct Vec2 { float x, y; };
struct Matrix4x4 { float m[4][4]; };

static int randInt(int lo, int hi) {
    if (lo >= hi) return lo;
    return lo + (int)(xorshift32() % (uint32_t)(hi - lo + 1));
}
static float randFloat(float lo, float hi) { return xorFloat(lo, hi); }

static ID3D11Device*           g_device    = nullptr;
static ID3D11DeviceContext*    g_context   = nullptr;
static IDXGISwapChain*         g_swapChain = nullptr;
static ID3D11RenderTargetView* g_rtv       = nullptr;

static void CreateRenderTarget()
{
    ID3D11Texture2D* bb = nullptr;
    g_swapChain->GetBuffer(0, IID_PPV_ARGS(&bb));
    if (!bb) return;
    g_device->CreateRenderTargetView(bb, nullptr, &g_rtv);
    bb->Release();
}
static void CleanupRenderTarget()
{
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
}
static bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount        = 2;
    sd.BufferDesc.Format  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow       = hWnd;
    sd.SampleDesc.Count   = 1;
    sd.Windowed           = TRUE;
    sd.SwapEffect         = DXGI_SWAP_EFFECT_DISCARD;
    sd.Flags              = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL level;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        levels, 2, D3D11_SDK_VERSION, &sd, &g_swapChain, &g_device, &level, &g_context);
    if (hr == DXGI_ERROR_UNSUPPORTED)
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            levels, 2, D3D11_SDK_VERSION, &sd, &g_swapChain, &g_device, &level, &g_context);
    if (FAILED(hr)) return false;
    CreateRenderTarget();
    return true;
}
static void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_swapChain) { g_swapChain->Release(); g_swapChain = nullptr; }
    if (g_context)   { g_context->Release();   g_context   = nullptr; }
    if (g_device)    { g_device->Release();     g_device    = nullptr; }
}

static std::vector<RECT> g_overlayMonitors;
static int g_monitorIndex   = 0;
static int g_appliedMonitor = -1;

static BOOL CALLBACK AddMonitor(HMONITOR, HDC, LPRECT rect, LPARAM list)
{
    reinterpret_cast<std::vector<RECT>*>(list)->push_back(*rect);
    return TRUE;
}
static void DetectMonitors()
{
    g_overlayMonitors.clear();
    EnumDisplayMonitors(nullptr, nullptr, AddMonitor, reinterpret_cast<LPARAM>(&g_overlayMonitors));
    if (g_overlayMonitors.empty())
        g_overlayMonitors.push_back({ 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) });
    g_appliedMonitor = -1;
}
static void ApplyOverlayMonitor(HWND hWnd)
{
    g_monitorIndex = std::clamp(g_monitorIndex, 0, (int)g_overlayMonitors.size() - 1);
    if (g_monitorIndex == g_appliedMonitor) return;
    const RECT& r = g_overlayMonitors[g_monitorIndex];
    SetWindowPos(hWnd, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOACTIVATE);
    g_appliedMonitor = g_monitorIndex;
    VX_LOG("[Overlay] monitor %d: %ldx%ld at %ld,%ld\n", g_monitorIndex + 1,
           r.right - r.left, r.bottom - r.top, r.left, r.top);
}

static uintptr_t g_base = 0;
static float     g_gameTime = 0.f;
static int       g_screenW = 1920, g_screenH = 1080;
static Matrix4x4 g_viewProjMatrix{};

struct CachedHero {
    uintptr_t addr;
    Vec3      pos;
    Vec3      lastPos;
    float     health, maxHealth;
    float     shield, magicShield;
    float     attackRange;
    float     boundingRadius;
    float     baseAD, bonusAD;
    float     attackSpeedMod;
    float     armor, magicResist;
    float     mana, maxMana;
    int32_t   level;
    uint8_t   team;
    bool      visible;
    bool      isDead;
    bool      wasVisible;
    bool      recallAlerted;
    float     lastSeenTime;
    char      name[64];
};
static std::vector<CachedHero> g_heroes;

struct RecallAlert {
    char name[64];
    float triggerTime;
};
static std::vector<RecallAlert> g_recallAlerts;
static float g_recallChannelTime = 8.0f;
static uint8_t   g_localTeam = 0;
static Vec3      g_localPos{};
static uintptr_t g_localAddr = 0;
static float     g_localAttackRange = 550.f;
static float     g_localBoundingRadius = 65.f;
static float     g_localBaseAD      = 0.f;
static float     g_localBonusAD     = 0.f;
static float     g_localASMod       = 1.f;
static float     g_localASScale     = 0.625f;
static int32_t   g_localLevel       = 1;
static float     g_localMana        = 0.f;
static float     g_localMaxMana     = 0.f;

struct CachedTurret {
    uintptr_t addr;
    Vec3      pos;
    float     health, maxHealth;
    uint8_t   team;
};
static std::vector<CachedTurret> g_turrets;

struct CachedMinion {
    uintptr_t addr;
    Vec3      pos;
    float     health, maxHealth;
    float     boundingRadius;
    uint8_t   team;
    bool      visible;
    bool      isDead;
    char      name[64];
};
static std::vector<CachedMinion> g_minions;

// ── Summoner spell tracking (per hero) ──
struct SummonerSpell {
    char  name[64];
    float readyAt;
    float totalCD;
    bool  ready;
};

struct TrackedHeroSpells {
    SummonerSpell d, f;
};
static TrackedHeroSpells g_heroSumms[12] = {};

// ── Auto Smite ──
static bool  g_smiteEnabled       = false;
static int   g_smiteSlot          = -1;   // 4 or 5 (D or F), -1 if no smite
static float g_smiteReadyAt       = 0.f;
static bool  g_smiteReady         = false;
static char  g_smiteName[64]      = "";

static float SmiteDamage(int level) {
    // Season 14+ smite: 600 base at level 1, scales up. Upgraded (pets) = 900.
    // Simple table: 600 + (level-1)*30, capped at practical values
    if (level < 1) level = 1;
    if (level > 18) level = 18;
    return 600.f + (level - 1) * 30.f;
}

// ── Awareness / Tracker ──
static bool g_trackerEnabled      = true;
static bool g_trackerSumms        = true;
static bool g_trackerJungle       = true;

struct JungleCamp {
    const char* name;
    const char* display;
    float       respawnTime;
    float       killedAt;
    bool        alive;
    Vec3        pos;
    float       buffRespawnAt; // exact respawn time from buff reading (0 = unknown)
};
static JungleCamp g_jungleCamps[] = {
    {"SRU_Baron",       "Baron",    360.f, 0.f, true, {5007.f, 0.f, 10471.f}, 0.f},
    {"SRU_Dragon",      "Dragon",   300.f, 0.f, true, {9866.f, 0.f, 4414.f}, 0.f},
    {"SRU_RiftHerald",  "Herald",   360.f, 0.f, true, {5007.f, 0.f, 10471.f}, 0.f},
    {"SRU_Blue",        "Blue (T)",  300.f, 0.f, true, {3821.f, 0.f, 8101.f}, 0.f},
    {"SRU_Blue",        "Blue (B)",  300.f, 0.f, true, {11131.f, 0.f, 6990.f}, 0.f},
    {"SRU_Red",         "Red (T)",   300.f, 0.f, true, {7066.f, 0.f, 10975.f}, 0.f},
    {"SRU_Red",         "Red (B)",   300.f, 0.f, true, {7762.f, 0.f, 4011.f}, 0.f},
    {"SRU_Gromp",       "Gromp (T)", 135.f, 0.f, true, {2288.f, 0.f, 8448.f}, 0.f},
    {"SRU_Gromp",       "Gromp (B)", 135.f, 0.f, true, {12703.f, 0.f, 6443.f}, 0.f},
    {"SRU_Murkwolf",    "Wolves (T)",135.f, 0.f, true, {3783.f, 0.f, 6495.f}, 0.f},
    {"SRU_Murkwolf",    "Wolves (B)",135.f, 0.f, true, {11059.f, 0.f, 8419.f}, 0.f},
    {"SRU_Razorbeak",   "Raptors (T)",135.f,0.f, true, {7061.f, 0.f, 5325.f}, 0.f},
    {"SRU_Razorbeak",   "Raptors (B)",135.f,0.f, true, {7820.f, 0.f, 9644.f}, 0.f},
    {"SRU_Krug",        "Krugs (T)", 135.f, 0.f, true, {8394.f, 0.f, 2641.f}, 0.f},
    {"SRU_Krug",        "Krugs (B)", 135.f, 0.f, true, {6499.f, 0.f, 12287.f}, 0.f},
};
constexpr int NUM_CAMPS = sizeof(g_jungleCamps) / sizeof(g_jungleCamps[0]);

static bool g_menuOpen      = true;
static int  g_activeTab     = 0;
static bool g_fpsShow       = true;
static bool g_keyDisplayOpen = true;
static float g_bgColor[3] = { 0.f, 0.f, 0.f }; 
static bool  g_highContrast = true;  

static bool g_espEnabled       = true;
static bool g_espNames         = true;
static bool g_espHealth        = true;
static bool g_espBox           = true;
static bool g_espShields       = true;
static bool g_espAttackRange   = true;
static bool g_espVisStatus     = true;
static bool g_espLastPosition  = true;
static bool g_espEnemyOnly     = false;
static bool g_espMinions       = true;
static bool g_espTurretRange   = true;
static float g_turretRange     = 850.f;

static bool  g_minimapEnabled   = true;
static float g_minimapSize      = 0.f;  // 0 = auto-compute from resolution
static float g_minimapOpacity   = 0.85f;
static float g_minimapOffX      = -1.f; // -1 = auto-compute from resolution
static float g_minimapOffY      = -1.f;
static bool  g_minimapAutoPos   = true; // true until user manually adjusts

static void MinimapAutoDefaults() {
    if (g_minimapSize <= 0.f)  g_minimapSize = g_screenH * 0.235f;
    if (g_minimapOffX < 0.f)   g_minimapOffX = g_screenW * 0.868f;
    if (g_minimapOffY < 0.f)   g_minimapOffY = g_screenH * 0.757f;
}
static void MinimapResetToGame() {
    g_minimapSize = g_screenH * 0.235f;
    g_minimapOffX = g_screenW * 0.868f;
    g_minimapOffY = g_screenH * 0.757f;
}

static int  g_drawMode         = 0;  // 0=outlined, 1=filled
static bool g_drawGlow         = true;
static float g_espBoxColor[4]  = { 1.f, 0.3f, 0.3f, 1.f };
static float g_allyColor[4]    = { 0.1f, 0.5f, 1.f, 1.f };
static float g_enemyColor[4]   = { 1.f, 0.2f, 0.2f, 1.f };

static bool g_sidebarEnabled   = true;
static int  g_sidebarSide      = 1;  // 0=left, 1=right

static char g_makcuPort[32]    = "";
static int  g_makcuBaud        = 115200;
static bool g_makcuAutoConnect = false;

static bool      g_orbEnabled      = true;
static int       g_orbMode         = 0;
static float     g_orbAttackSpeed  = 0.625f; // manual fallback attacks/sec
static int       g_orbWindupPct    = 35;
static float     g_orbPlayerAD     = 70.f;   // manual fallback AD
static float     g_orbMoveDelay    = 50.f;
static bool      g_orbAutoStats    = true;   // read AD/AS from memory
static float     g_orbBaseAS       = 0.625f; // champion base AS (manual, multiplied by ASMod)
static float     g_orbPingMs       = 0.f;    // estimated round-trip ping in ms (read via DMA)
static float     g_orbTimingBias   = -15.f;  // ms bias on attack window (negative = early)
static bool      g_orbPingComp     = true;   // enable ping-compensated attack timing

static int       g_orbCursorPolicy = 1;      // 0=Free (no cursor moves), 1=Aim+Restore via smooth path
static int       g_orbPathMinMs    = 35;     // cursor path duration min
static int       g_orbPathMaxMs    = 90;     // cursor path duration max
static int       g_orbReactMinMs   = 25;     // reaction floor after target/killable event
static int       g_orbReactJitterMs = 55;    // added reaction jitter
static bool      g_orbClickSelectInMode = true;
static bool      g_orbLastHitOnly  = true;  // only AA predicted killables where applicable
static int       g_orbLastHitBufferPct = 95; // predicted HP threshold = effectiveAD * pct/100
static int       g_orbSpellGateMs  = 130;   // generic spell gate
static bool      g_orbHumanize     = true;  // bounded timing/cadence variance
static std::unordered_map<std::string, int> g_orbWindupPctChamp;

struct KeyOption { const char* label; uint8_t hid; };
static const KeyOption g_keyOptions[] = {
    {"Q",0x14},{"W",0x1A},{"E",0x08},{"R",0x15},{"T",0x17},
    {"Y",0x1C},{"U",0x18},{"I",0x0C},{"O",0x12},{"P",0x13},
    {"A",0x04},{"S",0x16},{"D",0x07},{"F",0x09},{"G",0x0A},
    {"H",0x0B},{"J",0x0D},{"K",0x0E},{"L",0x0F},
    {"Z",0x1D},{"X",0x1B},{"C",0x06},{"V",0x19},{"B",0x05},
    {"N",0x11},{"M",0x10},
    {"1",0x1E},{"2",0x1F},{"3",0x20},{"4",0x21},{"5",0x22},
    {"6",0x23},{"7",0x24},{"8",0x25},{"9",0x26},{"0",0x27},
    {"F1",0x3A},{"F2",0x3B},{"F3",0x3C},{"F4",0x3D},
    {"F5",0x3E},{"F6",0x3F},{"F7",0x40},{"F8",0x41},
    {"F9",0x42},{"F10",0x43},{"F11",0x44},{"F12",0x45},
    {"Space",0x2C},{"Tab",0x2B},{"`",0x35},
    {"LMB",0xF0},{"RMB",0xF1},
};
static const int g_keyOptionCount = sizeof(g_keyOptions) / sizeof(g_keyOptions[0]);
static int FindKeyIdx(uint8_t hid) {
    for (int i = 0; i < g_keyOptionCount; i++)
        if (g_keyOptions[i].hid == hid) return i;
    return 0;
}
static void PressKey(uint8_t hid) {
    if (hid == 0xF0) { InputDevice::LeftDown(); InputDevice::LeftUp(); }
    else if (hid == 0xF1) { InputDevice::RightDown(); InputDevice::RightUp(); }
    else { InputDevice::KeyDown(hid); InputDevice::KeyUp(hid); }
}

static int g_orbAtkKeyIdx    = 10; // default A
static int g_orbSpellKeyIdx[4] = {0, 1, 2, 3}; // default Q, W, E, R

static ULONGLONG g_orbLastAttack   = 0;
static ULONGLONG g_orbLastMove     = 0;
static ULONGLONG g_orbLastActionEvent = 0; // target/killable change for reaction floor
static int       g_orbActionReactMs = 0;
static uintptr_t g_orbLastActionTarget = 0;
static int       g_orbTargetIdx    = -1;
static bool      g_orbTargetIsMinion = false;
static uintptr_t g_orbStickyAddr   = 0;
static ULONGLONG g_orbStickyUntil  = 0;
static float     g_cursorX = 960.f, g_cursorY = 540.f; 

static bool      g_orbDrawRange    = true;
static bool      g_orbDrawTarget   = true;
static bool      g_orbDrawLastHit  = true;

static bool      g_orbUseQ = true;
static bool      g_orbUseW = true;
static bool      g_orbUseE = true;
static bool      g_orbUseR = false;

static ULONGLONG g_orbLastSpell[4] = {};
static float     g_orbEffectiveAD  = 70.f;   // computed each tick for drawings
static bool      g_orbReadCooldowns = true;  // read spell CDs from memory

static int       g_orbTargetMode   = 1;
static int       g_orbPriorityMode = 0; // 0=closest to champion, 1=lowest HP, 2=most AD, 3=most AP

static int       g_orbSelectedHero = -1;
static uintptr_t g_orbSelectedAddr = 0;
static std::unordered_map<std::string, int> g_orbPriority;

static float     g_orbClickOffY    = 0.f;
static float     g_orbClickOffX    = 0.f;
static float     g_orbClickOffZ    = 0.f;
static uintptr_t g_orbClickTarget  = 0;
static int       g_orbClickSpread  = 70;

struct MinionHpSample { uintptr_t addr = 0; float hp = 0.f; float prevHp = 0.f; ULONGLONG t = 0; ULONGLONG prevT = 0; };
static MinionHpSample g_orbMinionHp[96];

static void OrbTrackMinionHp(uintptr_t addr, float hp, ULONGLONG now)
{
    if (!addr) return;
    int slot = -1, freeSlot = -1, oldest = 0; ULONGLONG oldestT = ~0ULL;
    for (int i = 0; i < 96; i++) {
        auto& s = g_orbMinionHp[i];
        if (s.addr == addr) { slot = i; break; }
        if (!s.addr && freeSlot < 0) freeSlot = i;
        if (s.t < oldestT) { oldestT = s.t; oldest = i; }
    }
    if (slot < 0) slot = (freeSlot >= 0) ? freeSlot : oldest;
    auto& s = g_orbMinionHp[slot];
    if (s.addr != addr) { s = {}; s.addr = addr; s.hp = s.prevHp = hp; s.t = s.prevT = now; return; }
    if (now - s.t >= 50 && fabsf(hp - s.hp) > 0.01f) { s.prevHp = s.hp; s.prevT = s.t; s.hp = hp; s.t = now; }
}

static float OrbPredictMinionHp(uintptr_t addr, float hp, float hitMs, ULONGLONG now)
{
    for (int i = 0; i < 96; i++) {
        auto& s = g_orbMinionHp[i];
        if (s.addr != addr || s.prevT == 0 || s.t <= s.prevT) continue;
        if (now - s.t > 800) break;
        float dt = (float)(s.t - s.prevT);
        float dps = (s.prevHp - s.hp) / std::max(dt, 1.f) * 1000.f; // hp/sec style via ms math below
        if (dps <= 0.f) break;
        float predict = hp - dps * (hitMs / 1000.f);
        return std::max(0.f, predict);
    }
    return hp;
}

static bool OrbCanReact(ULONGLONG now)
{
    if (g_orbReactMinMs <= 0 && g_orbReactJitterMs <= 0) return true;
    return (now - g_orbLastActionEvent) >= (ULONGLONG)std::max(0, g_orbActionReactMs);
}

static void OrbNoteActionEvent(uintptr_t targetAddr, ULONGLONG now)
{
    if (targetAddr != g_orbLastActionTarget) {
        g_orbLastActionTarget = targetAddr;
        g_orbLastActionEvent = now;
        g_orbActionReactMs = std::max(0, g_orbReactMinMs) + randInt(0, std::max(0, g_orbReactJitterMs));
    }
}

static void MoveCursorPath(float targetX, float targetY, int maxTotalMs)
{
    (void)maxTotalMs; // non-blocking: never Sleep in main tick
    float dx = targetX - g_cursorX;
    float dy = targetY - g_cursorY;
    float dist = sqrtf(dx*dx + dy*dy);
    if (dist < 1.5f) return;
    int steps = (dist < 18.f) ? 1 : ((dist < 80.f) ? 2 : 3);
    float nx = -dy / std::max(dist, 1.f), ny = dx / std::max(dist, 1.f);
    float arc = (g_orbHumanize && steps > 1) ? randFloat(-0.03f, 0.03f) * std::min(dist, 120.f) : 0.f;
    for (int i = 1; i <= steps; i++) {
        float t = (float)i / (float)steps;
        float ease = t * t * (3.f - 2.f * t);
        float wobble = (steps > 1 && i < steps) ? sinf(t * 3.14159265f) * arc : 0.f;
        float px = (i == steps) ? targetX : (g_cursorX + dx * ease + nx * wobble);
        float py = (i == steps) ? targetY : (g_cursorY + dy * ease + ny * wobble);
        int mx = (int)(px - g_cursorX);
        int my = (int)(py - g_cursorY);
        if (mx || my) InputDevice::MoveTo(mx, my);
        g_cursorX += (float)mx;
        g_cursorY += (float)my;
    }
}

static void OrbAimAndRestore(const Vec2& aim, bool enabled, Vec2& restore, bool& moved)
{
    moved = false;
    if (!enabled) return;
    float dx = aim.x - g_cursorX, dy = aim.y - g_cursorY;
    if ((dx*dx + dy*dy) < 9.f) return;
    restore = { g_cursorX, g_cursorY };
    MoveCursorPath(aim.x, aim.y, 0);
    moved = true;
}

static void OrbRestoreCursor(const Vec2& restore, bool moved)
{
    if (!moved) return;
    float dx = restore.x - g_cursorX, dy = restore.y - g_cursorY;
    if ((dx*dx + dy*dy) < 9.f) return;
    // quick restore: single move to avoid visible hitch
    int mx = (int)(restore.x - g_cursorX);
    int my = (int)(restore.y - g_cursorY);
    if (mx || my) InputDevice::MoveTo(mx, my);
    g_cursorX += (float)mx;
    g_cursorY += (float)my;
}

static void IssueAttackKey(uint8_t atkHid)
{
    if (atkHid == 0xF0) { InputDevice::LeftDown(); InputDevice::LeftUp(); }
    else if (atkHid == 0xF1) { InputDevice::RightDown(); InputDevice::RightUp(); }
    else { InputDevice::KeyDown(atkHid); InputDevice::KeyUp(atkHid); InputDevice::LeftDown(); InputDevice::LeftUp(); }
}

static void RollClickOffset(uintptr_t targetAddr, float boundingRadius)
{
    if (targetAddr == g_orbClickTarget && g_orbClickOffY != 0.f) return;
    g_orbClickTarget = targetAddr;
    float r = (boundingRadius > 10.f && boundingRadius < 200.f) ? boundingRadius : 65.f;
    float spread = g_orbClickSpread / 100.f;
    g_orbClickOffY = randFloat(30.f, 140.f) * spread;
    float hSpread = r * 0.4f * spread;
    g_orbClickOffX = randFloat(-hSpread, hSpread);
    g_orbClickOffZ = randFloat(-hSpread, hSpread);
}

static Vec3 ApplyClickOffset(const Vec3& pos)
{
    return { pos.x + g_orbClickOffX, pos.y + g_orbClickOffY, pos.z + g_orbClickOffZ };
}

struct SpellCache {
    int   level;
    float readyAt;
    float totalCD;
    float range;
    bool  ready;
    char  name[64];
};
static SpellCache g_spells[4] = {};

static char  g_localChampName[64] = "";
static float g_localHealth    = 0.f;
static float g_localMaxHealth = 0.f;

static float g_activeCastEnd    = 0.f;
static float g_activeAttackEnd  = 0.f;
static bool  g_activeIsAuto     = false;

static ChampionCombo::Settings g_comboSettings;
static ULONGLONG g_comboRetryCooldown[4] = {};
static bool g_comboEnabled = true;

static char g_configDir[MAX_PATH];
static char g_logDir[MAX_PATH];
static char g_configName[64]   = "default";
static char g_configStatus[128] = "";
static char g_licenseKey[128]  = "";

static char g_iconCacheDir[MAX_PATH];

static void InitPaths() {
    strncpy(g_configDir,    xorstr_("C:\\VeoxDMA\\Configs"), MAX_PATH - 1);
    strncpy(g_logDir,       xorstr_("C:\\VeoxDMA\\Logs"),    MAX_PATH - 1);
    strncpy(g_iconCacheDir, xorstr_("C:\\VeoxDMA\\Cache"),   MAX_PATH - 1);
}
static std::unordered_map<std::string, ID3D11ShaderResourceView*> g_iconCache;
static std::unordered_map<std::string, bool> g_iconRequested;
static char g_ddVersion[32] = "15.1.1";
static ULONG_PTR g_gdipToken = 0;

static void EnsureDirs() {
    std::error_code ec;
    std::filesystem::create_directories(xorstr_("C:\\VeoxDMA"), ec);
    std::filesystem::create_directories(g_configDir, ec);
    std::filesystem::create_directories(g_logDir, ec);
    std::filesystem::create_directories(g_iconCacheDir, ec);
    std::filesystem::create_directories(xorstr_("C:\\VeoxDMA\\Scripts"), ec);
}

static void FetchDDVersion() {
    wchar_t tmpPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tmpPath);
    wcscat_s(tmpPath, L"dd_versions.json");
    if (SUCCEEDED(URLDownloadToFileW(nullptr, L"https://ddragon.leagueoflegends.com/api/versions.json", tmpPath, 0, nullptr))) {
        FILE* f = _wfopen(tmpPath, L"r");
        if (f) {
            char buf[4096];
            size_t n = fread(buf, 1, sizeof(buf) - 1, f);
            buf[n] = 0;
            fclose(f);
            char* start = strchr(buf, '"');
            if (start) {
                start++;
                char* end = strchr(start, '"');
                if (end) { *end = 0; strncpy(g_ddVersion, start, sizeof(g_ddVersion) - 1); }
            }
            DeleteFileW(tmpPath);
        }
    }
    VX_LOG("[Icons] Data Dragon version: %s\n", g_ddVersion);
}

static void DownloadChampIconThread(std::string champName) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    char cachePath[MAX_PATH];
    snprintf(cachePath, sizeof(cachePath), "%s\\%s.png", g_iconCacheDir, champName.c_str());
    if (!std::filesystem::exists(cachePath)) {
        char url[512];
        snprintf(url, sizeof(url), xorstr_("https://ddragon.leagueoflegends.com/cdn/%s/img/champion/%s.png"), g_ddVersion, champName.c_str());
        wchar_t wUrl[512], wPath[MAX_PATH];
        MultiByteToWideChar(CP_UTF8, 0, url, -1, wUrl, 512);
        MultiByteToWideChar(CP_UTF8, 0, cachePath, -1, wPath, MAX_PATH);
        HRESULT hr = URLDownloadToFileW(nullptr, wUrl, wPath, 0, nullptr);
        VX_LOG("[Icons] Download %s: %s\n", champName.c_str(), SUCCEEDED(hr) ? "OK" : "FAILED");
    }
    CoUninitialize();
}

static ID3D11ShaderResourceView* LoadTextureFromPNG(const wchar_t* path) {
    if (!g_device) return nullptr;
    Gdiplus::Bitmap bmp(path);
    if (bmp.GetLastStatus() != Gdiplus::Ok) return nullptr;
    UINT w = bmp.GetWidth(), h = bmp.GetHeight();
    if (w == 0 || h == 0) return nullptr;

    Gdiplus::BitmapData data;
    Gdiplus::Rect rect(0, 0, (INT)w, (INT)h);
    if (bmp.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok)
        return nullptr;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData{};
    initData.pSysMem = data.Scan0;
    initData.SysMemPitch = data.Stride;

    ID3D11Texture2D* tex = nullptr;
    g_device->CreateTexture2D(&desc, &initData, &tex);
    bmp.UnlockBits(&data);
    if (!tex) return nullptr;

    ID3D11ShaderResourceView* srv = nullptr;
    g_device->CreateShaderResourceView(tex, nullptr, &srv);
    tex->Release();
    return srv;
}

static ID3D11ShaderResourceView* GetChampIcon(const char* name) {
    if (!name[0]) return nullptr;
    auto it = g_iconCache.find(name);
    if (it != g_iconCache.end()) return it->second;

    char cachePath[MAX_PATH];
    snprintf(cachePath, sizeof(cachePath), "%s\\%s.png", g_iconCacheDir, name);
    if (std::filesystem::exists(cachePath)) {
        wchar_t wPath[MAX_PATH];
        MultiByteToWideChar(CP_UTF8, 0, cachePath, -1, wPath, MAX_PATH);
        auto* srv = LoadTextureFromPNG(wPath);
        g_iconCache[name] = srv;
        return srv;
    }

    if (g_iconRequested.find(name) == g_iconRequested.end()) {
        g_iconRequested[name] = true;
        std::thread(DownloadChampIconThread, std::string(name)).detach();
    }
    return nullptr;
}

static void SaveConfig(const char* name) {
    EnsureDirs();
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\%s.cfg", g_configDir, name);
    FILE* f = fopen(path, "w");
    if (!f) { snprintf(g_configStatus, sizeof(g_configStatus), "Failed to save %s", name); return; }

    fprintf(f, xorstr_("# VeoxDMA Config: %s\n"), name);
    fprintf(f, xorstr_("esp_enabled=%d\n"), g_espEnabled);
    fprintf(f, xorstr_("esp_names=%d\n"), g_espNames);
    fprintf(f, xorstr_("esp_health=%d\n"), g_espHealth);
    fprintf(f, xorstr_("esp_box=%d\n"), g_espBox);
    fprintf(f, xorstr_("esp_shields=%d\n"), g_espShields);
    fprintf(f, xorstr_("esp_attack_range=%d\n"), g_espAttackRange);
    fprintf(f, xorstr_("esp_vis_status=%d\n"), g_espVisStatus);
    fprintf(f, xorstr_("esp_last_pos=%d\n"), g_espLastPosition);
    fprintf(f, xorstr_("esp_enemy_only=%d\n"), g_espEnemyOnly);
    fprintf(f, xorstr_("esp_minions=%d\n"), g_espMinions);
    fprintf(f, xorstr_("esp_turret_range=%d\n"), g_espTurretRange);
    fprintf(f, xorstr_("turret_range=%.0f\n"), g_turretRange);
    fprintf(f, xorstr_("draw_mode=%d\n"), g_drawMode);
    fprintf(f, xorstr_("draw_glow=%d\n"), g_drawGlow);
    fprintf(f, xorstr_("enemy_color=%.3f,%.3f,%.3f,%.3f\n"), g_enemyColor[0], g_enemyColor[1], g_enemyColor[2], g_enemyColor[3]);
    fprintf(f, xorstr_("ally_color=%.3f,%.3f,%.3f,%.3f\n"), g_allyColor[0], g_allyColor[1], g_allyColor[2], g_allyColor[3]);
    fprintf(f, xorstr_("bg_color=%.3f,%.3f,%.3f\n"), g_bgColor[0], g_bgColor[1], g_bgColor[2]);
        fprintf(f, xorstr_("sidebar=%d\n"), g_sidebarEnabled);
    fprintf(f, xorstr_("sidebar_side=%d\n"), g_sidebarSide);
    fprintf(f, xorstr_("minimap=%d\n"), g_minimapEnabled);
    fprintf(f, xorstr_("minimap_size=%.0f\n"), g_minimapSize);
    fprintf(f, xorstr_("minimap_opacity=%.2f\n"), g_minimapOpacity);
    fprintf(f, xorstr_("minimap_x=%.0f\n"), g_minimapOffX);
    fprintf(f, xorstr_("minimap_y=%.0f\n"), g_minimapOffY);
    fprintf(f, xorstr_("monitor=%d\n"), g_monitorIndex);
    fprintf(f, xorstr_("show_fps=%d\n"), g_fpsShow);
    fprintf(f, xorstr_("high_contrast=%d\n"), g_highContrast);
    fprintf(f, xorstr_("orb_enabled=%d\n"), g_orbEnabled);
    fprintf(f, xorstr_("orb_auto_stats=%d\n"), g_orbAutoStats);
    fprintf(f, xorstr_("orb_base_as=%.3f\n"), g_orbBaseAS);
    fprintf(f, xorstr_("orb_attack_speed=%.3f\n"), g_orbAttackSpeed);
    fprintf(f, xorstr_("orb_player_ad=%.0f\n"), g_orbPlayerAD);
    fprintf(f, xorstr_("orb_windup=%d\n"), g_orbWindupPct);
    fprintf(f, xorstr_("orb_move_delay=%.0f\n"), g_orbMoveDelay);
    fprintf(f, xorstr_("orb_path_min=%d\n"), g_orbPathMinMs);
    fprintf(f, xorstr_("orb_path_max=%d\n"), g_orbPathMaxMs);
    fprintf(f, xorstr_("orb_cursor_policy=%d\n"), g_orbCursorPolicy);
    fprintf(f, xorstr_("orb_react_min=%d\n"), g_orbReactMinMs);
    fprintf(f, xorstr_("orb_react_jitter=%d\n"), g_orbReactJitterMs);
    fprintf(f, xorstr_("orb_humanize=%d\n"), g_orbHumanize ? 1 : 0);
    fprintf(f, xorstr_("orb_click_select_mode=%d\n"), g_orbClickSelectInMode ? 1 : 0);
    fprintf(f, xorstr_("orb_lasthit_only=%d\n"), g_orbLastHitOnly ? 1 : 0);
    fprintf(f, xorstr_("orb_lasthit_buffer=%d\n"), g_orbLastHitBufferPct);
    fprintf(f, xorstr_("orb_spell_gate=%d\n"), g_orbSpellGateMs);
    for (auto& [name, pct] : g_orbWindupPctChamp)
        fprintf(f, xorstr_("orb_windup_champ_%s=%d\n"), name.c_str(), pct);
    fprintf(f, xorstr_("orb_click_spread=%d\n"), g_orbClickSpread);
    fprintf(f, xorstr_("orb_ping_comp=%d\n"), g_orbPingComp);
    fprintf(f, xorstr_("orb_timing_bias=%.0f\n"), g_orbTimingBias);
    fprintf(f, xorstr_("orb_atk_key_hid=%d\n"), g_keyOptions[g_orbAtkKeyIdx].hid);
    fprintf(f, xorstr_("orb_spell_q_hid=%d\n"), g_keyOptions[g_orbSpellKeyIdx[0]].hid);
    fprintf(f, xorstr_("orb_spell_w_hid=%d\n"), g_keyOptions[g_orbSpellKeyIdx[1]].hid);
    fprintf(f, xorstr_("orb_spell_e_hid=%d\n"), g_keyOptions[g_orbSpellKeyIdx[2]].hid);
    fprintf(f, xorstr_("orb_spell_r_hid=%d\n"), g_keyOptions[g_orbSpellKeyIdx[3]].hid);
    fprintf(f, xorstr_("orb_read_cooldowns=%d\n"), g_orbReadCooldowns);
    fprintf(f, xorstr_("orb_use_q=%d\n"), g_orbUseQ);
    fprintf(f, xorstr_("orb_use_w=%d\n"), g_orbUseW);
    fprintf(f, xorstr_("orb_use_e=%d\n"), g_orbUseE);
    fprintf(f, xorstr_("orb_use_r=%d\n"), g_orbUseR);
    fprintf(f, xorstr_("orb_draw_range=%d\n"), g_orbDrawRange);
    fprintf(f, xorstr_("orb_draw_target=%d\n"), g_orbDrawTarget);
    fprintf(f, xorstr_("orb_draw_lasthit=%d\n"), g_orbDrawLastHit);
    fprintf(f, xorstr_("orb_target_mode=%d\n"), g_orbTargetMode);
    fprintf(f, xorstr_("orb_priority_mode=%d\n"), g_orbPriorityMode);
    for (auto& [name, prio] : g_orbPriority)
        fprintf(f, xorstr_("orb_prio_%s=%d\n"), name.c_str(), prio);
        fprintf(f, xorstr_("makcu_port=%s\n"), g_makcuPort);
    fprintf(f, xorstr_("makcu_baud=%d\n"), g_makcuBaud);

    fclose(f);
    snprintf(g_configStatus, sizeof(g_configStatus), "Saved: %s", name);
    VX_LOG("[Config] Saved %s\n", path);
}

static void LoadConfig(const char* name) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\%s.cfg", g_configDir, name);
    FILE* f = fopen(path, "r");
    if (!f) { snprintf(g_configStatus, sizeof(g_configStatus), "No config: %s", name); return; }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char* val = eq + 1;
        char* nl = strchr(val, '\n'); if (nl) *nl = 0;
        char* cr = strchr(val, '\r'); if (cr) *cr = 0;

            if (!strcmp(line, xorstr_("esp_enabled")))       g_espEnabled = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("esp_names")))    g_espNames = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("esp_health")))   g_espHealth = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("esp_box")))      g_espBox = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("esp_shields")))  g_espShields = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("esp_attack_range"))) g_espAttackRange = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("esp_vis_status")))   g_espVisStatus = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("esp_last_pos")))     g_espLastPosition = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("esp_enemy_only")))   g_espEnemyOnly = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("esp_minions")))      g_espMinions = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("esp_turret_range"))) g_espTurretRange = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("turret_range")))     g_turretRange = (float)atof(val);
        else if (!strcmp(line, xorstr_("draw_quality")))     {} // legacy, ignored
        else if (!strcmp(line, xorstr_("draw_mode")))        g_drawMode = std::clamp(atoi(val), 0, 1);
        else if (!strcmp(line, xorstr_("draw_glow")))        g_drawGlow = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("enemy_color")))      sscanf(val, "%f,%f,%f,%f", &g_enemyColor[0], &g_enemyColor[1], &g_enemyColor[2], &g_enemyColor[3]);
        else if (!strcmp(line, xorstr_("ally_color")))       sscanf(val, "%f,%f,%f,%f", &g_allyColor[0], &g_allyColor[1], &g_allyColor[2], &g_allyColor[3]);
        else if (!strcmp(line, xorstr_("bg_color")))         sscanf(val, "%f,%f,%f", &g_bgColor[0], &g_bgColor[1], &g_bgColor[2]);
                else if (!strcmp(line, xorstr_("sidebar")))          g_sidebarEnabled = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("sidebar_side")))     g_sidebarSide = atoi(val);
        else if (!strcmp(line, xorstr_("minimap")))          g_minimapEnabled = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("minimap_size")))     g_minimapSize = std::clamp((float)atof(val), 60.f, 600.f);
        else if (!strcmp(line, xorstr_("minimap_opacity")))  g_minimapOpacity = std::clamp((float)atof(val), 0.3f, 1.f);
        else if (!strcmp(line, xorstr_("minimap_x")))        g_minimapOffX = std::max(0.f, (float)atof(val));
        else if (!strcmp(line, xorstr_("minimap_y")))        g_minimapOffY = std::max(0.f, (float)atof(val));
            else if (!strcmp(line, xorstr_("monitor")))          g_monitorIndex = atoi(val);
        else if (!strcmp(line, xorstr_("show_fps")))         g_fpsShow = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("high_contrast")))    g_highContrast = atoi(val) != 0;

        else if (!strcmp(line, xorstr_("orb_enabled")))      g_orbEnabled = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("orb_auto_stats")))   g_orbAutoStats = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("orb_base_as")))      g_orbBaseAS = (float)atof(val);
        else if (!strcmp(line, xorstr_("orb_attack_speed"))) g_orbAttackSpeed = (float)atof(val);
        else if (!strcmp(line, xorstr_("orb_player_ad")))    g_orbPlayerAD = (float)atof(val);
        else if (!strcmp(line, xorstr_("orb_windup")))       g_orbWindupPct = atoi(val);
        else if (!strcmp(line, xorstr_("orb_move_delay")))   g_orbMoveDelay = (float)atof(val);
        else if (!strcmp(line, xorstr_("orb_settle_min")))   g_orbPathMinMs = std::clamp(atoi(val), 0, 500); // legacy -> path min
        else if (!strcmp(line, xorstr_("orb_settle_max")))   g_orbPathMaxMs = std::clamp(atoi(val), 0, 500); // legacy -> path max
        else if (!strcmp(line, xorstr_("orb_path_min")))     g_orbPathMinMs = std::clamp(atoi(val), 0, 500);
        else if (!strcmp(line, xorstr_("orb_path_max")))     g_orbPathMaxMs = std::clamp(atoi(val), 0, 500);
        else if (!strcmp(line, xorstr_("orb_cursor_policy"))) g_orbCursorPolicy = std::clamp(atoi(val), 0, 1);
        else if (!strcmp(line, xorstr_("orb_react_min")))    g_orbReactMinMs = std::clamp(atoi(val), 0, 500);
        else if (!strcmp(line, xorstr_("orb_react_jitter"))) g_orbReactJitterMs = std::clamp(atoi(val), 0, 500);
        else if (!strcmp(line, xorstr_("orb_humanize")))     g_orbHumanize = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("orb_click_select_mode"))) g_orbClickSelectInMode = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("orb_lasthit_only"))) g_orbLastHitOnly = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("orb_lasthit_buffer"))) g_orbLastHitBufferPct = std::clamp(atoi(val), 50, 100);
        else if (!strcmp(line, xorstr_("orb_spell_gate")))   g_orbSpellGateMs = std::clamp(atoi(val), 0, 2000);
        else if (!strncmp(line, xorstr_("orb_windup_champ_"), 17)) {
            g_orbWindupPctChamp[line + 17] = std::clamp(atoi(val), 1, 100);
        }
        else if (!strcmp(line, xorstr_("orb_click_spread"))) g_orbClickSpread = std::clamp(atoi(val), 0, 100);
        else if (!strcmp(line, xorstr_("orb_ping_comp")))    g_orbPingComp = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("orb_timing_bias")))  g_orbTimingBias = std::clamp((float)atof(val), -100.f, 100.f);
        else if (!strcmp(line, xorstr_("orb_atk_key_hid")))   g_orbAtkKeyIdx = FindKeyIdx((uint8_t)atoi(val));
        else if (!strcmp(line, xorstr_("orb_spell_q_hid")))  g_orbSpellKeyIdx[0] = FindKeyIdx((uint8_t)atoi(val));
        else if (!strcmp(line, xorstr_("orb_spell_w_hid")))  g_orbSpellKeyIdx[1] = FindKeyIdx((uint8_t)atoi(val));
        else if (!strcmp(line, xorstr_("orb_spell_e_hid")))  g_orbSpellKeyIdx[2] = FindKeyIdx((uint8_t)atoi(val));
        else if (!strcmp(line, xorstr_("orb_spell_r_hid")))  g_orbSpellKeyIdx[3] = FindKeyIdx((uint8_t)atoi(val));
        else if (!strcmp(line, xorstr_("orb_atk_move_key"))) {} // legacy, ignored
        else if (!strcmp(line, xorstr_("orb_read_cooldowns"))) g_orbReadCooldowns = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("orb_use_q")))        g_orbUseQ = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("orb_use_w")))        g_orbUseW = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("orb_use_e")))        g_orbUseE = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("orb_use_r")))        g_orbUseR = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("orb_spell_cd")))     g_orbSpellGateMs = std::clamp((int)atof(val), 0, 2000); // legacy
        else if (!strcmp(line, xorstr_("orb_draw_range")))   g_orbDrawRange = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("orb_draw_target")))  g_orbDrawTarget = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("orb_draw_lasthit"))) g_orbDrawLastHit = atoi(val) != 0;
        else if (!strcmp(line, xorstr_("orb_target_mode")))  g_orbTargetMode = std::clamp(atoi(val), 0, 1);
        else if (!strcmp(line, xorstr_("orb_priority_mode"))) g_orbPriorityMode = std::clamp(atoi(val), 0, 3);
        else if (strncmp(line, xorstr_("orb_prio_"), 9) == 0) {
            g_orbPriority[line + 9] = std::clamp(atoi(val), 1, 5);
        }
                else if (!strcmp(line, xorstr_("makcu_port")))       strncpy(g_makcuPort, val, sizeof(g_makcuPort) - 1);
        else if (!strcmp(line, xorstr_("makcu_baud")))       g_makcuBaud = atoi(val);
    }
    fclose(f);
    snprintf(g_configStatus, sizeof(g_configStatus), "Loaded: %s", name);
    VX_LOG("[Config] Loaded %s\n", path);
}

static std::vector<std::string> ListConfigs() {
    std::vector<std::string> names;
    EnsureDirs();
    for (auto& entry : std::filesystem::directory_iterator(g_configDir)) {
        if (entry.path().extension() == ".cfg")
            names.push_back(entry.path().stem().string());
    }
    return names;
}

static LONG WINAPI CrashHandler(EXCEPTION_POINTERS* ep) {
    EnsureDirs();
    SYSTEMTIME st; GetLocalTime(&st);
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\crash_%04d%02d%02d_%02d%02d%02d.log",
        g_logDir, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    FILE* f = fopen(path, "w");
    if (f) {
        fprintf(f, xorstr_("Crash Report\n"));
        fprintf(f, xorstr_("=============================\n"));
        fprintf(f, xorstr_("Time: %04d-%02d-%02d %02d:%02d:%02d\n\n"), st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        fprintf(f, xorstr_("Exception Code:    0x%08lX\n"), ep->ExceptionRecord->ExceptionCode);
        fprintf(f, xorstr_("Exception Address: 0x%p\n"), ep->ExceptionRecord->ExceptionAddress);
        fprintf(f, xorstr_("Exception Flags:   0x%08lX\n\n"), ep->ExceptionRecord->ExceptionFlags);

        auto* ctx = ep->ContextRecord;
        fprintf(f, xorstr_("Registers:\n"));
        fprintf(f, xorstr_("  RAX = 0x%016llX  RBX = 0x%016llX\n"), ctx->Rax, ctx->Rbx);
        fprintf(f, xorstr_("  RCX = 0x%016llX  RDX = 0x%016llX\n"), ctx->Rcx, ctx->Rdx);
        fprintf(f, xorstr_("  RSI = 0x%016llX  RDI = 0x%016llX\n"), ctx->Rsi, ctx->Rdi);
        fprintf(f, xorstr_("  RBP = 0x%016llX  RSP = 0x%016llX\n"), ctx->Rbp, ctx->Rsp);
        fprintf(f, xorstr_("  R8  = 0x%016llX  R9  = 0x%016llX\n"), ctx->R8, ctx->R9);
        fprintf(f, xorstr_("  R10 = 0x%016llX  R11 = 0x%016llX\n"), ctx->R10, ctx->R11);
        fprintf(f, xorstr_("  R12 = 0x%016llX  R13 = 0x%016llX\n"), ctx->R12, ctx->R13);
        fprintf(f, xorstr_("  R14 = 0x%016llX  R15 = 0x%016llX\n"), ctx->R14, ctx->R15);
        fprintf(f, xorstr_("  RIP = 0x%016llX\n\n"), ctx->Rip);

        fprintf(f, xorstr_("Stack (top 256 bytes):\n"));
        uint8_t stack[256];
        SIZE_T copied = 0;
        if (ReadProcessMemory(GetCurrentProcess(), (void*)ctx->Rsp, stack, sizeof(stack), &copied)) {
            for (SIZE_T i = 0; i < copied; i += 16) {
                fprintf(f, xorstr_("  %016llX: "), ctx->Rsp + i);
                for (SIZE_T j = 0; j < 16 && (i + j) < copied; j++)
                    fprintf(f, xorstr_("%02X "), stack[i + j]);
                fprintf(f, "\n");
            }
        }
        fprintf(f, xorstr_("\nGame State:\n"));
        fprintf(f, xorstr_("  Base: 0x%llX  GameTime: %.1f\n"), (uint64_t)g_base, g_gameTime);
        fprintf(f, xorstr_("  Heroes: %d  Minions: %d  Turrets: %d\n"),
            (int)g_heroes.size(), (int)g_minions.size(), (int)g_turrets.size());

        fclose(f);
        VX_LOG("\n[CRASH] Log saved to %s\n", path);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

static bool WorldToScreen(const Vec3& world, Vec2& screen)
{
    const auto& m = g_viewProjMatrix.m;
    float w = m[0][3] * world.x + m[1][3] * world.y + m[2][3] * world.z + m[3][3];
    if (w < 0.001f) return false;

    float x = m[0][0] * world.x + m[1][0] * world.y + m[2][0] * world.z + m[3][0];
    float y = m[0][1] * world.x + m[1][1] * world.y + m[2][1] * world.z + m[3][1];

    float invW = 1.0f / w;
    screen.x = (g_screenW * 0.5f) + (g_screenW * 0.5f) * x * invW;
    screen.y = (g_screenH * 0.5f) - (g_screenH * 0.5f) * y * invW;
    return true;
}

static float WorldRadiusToScreen(const Vec3& center, float worldRadius)
{
    Vec2 cp, ep;
    if (!WorldToScreen(center, cp)) return 0.f;
    Vec3 edge = { center.x + worldRadius, center.y, center.z };
    if (!WorldToScreen(edge, ep)) return 0.f;
    float dx = ep.x - cp.x, dy = ep.y - cp.y;
    return sqrtf(dx * dx + dy * dy);
}

static float ScreenScale(const Vec3& worldPos)
{
    float sr = WorldRadiusToScreen(worldPos, 100.f);
    return std::clamp(sr / 30.f, 0.3f, 3.f);
}

static uintptr_t g_scannedViewPort = 0;
static uintptr_t g_scannedVPMatrix = 0;
static bool g_viewPortScanned = false;

static void SigScanViewPort()
{
    g_viewPortScanned = true;
    g_scannedViewPort = 0;
    g_scannedVPMatrix = 0;

    uint64_t moduleSize = 0x3000000;
    PVMMDLL_MAP_MODULEENTRY mod = nullptr;
    if (VMMDLL_Map_GetModuleFromNameU(TargetProcess.vHandle, TargetProcess.current_process.PID,
        (LPSTR)xorstr_("League of Legends.exe"), &mod, VMMDLL_MODULE_FLAG_NORMAL)) {
        moduleSize = mod->cbImageSize;
        VMMDLL_MemFree(mod);
    }
    VX_LOG("[SigScan] Scanning 0x%llX bytes...\n", (uint64_t)moduleSize);

    constexpr uint64_t CHUNK = 0x10000;
    std::vector<uint8_t> buf(CHUNK + 16);

    for (uint64_t off = 0; off < moduleSize; off += CHUNK) {
        DWORD cb = 0;
        DWORD toRead = (DWORD)(std::min)((uint64_t)(CHUNK + 13), moduleSize - off);
        if (!VMMDLL_MemReadEx(TargetProcess.vHandle, TargetProcess.current_process.PID,
            g_base + off, buf.data(), toRead, &cb, VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_ZEROPAD_ON_FAIL))
            continue;
        if (cb < 13) continue;
        for (DWORD i = 0; i + 13 <= cb; i++) {
            // ViewPort: 48 8B 3D ?? ?? ?? ?? FF 90 20 01 00 00
            if (!g_scannedViewPort &&
                buf[i]==0x48 && buf[i+1]==0x8B && buf[i+2]==0x3D &&
                buf[i+7]==0xFF && buf[i+8]==0x90 &&
                buf[i+9]==0x20 && buf[i+10]==0x01 && buf[i+11]==0x00 && buf[i+12]==0x00) {
                int32_t rel; memcpy(&rel, &buf[i+3], 4);
                int64_t rva = (int64_t)(off + i + 7) + (int64_t)rel;
                if (rva > 0) { g_scannedViewPort = (uintptr_t)rva; VX_LOG("[SigScan] ViewPort RVA 0x%llX\n", (uint64_t)rva); }
            }
            // ViewProjMatrices: 48 8D 0D ?? ?? ?? ?? 0F 10 00
            if (!g_scannedVPMatrix && i + 10 <= cb &&
                buf[i]==0x48 && buf[i+1]==0x8D && buf[i+2]==0x0D &&
                buf[i+7]==0x0F && buf[i+8]==0x10 && buf[i+9]==0x00) {
                int32_t rel; memcpy(&rel, &buf[i+3], 4);
                int64_t rva = (int64_t)(off + i + 7) + (int64_t)rel;
                if (rva > 0) { g_scannedVPMatrix = (uintptr_t)rva; VX_LOG("[SigScan] ViewProjMatrices RVA 0x%llX\n", (uint64_t)rva); }
            }
            if (g_scannedViewPort && g_scannedVPMatrix) goto scan_done;
        }
    }
    scan_done:
    if (!g_scannedViewPort) VX_LOG("[SigScan] ViewPort not found\n");
    if (!g_scannedVPMatrix) VX_LOG("[SigScan] ViewProjMatrices not found\n");
}

static void ReadEntityList(uintptr_t mgrGlobal, std::vector<uintptr_t>& out, uint32_t cap = 200)
{
    out.clear();
    uintptr_t mgr = TargetProcess.Read<uintptr_t>(g_base + mgrGlobal);
    if (!mgr) return;
    uintptr_t listPtr = TargetProcess.Read<uintptr_t>(mgr + Offsets::Manager::List);
    uint32_t  count   = TargetProcess.Read<uint32_t>(mgr + Offsets::Manager::Count);
    count = std::min(count, cap);
    if (!listPtr || count == 0) return;
    out.resize(count);
    TargetProcess.Read(listPtr, out.data(), count * sizeof(uintptr_t));
}

static void CacheGameState()
{
    g_gameTime = TargetProcess.Read<float>(g_base + Offsets::Globals::GameTime);
    if (g_gameTime < 1.0f) return;

    if (!g_viewPortScanned) SigScanViewPort();

    static int g_vpMethod = -1;
    static bool g_vpFound = false;

    auto isValidMatrix = [](const Matrix4x4& t) -> bool {
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                if (std::isnan(t.m[i][j]) || std::isinf(t.m[i][j]) || fabsf(t.m[i][j]) > 50000.f) return false;
        if (t.m[0][0] == 0.f && t.m[1][1] == 0.f && t.m[2][2] == 0.f) return false;
        if (fabsf(t.m[0][0]) > 50.f || fabsf(t.m[1][1]) > 50.f) return false;
        return true;
    };

    auto multiplyViewProj = [](const Matrix4x4& v, const Matrix4x4& p, Matrix4x4& out) {
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                out.m[i][j] = v.m[i][0]*p.m[0][j] + v.m[i][1]*p.m[1][j]
                             + v.m[i][2]*p.m[2][j] + v.m[i][3]*p.m[3][j];
    };

    auto tryMethod = [&](int method) -> bool {
        Matrix4x4 test{};
        if (method <= 3 && g_scannedVPMatrix) {
            // 0: View(+0)*Proj(+0x40)  1: direct +0x80  2: direct +0x40  3: direct +0
            uintptr_t addr = g_base + g_scannedVPMatrix;
            if (method == 0) {
                Matrix4x4 viewMat{}, projMat{};
                TargetProcess.Read(addr, &viewMat, sizeof(Matrix4x4));
                TargetProcess.Read(addr + 0x40, &projMat, sizeof(Matrix4x4));
                multiplyViewProj(viewMat, projMat, test);
            } else if (method == 1) { TargetProcess.Read(addr + 0x80, &test, sizeof(Matrix4x4));
            } else if (method == 2) { TargetProcess.Read(addr + 0x40, &test, sizeof(Matrix4x4));
            } else { TargetProcess.Read(addr, &test, sizeof(Matrix4x4)); }
        } else {
            int base = g_scannedVPMatrix ? 4 : 0;
            int vpIdx = (method - base) / 2;
            bool multiply = ((method - base) % 2) == 0;
            const uintptr_t vpRVAs[] = { g_scannedViewPort, Offsets::Globals::ViewPort };
            if (vpIdx >= 2 || !vpRVAs[vpIdx]) return false;
            uintptr_t vp = TargetProcess.Read<uintptr_t>(g_base + vpRVAs[vpIdx]);
            if (!vp) return false;
            if (multiply) {
                Matrix4x4 viewMat{}, projMat{};
                TargetProcess.Read(vp + Offsets::ViewPort::ViewMatrix, &viewMat, sizeof(Matrix4x4));
                TargetProcess.Read(vp + Offsets::ViewPort::ProjMatrix, &projMat, sizeof(Matrix4x4));
                multiplyViewProj(viewMat, projMat, test);
            } else {
                TargetProcess.Read(vp + Offsets::ViewPort::ProjMatrix + 0x40, &test, sizeof(Matrix4x4));
            }
        }
        if (!isValidMatrix(test)) return false;
        g_viewProjMatrix = test;
        return true;
    };

    int maxMethods = (g_scannedVPMatrix ? 4 : 0) + (g_scannedViewPort ? 2 : 0) + 2;
    if (g_vpFound) {
        tryMethod(g_vpMethod);
    } else {
        const char* names[] = {
            "Sig View*Proj", "Sig +0x80", "Sig +0x40", "Sig +0x00",
            "SigVP V*P", "SigVP precomp", "Hardcoded V*P", "Hardcoded precomp",
            "LadyDarknes V*P", "LadyDarknes precomp"
        };
        for (int m = 0; m < maxMethods; m++) {
            if (tryMethod(m)) {
                g_vpMethod = m;
                g_vpFound = true;
                VX_LOG("[ViewProj] Method %d: %s  diag=(%.3f, %.3f, %.3f, %.3f)\n",
                    m, names[m], g_viewProjMatrix.m[0][0], g_viewProjMatrix.m[1][1],
                    g_viewProjMatrix.m[2][2], g_viewProjMatrix.m[3][3]);
                break;
            }
        }
        if (!g_vpFound) {
            static bool once = false;
            if (!once) {
                once = true;
                VX_LOG("[ViewProj] ALL methods failed. Diagnostics:\n");
                if (g_scannedVPMatrix) {
                    float diag[4][4];
                    for (int s = 0; s < 4; s++) {
                        Matrix4x4 tmp{};
                        TargetProcess.Read(g_base + g_scannedVPMatrix + s * 0x40, &tmp, sizeof(Matrix4x4));
                        VX_LOG("  VPMatrix+0x%X diag=(%.3f, %.3f, %.3f, %.3f)\n",
                            s*0x40, tmp.m[0][0], tmp.m[1][1], tmp.m[2][2], tmp.m[3][3]);
                    }
                }
                uintptr_t vp = TargetProcess.Read<uintptr_t>(g_base + Offsets::Globals::ViewPort);
                VX_LOG("  Hardcoded VP ptr = 0x%llX\n", (uint64_t)vp);
                if (vp) {
                    for (uintptr_t off = 0x100; off <= 0x300; off += 0x40) {
                        Matrix4x4 tmp{};
                        TargetProcess.Read(vp + off, &tmp, sizeof(Matrix4x4));
                        VX_LOG("  VP+0x%llX diag=(%.3f, %.3f, %.3f, %.3f)\n",
                            (uint64_t)off, tmp.m[0][0], tmp.m[1][1], tmp.m[2][2], tmp.m[3][3]);
                    }
                }
            }
        }
    }

    // local player scatter read
    uintptr_t localPtr = TargetProcess.Read<uintptr_t>(g_base + Offsets::Globals::LocalPlayer);
    if (localPtr) {
        g_localAddr = localPtr;
        auto ls = TargetProcess.CreateScatterHandle();
        TargetProcess.AddScatterReadRequest(ls, localPtr + Offsets::GameObject::Team,           &g_localTeam,        sizeof(uint8_t));
        TargetProcess.AddScatterReadRequest(ls, localPtr + Offsets::GameObject::Position,       &g_localPos,         sizeof(Vec3));
        TargetProcess.AddScatterReadRequest(ls, localPtr + Offsets::GameObject::AttackRange,    &g_localAttackRange, sizeof(float));
        TargetProcess.AddScatterReadRequest(ls, localPtr + Offsets::GameObject::BoundingRadius, &g_localBoundingRadius, sizeof(float));
        TargetProcess.AddScatterReadRequest(ls, localPtr + Offsets::GameObject::BaseAD,         &g_localBaseAD,      sizeof(float));
        TargetProcess.AddScatterReadRequest(ls, localPtr + Offsets::GameObject::BonusAD,        &g_localBonusAD,     sizeof(float));
        TargetProcess.AddScatterReadRequest(ls, localPtr + Offsets::GameObject::AttackSpeedMod,   &g_localASMod,       sizeof(float));
        TargetProcess.AddScatterReadRequest(ls, localPtr + Offsets::GameObject::AttackSpeedScale, &g_localASScale,     sizeof(float));
        TargetProcess.AddScatterReadRequest(ls, localPtr + Offsets::GameObject::Level,          &g_localLevel,       sizeof(int32_t));
        TargetProcess.AddScatterReadRequest(ls, localPtr + Offsets::GameObject::Mana,           &g_localMana,        sizeof(float));
        TargetProcess.AddScatterReadRequest(ls, localPtr + Offsets::GameObject::MaxMana,        &g_localMaxMana,     sizeof(float));
        TargetProcess.AddScatterReadRequest(ls, localPtr + Offsets::GameObject::Health,         &g_localHealth,      sizeof(float));
        TargetProcess.AddScatterReadRequest(ls, localPtr + Offsets::GameObject::MaxHealth,      &g_localMaxHealth,   sizeof(float));
        TargetProcess.AddScatterReadRequest(ls, localPtr + Offsets::GameObject::Name,           &g_localChampName,   63);
        TargetProcess.ExecuteReadScatter(ls);
        TargetProcess.CloseScatterHandle(ls);
        g_localChampName[63] = 0;
        if (g_localAttackRange < 50.f || g_localAttackRange > 1500.f) g_localAttackRange = 550.f;
        if (g_localBoundingRadius < 10.f || g_localBoundingRadius > 200.f) g_localBoundingRadius = 65.f;

        if (g_orbPingComp) {
            uintptr_t netPtr = TargetProcess.Read<uintptr_t>(g_base + Offsets::Globals::NetClient);
            if (netPtr > 0x10000 && netPtr < 0x7FFFFFFFFFFF) {
                float rawPing = TargetProcess.Read<float>(netPtr + 0x3C);
                if (rawPing >= 0.f && rawPing < 2000.f) g_orbPingMs = rawPing;
            }
        }

        if (g_orbReadCooldowns) {

            uintptr_t spellBookBase = localPtr + Offsets::GameObject::SpellBook;
            for (int s = 0; s < 4; s++) {
                uintptr_t slotPtr = TargetProcess.Read<uintptr_t>(spellBookBase + Offsets::SpellBook::SpellSlots + s * 0x8);
                if (slotPtr > 0x10000 && slotPtr < 0x7FFFFFFFFFFF) {
                    g_spells[s].level   = TargetProcess.Read<int>(slotPtr + 0x28);
                    g_spells[s].readyAt = TargetProcess.Read<float>(slotPtr + 0x38);
                    g_spells[s].totalCD = TargetProcess.Read<float>(slotPtr + 0x74);
                    g_spells[s].ready   = (g_spells[s].level > 0 && g_spells[s].readyAt <= g_gameTime);
                    g_spells[s].name[0] = 0;
                    g_spells[s].range   = 0.f;
                    uintptr_t spellInfo = TargetProcess.Read<uintptr_t>(slotPtr + Offsets::SpellSlot::SpellSlotData);
                    if (spellInfo > 0x10000 && spellInfo < 0x7FFFFFFFFFFF) {
                        uintptr_t namePtr = TargetProcess.Read<uintptr_t>(spellInfo + 0x28);
                        if (namePtr > 0x10000 && namePtr < 0x7FFFFFFFFFFF)
                            TargetProcess.Read(namePtr, g_spells[s].name, 63);
                        float cr = TargetProcess.Read<float>(spellInfo + Offsets::SpellData::CastRange);
                        if (cr > 0.f && cr < 10000.f) g_spells[s].range = cr;
                    }
                    g_spells[s].name[63] = 0;
                } else {
                    g_spells[s] = {};
                }
            }

            g_activeCastEnd = 0.f;
            g_activeAttackEnd = 0.f;
            g_activeIsAuto = false;
            uintptr_t activeCastPtr = TargetProcess.Read<uintptr_t>(spellBookBase + Offsets::SpellBook::ActiveSpellCast);
            if (activeCastPtr > 0x10000 && activeCastPtr < 0x7FFFFFFFFFFF) {
                g_activeCastEnd   = TargetProcess.Read<float>(activeCastPtr + 0x1A4);
                g_activeAttackEnd = TargetProcess.Read<float>(activeCastPtr + 0x1A8);
                uintptr_t castInfo = TargetProcess.Read<uintptr_t>(activeCastPtr + 0x08);
                if (castInfo > 0x10000 && castInfo < 0x7FFFFFFFFFFF)
                    g_activeIsAuto = (TargetProcess.Read<uint8_t>(castInfo + 0x113) != 0);
            }
        }
    }

    std::vector<uintptr_t> ptrs;
    ReadEntityList(Offsets::Globals::HeroManager, ptrs, 12);
    if (!ptrs.empty()) {
    CachedHero oldHeroes[12]{};
    int oldCount = (int)(std::min)(g_heroes.size(), (size_t)12);
    for (int i = 0; i < oldCount; i++) oldHeroes[i] = g_heroes[i];
    g_heroes.resize(ptrs.size());

    auto scatter = TargetProcess.CreateScatterHandle();
    for (uint32_t i = 0; i < ptrs.size(); i++)
    {
        g_heroes[i].addr = ptrs[i];
        g_heroes[i].name[0] = 0;
        if (!ptrs[i]) continue;
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::Position,       &g_heroes[i].pos,            sizeof(Vec3));
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::Health,         &g_heroes[i].health,         sizeof(float));
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::MaxHealth,      &g_heroes[i].maxHealth,      sizeof(float));
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::Shield,         &g_heroes[i].shield,         sizeof(float));
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::MagicShield,    &g_heroes[i].magicShield,    sizeof(float));
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::AttackRange,    &g_heroes[i].attackRange,    sizeof(float));
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::BoundingRadius, &g_heroes[i].boundingRadius, sizeof(float));
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::BaseAD,         &g_heroes[i].baseAD,         sizeof(float));
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::BonusAD,        &g_heroes[i].bonusAD,        sizeof(float));
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::AttackSpeedMod, &g_heroes[i].attackSpeedMod, sizeof(float));
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::Armor,          &g_heroes[i].armor,          sizeof(float));
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::MagicResist,    &g_heroes[i].magicResist,    sizeof(float));
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::Mana,           &g_heroes[i].mana,           sizeof(float));
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::MaxMana,        &g_heroes[i].maxMana,        sizeof(float));
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::Level,          &g_heroes[i].level,          sizeof(int32_t));
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::Team,           &g_heroes[i].team,           sizeof(uint8_t));
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::Visible,        &g_heroes[i].visible,        sizeof(bool));
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::IsDead,         &g_heroes[i].isDead,         sizeof(bool));
        TargetProcess.AddScatterReadRequest(scatter, ptrs[i] + Offsets::GameObject::Name,           &g_heroes[i].name,           63);
    }
    TargetProcess.ExecuteReadScatter(scatter);
    TargetProcess.CloseScatterHandle(scatter);

    for (uint32_t i = 0; i < ptrs.size(); i++)
    {
        g_heroes[i].name[63] = 0;
        if (!ptrs[i]) { g_heroes[i].name[0] = 0; continue; }

        bool found = false;
        for (int j = 0; j < oldCount; j++) {
            auto& old = oldHeroes[j];
            if (old.addr == ptrs[i]) {
                g_heroes[i].wasVisible    = old.visible;
                g_heroes[i].lastPos       = old.lastPos;
                g_heroes[i].lastSeenTime  = old.lastSeenTime;
                g_heroes[i].recallAlerted = old.recallAlerted;
                found = true;
                break;
            }
        }
        if (!found) {
            g_heroes[i].wasVisible    = false;
            g_heroes[i].recallAlerted = false;
            g_heroes[i].lastPos       = g_heroes[i].pos;
            g_heroes[i].lastSeenTime  = 0.f;
        }
        if (g_heroes[i].visible) {
            g_heroes[i].lastPos       = g_heroes[i].pos;
            g_heroes[i].lastSeenTime  = g_gameTime;
            g_heroes[i].recallAlerted = false;
        }
    }

    // ── Read summoner spells (slots 4,5 = D,F) for all heroes ──
    auto ReadSummonerSlot = [](uintptr_t heroAddr, int slotIdx, SummonerSpell& out) {
        out = {};
        uintptr_t spellBookBase = heroAddr + Offsets::GameObject::SpellBook;
        uintptr_t slotPtr = TargetProcess.Read<uintptr_t>(spellBookBase + Offsets::SpellBook::SpellSlots + slotIdx * 0x8);
        if (slotPtr < 0x10000 || slotPtr > 0x7FFFFFFFFFFF) return;
        out.readyAt = TargetProcess.Read<float>(slotPtr + 0x38);
        out.totalCD = TargetProcess.Read<float>(slotPtr + 0x74);
        out.ready   = (out.readyAt <= g_gameTime);
        uintptr_t spellInfo = TargetProcess.Read<uintptr_t>(slotPtr + Offsets::SpellSlot::SpellSlotData);
        if (spellInfo > 0x10000 && spellInfo < 0x7FFFFFFFFFFF) {
            uintptr_t namePtr = TargetProcess.Read<uintptr_t>(spellInfo + 0x28);
            if (namePtr > 0x10000 && namePtr < 0x7FFFFFFFFFFF)
                TargetProcess.Read(namePtr, out.name, 63);
        }
        out.name[63] = 0;
    };
    for (int i = 0; i < (int)ptrs.size() && i < 12; i++) {
        if (!ptrs[i]) continue;
        ReadSummonerSlot(ptrs[i], 4, g_heroSumms[i].d);
        ReadSummonerSlot(ptrs[i], 5, g_heroSumms[i].f);
    }

    // ── Local player smite detection ──
    g_smiteSlot = -1;
    g_smiteReady = false;
    g_smiteName[0] = 0;
    if (localPtr) {
        for (int slot = 4; slot <= 5; slot++) {
            SummonerSpell tmp{};
            ReadSummonerSlot(localPtr, slot, tmp);
            if (strstr(tmp.name, "Smite") || strstr(tmp.name, "smite") ||
                strstr(tmp.name, "SummonerSmite")) {
                g_smiteSlot    = slot;
                g_smiteReadyAt = tmp.readyAt;
                g_smiteReady   = tmp.ready;
                strncpy(g_smiteName, tmp.name, 63);
                break;
            }
        }
    }

    g_recallAlerts.erase(
        std::remove_if(g_recallAlerts.begin(), g_recallAlerts.end(),
            [](const RecallAlert& a) { return g_gameTime - a.triggerTime > 5.f; }),
        g_recallAlerts.end());

    for (auto& hero : g_heroes) {
        if (!hero.addr || hero.isDead || hero.team == g_localTeam) continue;
        if (!hero.visible && hero.lastSeenTime > 0.f && !hero.recallAlerted) {
            float timeSince = g_gameTime - hero.lastSeenTime;
            if (timeSince >= g_recallChannelTime && timeSince < g_recallChannelTime + 1.5f) {
                hero.recallAlerted = true;
                RecallAlert alert{};
                strncpy(alert.name, hero.name, 63);
                alert.triggerTime = g_gameTime;
                g_recallAlerts.push_back(alert);
                VX_LOG("[Recall] %s possible recall (MIA %.1fs)\n", hero.name, timeSince);
            }
        }
    }
    }

    if (g_espTurretRange || g_orbEnabled) {
        std::vector<uintptr_t> tPtrs;
        ReadEntityList(Offsets::Globals::TurretManager, tPtrs, 30);
        if (!tPtrs.empty()) {
        g_turrets.resize(tPtrs.size());
        auto ts = TargetProcess.CreateScatterHandle();
        for (uint32_t i = 0; i < tPtrs.size(); i++) {
            g_turrets[i].addr = tPtrs[i];
            if (!tPtrs[i]) continue;
            TargetProcess.AddScatterReadRequest(ts, tPtrs[i] + Offsets::GameObject::Position,  &g_turrets[i].pos,       sizeof(Vec3));
            TargetProcess.AddScatterReadRequest(ts, tPtrs[i] + Offsets::GameObject::Health,    &g_turrets[i].health,    sizeof(float));
            TargetProcess.AddScatterReadRequest(ts, tPtrs[i] + Offsets::GameObject::MaxHealth, &g_turrets[i].maxHealth, sizeof(float));
            TargetProcess.AddScatterReadRequest(ts, tPtrs[i] + Offsets::GameObject::Team,      &g_turrets[i].team,      sizeof(uint8_t));
        }
        TargetProcess.ExecuteReadScatter(ts);
        TargetProcess.CloseScatterHandle(ts);
        }
    }

    if (g_espMinions || g_orbEnabled) {
        std::vector<uintptr_t> mPtrs;
        ReadEntityList(Offsets::Globals::MinionManager, mPtrs, 200);
        if (!mPtrs.empty()) {
        g_minions.resize(mPtrs.size());
        auto ms = TargetProcess.CreateScatterHandle();
        for (uint32_t i = 0; i < mPtrs.size(); i++) {
            g_minions[i].addr = mPtrs[i];
            g_minions[i].name[0] = 0;
            if (!mPtrs[i]) continue;
            TargetProcess.AddScatterReadRequest(ms, mPtrs[i] + Offsets::GameObject::Position,  &g_minions[i].pos,       sizeof(Vec3));
            TargetProcess.AddScatterReadRequest(ms, mPtrs[i] + Offsets::GameObject::Health,         &g_minions[i].health,         sizeof(float));
            TargetProcess.AddScatterReadRequest(ms, mPtrs[i] + Offsets::GameObject::MaxHealth,      &g_minions[i].maxHealth,      sizeof(float));
            TargetProcess.AddScatterReadRequest(ms, mPtrs[i] + Offsets::GameObject::BoundingRadius, &g_minions[i].boundingRadius, sizeof(float));
            TargetProcess.AddScatterReadRequest(ms, mPtrs[i] + Offsets::GameObject::Team,           &g_minions[i].team,           sizeof(uint8_t));
            TargetProcess.AddScatterReadRequest(ms, mPtrs[i] + Offsets::GameObject::Visible,   &g_minions[i].visible,   sizeof(bool));
            TargetProcess.AddScatterReadRequest(ms, mPtrs[i] + Offsets::GameObject::IsDead,    &g_minions[i].isDead,    sizeof(bool));
            TargetProcess.AddScatterReadRequest(ms, mPtrs[i] + Offsets::GameObject::Name,      &g_minions[i].name,      63);
        }
        TargetProcess.ExecuteReadScatter(ms);
        TargetProcess.CloseScatterHandle(ms);
        for (uint32_t i = 0; i < mPtrs.size(); i++) g_minions[i].name[63] = 0;
        }
    } else {
        g_minions.clear();
    }

    // ── Jungle camp tracking (vision + fog-of-war buff reading) ──
    if (g_trackerJungle && g_gameTime > 90.f) {
        // Vision-based: if we can see a live monster, camp is alive
        for (auto& camp : g_jungleCamps) {
            bool found = false;
            for (const auto& m : g_minions) {
                if (!m.addr || m.isDead || m.health <= 0.f) continue;
                if (m.name[0] == 0) continue;
                if (strstr(m.name, camp.name)) {
                    float dx = m.pos.x - camp.pos.x;
                    float dz = m.pos.z - camp.pos.z;
                    if (dx * dx + dz * dz < 2000.f * 2000.f) {
                        found = true;
                        break;
                    }
                }
            }
            if (found && !camp.alive) {
                camp.alive = true;
                camp.buffRespawnAt = 0.f;
            } else if (!found && camp.alive && camp.killedAt > 0.f) {
                // only mark dead from vision if we previously had vision (killedAt set)
            }
        }

        // Fog-of-war: scan minion objects near camp positions for respawn countdown buffs
        // ponytail: runs every 30 frames (~0.5s), 6 scatter passes; bump interval if DMA latency matters
        static int fogScanTick = 0;
        if (++fogScanTick >= 30) {
            fogScanTick = 0;

            // Collect all minion addrs near each camp position
            struct BufCand { uintptr_t addr; int campIdx; };
            std::vector<BufCand> cands;
            for (const auto& m : g_minions) {
                if (!m.addr) continue;
                for (int ci = 0; ci < NUM_CAMPS; ci++) {
                    float dx = m.pos.x - g_jungleCamps[ci].pos.x;
                    float dz = m.pos.z - g_jungleCamps[ci].pos.z;
                    if (dx * dx + dz * dz < 1500.f * 1500.f) {
                        if (strstr(m.name, "SRU_") && m.health > 0.f && !m.isDead) continue;
                        cands.push_back({m.addr, ci});
                    }
                }
            }

            if (!cands.empty()) {
                // Scatter 1: read BuffManager start/end ptrs
                struct BmPtrs { uintptr_t start = 0, end = 0; };
                std::vector<BmPtrs> bmP(cands.size());
                {
                    auto s = TargetProcess.CreateScatterHandle();
                    for (size_t i = 0; i < cands.size(); i++) {
                        TargetProcess.AddScatterReadRequest(s, cands[i].addr + Offsets::GameObject::BuffManager,        &bmP[i].start, 8);
                        TargetProcess.AddScatterReadRequest(s, cands[i].addr + Offsets::GameObject::BuffManager + 0x10, &bmP[i].end,   8);
                    }
                    TargetProcess.ExecuteReadScatter(s);
                    TargetProcess.CloseScatterHandle(s);
                }

                // Scatter 2: read buff entry pointers from each valid buff array
                struct EntInfo { int candIdx; uintptr_t entryPtr = 0; };
                std::vector<EntInfo> ents;
                {
                    auto s = TargetProcess.CreateScatterHandle();
                    for (size_t i = 0; i < cands.size(); i++) {
                        if (bmP[i].start < 0x10000 || bmP[i].end < 0x10000) continue;
                        if (bmP[i].start > 0x7FFFFFFFFFFF || bmP[i].end > 0x7FFFFFFFFFFF) continue;
                        int cnt = (int)((bmP[i].end - bmP[i].start) / 8);
                        if (cnt <= 0 || cnt > 128) continue;
                        for (int j = 0; j < cnt; j++) {
                            ents.push_back({(int)i, 0});
                            TargetProcess.AddScatterReadRequest(s, bmP[i].start + j * 8, &ents.back().entryPtr, 8);
                        }
                    }
                    TargetProcess.ExecuteReadScatter(s);
                    TargetProcess.CloseScatterHandle(s);
                }

                // Scatter 3: entry -> buff pointer (entry + 0x10)
                struct BufInfo { int candIdx; uintptr_t buffPtr = 0; };
                std::vector<BufInfo> bufs;
                {
                    auto s = TargetProcess.CreateScatterHandle();
                    for (auto& e : ents) {
                        if (e.entryPtr < 0x10000 || e.entryPtr > 0x7FFFFFFFFFFF) continue;
                        bufs.push_back({e.candIdx, 0});
                        TargetProcess.AddScatterReadRequest(s, e.entryPtr + 0x10, &bufs.back().buffPtr, 8);
                    }
                    TargetProcess.ExecuteReadScatter(s);
                    TargetProcess.CloseScatterHandle(s);
                }

                // Scatter 4: read endTime + nameStructPtr from each buff
                struct BufData { int candIdx; float endTime = 0.f; uintptr_t namePtr = 0; };
                std::vector<BufData> bd;
                {
                    auto s = TargetProcess.CreateScatterHandle();
                    for (auto& b : bufs) {
                        if (b.buffPtr < 0x10000 || b.buffPtr > 0x7FFFFFFFFFFF) continue;
                        bd.push_back({b.candIdx, 0.f, 0});
                        TargetProcess.AddScatterReadRequest(s, b.buffPtr + 0x1C, &bd.back().endTime,  4);
                        TargetProcess.AddScatterReadRequest(s, b.buffPtr + 0x10, &bd.back().namePtr,  8);
                    }
                    TargetProcess.ExecuteReadScatter(s);
                    TargetProcess.CloseScatterHandle(s);
                }

                // Scatter 5: nameStructPtr + 0x8 -> char* pointer
                struct NmInfo { int candIdx; float endTime; uintptr_t charPtr = 0; };
                std::vector<NmInfo> nm;
                {
                    auto s = TargetProcess.CreateScatterHandle();
                    for (auto& d : bd) {
                        if (d.namePtr < 0x10000 || d.namePtr > 0x7FFFFFFFFFFF) continue;
                        nm.push_back({d.candIdx, d.endTime, 0});
                        TargetProcess.AddScatterReadRequest(s, d.namePtr + 0x8, &nm.back().charPtr, 8);
                    }
                    TargetProcess.ExecuteReadScatter(s);
                    TargetProcess.CloseScatterHandle(s);
                }

                // Scatter 6: read name strings
                struct NmResult { int candIdx; float endTime; char name[64] = {}; };
                std::vector<NmResult> nr;
                {
                    auto s = TargetProcess.CreateScatterHandle();
                    for (auto& n : nm) {
                        if (n.charPtr < 0x10000 || n.charPtr > 0x7FFFFFFFFFFF) continue;
                        nr.push_back({n.candIdx, n.endTime, {}});
                        TargetProcess.AddScatterReadRequest(s, n.charPtr, nr.back().name, 63);
                    }
                    TargetProcess.ExecuteReadScatter(s);
                    TargetProcess.CloseScatterHandle(s);
                }

                // Match countdown buffs to camps
                for (auto& r : nr) {
                    r.name[63] = 0;
                    int ci = cands[r.candIdx].campIdx;
                    auto& camp = g_jungleCamps[ci];
                    if (strstr(r.name, "camprespawncountdownhidden")) {
                        camp.alive = false;
                        camp.buffRespawnAt = r.endTime + 60.f;
                        if (camp.killedAt <= 0.f)
                            camp.killedAt = camp.buffRespawnAt - camp.respawnTime;
                    } else if (strstr(r.name, "camprespawncountdownvisible")) {
                        camp.alive = false;
                        camp.buffRespawnAt = r.endTime;
                        if (camp.killedAt <= 0.f)
                            camp.killedAt = camp.buffRespawnAt - camp.respawnTime;
                    }
                }
            }

            // If buff says respawn time has passed, mark alive again
            for (auto& camp : g_jungleCamps) {
                if (!camp.alive && camp.buffRespawnAt > 0.f && g_gameTime >= camp.buffRespawnAt) {
                    camp.alive = true;
                    camp.killedAt = 0.f;
                    camp.buffRespawnAt = 0.f;
                }
            }
        }
    }

    // ── Auto smite ──
    if (g_smiteEnabled && g_smiteSlot >= 0 && g_smiteReady && g_gameTime > 90.f) {
        float smiteDmg = SmiteDamage(g_localLevel);
        int bestIdx = -1;
        float bestHP = 99999.f;
        for (int i = 0; i < (int)g_minions.size(); i++) {
            const auto& m = g_minions[i];
            if (!m.addr || m.isDead || m.health <= 0.f || !m.visible) continue;
            if (m.team == g_localTeam) continue;
            if (m.name[0] == 0) continue;
            bool isJungle = (strstr(m.name, "SRU_") != nullptr) ||
                            (strstr(m.name, "Sru_") != nullptr);
            if (!isJungle) continue;
            // skip small monsters (camps like mini raptors, mini krugs)
            if (m.maxHealth < 500.f) continue;
            float dx = m.pos.x - g_localPos.x;
            float dy = m.pos.y - g_localPos.y;
            float dz = m.pos.z - g_localPos.z;
            float dist = sqrtf(dx*dx + dy*dy + dz*dz);
            if (dist > 500.f) continue; // smite range ~500
            if (m.health <= smiteDmg && m.health < bestHP) {
                bestHP = m.health;
                bestIdx = i;
            }
        }
        if (bestIdx >= 0) {
            // Cast smite via keyboard (D = slot 4, F = slot 5)
            unsigned int hidCode = (g_smiteSlot == 4) ? 0x07 : 0x09; // D=0x07, F=0x09 HID
            InputDevice::KeyPress(hidCode);
        }
    }
}

struct SinCosEntry { float s, c; };
static SinCosEntry g_sincos12[13], g_sincos20[21], g_sincos50[51];
static bool g_sincosInit = false;
static void InitSinCos() {
    if (g_sincosInit) return;
    g_sincosInit = true;
    auto fill = [](SinCosEntry* tbl, int n) {
        float step = 6.28318f / (float)n;
        for (int i = 0; i <= n; i++) { tbl[i].s = sinf(i * step); tbl[i].c = cosf(i * step); }
    };
    fill(g_sincos12, 12); fill(g_sincos20, 20); fill(g_sincos50, 50);
}

static void DrawWorldCircle(ImDrawList* dl, const Vec3& center, float radius, ImU32 color, float thickness, int segments)
{
    Vec2 centerSp;
    if (!WorldToScreen(center, centerSp)) return;
    float margin = radius * 4.f;
    if (centerSp.x < -margin || centerSp.x > g_screenW + margin ||
        centerSp.y < -margin || centerSp.y > g_screenH + margin) return;

    float screenR = WorldRadiusToScreen(center, radius);
    if (segments <= 0) {
        segments = (int)(screenR * 0.5f);
        segments = std::clamp(segments, 12, 100);
    } else {
        if (segments < 8) segments = 8;
        if (segments > 128) segments = 128;
    }

    const SinCosEntry* tbl = nullptr;
    if (segments == 12) tbl = g_sincos12;
    else if (segments == 20) tbl = g_sincos20;
    else if (segments == 50) tbl = g_sincos50;

    ImVec2 pts[129];
    int count = 0;
    for (int i = 0; i <= segments; i++) {
        float sc, cc;
        if (tbl) { sc = tbl[i].s; cc = tbl[i].c; }
        else { float a = i * 6.28318f / (float)segments; sc = sinf(a); cc = cosf(a); }
        Vec3 wp = { center.x + cc * radius, center.y, center.z + sc * radius };
        Vec2 sp;
        if (WorldToScreen(wp, sp))
            pts[count++] = ImVec2(sp.x, sp.y);
    }
    if (count < 2) return;

    if (g_drawGlow) {
        ImU32 glow = (color & 0x00FFFFFF) | 0x28000000;
        dl->AddPolyline(pts, count, glow, ImDrawFlags_None, thickness + 4.f);
    }

    if (g_drawMode == 1 && count >= 3)
        dl->AddConvexPolyFilled(pts, count, (color & 0x00FFFFFF) | 0x1A000000);

    dl->AddPolyline(pts, count, color, ImDrawFlags_None, thickness);
}

static void DrawESP()
{
    if (!g_espEnabled || g_gameTime < 1.0f) return;
    auto* dl = ImGui::GetBackgroundDrawList();

    ImU32 enemyCol = ImGui::ColorConvertFloat4ToU32(ImVec4(g_enemyColor[0], g_enemyColor[1], g_enemyColor[2], g_enemyColor[3]));
    ImU32 allyCol  = ImGui::ColorConvertFloat4ToU32(ImVec4(g_allyColor[0], g_allyColor[1], g_allyColor[2], g_allyColor[3]));

    for (const auto& hero : g_heroes)
    {
        if (!hero.addr || hero.isDead) continue;
        bool isAlly = (hero.team == g_localTeam);
        if (g_espEnemyOnly && isAlly) continue;

        if (!hero.visible && !isAlly && g_espLastPosition && hero.lastSeenTime > 0.f)
        {
            float timeSince = g_gameTime - hero.lastSeenTime;

            // Color by time: yellow <10s, orange 10-30s, red 30s+
            ImU32 miaCol;
            if (timeSince < 10.f)       miaCol = IM_COL32(255, 255, 0, 255);
            else if (timeSince < 30.f)  miaCol = IM_COL32(255, 160, 0, 255);
            else                        miaCol = IM_COL32(255, 40, 40, 255);

            Vec2 lastSp;
            bool onScreen = WorldToScreen(hero.lastPos, lastSp) &&
                lastSp.x >= -50 && lastSp.x <= g_screenW + 50 &&
                lastSp.y >= -50 && lastSp.y <= g_screenH + 50;

            if (onScreen && lastSp.x >= 0 && lastSp.x <= g_screenW && lastSp.y >= 0 && lastSp.y <= g_screenH)
            {
    
                float pulse = 20.f + 5.f * sinf(g_gameTime * 3.f);
                dl->AddCircle(ImVec2(lastSp.x, lastSp.y), pulse, miaCol, 0, 2.f);
                dl->AddCircle(ImVec2(lastSp.x, lastSp.y), 10.f, miaCol, 0, 2.f);
                float cx = lastSp.x, cy = lastSp.y;
                dl->AddLine(ImVec2(cx - 7, cy - 7), ImVec2(cx + 7, cy + 7), miaCol, 2.f);
                dl->AddLine(ImVec2(cx + 7, cy - 7), ImVec2(cx - 7, cy + 7), miaCol, 2.f);

                char lastBuf[80];
                snprintf(lastBuf, sizeof(lastBuf), "%s [%.0fs]", hero.name, timeSince);
                auto ts = ImGui::CalcTextSize(lastBuf);
                dl->AddRectFilled(ImVec2(cx - ts.x/2 - 2, cy - 28), ImVec2(cx + ts.x/2 + 2, cy - 12), IM_COL32(20,20,20,255), 3.f);
                dl->AddText(ImVec2(cx - ts.x/2, cy - 27), miaCol, lastBuf);
            }
            else
            {

                float margin = 40.f;
                float cx = std::clamp(lastSp.x, margin, (float)g_screenW - margin);
                float cy = std::clamp(lastSp.y, margin, (float)g_screenH - margin);

                float dx = lastSp.x - cx, dy = lastSp.y - cy;
                float len = sqrtf(dx*dx + dy*dy);
                if (len > 0.f) { dx /= len; dy /= len; }
                else { dx = 0; dy = -1; }

                float arrowLen = 14.f, arrowW = 8.f;
                ImVec2 tip(cx + dx * arrowLen, cy + dy * arrowLen);
                ImVec2 left(cx - dy * arrowW - dx * 4, cy + dx * arrowW - dy * 4);
                ImVec2 right(cx + dy * arrowW - dx * 4, cy - dx * arrowW - dy * 4);
                dl->AddTriangleFilled(tip, left, right, miaCol);

                char edgeBuf[80];
                snprintf(edgeBuf, sizeof(edgeBuf), "%s %.0fs", hero.name, timeSince);
                auto es = ImGui::CalcTextSize(edgeBuf);
                float tx = cx - es.x/2, ty = cy - dy * 18 - es.y/2;
                tx = std::clamp(tx, 4.f, (float)g_screenW - es.x - 4.f);
                ty = std::clamp(ty, 4.f, (float)g_screenH - es.y - 4.f);
                dl->AddRectFilled(ImVec2(tx - 2, ty - 1), ImVec2(tx + es.x + 2, ty + es.y + 1), IM_COL32(20,20,20,255), 3.f);
                dl->AddText(ImVec2(tx, ty), miaCol, edgeBuf);
            }
            continue;
        }

        Vec2 screenPos;
        if (!WorldToScreen(hero.pos, screenPos)) continue;
        if (screenPos.x < 0 || screenPos.x > g_screenW || screenPos.y < 0 || screenPos.y > g_screenH) continue;

        ImU32 color = (isAlly ? allyCol : enemyCol) | 0xFF000000;
        if (!hero.visible && g_espVisStatus)
            color = IM_COL32(255, 180, 0, 255);

        float sc = ScreenScale(hero.pos);

        if (g_espBox) {
            float h = 80.f * sc, w = 40.f * sc;
            dl->AddRect(ImVec2(screenPos.x - w/2, screenPos.y - h),
                        ImVec2(screenPos.x + w/2, screenPos.y),
                        color, 0.f, 0, std::max(1.f, 2.f * sc));
        }

        if (g_espHealth && hero.maxHealth > 0.f) {
            float ratio = std::clamp(hero.health / hero.maxHealth, 0.f, 1.f);
            float barW = 50.f * sc, barH = std::max(3.f, 5.f * sc);
            float bx = screenPos.x - barW / 2;
            float by = screenPos.y + 5.f * sc;
            dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + barW, by + barH), IM_COL32(40, 40, 40, 255));
            ImU32 hpCol = ratio > 0.5f ? IM_COL32(0, 255, 0, 255) : (ratio > 0.25f ? IM_COL32(255, 255, 0, 255) : IM_COL32(255, 0, 0, 255));
            dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + barW * ratio, by + barH), hpCol);

            if (g_espShields) {
                float totalShield = hero.shield + hero.magicShield;
                if (totalShield > 1.f) {
                    float shieldRatio = std::clamp(totalShield / hero.maxHealth, 0.f, 1.f - ratio);
                    dl->AddRectFilled(ImVec2(bx + barW * ratio, by), ImVec2(bx + barW * (ratio + shieldRatio), by + barH), IM_COL32(255, 255, 255, 255));
                }
            }
        }

        if (g_espNames && hero.name[0]) {
            auto textSize = ImGui::CalcTextSize(hero.name);
            dl->AddText(ImVec2(screenPos.x - textSize.x/2, screenPos.y - 15.f * sc), color, hero.name);
        }

        if (g_espVisStatus && !hero.visible) {
            dl->AddText(ImVec2(screenPos.x - 10.f, screenPos.y - 28.f * sc), IM_COL32(255, 180, 0, 255), "[FOG]");
        }

        if (g_espAttackRange && hero.attackRange > 50.f) {
            DrawWorldCircle(dl, hero.pos, hero.attackRange, color, 2.f, 0);
        }
    }

    if (g_espTurretRange) {
        for (const auto& turret : g_turrets) {
            if (!turret.addr || turret.health <= 0.f) continue;
            bool isTAlly = (turret.team == g_localTeam);
            if (g_espEnemyOnly && isTAlly) continue;
            ImU32 tc = isTAlly ? IM_COL32(50, 120, 255, 255) : IM_COL32(255, 50, 50, 255);
            DrawWorldCircle(dl, turret.pos, g_turretRange, tc, 2.f, 0);

            Vec2 tsp;
            if (WorldToScreen(turret.pos, tsp) && turret.maxHealth > 0.f) {
                float tsc = ScreenScale(turret.pos);
                float tRatio = std::clamp(turret.health / turret.maxHealth, 0.f, 1.f);
                float tw = 60.f * tsc, th = std::max(3.f, 5.f * tsc);
                float tOff = 30.f * tsc;
                dl->AddRectFilled(ImVec2(tsp.x - tw/2, tsp.y + tOff), ImVec2(tsp.x + tw/2, tsp.y + tOff + th), IM_COL32(40, 40, 40, 255));
                dl->AddRectFilled(ImVec2(tsp.x - tw/2, tsp.y + tOff), ImVec2(tsp.x - tw/2 + tw * tRatio, tsp.y + tOff + th), tc);
            }
        }
    }

    if (g_espMinions) {
        for (const auto& m : g_minions) {
            if (!m.addr || m.health <= 0.f || !m.visible) continue;
            if (g_espEnemyOnly && m.team == g_localTeam) continue;
            Vec2 sp;
            if (!WorldToScreen(m.pos, sp)) continue;
            if (sp.x < 0 || sp.x > g_screenW || sp.y < 0 || sp.y > g_screenH) continue;

            bool isJungle = (m.team != g_localTeam && m.team != 100 && m.team != 200);
            ImU32 mc;
            if (isJungle)
                mc = IM_COL32(255, 200, 0, 255);
            else if (m.team == g_localTeam)
                mc = IM_COL32(0, 255, 0, 255);
            else
                mc = IM_COL32(255, 80, 80, 255);

            float circleRadius = isJungle ? 80.f : 50.f;
            DrawWorldCircle(dl, m.pos, circleRadius, mc, 2.f, 0);

            float msc = ScreenScale(m.pos);
            float ratio = std::clamp(m.health / std::max(m.maxHealth, 1.f), 0.f, 1.f);
            float bw = (isJungle ? 40.f : 30.f) * msc;
            float bh = std::max(2.f, (isJungle ? 4.f : 3.f) * msc);
            float bOff = 3.f * msc;
            dl->AddRectFilled(ImVec2(sp.x - bw/2, sp.y + bOff), ImVec2(sp.x + bw/2, sp.y + bOff + bh), IM_COL32(40, 40, 40, 255));
            dl->AddRectFilled(ImVec2(sp.x - bw/2, sp.y + bOff), ImVec2(sp.x - bw/2 + bw * ratio, sp.y + bOff + bh), mc);
        }
    }

    if (g_sidebarEnabled) {
        float iconSz = 48.f;
        float barH = 5.f;
        float gap = 4.f;
        float cellW = iconSz + gap;

        int enemyCount = 0;
        for (const auto& hero : g_heroes)
            if (hero.addr && hero.team != g_localTeam) enemyCount++;

        float totalW = enemyCount * cellW - gap;
        float startX = ((float)g_screenW - totalW) / 2.f;
        float startY = 8.f;
        int idx = 0;

        for (const auto& hero : g_heroes) {
            if (!hero.addr || hero.team == g_localTeam) continue;
            float x = startX + idx * cellW;
            float y = startY;

            dl->AddRectFilled(ImVec2(x - 1, y - 1), ImVec2(x + iconSz + 1, y + iconSz + barH + 3), IM_COL32(20, 20, 20, 255), 3.f);

            auto* icon = GetChampIcon(hero.name);
            if (icon)
                dl->AddImage((ImTextureID)icon, ImVec2(x, y), ImVec2(x + iconSz, y + iconSz));

            if (hero.isDead) {
                dl->AddRectFilled(ImVec2(x, y), ImVec2(x + iconSz, y + iconSz), IM_COL32(20, 20, 20, 255));
                auto ds = ImGui::CalcTextSize("DEAD");
                dl->AddText(ImVec2(x + iconSz/2 - ds.x/2, y + iconSz/2 - ds.y/2), IM_COL32(255, 50, 50, 255), "DEAD");
            } else if (!hero.visible) {
                dl->AddRect(ImVec2(x, y), ImVec2(x + iconSz, y + iconSz), IM_COL32(255, 180, 0, 255), 0.f, 0, 2.f);
            }

            if (hero.maxHealth > 0.f && !hero.isDead) {
                float ratio = std::clamp(hero.health / hero.maxHealth, 0.f, 1.f);
                float by = y + iconSz + 2.f;
                dl->AddRectFilled(ImVec2(x, by), ImVec2(x + iconSz, by + barH), IM_COL32(40, 40, 40, 255));
                ImU32 hc = ratio > 0.5f ? IM_COL32(0, 200, 0, 255) : (ratio > 0.25f ? IM_COL32(255, 200, 0, 255) : IM_COL32(255, 0, 0, 255));
                dl->AddRectFilled(ImVec2(x, by), ImVec2(x + iconSz * ratio, by + barH), hc);
            }
            idx++;
        }
    }

    if (!g_recallAlerts.empty()) {
        float alertY = 120.f;
        for (const auto& alert : g_recallAlerts) {
            float age = g_gameTime - alert.triggerTime;
            if (age > 5.f) continue;

            char alertTxt[128];
            snprintf(alertTxt, sizeof(alertTxt), "  RECALL  %s  ", alert.name);
            auto sz = ImGui::CalcTextSize(alertTxt);
            float cx = g_screenW / 2.f;

            dl->AddRectFilled(
                ImVec2(cx - sz.x/2 - 8, alertY - 2),
                ImVec2(cx + sz.x/2 + 8, alertY + sz.y + 4),
                IM_COL32(140, 20, 20, 255), 6.f);
            dl->AddRect(
                ImVec2(cx - sz.x/2 - 8, alertY - 2),
                ImVec2(cx + sz.x/2 + 8, alertY + sz.y + 4),
                IM_COL32(255, 60, 60, 255), 6.f, 0, 2.f);
            dl->AddText(ImVec2(cx - sz.x/2, alertY + 1), IM_COL32(255, 255, 255, 255), alertTxt);

            alertY += sz.y + 12.f;
        }
    }
}

static void DrawMinimap()
{
    if (!g_minimapEnabled || g_gameTime < 1.0f) return;
    MinimapAutoDefaults();
    auto* dl = ImGui::GetBackgroundDrawList();

    constexpr float MAP_MIN = -120.f;
    constexpr float MAP_MAX = 14870.f;
    constexpr float MAP_RANGE = MAP_MAX - MAP_MIN;

    float sz = g_minimapSize;
    float ox = g_minimapOffX;
    float oy = g_minimapOffY;

    dl->AddRectFilled(ImVec2(ox, oy), ImVec2(ox + sz, oy + sz), IM_COL32(15, 20, 15, 255), 4.f);
    dl->AddRect(ImVec2(ox, oy), ImVec2(ox + sz, oy + sz), IM_COL32(80, 80, 80, 255), 4.f, 0, 1.5f);

    auto worldToMap = [&](const Vec3& w) -> ImVec2 {
        float mx = (w.x - MAP_MIN) / MAP_RANGE * sz;
        float my = (1.f - (w.z - MAP_MIN) / MAP_RANGE) * sz; // Z flipped: top of map = high Z
        return ImVec2(ox + std::clamp(mx, 1.f, sz - 1.f), oy + std::clamp(my, 1.f, sz - 1.f));
    };

    for (const auto& t : g_turrets) {
        if (!t.addr || t.health <= 0.f) continue;
        ImVec2 tp = worldToMap(t.pos);
        ImU32 tc = (t.team == g_localTeam) ? IM_COL32(60, 140, 255, 255) : IM_COL32(255, 60, 60, 255);
        dl->AddRectFilled(ImVec2(tp.x - 3, tp.y - 3), ImVec2(tp.x + 3, tp.y + 3), tc);
    }

    for (const auto& h : g_heroes) {
        if (!h.addr) continue;
        bool isAlly = (h.team == g_localTeam);
        bool isLocal = (h.addr == g_localAddr);

        Vec3 drawPos = h.visible ? h.pos : h.lastPos;
        if (h.isDead && !isLocal) continue;

        ImVec2 mp = worldToMap(drawPos);
        float r = isLocal ? 5.f : 4.f;

        ImU32 col;
        if (isLocal) {
            col = IM_COL32(0, 255, 120, 255);
        } else if (isAlly) {
            col = IM_COL32(60, 160, 255, 255);
        } else {

            if (h.visible) {
                col = IM_COL32(255, 60, 60, 255);
            } else if (h.lastSeenTime > 0.f) {
                float timeSince = g_gameTime - h.lastSeenTime;
                uint8_t fade = (uint8_t)(std::max(0.3f, 1.f - timeSince / 60.f) * 255);
                col = IM_COL32(fade, fade / 3, fade / 3, 255);
                r = 3.f;
            } else {
                continue;
            }

            if (h.addr == g_orbSelectedAddr) {
                dl->AddCircle(mp, r + 3, IM_COL32(255, 200, 0, 255), 0, 2.f);
            }
        }

        dl->AddCircleFilled(mp, r, col);

        if (isLocal)
            dl->AddCircle(mp, r, IM_COL32(255, 255, 255, 255), 0, 1.5f);
    }

    dl->AddText(ImVec2(ox + 4, oy + 2), IM_COL32(200, 200, 200, 255), "MAP");
}

static void MoveCursorTo(float targetX, float targetY)
{
    int dx = (int)(targetX - g_cursorX);
    int dy = (int)(targetY - g_cursorY);
    if (dx == 0 && dy == 0) return;
    InputDevice::MoveTo(dx, dy);
    g_cursorX = targetX;
    g_cursorY = targetY;
}

#include "LuaEngine.h"

static void OrbwalkerTick()
{
    if (!g_orbEnabled || !InputDevice::IsConnected() || !Keyboard::IsReady()) return;
    if (g_gameTime < 1.0f) return;

    int mode = 0;
    if      (Keyboard::IsKeyDown(VK_SPACE)) mode = 1;
    else if (Keyboard::IsKeyDown('C'))      mode = 2;
    else if (Keyboard::IsKeyDown('V'))      mode = 3;
    else if (Keyboard::IsKeyDown('X'))      mode = 4;
    else if (Keyboard::IsKeyDown('Z'))      mode = 5;
    g_orbMode = mode;
    if (mode == 0) { g_orbTargetIdx = -1; g_orbTargetIsMinion = false; g_orbStickyAddr = 0; g_orbStickyUntil = 0; g_orbLastActionTarget = 0; return; }

    ULONGLONG now = GetTickCount64();
    float effectiveAS = g_orbAttackSpeed;
    float effectiveAD = g_orbPlayerAD;
    if (g_orbAutoStats) {
        float totalAD = g_localBaseAD + g_localBonusAD;
        if (totalAD > 10.f && totalAD < 2000.f) effectiveAD = totalAD;
        float baseAS = (g_localASScale > 0.3f && g_localASScale < 1.5f) ? g_localASScale : g_orbBaseAS;
        float totalAS = baseAS * g_localASMod;
        if (totalAS > 0.2f && totalAS < 10.f) effectiveAS = std::clamp(totalAS, 0.2f, 2.6f);
    } else {
        effectiveAS = std::clamp(effectiveAS, 0.2f, 2.6f);
    }
    g_orbEffectiveAD = effectiveAD;
    float attackDelayMs = 1000.f / std::max(effectiveAS, 0.1f);
    int windPct = g_orbWindupPct;
    if (g_localChampName[0]) {
        auto wit = g_orbWindupPctChamp.find(g_localChampName);
        if (wit != g_orbWindupPctChamp.end()) windPct = wit->second;
    }
    float windupMs      = std::max(60.f, attackDelayMs * (std::clamp(windPct, 1, 100) / 100.f));
    // leave more post-windup margin at high AS to reduce AA cancels (correctness)
    float postWindupMargin = (attackDelayMs < 500.f) ? 55.f : 35.f;
    if (windupMs > attackDelayMs - postWindupMargin) windupMs = std::max(60.f, attackDelayMs - postWindupMargin);

    static float attackJitter = 0.f;
    static float moveJitter   = 0.f;
    static float sessionDrift = 0.f;
    if (sessionDrift == 0.f) sessionDrift = g_orbHumanize ? xorFloat(-8.f, 8.f) : 0.f;
    float pingComp = g_orbPingComp ? (2.f * std::max(0.f, g_orbPingMs) + g_orbTimingBias) : 0.f;
    bool canAttack = (now - g_orbLastAttack) >= (ULONGLONG)(attackDelayMs + pingComp + attackJitter + (g_orbHumanize ? sessionDrift : 0.f));
    bool gameInAAWindup = (g_activeIsAuto && g_gameTime > 1.f && g_activeAttackEnd > g_gameTime + 0.001f
                           && g_activeAttackEnd < g_gameTime + 2.0f);
    bool canMove   = !gameInAAWindup && (now - g_orbLastAttack) >= (ULONGLONG)(windupMs + moveJitter);
    auto aimEnabled = [&]() { return g_orbCursorPolicy == 1; };

    auto dist3d = [](const Vec3& a, const Vec3& b) -> float {
        float dx = a.x - b.x, dz = a.z - b.z;
        return sqrtf(dx*dx + dz*dz);
    };
    auto safeRadius = [](float r) { return (r > 10.f && r < 200.f) ? r : 65.f; };
    float localRadius = safeRadius(g_localBoundingRadius);
    auto attackRangeTo = [&](float targetRadius) {
        return g_localAttackRange + localRadius + safeRadius(targetRadius) + 12.f;
    };
    auto getPrio = [](const char* name) -> int {
        auto it = g_orbPriority.find(name);
        return (it != g_orbPriority.end()) ? std::clamp(it->second, 1, 5) : 3;
    };
    auto orbDoMove = [&](int minGapMs) {
        if (!canMove) return;
        if ((now - g_orbLastMove) <= (ULONGLONG)(minGapMs + randInt(0, 35))) return;
        InputDevice::RightDown();
        InputDevice::RightUp();
        g_orbLastMove = now;
    };

    // track visible minion HP for prediction
    for (const auto& m : g_minions) {
        if (m.addr && m.visible && m.health > 0.f) OrbTrackMinionHp(m.addr, m.health, now);
    }

    if (mode == 5) {
        orbDoMove(70);
        g_orbTargetIdx = -1; g_orbTargetIsMinion = false;
        return;
    }

    // mouse mode
    if (g_orbTargetMode == 0) {
        POINT cp; if (GetCursorPos(&cp)) { g_cursorX = (float)cp.x; g_cursorY = (float)cp.y; }
        Vec2 cursorSp = { g_cursorX, g_cursorY };
        bool cursorValid = (cursorSp.x > 0 && cursorSp.x < g_screenW && cursorSp.y > 0 && cursorSp.y < g_screenH);

        if ((mode == 1 || mode == 2) && cursorValid) {
            const bool* useSpell[] = {&g_orbUseQ, &g_orbUseW, &g_orbUseE, &g_orbUseR};
            for (int s = 0; s < 4; s++) {
                if (!*useSpell[s]) continue;
                if (g_orbReadCooldowns && g_spells[s].level > 0 && !g_spells[s].ready) continue;
                if ((now - g_orbLastSpell[s]) < (ULONGLONG)(g_orbSpellGateMs + randInt(0, 40))) continue;
                PressKey(g_keyOptions[g_orbSpellKeyIdx[s]].hid);
                g_orbLastSpell[s] = now;
                break;
            }
        }

        bool didAttack = false;
        if (canAttack && cursorValid) {
            IssueAttackKey(g_keyOptions[g_orbAtkKeyIdx].hid);
            g_orbLastAttack = now;
            didAttack = true;
            attackJitter = xorFloat(8.f, 42.f);
            moveJitter   = xorFloat(4.f, 22.f);
            sessionDrift = std::clamp(sessionDrift + xorFloat(-2.f, 2.f), -12.f, 12.f);
        }
        if (!didAttack) orbDoMove(std::max(20, (int)g_orbMoveDelay));
        g_orbTargetIdx = -1; g_orbTargetIsMinion = false;
        return;
    }

    // target mode
    // click-select target (also in orbwalk modes when enabled)
    {
        static bool prevLMB = false;
        bool curLMB = Keyboard::IsKeyDown(VK_LBUTTON);
        bool allowSelect = (mode == 0) || (g_orbClickSelectInMode && g_orbTargetMode == 1);
        if (curLMB && !prevLMB && allowSelect) {
            POINT cp; if (GetCursorPos(&cp)) { g_cursorX = (float)cp.x; g_cursorY = (float)cp.y; }
            float bestDist = 60.f;
            int bestIdx = -1;
            for (int i = 0; i < (int)g_heroes.size(); i++) {
                const auto& h = g_heroes[i];
                if (!h.addr || h.team == g_localTeam || h.health <= 0.f) continue;
                Vec2 sp;
                if (!WorldToScreen(h.pos, sp)) continue;
                float dx = sp.x - g_cursorX, dy = sp.y - g_cursorY;
                float d = sqrtf(dx*dx + dy*dy);
                if (d < bestDist) { bestDist = d; bestIdx = i; }
            }
            if (bestIdx >= 0) {
                g_orbSelectedAddr = g_heroes[bestIdx].addr;
                g_orbSelectedHero = bestIdx;
                g_orbLastActionTarget = 0; g_orbLastActionEvent = now;
            } else if (mode == 0) {
                g_orbSelectedAddr = 0;
                g_orbSelectedHero = -1;
            }
        }
        prevLMB = curLMB;
    }

    if (g_orbSelectedAddr) {
        g_orbSelectedHero = -1;
        for (int i = 0; i < (int)g_heroes.size(); i++) {
            if (g_heroes[i].addr == g_orbSelectedAddr) { g_orbSelectedHero = i; break; }
        }
        if (g_orbSelectedHero >= 0) {
            const auto& sel = g_heroes[g_orbSelectedHero];
            if (sel.isDead || sel.health <= 0.f) { g_orbSelectedAddr = 0; g_orbSelectedHero = -1; }
        } else {
            g_orbSelectedAddr = 0;
        }
    }

    g_orbTargetIdx = -1;
    g_orbTargetIsMinion = false;
    Vec3 targetWorldPos{};
    uintptr_t targetAddr = 0;
    float targetRadius = 65.f;
    float targetRange = 0.f;
    bool targetIsDummy = false;

    if (mode == 1 || mode == 2) {
        if (g_orbSelectedHero >= 0) {
            const auto& sel = g_heroes[g_orbSelectedHero];
            float rng = attackRangeTo(sel.boundingRadius);
            if (sel.visible && sel.health > 0.f && dist3d(g_localPos, sel.pos) <= rng) {
                g_orbTargetIdx = g_orbSelectedHero; g_orbTargetIsMinion = false; targetWorldPos = sel.pos; targetAddr = sel.addr; targetRadius = sel.boundingRadius; targetRange = rng;
            }
        }

        // Sticky target
        if (g_orbTargetIdx < 0 && g_orbStickyAddr && now < g_orbStickyUntil) {
            bool kept = false;
            for (int i = 0; i < (int)g_heroes.size(); i++) {
                const auto& h = g_heroes[i];
                if (h.addr != g_orbStickyAddr) continue;
                float rng = attackRangeTo(h.boundingRadius);
                if (h.health > 0.f && h.visible && dist3d(g_localPos, h.pos) <= rng * 1.05f) {
                    g_orbTargetIdx = i; g_orbTargetIsMinion = false; targetWorldPos = h.pos; targetAddr = h.addr; targetRadius = h.boundingRadius; targetRange = rng; kept = true;
                }
                break;
            }
            if (!kept) { g_orbStickyAddr = 0; g_orbStickyUntil = 0; }
        } else if (g_orbStickyAddr && now >= g_orbStickyUntil) { g_orbStickyAddr = 0; g_orbStickyUntil = 0; }

        if (g_orbTargetIdx < 0) {
            float bestScore = -999999.f;
            for (int i = 0; i < (int)g_heroes.size(); i++) {
                const auto& h = g_heroes[i];
                if (!h.addr || h.team == g_localTeam || h.health <= 0.f || !h.visible) continue;
                float d = dist3d(g_localPos, h.pos);
                float rng = attackRangeTo(h.boundingRadius);
                if (d > rng) continue;

                float score = 0.f;
                switch (g_orbPriorityMode) {
                case 0: score = -d; break;
                case 1: score = -h.health; break;
                case 2: score = h.baseAD + h.bonusAD; break;
                case 3: score = -d; break; // AP not available in hero cache
                default: score = -d; break;
                }
                score += (float)(5 - getPrio(h.name)) * 60.f;
                if (score > bestScore) {
                    bestScore = score;
                    g_orbTargetIdx = i; g_orbTargetIsMinion = false; targetWorldPos = h.pos; targetAddr = h.addr; targetRadius = h.boundingRadius; targetRange = rng;
                }
            }
        }

        // target dummies (minions with maxHP > 2000)
        if (g_orbTargetIdx < 0) {
            float bestDist = 99999.f;
            for (int i = 0; i < (int)g_minions.size(); i++) {
                const auto& m = g_minions[i];
                if (!m.addr || m.team == g_localTeam || m.health <= 0.f || !m.visible) continue;
                if (m.maxHealth < 2000.f) continue;
                float d = dist3d(g_localPos, m.pos);
                float rng = attackRangeTo(m.boundingRadius);
                if (d > rng) continue;
                if (d < bestDist) {
                    bestDist = d;
                    g_orbTargetIdx = i; g_orbTargetIsMinion = true; targetIsDummy = true; targetWorldPos = m.pos; targetAddr = m.addr; targetRadius = m.boundingRadius; targetRange = rng;
                }
            }
        }
    }
    else if (mode == 3 || mode == 4) {
        float lastHitThresh = effectiveAD * (std::clamp(g_orbLastHitBufferPct, 50, 100) / 100.f);
        float bestScore = -999999.f;
        int fallbackIdx = -1; float fallbackD = 999999.f;
        for (int i = 0; i < (int)g_minions.size(); i++) {
            const auto& m = g_minions[i];
            if (!m.addr || m.team == g_localTeam || m.health <= 0.f || !m.visible) continue;
            float d = dist3d(g_localPos, m.pos);
            float rng = attackRangeTo(m.boundingRadius);
            if (d > rng) continue;
            if (mode == 3 && d < fallbackD) { fallbackD = d; fallbackIdx = i; }
            float travelMs = (d / 1800.f) * 1000.f; // rough travel proxy
            float hitMs = windupMs + travelMs + std::max(0.f, g_orbPingMs * 0.5f);
            float pred = OrbPredictMinionHp(m.addr, m.health, hitMs, now);
            if (pred > 0.f && pred <= lastHitThresh) {
                float score = 100000.f - pred + (m.maxHealth > 1000.f ? 50.f : 0.f);
                if (score > bestScore) {
                    bestScore = score; g_orbTargetIdx = i; g_orbTargetIsMinion = true; targetWorldPos = m.pos; targetAddr = m.addr; targetRadius = m.boundingRadius; targetRange = rng;
                }
            }
        }
        if (g_orbTargetIdx < 0 && mode == 3 && !g_orbLastHitOnly && fallbackIdx >= 0) {
            const auto& m = g_minions[fallbackIdx];
            float rng = attackRangeTo(m.boundingRadius);
            g_orbTargetIdx = fallbackIdx; g_orbTargetIsMinion = true; targetWorldPos = m.pos; targetAddr = m.addr; targetRadius = m.boundingRadius; targetRange = rng;
        }
    }

    bool hasTarget = (g_orbTargetIdx >= 0);
    if (hasTarget) OrbNoteActionEvent(targetAddr, now);
    bool reactReady = !hasTarget || OrbCanReact(now);
    Vec2 targetScreenPos{};
    bool targetOnScreen = false;
    if (g_orbTargetIdx >= 0) {
        // keep target selected for draw; react gate only blocks action
        uintptr_t drawAddr = 0; float drawRadius = 65.f; Vec3 drawPos{};
        if (g_orbTargetIsMinion && g_orbTargetIdx < (int)g_minions.size()) { drawAddr = g_minions[g_orbTargetIdx].addr; drawRadius = g_minions[g_orbTargetIdx].boundingRadius; drawPos = g_minions[g_orbTargetIdx].pos; }
        else if (!g_orbTargetIsMinion && g_orbTargetIdx < (int)g_heroes.size()) { drawAddr = g_heroes[g_orbTargetIdx].addr; drawRadius = g_heroes[g_orbTargetIdx].boundingRadius; drawPos = g_heroes[g_orbTargetIdx].pos; }
        RollClickOffset(drawAddr ? drawAddr : targetAddr, drawRadius);
        targetOnScreen = WorldToScreen(ApplyClickOffset(drawPos.x || drawPos.y || drawPos.z ? drawPos : targetWorldPos), targetScreenPos);
    } else {
        g_orbClickTarget = 0;
    }
    bool actionHasTarget = hasTarget && reactReady;
    if (g_orbTargetIdx >= 0 && g_orbTargetIsMinion && g_orbTargetIdx < (int)g_minions.size())
        targetIsDummy = g_minions[g_orbTargetIdx].maxHealth >= 2000.f;

    Vec2 cursorRestore{}; bool cursorMoved = false;
    auto aimAction = [&](const Vec2& aim, bool doAim) {
        if (!doAim || !aimEnabled()) return;
        float dx = aim.x - g_cursorX, dy = aim.y - g_cursorY;
        if ((dx*dx + dy*dy) < 9.f) return;
        if (!cursorMoved) cursorRestore = { g_cursorX, g_cursorY };
        MoveCursorPath(aim.x, aim.y, 0);
        cursorMoved = true;
    };

    // spells (combo/harass) on heroes or dummies
    bool canSpellTarget = actionHasTarget && targetOnScreen && (mode == 1 || mode == 2) && (!g_orbTargetIsMinion || targetIsDummy);
    if (canSpellTarget) {
        bool didSpell = false;
        if (g_comboEnabled && g_localChampName[0] != 0) {
            ChampionCombo::Query q = {};
            q.champion = g_localChampName;
            if (g_orbTargetIsMinion && g_orbTargetIdx >= 0 && g_orbTargetIdx < (int)g_minions.size()) {
                const auto& tgtm = g_minions[g_orbTargetIdx];
                float dx = g_localPos.x - tgtm.pos.x, dz = g_localPos.z - tgtm.pos.z; q.distance = sqrtf(dx*dx + dz*dz);
                q.targetHpPct = (tgtm.maxHealth > 0.f) ? (tgtm.health / tgtm.maxHealth * 100.f) : 100.f;
                q.targetHp = tgtm.health; q.targetArmor = 0.f;
            } else if (g_orbTargetIdx >= 0 && g_orbTargetIdx < (int)g_heroes.size()) {
                const auto& tgt = g_heroes[g_orbTargetIdx];
                float dx = g_localPos.x - tgt.pos.x, dz = g_localPos.z - tgt.pos.z; q.distance = sqrtf(dx*dx + dz*dz);
                q.targetHpPct = (tgt.maxHealth > 0.f) ? (tgt.health / tgt.maxHealth * 100.f) : 100.f;
                q.targetHp = tgt.health; q.targetArmor = tgt.armor;
            }
            q.localHpPct  = (g_localMaxHealth > 0.f) ? (g_localHealth / g_localMaxHealth * 100.f) : 100.f;
            q.totalAttackDamage = effectiveAD;
            q.bonusAttackDamage = g_localBonusAD;
            q.localAttackRange = g_localAttackRange;
            q.aaGapMs = (int)attackDelayMs;
            q.hasAttackTarget = actionHasTarget;
            q.hasSpellWindow = canAttack || canMove;
            q.targetIsMinion = g_orbTargetIsMinion && !targetIsDummy;
            for (int s = 0; s < 4; s++) {
                q.spells[s].slot = s;
                q.spells[s].key = g_keyOptions[g_orbSpellKeyIdx[s]].hid;
                q.spells[s].ready = g_orbReadCooldowns ? g_spells[s].ready : true;
                q.spells[s].level = g_spells[s].level;
                q.spells[s].name = g_spells[s].name;
                q.spells[s].range = g_spells[s].range;
                q.retryReady[s] = (now >= g_comboRetryCooldown[s]);
            }
            ChampionCombo::Action act = ChampionCombo::Choose(q, g_comboSettings);
            if (act.valid && act.slot >= 0 && act.slot < 4 && act.castHoldMs == 0 && !act.useAuxTarget) {
                if (act.castType == ChampionCombo::CastType::Position)
                    aimAction(targetScreenPos, true);
                PressKey((uint8_t)act.key);
                g_orbLastSpell[act.slot] = now;
                if (act.retryMs > 0) g_comboRetryCooldown[act.slot] = now + act.retryMs;
                didSpell = true;
            }
        }
        if (!didSpell) {
            const bool* useSpell[] = {&g_orbUseQ, &g_orbUseW, &g_orbUseE, &g_orbUseR};
            for (int s = 0; s < 4; s++) {
                if (!*useSpell[s]) continue;
                if (g_orbReadCooldowns && g_spells[s].level > 0 && !g_spells[s].ready) continue;
                if ((now - g_orbLastSpell[s]) < (ULONGLONG)(g_orbSpellGateMs + randInt(0, 40))) continue;
                aimAction(targetScreenPos, true);
                PressKey(g_keyOptions[g_orbSpellKeyIdx[s]].hid);
                g_orbLastSpell[s] = now;
                break;
            }
        }
    }

    // attack
    bool didAttack = false;
    if (canAttack && actionHasTarget) {
        float dNow = dist3d(g_localPos, targetWorldPos);
        if (dNow <= targetRange * 1.08f) {
            if (targetOnScreen) aimAction(targetScreenPos, true);
            IssueAttackKey(g_keyOptions[g_orbAtkKeyIdx].hid);
            g_orbLastAttack = now;
            didAttack = true;
            attackJitter = xorFloat(8.f, 42.f);
            moveJitter   = xorFloat(4.f, 22.f);
            sessionDrift = std::clamp(sessionDrift + xorFloat(-2.f, 2.f), -12.f, 12.f);
            g_orbClickOffY = 0.f;
            if (!g_orbTargetIsMinion && g_orbTargetIdx >= 0 && g_orbTargetIdx < (int)g_heroes.size()) {
                g_orbStickyAddr = g_heroes[g_orbTargetIdx].addr;
                g_orbStickyUntil = now + 3000;
            }
        } else {
            g_orbLastAttack = now - (ULONGLONG)std::max(80.f, attackDelayMs - 110.f);
        }
    }

    if (cursorMoved) OrbRestoreCursor(cursorRestore, true);

    // move — right-click wherever user's mouse is (no cursor movement)
    if (!didAttack) orbDoMove(std::max(20, (int)g_orbMoveDelay));
}

static void OrbwalkerDraw()
{
    if (!g_orbEnabled || g_gameTime < 1.0f) return;
    auto* dl = ImGui::GetBackgroundDrawList();

    if (g_orbDrawRange && g_localAttackRange > 50.f) {
        ImU32 rangeCol = IM_COL32(0, 180, 140, 255);
        float drawRadius = (g_localBoundingRadius > 10.f && g_localBoundingRadius < 200.f) ? g_localBoundingRadius : 65.f;
        DrawWorldCircle(dl, g_localPos, g_localAttackRange + drawRadius, rangeCol, 1.5f, 0);
    }

    if (g_orbDrawTarget && g_orbTargetIdx >= 0) {
        Vec3 targetPos{};
        bool valid = false;
        if (g_orbTargetIsMinion && g_orbTargetIdx < (int)g_minions.size()) {
            targetPos = g_minions[g_orbTargetIdx].pos;
            valid = true;
        } else if (!g_orbTargetIsMinion && g_orbTargetIdx < (int)g_heroes.size()) {
            targetPos = g_heroes[g_orbTargetIdx].pos;
            valid = true;
        }
        if (valid) {
            Vec2 sp;
            if (WorldToScreen(targetPos, sp)) {
                dl->AddCircle(ImVec2(sp.x, sp.y), 20.f, IM_COL32(255, 50, 50, 255), 0, 2.5f);
                dl->AddCircle(ImVec2(sp.x, sp.y), 22.f, IM_COL32(40, 40, 40, 255), 0, 1.f);
            }
        }
    }

    if (g_orbTargetMode == 1 && g_orbSelectedHero >= 0 && g_orbSelectedHero < (int)g_heroes.size()) {
        const auto& sel = g_heroes[g_orbSelectedHero];
        if (sel.addr && sel.health > 0.f) {
            // pulsing world-space ring under target
            float pulse = 0.6f + 0.4f * sinf(g_gameTime * 4.f);
            uint8_t pulseR = (uint8_t)(255 * pulse), pulseG = (uint8_t)(200 * pulse);
            DrawWorldCircle(dl, sel.pos, 80.f, IM_COL32(pulseR, pulseG, 0, 255), 2.5f, 0);
            DrawWorldCircle(dl, sel.pos, 85.f, IM_COL32((uint8_t)(255*pulse), (uint8_t)(100*pulse), 0, 255), 1.5f, 0);

            Vec2 sp;
            if (WorldToScreen(sel.pos, sp)) {
                ImU32 gold = IM_COL32(255, 200, 0, 255);
                ImU32 glow = IM_COL32((uint8_t)(255*pulse), (uint8_t)(160*pulse), 0, 255);
                ImU32 dark = IM_COL32(0, 0, 0, 255);
                float r = 36.f;
                // outer glow
                dl->AddCircle(ImVec2(sp.x, sp.y), r + 4, glow, 0, 4.f);
                // main ring
                dl->AddCircle(ImVec2(sp.x, sp.y), r, dark, 0, 4.f);
                dl->AddCircle(ImVec2(sp.x, sp.y), r, gold, 0, 2.5f);
                // crosshair lines
                dl->AddLine(ImVec2(sp.x - r - 8, sp.y), ImVec2(sp.x - r + 12, sp.y), gold, 2.5f);
                dl->AddLine(ImVec2(sp.x + r - 12, sp.y), ImVec2(sp.x + r + 8, sp.y), gold, 2.5f);
                dl->AddLine(ImVec2(sp.x, sp.y - r - 8), ImVec2(sp.x, sp.y - r + 12), gold, 2.5f);
                dl->AddLine(ImVec2(sp.x, sp.y + r - 12), ImVec2(sp.x, sp.y + r + 8), gold, 2.5f);
                // target name + "SELECTED" label
                char selBuf[96];
                snprintf(selBuf, sizeof(selBuf), ">> %s <<", sel.name);
                auto sz = ImGui::CalcTextSize(selBuf);
                float labelY = sp.y - r - 22;
                dl->AddRectFilled(ImVec2(sp.x - sz.x/2 - 4, labelY - 2), ImVec2(sp.x + sz.x/2 + 4, labelY + sz.y + 2), IM_COL32(20,20,20,255), 4.f);
                dl->AddRect(ImVec2(sp.x - sz.x/2 - 4, labelY - 2), ImVec2(sp.x + sz.x/2 + 4, labelY + sz.y + 2), gold, 4.f, 0, 1.f);
                dl->AddText(ImVec2(sp.x - sz.x/2, labelY), gold, selBuf);
            }
        }
    }

    if (g_orbDrawLastHit) {
        ULONGLONG now = GetTickCount64();
        float lastHitThresh = g_orbEffectiveAD * (std::clamp(g_orbLastHitBufferPct, 50, 100) / 100.f);
        for (const auto& m : g_minions) {
            if (!m.addr || m.team == g_localTeam || m.health <= 0.f || !m.visible) continue;
            float dx = g_localPos.x - m.pos.x, dz = g_localPos.z - m.pos.z;
            float d = sqrtf(dx*dx + dz*dz);
            float pred = OrbPredictMinionHp(m.addr, m.health, (d / 1800.f) * 1000.f + g_orbPingMs * 0.5f, now);
            if ((pred > 0.f ? pred : m.health) > lastHitThresh) continue;
            Vec2 sp;
            if (!WorldToScreen(m.pos, sp)) continue;
            if (sp.x < 0 || sp.x > g_screenW || sp.y < 0 || sp.y > g_screenH) continue;
            dl->AddCircleFilled(ImVec2(sp.x, sp.y - 10.f), 5.f, IM_COL32(255, 50, 50, 255));
            dl->AddCircle(ImVec2(sp.x, sp.y - 10.f), 5.f, IM_COL32(255, 255, 255, 255), 0, 1.f);
        }
    }

    if (g_orbMode > 0) {
        static const char* names[] = {"", "COMBO", "HARASS", "LANE CLEAR", "LAST HIT", "FLEE"};
        static const ImU32 colors[] = {0, IM_COL32(255,50,50,255), IM_COL32(255,180,0,255),
                          IM_COL32(0,200,255,255), IM_COL32(255,255,0,255), IM_COL32(0,255,100,255)};
        const char* txt = names[g_orbMode];
        auto sz = ImGui::CalcTextSize(txt);
        float cx = g_screenW / 2.f;
        float cy = (float)g_screenH - 60.f;
        dl->AddRectFilled(ImVec2(cx - sz.x/2 - 8, cy - 2), ImVec2(cx + sz.x/2 + 8, cy + sz.y + 2),
                          IM_COL32(20,20,20,255), 4.f);
        dl->AddText(ImVec2(cx - sz.x/2.f, cy), colors[g_orbMode], txt);
    }
}

static void DrawKeyDisplay()
{
    if (!g_keyDisplayOpen) return;

    ImGui::SetNextWindowSize(ImVec2(160, 200), ImGuiCond_FirstUseEver);
    ImGui::Begin("Keys", &g_keyDisplayOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);

    struct KeyEntry { const char* label; uint32_t vk; };
    const KeyEntry keys[] = {
        { "[Space] Combo",  VK_SPACE },
        { "[V] Lane Clear", 'V' },
        { "[C] Harass",     'C' },
        { "[X] Last Hit",   'X' },
        { "[Z] Flee",       'Z' },
    };

    bool kbReady = Keyboard::IsReady();
    for (const auto& k : keys) {
        bool down = kbReady && Keyboard::IsKeyDown(k.vk);
        ImVec4 col = down ? ImVec4(0.f, 1.f, 0.f, 1.f) : ImVec4(0.9f, 0.9f, 0.9f, 1.f);
        ImGui::TextColored(col, "%s", k.label);
    }

    if (!kbReady)
        ImGui::TextColored(ImVec4(1.f, 0.3f, 0.3f, 0.7f), "(no DMA keyboard)");

    ImGui::End();
}

// iOS-style toggle switch (public API only)
static bool Toggle(const char* label, bool* v) {
    float h = ImGui::GetFrameHeight() * 0.85f;
    float w = h * 1.75f;
    float radius = h * 0.5f;
    float knobR = radius * 0.78f;

    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImU32 bgCol = *v ? IM_COL32(88, 142, 255, 255) : IM_COL32(60, 60, 72, 255);
    dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), bgCol, radius);
    float knobX = *v ? (pos.x + w - radius) : (pos.x + radius);
    dl->AddCircleFilled(ImVec2(knobX, pos.y + radius), knobR, IM_COL32(240, 240, 245, 255));

    ImGui::InvisibleButton(label, ImVec2(w, h));
    bool pressed = ImGui::IsItemClicked();
    if (pressed) *v = !*v;

    ImGui::SameLine(0, 8);
    ImGui::TextUnformatted(label);
    return pressed;
}

// Section header with accent bar
static void SectionHeader(const char* label) {
    ImGui::Spacing();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + 3, pos.y + ImGui::GetTextLineHeight()), IM_COL32(88, 142, 255, 255), 1.5f);
    ImGui::Indent(10);
    ImGui::TextColored(ImVec4(0.88f, 0.88f, 0.92f, 1.0f), "%s", label);
    ImGui::Unindent(10);
    ImGui::Spacing();
}

// Status pill: colored rounded rect with text
static void StatusPill(const char* text, bool ok) {
    ImVec2 sz = ImGui::CalcTextSize(text);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float pad = 6.f;
    ImU32 bg = ok ? IM_COL32(30, 85, 45, 255) : IM_COL32(85, 30, 30, 255);
    ImU32 tx = ok ? IM_COL32(100, 220, 120, 255) : IM_COL32(240, 100, 100, 255);
    dl->AddRectFilled(ImVec2(pos.x, pos.y), ImVec2(pos.x + sz.x + pad*2, pos.y + sz.y + 4), bg, 4.f);
    dl->AddText(ImVec2(pos.x + pad, pos.y + 2), tx, text);
    ImGui::Dummy(ImVec2(sz.x + pad*2, sz.y + 4));
}

// Backward compat — old code still calls Chk in some places
static bool Chk(const char* label, bool* v) { return Toggle(label, v); }

static void DrawMenu()
{
    if (!g_menuOpen) return;

    ImGui::SetNextWindowSize(ImVec2(540, 480), ImGuiCond_FirstUseEver);
    ImGui::Begin(xorstr_("VeoxDMA"), &g_menuOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
    {
        ImVec2 pos = ImGui::GetWindowPos();
        ImVec2 sz  = ImGui::GetWindowSize();
        bool clamp = false;
        if (pos.x + sz.x < 50)  { pos.x = 0; clamp = true; }
        if (pos.y + sz.y < 50)  { pos.y = 0; clamp = true; }
        if (pos.x > g_screenW - 50) { pos.x = (float)g_screenW - sz.x; clamp = true; }
        if (pos.y > g_screenH - 50) { pos.y = (float)g_screenH - sz.y; clamp = true; }
        if (clamp) ImGui::SetWindowPos(pos);
    }

    // ── Title bar ──
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos();
        float ww = ImGui::GetWindowWidth();
        dl->AddRectFilled(wp, ImVec2(wp.x + ww, wp.y + 36), IM_COL32(25, 40, 72, 255), 8.f, ImDrawFlags_RoundCornersTop);
        dl->AddText(ImVec2(wp.x + 14, wp.y + 9), IM_COL32(88, 142, 255, 255), "VEOX");
        dl->AddText(ImVec2(wp.x + 52, wp.y + 9), IM_COL32(180, 180, 195, 255), "DMA");
        if (g_localChampName[0]) {
            char cb[80]; snprintf(cb, sizeof(cb), "%s  Lv%d", g_localChampName, g_localLevel);
            ImVec2 ts = ImGui::CalcTextSize(cb);
            dl->AddText(ImVec2(wp.x + ww - ts.x - 14, wp.y + 9), IM_COL32(140, 140, 160, 255), cb);
        }
        ImGui::Dummy(ImVec2(0, 28));
    }

    // ── Build key combo string once ──
    static char keyComboStr[2048] = {};
    static bool keyComboBuilt = false;
    if (!keyComboBuilt) {
        char* p = keyComboStr;
        for (int i = 0; i < g_keyOptionCount; i++) {
            int len = (int)strlen(g_keyOptions[i].label);
            memcpy(p, g_keyOptions[i].label, len); p += len; *p++ = '\0';
        }
        *p = '\0';
        keyComboBuilt = true;
    }

    if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_FittingPolicyScroll))
    {
        // ════════════════════════════════════════════
        //  COMBAT
        // ════════════════════════════════════════════
        if (ImGui::BeginTabItem("Combat"))
        {
            // Device status row
            {
                auto mState = Makcu::GetState();
                bool mOk = (mState == Makcu::State::Connected);
                bool kOk = Keyboard::IsReady();
                StatusPill(mOk ? "Makcu OK" : "Makcu OFF", mOk);
                ImGui::SameLine(0, 8);
                StatusPill(kOk ? "Keyboard OK" : "No Keyboard", kOk);
            }

            ImGui::Spacing();
            SectionHeader("Orbwalker");
            Toggle("Enabled", &g_orbEnabled);
            float sliderW = ImGui::GetContentRegionAvail().x * 0.55f;
            ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "Targeting");
            ImGui::SameLine(); ImGui::SetNextItemWidth(sliderW);
            ImGui::Combo("##target_mode", &g_orbTargetMode, "Mouse\0Target\0");
            ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "Attack Key");
            ImGui::SameLine(); ImGui::SetNextItemWidth(sliderW);
            ImGui::Combo("##atk_key", &g_orbAtkKeyIdx, keyComboStr);
            if (g_orbTargetMode == 1) {
                ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "Priority");
                ImGui::SameLine(); ImGui::SetNextItemWidth(sliderW);
                ImGui::Combo("##prio_mode", &g_orbPriorityMode, "Closest Enemy\0Lowest HP\0Most AD\0Most AP\0");
            }

            if (g_orbMode > 0) {
                static const char* mn[] = {"","COMBO","HARASS","CLEAR","LAST HIT","FLEE"};
                static const ImU32 mc[] = {0, IM_COL32(255,80,80,255), IM_COL32(255,180,50,255),
                    IM_COL32(50,200,255,255), IM_COL32(255,230,50,255), IM_COL32(80,255,130,255)};
                ImVec2 pos = ImGui::GetCursorScreenPos();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 ts = ImGui::CalcTextSize(mn[g_orbMode]);
                dl->AddRectFilled(pos, ImVec2(pos.x + ts.x + 16, pos.y + ts.y + 6), mc[g_orbMode] & 0x40FFFFFF, 4.f);
                dl->AddText(ImVec2(pos.x + 8, pos.y + 3), mc[g_orbMode], "%s", mn[g_orbMode]);
                ImGui::Dummy(ImVec2(ts.x + 16, ts.y + 6));
            }

            ImGui::Spacing();
            SectionHeader("Spells");
            Toggle("Q", &g_orbUseQ); ImGui::SameLine(0, 16);
            Toggle("W", &g_orbUseW); ImGui::SameLine(0, 16);
            Toggle("E", &g_orbUseE); ImGui::SameLine(0, 16);
            Toggle("R", &g_orbUseR);
            Toggle("Read Cooldowns", &g_orbReadCooldowns);

            if (g_orbReadCooldowns) {
                ImGui::Spacing();
                const char* sn[] = {"Q","W","E","R"};
                for (int s = 0; s < 4; s++) {
                    if (s > 0) ImGui::SameLine(0, 6);
                    if (g_spells[s].level > 0) {
                        StatusPill(sn[s], g_spells[s].ready);
                    } else {
                        ImGui::TextDisabled("[%s]", sn[s]);
                    }
                }
            }

            ImGui::Spacing();
            SectionHeader("Champion Combo");
            Toggle("Enabled##combo", &g_comboEnabled);
            if (g_localChampName[0]) {
                bool sup = ChampionCombo::Supports(g_localChampName);
                ImGui::SameLine(0, 12);
                StatusPill(sup ? "Profile" : "Generic", sup);
            }
            Toggle("Generic Fallback", &g_comboSettings.genericEnabled);
            Toggle("Generic Uses R", &g_comboSettings.genericUltimate);

            ImGui::Spacing();
            SectionHeader("Last Hit");
            Toggle("Only Killables (no push AA)", &g_orbLastHitOnly);
            ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "Hit Buffer");
            ImGui::SameLine(); ImGui::SetNextItemWidth(sliderW);
            ImGui::SliderInt("##lhbuf", &g_orbLastHitBufferPct, 70, 100, "%d%% AD");

            ImGui::Spacing();
            SectionHeader("Timing");
            Toggle("Auto Stats (DMA)", &g_orbAutoStats);
            if (g_orbAutoStats) {
                float totalAD = g_localBaseAD + g_localBonusAD;
                float baseAS = (g_localASScale > 0.3f && g_localASScale < 1.5f) ? g_localASScale : g_orbBaseAS;
                float totalAS = baseAS * g_localASMod;
                ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1),
                    "AD %.0f  |  AS %.3f  |  Range %.0f  |  Base %.3f", totalAD, totalAS, g_localAttackRange, baseAS);
            } else {
                ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "Atk Speed");
                ImGui::SameLine(); ImGui::SetNextItemWidth(sliderW);
                ImGui::SliderFloat("##man_as", &g_orbAttackSpeed, 0.3f, 3.0f, "%.2f");
                ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "Atk Damage");
                ImGui::SameLine(); ImGui::SetNextItemWidth(sliderW);
                ImGui::SliderFloat("##man_ad", &g_orbPlayerAD, 40.f, 500.f, "%.0f");
            }
            ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "Windup %%");
            ImGui::SameLine(); ImGui::SetNextItemWidth(sliderW);
            ImGui::SliderInt("##windup", &g_orbWindupPct, 1, 100, "%d%%");
            ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "Move Delay");
            ImGui::SameLine(); ImGui::SetNextItemWidth(sliderW);
            ImGui::SliderFloat("##movedelay", &g_orbMoveDelay, 20.f, 200.f, "%.0f ms");
            ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "Spell Gate");
            ImGui::SameLine(); ImGui::SetNextItemWidth(sliderW);
            ImGui::SliderInt("##spellgate", &g_orbSpellGateMs, 0, 500, "%d ms");
            ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "Click Spread");
            ImGui::SameLine(); ImGui::SetNextItemWidth(sliderW);
            ImGui::SliderInt("##click_spread", &g_orbClickSpread, 0, 100, "%d%%");
            Toggle("Ping Compensation##pingcomp", &g_orbPingComp);
            if (g_orbPingComp) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.5f,0.7f,0.5f,1), "(%.0f ms)", g_orbPingMs);
                ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "Timing Bias");
                ImGui::SameLine(); ImGui::SetNextItemWidth(sliderW);
                ImGui::SliderFloat("##timing_bias", &g_orbTimingBias, -100.f, 100.f, "%.0f ms");
            }

            ImGui::Spacing();
            SectionHeader("Cursor / Behavior");
            Toggle("Humanize Timing", &g_orbHumanize);
            ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "Cursor Policy");
            ImGui::SameLine(); ImGui::SetNextItemWidth(sliderW);
            ImGui::Combo("##curpol", &g_orbCursorPolicy, "Free (no aim move)\0Aim + Restore (smooth)\0");
            ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "Path Min");
            ImGui::SameLine(); ImGui::SetNextItemWidth(sliderW);
            ImGui::SliderInt("##pathmin", &g_orbPathMinMs, 10, 200, "%d ms");
            ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "Path Max");
            ImGui::SameLine(); ImGui::SetNextItemWidth(sliderW);
            ImGui::SliderInt("##pathmax", &g_orbPathMaxMs, 20, 300, "%d ms");
            if (g_orbPathMaxMs < g_orbPathMinMs) g_orbPathMaxMs = g_orbPathMinMs;
            ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "React Min");
            ImGui::SameLine(); ImGui::SetNextItemWidth(sliderW);
            ImGui::SliderInt("##reactmin", &g_orbReactMinMs, 0, 250, "%d ms");
            ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "React Jitter");
            ImGui::SameLine(); ImGui::SetNextItemWidth(sliderW);
            ImGui::SliderInt("##reactjit", &g_orbReactJitterMs, 0, 250, "%d ms");
            if (g_orbTargetMode == 1)
                Toggle("Click-Select In Mode", &g_orbClickSelectInMode);
            if (g_localChampName[0]) {
                std::string champKey = g_localChampName;
                auto wit = g_orbWindupPctChamp.find(champKey);
                bool hasOverride = (wit != g_orbWindupPctChamp.end());
                char ovLabel[96];
                snprintf(ovLabel, sizeof(ovLabel), "Per-champ Windup##ov_%s", g_localChampName);
                if (ImGui::Checkbox(ovLabel, &hasOverride)) {
                    if (hasOverride) g_orbWindupPctChamp[champKey] = std::clamp(g_orbWindupPct, 1, 100);
                    else g_orbWindupPctChamp.erase(champKey);
                }
                if (hasOverride) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "(%s)", g_localChampName);
                    ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "  Override %%");
                    ImGui::SameLine(); ImGui::SetNextItemWidth(sliderW);
                    int ov = std::clamp(g_orbWindupPctChamp[champKey], 1, 100);
                    char ovId[96]; snprintf(ovId, sizeof(ovId), "##windupov_%s", g_localChampName);
                    if (ImGui::SliderInt(ovId, &ov, 1, 100, "%d%%"))
                        g_orbWindupPctChamp[champKey] = ov;
                } else {
                    ImGui::TextDisabled("Uses global Windup %% (%d%%). Saved as orb_windup_champ_%s", g_orbWindupPct, g_localChampName);
                }
            }

            // Priority (target mode only)
            if (g_orbTargetMode == 1) {
                ImGui::Spacing();
                SectionHeader("Target Priority");
                if (g_orbSelectedAddr) {
                    for (const auto& h : g_heroes) {
                        if (h.addr == g_orbSelectedAddr) {
                            ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), "Locked: %s", h.name);
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Clear##lock")) { g_orbSelectedAddr = 0; g_orbSelectedHero = -1; }
                            break;
                        }
                    }
                }

                for (int i = 0; i < (int)g_heroes.size(); i++) {
                    const auto& h = g_heroes[i];
                    if (!h.addr || h.team == g_localTeam) continue;
                    ImGui::PushID(i);

                    auto* icon = GetChampIcon(h.name);
                    if (icon) {
                        ImVec2 cur = ImGui::GetCursorScreenPos();
                        ImGui::GetWindowDrawList()->AddImage((ImTextureID)icon, cur, ImVec2(cur.x+18, cur.y+18));
                        ImGui::Dummy(ImVec2(18, 18)); ImGui::SameLine();
                    }

                    bool dead = h.isDead || h.health <= 0.f;
                    bool sel = (h.addr == g_orbSelectedAddr);
                    ImVec4 nc = dead ? ImVec4(0.4f,0.4f,0.4f,1) : sel ? ImVec4(1,0.8f,0,1) : ImVec4(0.88f,0.88f,0.92f,1);
                    ImGui::TextColored(nc, "%-10s", h.name);

                    if (!dead) {
                        ImGui::SameLine();
                        if (sel) { if (ImGui::SmallButton("X##d")) { g_orbSelectedAddr = 0; g_orbSelectedHero = -1; } }
                        else     { if (ImGui::SmallButton("Lock")) { g_orbSelectedAddr = h.addr; g_orbSelectedHero = i; } }
                    }
                    ImGui::PopID();
                }
            }

            ImGui::EndTabItem();
        }

        // ════════════════════════════════════════════
        //  VISUALS
        // ════════════════════════════════════════════
        if (ImGui::BeginTabItem("Visuals"))
        {
            float sliderW = ImGui::GetContentRegionAvail().x * 0.55f;
            SectionHeader("Champion ESP");
            Toggle("Enable Drawings", &g_espEnabled);
            Toggle("Enemy Only", &g_espEnemyOnly);
            Toggle("Names", &g_espNames);
            Toggle("Health Bars", &g_espHealth);
            Toggle("Shields", &g_espShields);
            Toggle("Bounding Box", &g_espBox);
            Toggle("Visibility", &g_espVisStatus);
            Toggle("Last Known Position", &g_espLastPosition);

            ImGui::Spacing();
            SectionHeader("Orbwalker Visuals");
            Toggle("Attack Range Circle", &g_orbDrawRange);
            Toggle("Target Highlight", &g_orbDrawTarget);
            Toggle("Last-Hit Markers", &g_orbDrawLastHit);

            ImGui::Spacing();
            SectionHeader("Range Circles");
            Toggle("Enemy Attack Range", &g_espAttackRange);
            Toggle("Turret Range", &g_espTurretRange);
            if (g_espTurretRange) {
                ImGui::SetNextItemWidth(sliderW);
                ImGui::SliderFloat("##turr", &g_turretRange, 700.f, 1000.f, "%.0f units");
            }
            ImGui::SetNextItemWidth(sliderW);
            ImGui::Combo("##drawmode", &g_drawMode, "Outlined\0Filled\0");
            Toggle("Circle Glow", &g_drawGlow);

            ImGui::Spacing();
            SectionHeader("Minions");
            Toggle("Minion Health Bars", &g_espMinions);

            ImGui::Spacing();
            SectionHeader("Sidebar");
            Toggle("Enemy Sidebar", &g_sidebarEnabled);
            if (g_sidebarEnabled) {
                ImGui::SetNextItemWidth(sliderW);
                ImGui::Combo("##side", &g_sidebarSide, "Left\0Right\0");
            }

            ImGui::Spacing();
            SectionHeader("Minimap");
            Toggle("Enable Minimap", &g_minimapEnabled);
            if (g_minimapEnabled) {
                ImGui::SetNextItemWidth(sliderW);
                ImGui::SliderFloat("##mmsz", &g_minimapSize, 100.f, g_screenH * 0.4f, "Size: %.0f px");
                ImGui::SetNextItemWidth(sliderW);
                ImGui::SliderFloat("##mmx", &g_minimapOffX, 0.f, (float)g_screenW - g_minimapSize, "X: %.0f");
                ImGui::SetNextItemWidth(sliderW);
                ImGui::SliderFloat("##mmy", &g_minimapOffY, 0.f, (float)g_screenH - g_minimapSize, "Y: %.0f");
                ImGui::SetNextItemWidth(sliderW);
                ImGui::SliderFloat("##mmop", &g_minimapOpacity, 0.3f, 1.f, "Opacity: %.2f");
                if (ImGui::Button("Reset Position")) MinimapResetToGame();
            }

            ImGui::Spacing();
            SectionHeader("Colors");
            ImGui::ColorEdit4("Enemy##col", g_enemyColor, ImGuiColorEditFlags_NoInputs);
            ImGui::SameLine(0, 16);
            ImGui::ColorEdit4("Ally##col", g_allyColor, ImGuiColorEditFlags_NoInputs);

            ImGui::EndTabItem();
        }

        // ════════════════════════════════════════════
        //  TRACKER
        // ════════════════════════════════════════════
        if (ImGui::BeginTabItem("Tracker"))
        {
            SectionHeader("Auto Smite");
            Toggle("Enabled##smite", &g_smiteEnabled);
            if (g_smiteSlot >= 0) {
                ImGui::SameLine(0, 12);
                char sb[64]; snprintf(sb, sizeof(sb), "%s (%c)", g_smiteName, g_smiteSlot == 4 ? 'D' : 'F');
                StatusPill(sb, g_smiteReady);
                if (!g_smiteReady && g_smiteReadyAt > g_gameTime) {
                    ImGui::SameLine(0, 6);
                    ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "%.0fs", g_smiteReadyAt - g_gameTime);
                }
            }

            ImGui::Spacing();
            SectionHeader("Summoner Spells");
            Toggle("Track Summoners", &g_trackerSumms);
            if (g_trackerSumms) {
                for (int i = 0; i < (int)g_heroes.size() && i < 12; i++) {
                    const auto& h = g_heroes[i];
                    if (!h.addr || h.team == g_localTeam) continue;
                    auto& summs = g_heroSumms[i];

                    ImGui::PushID(200 + i);
                    ImVec4 statusCol = h.isDead ? ImVec4(0.4f,0.4f,0.4f,1) :
                        h.visible ? ImVec4(0.3f,0.85f,0.45f,1) : ImVec4(1.f,0.5f,0.2f,1);
                    ImGui::TextColored(statusCol, "%-12s", h.name);
                    ImGui::SameLine();

                    auto DrawSumm = [](const SummonerSpell& sp, float gt) {
                        if (!sp.name[0]) { ImGui::TextDisabled("?"); return; }
                        const char* sn = sp.name;
                        if (strncmp(sn, "Summoner", 8) == 0) sn += 8;
                        if (sp.ready) ImGui::TextColored(ImVec4(0.3f,0.85f,0.45f,1), "%s", sn);
                        else {
                            float cd = sp.readyAt - gt; if (cd < 0) cd = 0;
                            ImGui::TextColored(ImVec4(0.95f,0.35f,0.35f,1), "%s %.0fs", sn, cd);
                        }
                    };
                    DrawSumm(summs.d, g_gameTime);
                    ImGui::SameLine(0, 12);
                    DrawSumm(summs.f, g_gameTime);
                    ImGui::PopID();
                }
            }

            ImGui::Spacing();
            SectionHeader("Jungle Timers");
            Toggle("Track Camps", &g_trackerJungle);
            if (g_trackerJungle) {
                for (auto& camp : g_jungleCamps) {
                    ImGui::PushID(&camp);
                    if (camp.alive) {
                        ImGui::TextColored(ImVec4(0.3f,0.85f,0.45f,1), "%-20s", camp.display);
                        ImGui::SameLine(); StatusPill("Alive", true);
                    } else {
                        ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "%-20s", camp.display);
                        ImGui::SameLine();
                        if (camp.buffRespawnAt > 0.f || camp.killedAt > 0.f) {
                            float ra = camp.buffRespawnAt > 0.f ? camp.buffRespawnAt : camp.killedAt + camp.respawnTime;
                            float tl = ra - g_gameTime;
                            if (tl > 0.f) {
                                char tb[16]; snprintf(tb, sizeof(tb), "%d:%02d", (int)(tl/60.f), (int)tl % 60);
                                ImU32 col = camp.buffRespawnAt > 0.f ? IM_COL32(100,220,120,255) : IM_COL32(200,200,210,255);
                                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col), "%s", tb);
                            } else {
                                StatusPill("Spawning", true);
                            }
                        } else {
                            ImGui::TextDisabled("--:--");
                        }
                    }
                    ImGui::PopID();
                }
            }

            ImGui::EndTabItem();
        }

        // ════════════════════════════════════════════
        //  DEVICE
        // ════════════════════════════════════════════
        if (ImGui::BeginTabItem("Device"))
        {
            float sliderW = ImGui::GetContentRegionAvail().x * 0.55f;
            SectionHeader("Makcu Controller");
            {
                auto state = Makcu::GetState();
                bool connected = (state == Makcu::State::Connected);
                StatusPill(InputDevice::StateString(), connected);

                if (state == Makcu::State::Failed) {
                    char err[240]; Makcu::GetError(err, sizeof(err));
                    if (err[0]) ImGui::TextColored(ImVec4(0.95f,0.35f,0.35f,1), "%s", err);
                }
            }

            ImGui::Spacing();
            ImGui::SetNextItemWidth(sliderW);
            ImGui::InputText("##port", g_makcuPort, sizeof(g_makcuPort));
            ImGui::SameLine(); ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "COM Port");
            ImGui::SameLine();
            if (ImGui::Button("Detect")) {
                auto ports = InputDevice::ListPorts();
                for (auto& p : ports) {
                    if (p.find("CH343") != std::string::npos || p.find("CH340") != std::string::npos) {
                        auto c = p.find("COM"); auto e = p.find(")", c);
                        if (c != std::string::npos && e != std::string::npos)
                            snprintf(g_makcuPort, sizeof(g_makcuPort), "%s", p.substr(c, e-c).c_str());
                        break;
                    }
                }
            }

            ImGui::SetNextItemWidth(sliderW);
            ImGui::SliderInt("##baud", &g_makcuBaud, 115200, 4000000, "%d baud");

            ImGui::Spacing();
            {
                auto state = Makcu::GetState();
                float bw = ImGui::GetContentRegionAvail().x * 0.5f;
                if (state == Makcu::State::Connected || state == Makcu::State::Connecting) {
                    if (ImGui::Button("Disconnect", ImVec2(bw, 32))) InputDevice::Disconnect();
                } else {
                    if (ImGui::Button("Connect", ImVec2(bw, 32)))
                        InputDevice::Connect(g_makcuPort[0] ? g_makcuPort : nullptr, g_makcuBaud);
                }
            }

            if (InputDevice::IsConnected()) {
                ImGui::Spacing();
                SectionHeader("Quick Test");
                if (ImGui::Button("Move(5,5)", ImVec2(90, 28))) InputDevice::Move(5,5);
                ImGui::SameLine();
                if (ImGui::Button("L-Click", ImVec2(90, 28))) InputDevice::LeftClick();
                ImGui::SameLine();
                if (ImGui::Button("R-Click", ImVec2(90, 28))) InputDevice::RightClick();
            }

            ImGui::Spacing();
            SectionHeader("Fuser Overlay");
            ImGui::Text("%d monitor(s) detected", (int)g_overlayMonitors.size());
            ImGui::SameLine();
            if (ImGui::SmallButton("Refresh")) DetectMonitors();

            for (int i = 0; i < (int)g_overlayMonitors.size(); ++i) {
                const RECT& r = g_overlayMonitors[i];
                char label[128];
                snprintf(label, sizeof(label), "%ldx%ld##mon%d", r.right - r.left, r.bottom - r.top, i);
                bool sel = (i == g_monitorIndex);
                if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.345f, 0.557f, 1.0f, 0.35f));
                if (ImGui::Button(label, ImVec2(100, 28))) g_monitorIndex = i;
                if (sel) ImGui::PopStyleColor();
                if (i + 1 < (int)g_overlayMonitors.size()) ImGui::SameLine();
            }

            ImGui::Spacing();
            ImGui::ColorEdit3("BG Color", g_bgColor, ImGuiColorEditFlags_NoInputs);
            Toggle("Show FPS", &g_fpsShow);
            Toggle("Show Key Display", &g_keyDisplayOpen);

            ImGui::EndTabItem();
        }

        // ════════════════════════════════════════════
        //  SCRIPTS
        // ════════════════════════════════════════════
        if (ImGui::BeginTabItem("Scripts"))
        {
            if (ImGui::Button("Reload All", ImVec2(100, 28))) LuaScanScripts();

            if (g_scripts.empty()) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "No scripts found.");
            }

            ImGui::Spacing();
            for (int i = 0; i < (int)g_scripts.size(); i++) {
                ImGui::PushID(i);
                auto& sc = g_scripts[i];

                Toggle(sc.name.c_str(), &sc.enabled);
                ImGui::SameLine(0, 8);
                if (sc.hasError) {
                    StatusPill("ERR", false);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", sc.errorMsg);
                } else if (sc.L) {
                    StatusPill("OK", true);
                }
                ImGui::SameLine(0, 8);
                if (ImGui::SmallButton("Reload")) LuaLoadScript(sc);

                ImGui::PopID();
            }
            ImGui::EndTabItem();
        }

        // ════════════════════════════════════════════
        //  CONFIG
        // ════════════════════════════════════════════
        if (ImGui::BeginTabItem("Config"))
        {
            float sliderW = ImGui::GetContentRegionAvail().x * 0.55f;
            SectionHeader("Save / Load");
            ImGui::SetNextItemWidth(sliderW);
            ImGui::InputText("##cfgname", g_configName, sizeof(g_configName));

            ImGui::Spacing();
            if (ImGui::Button("Save", ImVec2(100, 30))) SaveConfig(g_configName);
            ImGui::SameLine();
            if (ImGui::Button("Load", ImVec2(100, 30))) LoadConfig(g_configName);
            ImGui::SameLine();
            if (ImGui::Button("Save Default", ImVec2(120, 30))) SaveConfig("default");

            if (g_configStatus[0]) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.3f,0.85f,0.45f,1), "%s", g_configStatus);
            }

            ImGui::Spacing();
            SectionHeader("Saved Configs");
            auto configs = ListConfigs();
            for (auto& name : configs) {
                bool active = (strcmp(g_configName, name.c_str()) == 0);
                if (active) ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.345f, 0.557f, 1.0f, 0.25f));
                if (ImGui::Selectable(name.c_str(), active)) {
                    strncpy(g_configName, name.c_str(), sizeof(g_configName) - 1);
                    LoadConfig(g_configName);
                }
                if (active) ImGui::PopStyleColor();
            }
            if (configs.empty()) ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "No configs yet.");

            ImGui::Spacing();
            SectionHeader("Info");
            ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "Game Time: %.1f", g_gameTime);
            ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "Heroes: %d  Minions: %d  Turrets: %d",
                (int)g_heroes.size(), (int)g_minions.size(), (int)g_turrets.size());
            ImGui::TextColored(ImVec4(0.55f,0.55f,0.62f,1), "Base: 0x%llX  PID: %d", (uint64_t)g_base, TargetProcess.current_process.PID);

#ifndef NDEBUG
            ImGui::Spacing();
            SectionHeader("Debug Tools");
            if (ImGui::Button("Scan SpellBook") && g_localAddr) {
                uintptr_t sb = g_localAddr + Offsets::GameObject::SpellBook;
                VX_LOG("\n[SpellScan] SB=0x%llX  gameTime=%.1f\n", (uint64_t)sb, g_gameTime);
                VX_LOG("--- pointer scan ---\n");
                for (int off = 0x0; off <= 0xA00; off += 0x8) {
                    uintptr_t ptr = TargetProcess.Read<uintptr_t>(sb + off);
                    if (ptr > 0x100000 && ptr < 0x7FFFFFFFFFFF) {
                        int lv = TargetProcess.Read<int>(ptr + 0x28);
                        if (lv >= 0 && lv <= 6) {
                            float f30 = TargetProcess.Read<float>(ptr + 0x30);
                            float f74 = TargetProcess.Read<float>(ptr + 0x74);
                            VX_LOG("  +0x%03X -> 0x%llX lv=%d [+0x30]=%.2f [+0x74]=%.2f\n",
                                off, (uint64_t)ptr, lv, f30, f74);
                        }
                    }
                }
                VX_LOG("[SpellScan] Done.\n\n");
            }
            ImGui::SameLine();
            if (ImGui::Button("Find Slots") && g_localAddr) {
                uintptr_t sb = g_localAddr + Offsets::GameObject::SpellBook;
                VX_LOG("\n[SlotFinder] SB=0x%llX\n", (uint64_t)sb);
                for (int off = 0x0; off <= 0x1000; off += 0x8) {
                    uintptr_t p0 = TargetProcess.Read<uintptr_t>(sb + off);
                    if (p0 < 0x10000 || p0 > 0x7FFFFFFFFFFF) continue;
                    int lv0 = TargetProcess.Read<int>(p0 + 0x28);
                    if (lv0 < 0 || lv0 > 6) continue;
                    int good = 1;
                    for (int s = 1; s < 4; s++) {
                        uintptr_t ps = TargetProcess.Read<uintptr_t>(sb + off + s * 8);
                        if (ps < 0x10000 || ps > 0x7FFFFFFFFFFF) break;
                        int lvs = TargetProcess.Read<int>(ps + 0x28);
                        if (lvs < 0 || lvs > 6) break;
                        good++;
                    }
                    if (good < 3) continue;
                    VX_LOG("  ** HIT SB+0x%03X **\n", off);
                    for (int s = 0; s < 6; s++) {
                        uintptr_t ps = TargetProcess.Read<uintptr_t>(sb + off + s * 8);
                        if (!ps) continue;
                        int lv = TargetProcess.Read<int>(ps + 0x28);
                        float cd = TargetProcess.Read<float>(ps + 0x74);
                        uintptr_t info = TargetProcess.Read<uintptr_t>(ps + 0x130);
                        uintptr_t np = info ? TargetProcess.Read<uintptr_t>(info + 0x28) : 0;
                        char nm[80]{}; if (np) TargetProcess.Read(np, nm, 79);
                        VX_LOG("    [%d] lv=%d cd=%.1f %s\n", s, lv, cd, nm);
                    }
                }
                VX_LOG("[SlotFinder] Done.\n\n");
            }
#endif

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.4f,0.4f,0.47f,1), "INSERT to toggle menu");

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static LRESULT CALLBACK WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    switch (msg)
    {
    case WM_SIZE:
        if (g_device && wParam != SIZE_MINIMIZED)
        {
            CleanupRenderTarget();
            g_swapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
            g_screenW = LOWORD(lParam);
            g_screenH = HIWORD(lParam);
        }
        return 0;
    case WM_DISPLAYCHANGE:
        DetectMonitors();
        ApplyOverlayMonitor(hWnd);
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_INSERT) g_menuOpen = !g_menuOpen;
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

static bool GameMemoryReadable()
{
    return g_base && TargetProcess.Read<uint16_t>(g_base) == 0x5A4D;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    SetUnhandledExceptionFilter(CrashHandler);
    InitPaths();
    strncpy(g_scriptDir, xorstr_("C:\\VeoxDMA\\Scripts"), MAX_PATH - 1);
    EnsureDirs();
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Gdiplus::GdiplusStartupInput gdipInput;
    Gdiplus::GdiplusStartup(&g_gdipToken, &gdipInput, nullptr);

    AllocConsole();
    FILE* fDummy;
    freopen_s(&fDummy, "CONIN$",  "r", stdin);
    freopen_s(&fDummy, "CONOUT$", "w", stderr);
    freopen_s(&fDummy, "CONOUT$", "w", stdout);
    printf(xorstr_("VeoxDMA - League of Legends\n"));
    printf(xorstr_("===========================\n"));

    Auth::g_authenticated = true;
    printf(xorstr_("[*] KeyAuth disabled\n"));

    FetchDDVersion();
    InitSinCos();
    LoadConfig("default");
    LuaInit();
    LuaScanScripts();

    if (!TargetProcess.Init(xorstr_("League of Legends.exe")))
    {
        printf(xorstr_("[!] Failed to find League of Legends. Is the game running?\n"));
        printf(xorstr_("Press Enter to exit...\n"));
        getchar();
        return -1;
    }
    TargetProcess.FixCr3();

    g_base = TargetProcess.current_process.base_address;
    VX_LOG("[+] Base address: 0x%llX\n", (uint64_t)g_base);
    VX_LOG("MZ check: 0x%llX (want ...5a4d)\n", (uint64_t)TargetProcess.Read<uint64_t>(g_base));


    if (Keyboard::InitKeyboard())
        VX_LOG("[+] Keyboard initialized (game PC key state via DMA)\n");
    else
        VX_LOG("[!] Keyboard init failed - keybinds will not work\n");

    ImGui_ImplWin32_EnableDpiAwareness();

    WNDCLASSEX wc{};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance      = hInstance;
    wc.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName  = L"OverlayWnd";
    RegisterClassEx(&wc);

    DetectMonitors();
    HWND hWnd = CreateWindowEx(WS_EX_APPWINDOW, wc.lpszClassName, L"Overlay",
        WS_POPUP, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
        NULL, NULL, hInstance, NULL);
    if (!hWnd) return -1;

    if (!CreateDeviceD3D(hWnd))
    {
        CleanupDeviceD3D();
        return -1;
    }
    ApplyOverlayMonitor(hWnd);
    ShowWindow(hWnd, nCmdShow);

    {
        const RECT& r = g_overlayMonitors[g_monitorIndex];
        g_screenW = r.right - r.left;
        g_screenH = r.bottom - r.top;
        g_cursorX = g_screenW / 2.f;
        g_cursorY = g_screenH / 2.f;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags &= ~(ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad);
    // ── Modern dark theme ──
    {
        ImGuiStyle& s = ImGui::GetStyle();
        s.WindowRounding    = 8.f;
        s.ChildRounding     = 6.f;
        s.FrameRounding     = 6.f;
        s.PopupRounding     = 6.f;
        s.GrabRounding      = 4.f;
        s.TabRounding       = 6.f;
        s.ScrollbarRounding = 6.f;
        s.WindowPadding     = ImVec2(14, 14);
        s.FramePadding      = ImVec2(10, 6);
        s.ItemSpacing       = ImVec2(10, 8);
        s.ItemInnerSpacing  = ImVec2(8, 4);
        s.ScrollbarSize     = 12.f;
        s.GrabMinSize       = 10.f;
        s.WindowBorderSize  = 0.f;
        s.ChildBorderSize   = 0.f;
        s.TabBorderSize     = 0.f;
        s.WindowTitleAlign  = ImVec2(0.5f, 0.5f);

        ImVec4* c = s.Colors;
        ImVec4 bg      (0.082f, 0.082f, 0.098f, 1.0f);
        ImVec4 surface (0.110f, 0.110f, 0.133f, 1.0f);
        ImVec4 raised  (0.145f, 0.145f, 0.173f, 1.0f);
        ImVec4 border  (0.200f, 0.200f, 0.235f, 0.5f);
        ImVec4 accent  (0.345f, 0.557f, 1.000f, 1.0f); // blue accent
        ImVec4 accentDim(0.250f, 0.400f, 0.750f, 1.0f);
        ImVec4 accentBg(0.200f, 0.300f, 0.600f, 0.25f);
        ImVec4 textMain(0.92f, 0.92f, 0.95f, 1.0f);
        ImVec4 textDim (0.55f, 0.55f, 0.62f, 1.0f);
        ImVec4 green   (0.30f, 0.85f, 0.45f, 1.0f);
        ImVec4 red     (0.95f, 0.35f, 0.35f, 1.0f);

        c[ImGuiCol_Text]                  = textMain;
        c[ImGuiCol_TextDisabled]          = textDim;
        c[ImGuiCol_WindowBg]              = bg;
        c[ImGuiCol_ChildBg]               = ImVec4(0,0,0,0);
        c[ImGuiCol_PopupBg]               = surface;
        c[ImGuiCol_Border]                = border;
        c[ImGuiCol_FrameBg]               = surface;
        c[ImGuiCol_FrameBgHovered]        = raised;
        c[ImGuiCol_FrameBgActive]         = raised;
        c[ImGuiCol_TitleBg]               = bg;
        c[ImGuiCol_TitleBgActive]         = bg;
        c[ImGuiCol_TitleBgCollapsed]      = bg;
        c[ImGuiCol_MenuBarBg]             = surface;
        c[ImGuiCol_ScrollbarBg]           = bg;
        c[ImGuiCol_ScrollbarGrab]         = raised;
        c[ImGuiCol_ScrollbarGrabHovered]  = accentDim;
        c[ImGuiCol_ScrollbarGrabActive]   = accent;
        c[ImGuiCol_CheckMark]             = accent;
        c[ImGuiCol_SliderGrab]            = accentDim;
        c[ImGuiCol_SliderGrabActive]      = accent;
        c[ImGuiCol_Button]                = raised;
        c[ImGuiCol_ButtonHovered]         = accentBg;
        c[ImGuiCol_ButtonActive]          = accentDim;
        c[ImGuiCol_Header]                = accentBg;
        c[ImGuiCol_HeaderHovered]         = ImVec4(accent.x, accent.y, accent.z, 0.35f);
        c[ImGuiCol_HeaderActive]          = ImVec4(accent.x, accent.y, accent.z, 0.50f);
        c[ImGuiCol_Separator]             = border;
        c[ImGuiCol_SeparatorHovered]      = accent;
        c[ImGuiCol_SeparatorActive]       = accent;
        c[ImGuiCol_ResizeGrip]            = ImVec4(0,0,0,0);
        c[ImGuiCol_ResizeGripHovered]     = accentDim;
        c[ImGuiCol_ResizeGripActive]      = accent;
        c[ImGuiCol_Tab]                   = surface;
        c[ImGuiCol_TabHovered]            = ImVec4(accent.x, accent.y, accent.z, 0.40f);
        c[ImGuiCol_TabActive]             = raised;
        c[ImGuiCol_TabUnfocused]          = surface;
        c[ImGuiCol_TabUnfocusedActive]    = raised;
    }
    ImGui_ImplWin32_Init(hWnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    VX_LOG("[+] Overlay ready. Press INSERT to toggle menu.\n");

    DWORD lastCacheTick = 0;

    MSG msg{};
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }

        ApplyOverlayMonitor(hWnd);

        DWORD now = GetTickCount();
        if (now - lastCacheTick > (DWORD)(40 + randInt(0, 25)))
        {
            if (!GameMemoryReadable())
            {
                static DWORD lastReattach = 0;
                static DWORD retryInterval = 2000;
                static bool printed = false;
                if (!printed) {
                    VX_LOG("[!] Game memory unreadable, waiting for game...\n");
                    printed = true;
                    g_heroes.clear();
                    g_minions.clear();
                    g_turrets.clear();
                    g_gameTime = 0.f;
                }
                if (now - lastReattach > retryInterval) {
                    lastReattach = now;
                    if (TargetProcess.Reattach() && GameMemoryReadable())
                    {
                        g_base = TargetProcess.current_process.base_address;
                        g_viewPortScanned = false;
                        printed = false;
                        retryInterval = 2000;
                        VX_LOG("[+] Game found! (base 0x%llX)\n", (uint64_t)g_base);
                    }
                    else if (retryInterval < 10000) {
                        retryInterval += 2000;
                    }
                }
            }
            else
            {
                CacheGameState();
                OrbwalkerTick();
                LuaOnTick();
            }
            lastCacheTick = now;

            // Runtime integrity check (~every 10s given typical tick rate)
            static DWORD lastIntegrityCheck = 0;
            if (now - lastIntegrityCheck > 10000) {
                lastIntegrityCheck = now;
#if defined(NDEBUG) && !defined(VX_NO_AUTH)
                if (!Auth::RuntimeIntegrityCheck()) {
                    VX_LOG("[!] Runtime integrity check failed. Exiting.\n");
                    PostQuitMessage(0);
                }
#endif
            }
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        if (g_fpsShow)
        {
            char fpsBuf[32];
            snprintf(fpsBuf, sizeof(fpsBuf), "FPS: %.0f", io.Framerate);
            ImGui::GetBackgroundDrawList()->AddText(ImVec2(10.f, 10.f), IM_COL32(0, 255, 0, 255), fpsBuf);
        }

        DrawESP();
        DrawMinimap();
        OrbwalkerDraw();
        LuaOnDraw();
        DrawKeyDisplay();
        DrawMenu();

        ImGui::Render();

        const float clear[4] = { g_bgColor[0], g_bgColor[1], g_bgColor[2], 1.f };
        g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_context->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swapChain->Present(1, 0);
    }

    LuaShutdown();
    for (auto& [k, v] : g_iconCache) { if (v) v->Release(); }
    g_iconCache.clear();
    InputDevice::Shutdown();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    if (g_gdipToken) Gdiplus::GdiplusShutdown(g_gdipToken);
    CoUninitialize();
    DestroyWindow(hWnd);
    return (int)msg.wParam;
}
