# VeoxDMA League

VeoxDMA is a Windows C++/DirectX 11 DMA-based League of Legends overlay/tooling project (ImGui UI). It uses DMA memory access (VMM/LeechCore) and Makcu-based HID input for orbwalking/input actions.

> This repository is for development/maintenance of the project. Build outputs, local configs, and machine-specific deployment paths are environment-dependent.

## Features

- DMA-driven game state reading (heroes, minions, turrets, local player, spells/cooldowns where available)
- ESP/visual overlays (health, names, ranges, visibility/last position, turret/minion visuals)
- Minimap/sidebar/tracker utilities
- Orbwalker with multiple modes:
  - Combo, Harass, Lane Clear, Last Hit, Flee
  - Mouse and Target targeting modes
  - Ping compensation + timing bias
  - Auto stats (DMA) or manual AD/AS
  - Champion combo integration hooks (profiles/generic fallback)
  - Makcu attack/spell/move input

## Orbwalker (current implementation)

Core logic lives in `leagueoflegends/main/main.cpp` (`OrbwalkerTick`, `OrbwalkerDraw`).

### Behavior
- **Aim + Restore (smooth):** target-mode actions can move the cursor on a bounded eased path, issue the action, and restore (`orb_cursor_policy=1`). `Free` mode avoids cursor aim moves (`orb_cursor_policy=0`).
- **Attack timing:** AS/windup-based windows with ping compensation, bounded windup, session drift/jitter, and reaction floor.
- **Last hit prediction:** sampled minion HP delta + travel/ping proxy predicts HP at hit time; `Only Killables` prevents push AAs in last-hit/fallback cases.
- **Targeting:** click-select, sticky target, priority modes, and `g_orbPriority` weighting.
- **Spell weaving:** champion combo when available, generic Q/W/E/R fallback with spell gate/cooldown checks.
- **Per-champion windup overrides:** config-driven (`orb_windup_champ_<ChampionName>=1-100`).

### Key config settings
Defaults are intended to load from `C:\VeoxDMA\Configs\default.cfg`. The app auto-creates required VeoxDMA folders when saving/config paths run.

```ini
# Orbwalker
orb_enabled=1
orb_auto_stats=1
orb_windup=35
orb_move_delay=50
orb_spell_gate=130
orb_ping_comp=1
orb_timing_bias=-15

# Cursor / behavior
orb_cursor_policy=1          # 0=Free, 1=Aim + Restore (smooth)
orb_path_min=35
orb_path_max=90
orb_react_min=25
orb_react_jitter=55
orb_humanize=1
orb_click_select_mode=1

# Last hit
orb_lasthit_only=1
orb_lasthit_buffer=95        # % of effective AD

# Per-champion windup override example
# orb_windup_champ_Jinx=16
```

## Build

Open `leagueoflegends.sln` in Visual Studio and build:

- Configuration: `Release`
- Platform: `x64`

Example CLI build:

```bat
msbuild leagueoflegends.sln /m /p:Configuration=Release /p:Platform=x64
```

Typical output: `x64/Release/VeoxDMA.exe`.

## Runtime dependencies

The executable expects the required DMA/runtime DLLs alongside it (from `Release/` in this repo layout), commonly including:

- `vmm.dll`, `vmmyara.dll`
- `leechcore.dll`, `leechcore_device_rawtcp.dll`, `leechcore_driver.dll`
- `FTD3XX.dll`, `FTD3XXWU.dll`
- `dbghelp.dll`, `symsrv.dll`, `tinylz4.dll`, `vcruntime140.dll`

Makcu/input device setup and game/DMA environment must be valid for orbwalker input to function.

## Local paths

The project uses:

- `C:\VeoxDMA\Configs\` — config files (`default.cfg`, saved profiles)
- `C:\VeoxDMA\Logs\`
- `C:\VeoxDMA\Cache\`
- `C:\VeoxDMA\Scripts\`

Folders are created automatically when save/config directory initialization runs.

## Auth / development toggles

`Auth` is integrated in the startup path. The source includes development toggles:

- runtime marker file: `C:\VeoxDMA\noauth`
- environment variable: `VEOX_NO_AUTH=1`
- compile-time define: `VX_NO_AUTH` (project Release config may define it)

Use only in builds/environments you control.

## Deployment notes

For multi-machine/DMA setups, deploy the rebuilt `VeoxDMA.exe` and required DLLs together after source changes. Config changes can be deployed by updating `C:\VeoxDMA\Configs\default.cfg` on the target machine.

See `Tools/deploy_orbwalker.txt` for a short deployment checklist.

## Status

Active internal codebase with iterative orbwalker/timing/input improvements. Tune windup, reaction, path timing, buffer, and attack keybind to your champion, ping, and in-game input settings.
