#pragma once

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include <string>
#include <vector>
#include <filesystem>
#include <cstdio>
#include <algorithm>

struct LuaScript {
    std::string path;
    std::string name;
    lua_State*  L       = nullptr;
    bool        enabled = true;
    bool        hasError = false;
    char        errorMsg[256] = {};
};

static char g_scriptDir[MAX_PATH];

static std::vector<LuaScript> g_scripts;
static int lua_game_time(lua_State* L) {
    lua_pushnumber(L, g_gameTime);
    return 1;
}

static int lua_game_screenW(lua_State* L) {
    lua_pushinteger(L, g_screenW);
    return 1;
}

static int lua_game_screenH(lua_State* L) {
    lua_pushinteger(L, g_screenH);
    return 1;
}

static int lua_game_cursorX(lua_State* L) {
    lua_pushnumber(L, g_cursorX);
    return 1;
}

static int lua_game_cursorY(lua_State* L) {
    lua_pushnumber(L, g_cursorY);
    return 1;
}

static int lua_player_pos(lua_State* L) {
    lua_pushnumber(L, g_localPos.x);
    lua_pushnumber(L, g_localPos.y);
    lua_pushnumber(L, g_localPos.z);
    return 3;
}

static int lua_player_team(lua_State* L) {
    lua_pushinteger(L, g_localTeam);
    return 1;
}

static int lua_player_health(lua_State* L) {
    for (const auto& h : g_heroes) {
        if (h.addr == g_localAddr) {
            lua_pushnumber(L, h.health);
            return 1;
        }
    }
    lua_pushnumber(L, 0);
    return 1;
}

static int lua_player_maxHealth(lua_State* L) {
    for (const auto& h : g_heroes) {
        if (h.addr == g_localAddr) {
            lua_pushnumber(L, h.maxHealth);
            return 1;
        }
    }
    lua_pushnumber(L, 0);
    return 1;
}

static int lua_player_mana(lua_State* L) {
    lua_pushnumber(L, g_localMana);
    return 1;
}

static int lua_player_maxMana(lua_State* L) {
    lua_pushnumber(L, g_localMaxMana);
    return 1;
}

static int lua_player_level(lua_State* L) {
    lua_pushinteger(L, g_localLevel);
    return 1;
}

static int lua_player_attackRange(lua_State* L) {
    lua_pushnumber(L, g_localAttackRange);
    return 1;
}

static int lua_player_baseAD(lua_State* L) {
    lua_pushnumber(L, g_localBaseAD);
    return 1;
}

static int lua_player_bonusAD(lua_State* L) {
    lua_pushnumber(L, g_localBonusAD);
    return 1;
}

static int lua_player_totalAD(lua_State* L) {
    lua_pushnumber(L, g_localBaseAD + g_localBonusAD);
    return 1;
}

static int lua_player_attackSpeed(lua_State* L) {
    lua_pushnumber(L, g_localASMod);
    return 1;
}

static int lua_heroes_count(lua_State* L) {
    lua_pushinteger(L, (int)g_heroes.size());
    return 1;
}

static void lua_pushHero(lua_State* L, int idx) {
    if (idx < 0 || idx >= (int)g_heroes.size()) {
        lua_pushnil(L);
        return;
    }
    const auto& h = g_heroes[idx];
    lua_createtable(L, 0, 16);

    lua_pushnumber(L, h.pos.x);      lua_setfield(L, -2, "x");
    lua_pushnumber(L, h.pos.y);      lua_setfield(L, -2, "y");
    lua_pushnumber(L, h.pos.z);      lua_setfield(L, -2, "z");
    lua_pushnumber(L, h.health);     lua_setfield(L, -2, "health");
    lua_pushnumber(L, h.maxHealth);  lua_setfield(L, -2, "maxHealth");
    lua_pushnumber(L, h.shield);     lua_setfield(L, -2, "shield");
    lua_pushnumber(L, h.magicShield);lua_setfield(L, -2, "magicShield");
    lua_pushnumber(L, h.armor);      lua_setfield(L, -2, "armor");
    lua_pushnumber(L, h.magicResist);lua_setfield(L, -2, "magicResist");
    lua_pushnumber(L, h.attackRange);lua_setfield(L, -2, "attackRange");
    lua_pushnumber(L, h.baseAD);     lua_setfield(L, -2, "baseAD");
    lua_pushnumber(L, h.bonusAD);    lua_setfield(L, -2, "bonusAD");
    lua_pushnumber(L, h.mana);       lua_setfield(L, -2, "mana");
    lua_pushnumber(L, h.maxMana);    lua_setfield(L, -2, "maxMana");
    lua_pushinteger(L, h.level);     lua_setfield(L, -2, "level");
    lua_pushinteger(L, h.team);      lua_setfield(L, -2, "team");
    lua_pushboolean(L, h.visible);   lua_setfield(L, -2, "visible");
    lua_pushboolean(L, h.isDead);    lua_setfield(L, -2, "dead");
    lua_pushstring(L, h.name);       lua_setfield(L, -2, "name");
    lua_pushboolean(L, h.team != g_localTeam); lua_setfield(L, -2, "isEnemy");
}

