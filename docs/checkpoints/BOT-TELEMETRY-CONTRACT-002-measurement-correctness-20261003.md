# BOT-TELEMETRY-CONTRACT-002 - measurement correctness and baseline gate

**Date:** 2026-10-03
**Status:** SOURCE VALIDATED / TACTICAL CONSTANTS FROZEN
**Repository:** Uku-Developer/zero
**Branch:** reclamation/bot-core-001

## Goal

Correct telemetry and evaluator semantics exposed by live Duel-v3 evidence without tuning Duel behavior.

## Changes

- RACT schema 1 now declares stage=post_policy_pre_actuator. Its legacy applied_mask remains a pre-actuator InputState snapshot.
- RINPUT schema 1 declares stage=post_actuator_pre_game_update.
- PolicyNode periodic telemetry uses Zero 31-bit tick arithmetic and an explicit initialization flag.
- DuelPolicy state age and opponent recency/deadline logic use Zero tick helpers.
- opponent instability memory resets across target changes, no-opponent, self-unavailable/death/respawn lifecycle gaps.
- evaluator uses 10 ms gameplay ticks when present, including rollover handling; wall timestamps remain legacy fallback only.
- lifecycle/gap boundaries censor occupancy and recharge conversion rather than connecting disconnected evidence.
- explicit local identity is supported for a single client log.
- RACT/RINPUT records are parsed and paired strictly by client-log order; orphan and actuator-mask-change counts are reported.
- shot_path_confidence_zero_samples replaces the incorrect zero_blocked label.
- AppliedInputTelemetry.h is included in the Visual Studio project inventory.
- Python cache, replay and generated JSONL artifacts are ignored.

## Validation

PASS: tests/reclamation/run-all-tests.ps1.
PASS: Duel evaluator 20/20.
PASS: replay extractor 7/7.
PASS: transition dataset 5/5.
PASS: git diff --check.
PASS: full zero.sln Release/x64 build.

## Frozen baseline evidence

Six 60-second current-policy duels were collected before rebuilding the corrected source. Launch order alternated equally. Server outcomes were 0-0, 0-0, 1-1, 0-0, 0-0, 1-1. The earlier exploratory 3-0 pair did not reproduce.

Across twelve explicit client observations: 73,477 RACT records paired with RINPUT, zero RACT orphans, 287 RINPUT orphans, and 69,136 paired mask changes across the actuator boundary. The raw evidence is outside Git under C:\\Projects\\reclamation-build\\bot-baseline-evidence\\. Aggregate summary SHA-256: A8A66E168F4014CACA3F4EDD1121885CABD599983E468A3B093FD2EF690DB63C.

## Interpretation guard

No tactical threshold change is authorized by this sprint. RAI sample fractions are transition-triggered plus periodic evidence; use time-weighted *_seconds metrics for occupancy. The duel is the experimental unit, not each client log. A future candidate must be compared against the frozen multi-run baseline with both launch orders represented and authoritative Server outcomes reconciled.

## Next

Create reviewable Bot Core Git commits/PR, keeping runtime/policy/telemetry/evaluator separate from replay/training tooling where practical. Then design a paired Duel candidate experiment only if a reproducible mechanism is supported by the baseline.
