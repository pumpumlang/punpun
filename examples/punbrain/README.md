# PunBrain

PunBrain is a clean-room, in-game stronghold calculator built as a PunPun showcase. It is inspired by the workflow of Ninjabrain Bot, but does not copy Ninjabrain Bot source code or branding.

The project is split deliberately:

- `main.pp` is the PunPun core executable. It owns the stronghold ring prior, robust angular likelihood, candidate posterior, blind calculator, calibration command, and machine-readable JSON protocol.
- `fabric/` is a thin Minecraft client companion. It captures Eye of Ender trajectories, renders the HUD, handles hotkeys/clipboard, and exposes a localhost API.

## Why the eye measurement is different

Traditional external calculators usually receive an F3 coordinate and a crosshair angle. PunBrain can do better when the Fabric companion is installed:

1. It detects the actual `EyeOfEnderEntity` in the client world.
2. It samples the entity's native double-precision X/Z position over multiple game ticks.
3. It discards edge samples when enough data exists.
4. It fits the whole horizontal trajectory with iteratively reweighted orthogonal regression.
5. Huber-style weights suppress one-frame/tick outliers.
6. The residual RMS and track length produce a per-throw angular uncertainty instead of assuming every throw is equally precise.
7. The fitted eye trajectory itself is the ray origin, so player movement and boat movement do not corrupt the measurement.
8. The PunPun core uses a Gaussian likelihood with a broad outlier component, so one bad measurement cannot zero an otherwise coherent posterior.

That replaces "subpixel crosshair correction" with direct game-state measurement. No OCR and no F3 coordinate rounding are involved.

## Current feature surface

- Any-number-of-eyes stronghold prediction
- Vanilla eight-ring stronghold prior
- Posterior candidate probabilities and top-five candidates
- Per-throw uncertainty
- Robust outlier handling
- One-eye ring intersections
- Pairwise multi-eye triangulation neighborhoods
- Live distance and facing angle while travelling
- Suggested lateral position for the next throw
- Automatic Eye of Ender detection
- Automatic exact eye-coordinate ingestion
- Automatic result clipboard copy above a configurable confidence threshold
- Manual result copy hotkey
- Boat/moving-player-safe eye rays
- Lock/reset/toggle hotkeys
- In-game HUD with dark/light/speedrun themes
- Local HTTP API for integrations/OBS tooling
- Blind-coordinate helper
- Robust calibration command (median/MAD based)
- Divine-sector protocol command for combining fossil-derived constraints in tooling
- All stronghold rings are supported, so long-distance / All Advancements-style routes are not restricted to ring zero

The Fabric module currently targets Minecraft Java **1.21.1** with Yarn mappings. The PunPun core is not tied to a client version.

## Build the PunPun core

From the repository root:

```sh
make compiler
./build/ppc check examples/punbrain/main.pp
./build/ppc build --backend=c -O2 -o punbrain-core examples/punbrain/main.pp
./punbrain-core version
```

The core uses PunPun's documented `@inject->c` FFI for numeric parsing and the low-level probability kernel. The executable entrypoint, build, packaging, and protocol remain a normal PunPun program.

Copy the resulting binary to one of these locations:

```text
.minecraft/config/punbrain/punbrain-core       Linux/macOS
.minecraft/config/punbrain/punbrain-core.exe   Windows
```

Alternatively set `corePath` in `.minecraft/config/punbrain/config.json`.

## Build the Fabric companion

Java 21 is required for Minecraft 1.21.1.

```sh
cd examples/punbrain/fabric
gradle build
```

Put `build/libs/punbrain-fabric-0.1.0.jar` in the Minecraft `mods` directory with Fabric Loader and Fabric API.

Pinned development dependencies:

```text
Minecraft   1.21.1
Yarn        1.21.1+build.3
Loader      0.16.14
Fabric API  0.116.17+1.21.1
Loom        1.8.13
```

## In game

Throw an Eye of Ender normally. PunBrain detects it automatically, waits for a usable trajectory, fits the eye line, sends all recorded throws to the PunPun core, and updates the HUD.

Default keys:

| Key | Action |
|---|---|
| `P` | reset measurements |
| `O` | toggle HUD |
| `K` | copy predicted stronghold coordinates |
| `L` | lock/unlock the current calculation |

The generated config controls HUD position, theme, model sigma, boat sigma, sample count, core path, API port, and automatic clipboard threshold.

## Core CLI

```text
punbrain-core solve x z bearing sigma [x z bearing sigma ...]
punbrain-core blind netherX netherZ
punbrain-core divine x z bearing [spread]
punbrain-core calibrate errorDeg [errorDeg ...]
punbrain-core version
```

Minecraft yaw convention is used: `0` points +Z, `-90` points +X, and `90` points -X.

## Local API

The Fabric companion binds only to loopback by default:

```text
GET /v1/status
GET /v1/reset
GET /v1/eye?x=...&z=...&bearing=...&sigma=...
GET /v1/blind?x=...&z=...
```

Default address: `127.0.0.1:52533`.

`/v1/eye` is useful for external tools or manual fallback input. Normal in-game usage does not need it because eye capture is automatic.

## Relationship to Ninjabrain Bot

Ninjabrain Bot is a separate GPL-3.0 project by its own contributors. PunBrain is a fresh implementation based on public Minecraft mechanics and the general calculator workflow. No Ninjabrain source is vendored into this directory. If code is later copied or adapted from Ninjabrain Bot rather than independently implemented, the licensing for that derivative work must be handled accordingly.

## Precision work still worth measuring

The companion deliberately reports fit residuals and sample counts so real-run telemetry can be used to calibrate defaults. The next useful accuracy experiment is to compare trajectory windows (for example ticks 2-8 vs. 2-14), boat vs. foot throws, and network conditions against known stronghold positions, then tune `baseModelSigma` from those residual distributions rather than guessing.