static int lua_heroes_get(lua_State* L) {
    int idx = (int)luaL_checkinteger(L, 1) - 1;
    lua_pushHero(L, idx);
    return 1;
}

static int lua_heroes_enemies(lua_State* L) {
    lua_newtable(L);
    int n = 0;
    for (int i = 0; i < (int)g_heroes.size(); i++) {
        if (!g_heroes[i].addr || g_heroes[i].team == g_localTeam) continue;
        lua_pushHero(L, i);
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}

static int lua_heroes_allies(lua_State* L) {
    lua_newtable(L);
    int n = 0;
    for (int i = 0; i < (int)g_heroes.size(); i++) {
        if (!g_heroes[i].addr || g_heroes[i].team != g_localTeam) continue;
        lua_pushHero(L, i);
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}

static int lua_minions_count(lua_State* L) {
    lua_pushinteger(L, (int)g_minions.size());
    return 1;
}

static int lua_minions_get(lua_State* L) {
    int idx = (int)luaL_checkinteger(L, 1) - 1;
    if (idx < 0 || idx >= (int)g_minions.size()) { lua_pushnil(L); return 1; }
    const auto& m = g_minions[idx];
    lua_createtable(L, 0, 8);
    lua_pushnumber(L, m.pos.x);     lua_setfield(L, -2, "x");
    lua_pushnumber(L, m.pos.y);     lua_setfield(L, -2, "y");
    lua_pushnumber(L, m.pos.z);     lua_setfield(L, -2, "z");
    lua_pushnumber(L, m.health);    lua_setfield(L, -2, "health");
    lua_pushnumber(L, m.maxHealth); lua_setfield(L, -2, "maxHealth");
    lua_pushinteger(L, m.team);     lua_setfield(L, -2, "team");
    lua_pushboolean(L, m.visible);  lua_setfield(L, -2, "visible");
    lua_pushboolean(L, m.isDead);   lua_setfield(L, -2, "dead");
    lua_pushboolean(L, m.team != g_localTeam); lua_setfield(L, -2, "isEnemy");
    return 1;
}

static int lua_spells_get(lua_State* L) {
    int slot = (int)luaL_checkinteger(L, 1) - 1;
    if (slot < 0 || slot > 3) { lua_pushnil(L); return 1; }
    const auto& s = g_spells[slot];
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, s.level);    lua_setfield(L, -2, "level");
    lua_pushnumber(L, s.readyAt);   lua_setfield(L, -2, "readyAt");
    lua_pushnumber(L, s.totalCD);   lua_setfield(L, -2, "totalCD");
    lua_pushboolean(L, s.ready);    lua_setfield(L, -2, "ready");
    return 1;
}

static int lua_spells_isReady(lua_State* L) {
    int slot = (int)luaL_checkinteger(L, 1) - 1;
    if (slot < 0 || slot > 3) { lua_pushboolean(L, false); return 1; }
    lua_pushboolean(L, g_spells[slot].ready);
    return 1;
}

static int lua_action_castSpell(lua_State* L) {
    int slot = (int)luaL_checkinteger(L, 1) - 1;
    if (slot < 0 || slot > 3) return 0;
    static const uint8_t hidCodes[] = {0x14, 0x1A, 0x08, 0x15}; // Q W E R
    InputDevice::KeyPress(hidCodes[slot]);
    return 0;
}

static int lua_action_moveCursor(lua_State* L) {
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);
    MoveCursorTo(x, y);
    return 0;
}

