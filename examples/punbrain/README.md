# PunBrain

PunBrain is a standalone stronghold calculator implemented as a PunPun program. It is a PunPun-native reimplementation of the Ninjabrain Bot workflow and probability model, not a relay to a running Ninjabrain Bot process.

The application owns its eye measurements, stronghold prior, Bayesian conditioning, prediction state, clipboard watcher, GUI, blind helper, calibration, and local API. Ninjabrain Bot does not need to be installed or running.

> **License:** PunBrain is a modified/translated work based on Ninjabrain Bot's GPLv3 source and behavior. PunBrain is therefore distributed under GPLv3. See `NOTICE.md` and `LICENSE`.

## What is implemented

- Minecraft eight-ring stronghold distribution and ring counts.
- Biome-snapping-smoothed radial prior based on Ninjabrain Bot's approximated density model.
- Ray-local prior construction around the first eye.
- Any-number-of-eyes Bayesian conditioning.
- Ninjabrain-style horizontal-angle correction for client packet rounding.
- Crosshair correction.
- Player-position-imprecision contribution to angular variance.
- Ranked stronghold chunks with certainty, distance, and direction.
- Pre-1.19 `(8,8)` and 1.19+ `(0,0)` stronghold coordinate modes.
- Automatic `F3+C` clipboard ingestion.
- Manual `x z yaw [correction increments]` entry.
- Undo, reset, lock, and ±0.01° subpixel correction.
- Standard-deviation configuration and calibration command.
- Blind-coordinate helper.
- Standalone self-hosted GUI.
- JSON CLI output suitable for scripts and overlays.
- Local GUI/state HTTP endpoint.

The default inference path is the compatibility path. Experimental precision work should be kept separate so a convenience feature cannot silently change the calculator's statistical meaning.

## Build

From the PunPun repository root:

```sh
make compiler
./build/ppc check examples/punbrain/main.pp
./build/ppc build --backend=c -O2 -o punbrain examples/punbrain/main.pp
./punbrain selftest
```

The program uses PunPun 1.3's documented `@inject->c` native interop for the performance-sensitive probability kernel and operating-system integration. The program entry point and distributable source remain `main.pp`.

## Run the GUI

```sh
./punbrain gui
```

PunBrain starts its own local UI at `http://127.0.0.1:52534` and opens it in your default browser. This is PunBrain's own server and calculator, not Ninjabrain Bot's API.

While the process is running, throw an Eye of Ender, aim directly at it, and press `F3+C`. PunBrain watches the system clipboard for Minecraft's copied `/execute in minecraft:overworld ...` command, parses the player position and yaw, applies the configured correction, records the eye, and recomputes the posterior.

On Wayland Linux the automatic clipboard path prefers `wl-paste`; install `wl-clipboard` if it is missing. X11 fallbacks are `xclip` and `xsel`. Windows uses PowerShell's clipboard command and macOS uses `pbpaste`.

## Useful commands

```text
punbrain gui
punbrain watch
punbrain solve x z yaw sigma [x z yaw sigma ...]
punbrain parse "<F3+C command>"
punbrain blind netherX netherZ
punbrain calibrate angularError [angularError ...]
punbrain selftest
punbrain version
```

`watch` gives the automatic `F3+C` workflow without opening the GUI. Each accepted eye prints the current state as JSON.

## Accuracy and compatibility

PunBrain ports the important statistical structure rather than using simple line intersection:

1. The first eye creates a ray-local candidate prior.
2. The prior uses the vanilla stronghold rings and Ninjabrain-style smoothed radial density.
3. Each eye contributes a Gaussian angular likelihood.
4. The variance includes the configured eye-measurement standard deviation and player-position imprecision.
5. Candidate weights are normalized and ranked as posterior probabilities.

The port deliberately includes Ninjabrain Bot's tiny horizontal-angle correction, including the `0.000824 * sin(alpha + 45°)` packet-rounding compensation. This matters once measurements become precise enough that tiny systematic errors stop being tiny in practice.

This is a new port, not a claim that every floating-point result is bit-for-bit identical to a particular Ninjabrain Bot release. Regression tests should be expanded with known Ninjabrain test vectors as the port matures.

## Speedrun rules

PunBrain being functionally similar to an allowed calculator does **not** automatically make this new executable approved for submitted Minecraft speedruns. The current rules/legal-tool list must be checked independently. See `SPEEDRUN_RULES.md`.

For practice, development, and testing, PunBrain is fully standalone. No Ninjabrain API server is involved.

## Project layout

```text
examples/punbrain/
├── main.pp            PunPun application, calculator core, clipboard integration, and GUI server
├── README.md          build and usage guide
├── NOTICE.md          upstream attribution and modification notice
├── LICENSE            GNU GPL v3
└── SPEEDRUN_RULES.md  submission/approval boundary
```
