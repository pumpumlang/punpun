# PunBrain Legal Mode: rules-safety notes

This file documents the constraints intentionally imposed on the leaderboard-facing PunBrain build after reviewing the Minecraft Java Edition Speedrunning rules v7 document.

Reviewed rules document:

```text
https://rawcdn.githack.com/Minecraft-Java-Edition-Speedrunning/rules/main/pub/pdf/rules_v7.pdf
```

## Design invariants

The legal-mode distributable is kept within these boundaries:

1. **External process only.** PunBrain Legal Mode is not a Minecraft mod and does not install code into the client.
2. **Ninjabrain is authoritative.** Stronghold predictions, corrections, eye measurements, blind/divine/boat state, probabilities, and related interpretations come from Ninjabrain Bot.
3. **API input only.** The program reads only the enabled Ninjabrain Bot HTTP API on loopback port `52533`.
4. **Known endpoints only.** The relay allow-lists `stronghold`, `all-advancements`, `blind`, `divine`, `boat`, `information-messages`, `version`, and `ping`.
5. **Display/relay only.** API values may be laid out, labelled, and pretty-printed for visibility. Legal Mode does not use those values to generate a second prediction, modify certainty, derive routes, convert them into additional gameplay information, or otherwise reinterpret them.
6. **No Minecraft telemetry.** No memory access, entity inspection, packet inspection, world scanning, log parsing for hidden state, or direct Eye of Ender trajectory sampling.
7. **No screen or audio scraping.** No OCR, pixel analysis, video-frame interpretation, or game-audio analysis is used to gain gameplay information.
8. **No gameplay input automation.** Legal Mode does not press keys, move the mouse, throw eyes, alter camera movement, or send Minecraft commands.
9. **No automatic clipboard manipulation.** The leaderboard-facing build does not silently copy derived coordinates or use the clipboard as an automation channel.
10. **No Fabric dependency.** The removed prototype used Fabric API and direct entity access. Neither is present in the legal-mode distributable.

## Why the earlier prototype was removed

The first PunBrain experiment used a Fabric client companion to sample Eye of Ender entity positions at native precision and fit the observed trajectory. Although technically useful, that design consumed game state beyond the conservative API-display workflow allowed for an unapproved external tool. It also depended on Fabric API. Both made it unsuitable for presentation as a leaderboard-legal tool.

The prototype is therefore not part of the current source tree produced by this PR. Do not recover an older commit containing that experiment and use it for submitted runs.

## About Ninjabrain versions

PunBrain Legal Mode does not make an otherwise-illegal Ninjabrain build legal. The Ninjabrain Bot version and every other mod/tool in the run must independently satisfy the current category rules and legal-build requirements.

## Moderation and future rules

This is an engineering constraint document, not an official moderation ruling. MCSR rules, explicitly legal builds, and tool approvals can change after this commit. The leaderboard moderators have final authority over submitted runs.

Before recording a run for submission:

- verify the current rules rather than relying on this frozen v7 review;
- verify the exact Ninjabrain Bot build you intend to use is allowed;
- avoid adding local modifications that expand Legal Mode beyond display/relay behavior.

If future rules prohibit or narrow Ninjabrain API display, this mode must be revised before further leaderboard use.