static int lua_action_rightClick(lua_State* L) {
    (void)L;
    InputDevice::RightClick();
    return 0;
}

static int lua_action_leftClick(lua_State* L) {
    (void)L;
    InputDevice::LeftClick();
    return 0;
}

static int lua_action_pressKey(lua_State* L) {
    unsigned int code = (unsigned int)luaL_checkinteger(L, 1);
    InputDevice::KeyPress(code);
    return 0;
}

static int lua_action_keyDown(lua_State* L) {
    unsigned int code = (unsigned int)luaL_checkinteger(L, 1);
    InputDevice::KeyDown(code);
    return 0;
}

static int lua_action_keyUp(lua_State* L) {
    unsigned int code = (unsigned int)luaL_checkinteger(L, 1);
    InputDevice::KeyUp(code);
    return 0;
}

static int lua_action_attackMove(lua_State* L) {
    (void)L;
    InputDevice::KeyDown(0x04); // A
    InputDevice::KeyUp(0x04);
    InputDevice::LeftClick();
    return 0;
}

static int lua_draw_line(lua_State* L) {
    float x1 = (float)luaL_checknumber(L, 1);
    float y1 = (float)luaL_checknumber(L, 2);
    float x2 = (float)luaL_checknumber(L, 3);
    float y2 = (float)luaL_checknumber(L, 4);
    int r = (int)luaL_checkinteger(L, 5);
    int g = (int)luaL_checkinteger(L, 6);
    int b = (int)luaL_checkinteger(L, 7);
    float thick = (float)luaL_optnumber(L, 8, 1.0);
    ImGui::GetBackgroundDrawList()->AddLine(
        ImVec2(x1,y1), ImVec2(x2,y2), IM_COL32(r,g,b,255), thick);
    return 0;
}

static int lua_draw_rect(lua_State* L) {
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);
    float w = (float)luaL_checknumber(L, 3);
    float h = (float)luaL_checknumber(L, 4);
    int r = (int)luaL_checkinteger(L, 5);
    int g = (int)luaL_checkinteger(L, 6);
    int b = (int)luaL_checkinteger(L, 7);
    bool filled = lua_toboolean(L, 8);
    auto dl = ImGui::GetBackgroundDrawList();
    ImU32 col = IM_COL32(r,g,b,255);
    if (filled) dl->AddRectFilled(ImVec2(x,y), ImVec2(x+w,y+h), col);
    else        dl->AddRect(ImVec2(x,y), ImVec2(x+w,y+h), col);
    return 0;
}

static int lua_draw_circle(lua_State* L) {
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);
    float rad = (float)luaL_checknumber(L, 3);
    int r = (int)luaL_checkinteger(L, 4);
    int g = (int)luaL_checkinteger(L, 5);
    int b = (int)luaL_checkinteger(L, 6);
    bool filled = lua_toboolean(L, 7);
    int segs = (int)luaL_optinteger(L, 8, 0);
    auto dl = ImGui::GetBackgroundDrawList();
    ImU32 col = IM_COL32(r,g,b,255);
    if (filled) dl->AddCircleFilled(ImVec2(x,y), rad, col, segs);
    else        dl->AddCircle(ImVec2(x,y), rad, col, segs);
    return 0;
}

static int lua_draw_text(lua_State* L) {
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);
    const char* text = luaL_checkstring(L, 3);
    int r = (int)luaL_checkinteger(L, 4);
    int g = (int)luaL_checkinteger(L, 5);
    int b = (int)luaL_checkinteger(L, 6);
    ImGui::GetBackgroundDrawList()->AddText(ImVec2(x,y), IM_COL32(r,g,b,255), text);
    return 0;
}

