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
- LAN payloads now include shared damage bursts so deathmatch hits reconcile across peers, plus local respawn timers that reset health/ammo on the arena spawn.
- Baked nav points per arena steer zombies through weighted meshes for more varied routes, while the HUD adds hit-confirm markers and a compact killfeed for multiplayer.
- LAN frag/assist events mirror killfeed entries for all peers and keep team deathmatch scores aligned, including late-join bursts.
- Light cover chunks per arena and safe respawn picks keep lanes protected while spectators drift above spawn until they rejoin.

## Building
1. Install Raylib development headers/libraries (e.g., `sudo apt install libraylib-dev` or build from source).
2. Run `make` to produce `build/u8_fps`.

### Syncing with `main` when your branch conflicts
- If you just want your local branch to match the latest `main` and do **not** need your local edits, hard-reset to the remote tip:
  ```bash
  git fetch origin
  git reset --hard origin/main
  ```
- If a merge is already in progress and you only want the remote (“theirs”) version of every conflicting file, finish the merge by taking theirs everywhere:
  ```bash
  git checkout --theirs .
  git add .
  git commit -m "Resolve conflicts by taking remote"
  ```
- If you need to keep local edits, resolve file-by-file with your diff tool or `git checkout --ours/--theirs <file>`, then `git add` each resolved file and `git commit`.
- Seeing `+` and `-` markers in a diff viewer? They are just Git’s before/after notation and are **not** part of the saved file—you do not delete them in the editor. Conflict markers look like `<<<<<<<`/`=======`/`>>>>>>>`; those must be removed or resolved before committing.
- Seeing prompts like **Accept Current / Accept Incoming / Accept Both** (common in VS Code)?
  - **Current change** = your branch content. Choose this to keep your local edits.
  - **Incoming change** = the branch you are merging (often `main`). Choose this to take the newest remote code.
  - **Both** = keep both blocks and reconcile any overlap manually.
  - If you want everything from `main` and none of your local changes across many files, repeatedly choose **Accept Incoming** or, faster, run:
    ```bash
    git checkout --theirs .
    git add .
    git commit -m "Resolve conflicts by taking remote"
    ```
    This applies the newest remote content everywhere in one shot so you do not have to click through dozens of conflict blocks.

## Running
- Launch with `./build/u8_fps` after building.
- Add `--zombies` to jump straight into the Zombies prototype loop, or `--team` to bias the lobby toward team deathmatch before you spawn (default is multiplayer FFA).
- Main menu: navigate buttons with arrow keys and Enter/Space. Pick Multiplayer or Zombies, flip FFA/Teams, swap your team, change arena, toggle audio/checksum/flashlight/dither, save layouts, edit your name, then press Start.
- Controls: WASD to move, mouse to look, `Q` to cycle weapons, left mouse to fire, `E` to use perk/wall-buy/mystery box props in Zombies (revive requires a nearby peer), ESC or window close to exit.
- The prototype disables the mouse cursor; use Alt+Tab if needed to regain focus.
- Zombies economy: earn cash/score from kills, spend on perks (blue/teal/lime), wall ammo (red), or the mystery box (gold). Right mouse performs a melee weaken that shares bounty cash with peers when assists land.
- Multiplayer fragging: free-for-all tracks your frags/deaths, while team deathmatch syncs a team bit over LAN so name tags and HUD rows reflect Blue/Gold squads.
- Flashlight cone now uses layered falloff and the dither overlay deepens with on-screen depth for a grounded PS1 aesthetic.

### Arena presets and overrides
- Default presets: `Courtyard`, `Hangar`, and `Corridors` each ship with perk, ammo, and box spots tuned for handheld-readable routes.
- Saving/overrides: pressing `P` writes `layout_<arena>.txt` in the project root (one prop per line: `kind x y z`). On launch, any matching file overrides the baked-in layout so routes can be themed without recompiling.

## Next steps
- Make cover layouts loadable alongside perk/prop overrides so arenas can be rethemed without code changes.
- Let LAN peers acknowledge received frag/assist events to prevent duplicate feed spam during high packet loss.
- Add lightweight gore/impact sprites and per-weapon kick sounds to deepen hit feedback without hurting performance.
