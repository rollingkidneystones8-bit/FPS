# U8 FPS prototype

Low-poly, PS1/PS2-inspired LAN FPS prototype targeted at ultra-lean handheld hardware. Core goals include hitscan combat, five weapons with distinct handling, and zombie-focused wave play with perks, wall buys, a mystery box, and boss encounters.

## Current state
- Minimal Raylib-based arena scene for rapid iteration on movement and rendering.
- Fixed low internal resolution (320x180) scaled up for crisp visuals while keeping GPU load low.
- Simple first-person controller with WASD + mouse look, recoil, and lightweight HUD overlay.
- PS1/PS2-inspired level dressing with snapped vertex placement for retro edges and simple prop clusters.
- Standalone Zombies mode with waves, hitscan combat, five placeholder weapons, ammo, perks (quickfire/speed/revive), wall-buy ammo, and a multi-roll mystery box.
- Experimental LAN heartbeat that advertises player positions, weapon, ammo, health, perks, and downed status for up to eight peers, with in-world name tags.
- LAN peers smooth through packet loss, use compressed/quantized packets with an optional checksum toggle, and can be renamed via the lobby name bar while broadcasting cash/score and revive state.
- LAN peers smooth through packet loss, use compressed/quantized packets with an optional checksum toggle, share cash/score deltas for assists/revives, and resend the latest snapshot when new peers appear.
- Peers now include join ages for late-join catch-up bursts, plus shared bounty pips when assist cash/score land.
- Menu-style lobby lets you set name/audio/checksum before spawning in and swap between Multiplayer (FFA/Teams) and Zombies while cycling arenas.
- Zombies now include sprinters and spitters alongside bosses, with PS1-style wobble, telegraph glow cues, spit trails, and dissolving corpses. Melee weaken assists share bounty with peers.
- HUD shows cooldown pips plus audio toggle status for quick readability on handheld, plus cash/score and peer perk readouts. Video toggles include a layered flashlight cone and optional depth-aware dithering.
- Multiple arena presets with PS1-style prop grids (perks, wall ammo, mystery box) can be cycled in the lobby; current layouts can be saved/overridden via simple text presets.
- Multiplayer sandbox now supports free-for-all or team deathmatch scoring with lightweight frag tracking and team bits synced across the LAN payload.

## Building
1. Install Raylib development headers/libraries (e.g., `sudo apt install libraylib-dev` or build from source).
2. Run `make` to produce `build/u8_fps`.

## Running
- Launch with `./build/u8_fps` after building.
- Add `--zombies` to jump straight into the Zombies prototype loop, or `--team` to bias the lobby toward team deathmatch before you spawn (default is multiplayer FFA).
- Controls: WASD to move, mouse to look, `Q` to cycle weapons, left mouse to fire, `E` to use perk/wall-buy/mystery box props in Zombies (revive requires a nearby peer), ESC or window close to exit.
- The prototype disables the mouse cursor; use Alt+Tab if needed to regain focus.
- New lobby: edit your name (Enter), toggle audio (M) or checksum (C), flip flashlight (F) and dither (V), swap arenas with arrow keys, save current prop layout with `P`, cycle between Multiplayer and Zombies with TAB, toggle FFA/Teams with `G`, swap your team with `H` (when in team deathmatch), then press Space to spawn.
- Zombies economy: earn cash/score from kills, spend on perks (blue/teal/lime), wall ammo (red), or the mystery box (gold). Right mouse performs a melee weaken that shares bounty cash with peers when assists land.
- Multiplayer fragging: free-for-all tracks your frags/deaths, while team deathmatch syncs a team bit over LAN so name tags and HUD rows reflect Blue/Gold squads.
- Flashlight cone now uses layered falloff and the dither overlay deepens with on-screen depth for a grounded PS1 aesthetic.

### Arena presets and overrides
- Default presets: `Courtyard`, `Hangar`, and `Corridors` each ship with perk, ammo, and box spots tuned for handheld-readable routes.
- Saving/overrides: pressing `P` writes `layout_<arena>.txt` in the project root (one prop per line: `kind x y z`). On launch, any matching file overrides the baked-in layout so routes can be themed without recompiling.

## Next steps
- Harden multiplayer damage reconciliation over LAN (shared raycasts or damage bursts) and add lightweight respawn timers for local players.
- Bake simple nav meshes per arena preset to improve zombie pathing variety and co-op routing.
- Add hit confirmation sprites/killfeed that respect the handheld HUD scale for both FFA and TDM.