static int lua_draw_w2s(lua_State* L) {
    Vec3 w;
    w.x = (float)luaL_checknumber(L, 1);
    w.y = (float)luaL_checknumber(L, 2);
    w.z = (float)luaL_checknumber(L, 3);
    Vec2 s;
    bool vis = WorldToScreen(w, s);
    lua_pushnumber(L, s.x);
    lua_pushnumber(L, s.y);
    lua_pushboolean(L, vis);
    return 3;
}

static int lua_util_dist(lua_State* L) {
    float dx = (float)(luaL_checknumber(L, 1) - luaL_checknumber(L, 4));
    float dz = (float)(luaL_checknumber(L, 3) - luaL_checknumber(L, 6));
    lua_pushnumber(L, sqrtf(dx*dx + dz*dz));
    return 1;
}

static int lua_util_print(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    printf("[Lua] %s\n", msg);
    return 0;
}
static void LuaRegisterAPI(lua_State* L)
{
    // game.*
    static const luaL_Reg gameLib[] = {
        {"time",     lua_game_time},
        {"screenW",  lua_game_screenW},
        {"screenH",  lua_game_screenH},
        {"cursorX",  lua_game_cursorX},
        {"cursorY",  lua_game_cursorY},
        {NULL, NULL}
    };
    luaL_newlib(L, gameLib);
    lua_setglobal(L, "game");

    // player.*
    static const luaL_Reg playerLib[] = {
        {"pos",         lua_player_pos},
        {"team",        lua_player_team},
        {"health",      lua_player_health},
        {"maxHealth",   lua_player_maxHealth},
        {"mana",        lua_player_mana},
        {"maxMana",     lua_player_maxMana},
        {"level",       lua_player_level},
        {"attackRange", lua_player_attackRange},
        {"baseAD",      lua_player_baseAD},
        {"bonusAD",     lua_player_bonusAD},
        {"totalAD",     lua_player_totalAD},
        {"attackSpeed", lua_player_attackSpeed},
        {NULL, NULL}
    };
    luaL_newlib(L, playerLib);
    lua_setglobal(L, "player");

    // heroes.*
    static const luaL_Reg heroesLib[] = {
        {"count",   lua_heroes_count},
        {"get",     lua_heroes_get},
        {"enemies", lua_heroes_enemies},
        {"allies",  lua_heroes_allies},
        {NULL, NULL}
    };
    luaL_newlib(L, heroesLib);
    lua_setglobal(L, "heroes");

    // minions.*
    static const luaL_Reg minionsLib[] = {
        {"count", lua_minions_count},
        {"get",   lua_minions_get},
        {NULL, NULL}
    };
    luaL_newlib(L, minionsLib);
    lua_setglobal(L, "minions");

    // spells.*
    static const luaL_Reg spellsLib[] = {
        {"get",     lua_spells_get},
        {"isReady", lua_spells_isReady},
        {NULL, NULL}
    };
    luaL_newlib(L, spellsLib);
    lua_setglobal(L, "spells");

    // action.*
    static const luaL_Reg actionLib[] = {
        {"castSpell",  lua_action_castSpell},
        {"moveCursor", lua_action_moveCursor},
        {"rightClick", lua_action_rightClick},
        {"leftClick",  lua_action_leftClick},
        {"pressKey",   lua_action_pressKey},
        {"keyDown",    lua_action_keyDown},
        {"keyUp",      lua_action_keyUp},
        {"attackMove", lua_action_attackMove},
        {NULL, NULL}
    };
    luaL_newlib(L, actionLib);
    lua_setglobal(L, "action");

    // draw.*
    static const luaL_Reg drawLib[] = {
        {"line",          lua_draw_line},
        {"rect",          lua_draw_rect},
        {"circle",        lua_draw_circle},
        {"text",          lua_draw_text},
        {"worldToScreen", lua_draw_w2s},
        {NULL, NULL}
    };
    luaL_newlib(L, drawLib);
    lua_setglobal(L, "draw");

    // util.*
    static const luaL_Reg utilLib[] = {
        {"dist",  lua_util_dist},
        {"print", lua_util_print},
        {NULL, NULL}
    };
    luaL_newlib(L, utilLib);
    lua_setglobal(L, "util");

    lua_newtable(L);
    lua_pushinteger(L, 0x14); lua_setfield(L, -2, "Q");
    lua_pushinteger(L, 0x1A); lua_setfield(L, -2, "W");
    lua_pushinteger(L, 0x08); lua_setfield(L, -2, "E");
    lua_pushinteger(L, 0x15); lua_setfield(L, -2, "R");
    lua_pushinteger(L, 0x07); lua_setfield(L, -2, "D");
    lua_pushinteger(L, 0x09); lua_setfield(L, -2, "F");
    lua_pushinteger(L, 0x04); lua_setfield(L, -2, "A");
    lua_pushinteger(L, 0x16); lua_setfield(L, -2, "S");
    lua_pushinteger(L, 0x2C); lua_setfield(L, -2, "SPACE");
    lua_pushinteger(L, 0x29); lua_setfield(L, -2, "ESC");
    lua_pushinteger(L, 0x3A); lua_setfield(L, -2, "F1");
    lua_pushinteger(L, 0x3B); lua_setfield(L, -2, "F2");
    lua_pushinteger(L, 0x3C); lua_setfield(L, -2, "F3");
    lua_pushinteger(L, 0x3D); lua_setfield(L, -2, "F4");
    lua_pushinteger(L, 0x3E); lua_setfield(L, -2, "F5");
    lua_setglobal(L, "KEY");
}
static bool LuaLoadScript(LuaScript& script)
{
    if (script.L) {
        lua_close(script.L);
        script.L = nullptr;
    }
    script.hasError = false;
    script.errorMsg[0] = 0;

    lua_State* L = luaL_newstate();
    if (!L) {
        snprintf(script.errorMsg, sizeof(script.errorMsg), "Failed to create Lua state");
        script.hasError = true;
        return false;
    }

    luaL_openlibs(L);
    LuaRegisterAPI(L);

    if (luaL_dofile(L, script.path.c_str()) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        snprintf(script.errorMsg, sizeof(script.errorMsg), "%.255s", err ? err : "unknown error");
        script.hasError = true;
        lua_close(L);
        return false;
    }

    script.L = L;
    return true;
}

