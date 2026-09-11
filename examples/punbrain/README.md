# PunBrain Legal Mode

PunBrain Legal Mode is a deliberately rule-conservative PunPun showcase for Minecraft Java speedrunning. It does **not** replace Ninjabrain Bot's approved calculator during leaderboard runs. Instead, it connects to Ninjabrain Bot's documented local HTTP API and displays or relays only the data that Ninjabrain Bot already supplies.

This version was redesigned after reviewing Minecraft Java Edition Speedrunning rules v7. The earlier experimental Fabric/direct-eye-telemetry prototype has been removed from the distributable.

## What Legal Mode does

- Connects only to Ninjabrain Bot's local API at `127.0.0.1:52533`.
- Supports the API's `stronghold`, `all-advancements`, `blind`, `divine`, `boat`, `information-messages`, `version`, and `ping` endpoints.
- Includes a PunPun CLI relay that prints the selected Ninjabrain API response without deriving new predictions.
- Includes an external always-on-top overlay that formats fields supplied by Ninjabrain Bot for easier viewing while playing.
- Leaves stronghold calculations, eye processing, corrections, probabilities, and player-state interpretation to Ninjabrain Bot.

## What Legal Mode deliberately does not do

- No Fabric mod and no Fabric API dependency.
- No Minecraft memory or entity inspection.
- No direct `EyeOfEnderEntity` trajectory capture.
- No OCR, screen scraping, or audio analysis.
- No simulated Minecraft input.
- No automatic clipboard manipulation.
- No independent stronghold triangulation or extra interpretation layered on Ninjabrain Bot output.

Those omissions are intentional. A useful tool that gets a submitted run rejected is a remarkably elaborate way to lose time.

## Requirements

1. A Ninjabrain Bot version that is legal for the category and ruleset you are running.
2. Ninjabrain Bot's HTTP API enabled in its advanced settings.
3. PunPun 1.3 to build the CLI relay.
4. `curl` available for the CLI relay. Modern Windows includes `curl.exe`; Linux and macOS commonly provide `curl` through the system package manager.
5. Python 3 with Tk support for the overlay.

Rules and legal-build lists can change. Always verify the current MCSR rules before a submitted run. This project is designed around the v7 allowance for displaying Ninjabrain Bot API data; it is not a declaration by the leaderboard moderators that every future version is approved.

## Build the PunPun relay

From the PunPun repository root:

```sh
make compiler
./build/ppc check examples/punbrain/main.pp
./build/ppc build --backend=c -O2 -o punbrain-legal examples/punbrain/main.pp
./punbrain-legal legal-version
```

Expected version output:

```text
PunBrain Legal Relay 0.2.0
```

Query Ninjabrain data directly:

```sh
./punbrain-legal stronghold
./punbrain-legal blind
./punbrain-legal boat
./punbrain-legal divine
./punbrain-legal all-advancements
```

The relay does not calculate on the returned values. It prints the selected endpoint response.

## Run the external overlay

Enable the API in Ninjabrain Bot first, then run:

```sh
python3 examples/punbrain/legal-overlay.py --borderless
```

On Windows you can also use:

```powershell
py examples/punbrain/legal-overlay.py --borderless
```

Useful options:

```text
--endpoint stronghold
--interval-ms 200
--alpha 0.92
--borderless
--geometry 760x430+24+24
```

Press `Esc` to close the overlay.

The default `stronghold` view displays Ninjabrain's supplied result type, player position fields, prediction chunks/certainties/distances, and eye-throw fields. Other endpoints are shown as their returned JSON.

## Ninjabrain Bot API

Ninjabrain Bot exposes its API on port `52533` when enabled, under `/api/v1`. Legal Mode allow-lists only these known endpoints:

```text
/api/v1/stronghold
/api/v1/all-advancements
/api/v1/blind
/api/v1/divine
/api/v1/boat
/api/v1/information-messages
/api/v1/version
/api/v1/ping
```

The overlay never contacts Minecraft itself.

## Why automatic eye-coordinate capture was removed

The original experiment sampled eye entities directly from a custom client mod and automatically ingested those measurements. That is useful for research, but it is not appropriate to ship as a leaderboard-legal mode under the v7 rules reviewed for this project. Legal Mode therefore lets the approved Ninjabrain workflow remain the source of eye measurements and calculations.

## Rule review

See [`LEGALITY.md`](LEGALITY.md) for the design invariants used to keep this mode conservative.

The rules document reviewed during this refactor was:

```text
https://rawcdn.githack.com/Minecraft-Java-Edition-Speedrunning/rules/main/pub/pdf/rules_v7.pdf
```