static void LuaUnloadScript(LuaScript& script)
{
    if (script.L) {
        lua_close(script.L);
        script.L = nullptr;
    }
}

static void LuaCallCallback(LuaScript& script, const char* funcName)
{
    if (!script.L || !script.enabled || script.hasError) return;

    lua_getglobal(script.L, funcName);
    if (!lua_isfunction(script.L, -1)) {
        lua_pop(script.L, 1);
        return;
    }

    if (lua_pcall(script.L, 0, 0, 0) != LUA_OK) {
        const char* err = lua_tostring(script.L, -1);
        snprintf(script.errorMsg, sizeof(script.errorMsg), "%.255s", err ? err : "runtime error");
        script.hasError = true;
        lua_pop(script.L, 1);
    }
}

static void LuaInit()
{
    std::filesystem::create_directories(g_scriptDir);
}

static void LuaScanScripts()
{
    // Unload all
    for (auto& s : g_scripts) LuaUnloadScript(s);
    g_scripts.clear();

    if (!std::filesystem::exists(g_scriptDir)) return;

    for (const auto& entry : std::filesystem::directory_iterator(g_scriptDir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".lua") continue;
        LuaScript sc;
        sc.path = entry.path().string();
        sc.name = entry.path().stem().string();
        g_scripts.push_back(std::move(sc));
    }

    std::sort(g_scripts.begin(), g_scripts.end(),
        [](const LuaScript& a, const LuaScript& b) { return a.name < b.name; });

    for (auto& s : g_scripts) {
        LuaLoadScript(s);
    }
}

static void LuaShutdown()
{
    for (auto& s : g_scripts) LuaUnloadScript(s);
    g_scripts.clear();
}

static void LuaOnTick()
{
    for (auto& s : g_scripts) LuaCallCallback(s, "OnTick");
}

static void LuaOnDraw()
{
    for (auto& s : g_scripts) LuaCallCallback(s, "OnDraw");
}
