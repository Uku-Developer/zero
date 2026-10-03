# BOT-CORE-001 â€” Zero headless-client recovery

**Date:** 2026-10-03
**Status:** SOURCE + OFFLINE TESTS PASS; live combat lifecycle not yet proven
**Repository:** `Uku-Developer/zero`
**Branch:** `reclamation/bot-core-001`
**Base HEAD:** `b3858f8c6b0877794379da0c919fb02b805e8ea8`

> **Current evidence note:** the original live-proof-pending text below records the state at the start of this recovery pass. The 2026-10-03 evidence addendum at the end supersedes that pending status without deleting the historical checkpoint.

## Goal

Recover Reclamation bot intelligence behind a policy boundary and use Zero as a real Subspace-client
embodiment rather than making Server fake-player packets the permanent competitive bot substrate.

## Recovered architecture

The worktree already implements a clean split:

- `zero/reclamation/Core.h` â€” runtime-light world snapshot, action, telemetry and policy contracts.
- `DuelPolicy` / `SimpleEngagementPolicy` â€” deterministic policy logic.
- `TrajectoryPredictor` â€” interception math separated from runtime.
- `ZeroAdapter` â€” converts Zero-observable client/game state into the Reclamation contract and
  applies policy actions through ordinary Zero input/steering.
- `PolicyNode` â€” behavior-tree bridge plus decision/applied-input telemetry.
- Reclamation zone controller/behaviors register `core` and `duel`.
- Zero now exposes `reclamation` / `reclaim` as a server target.

## Offline intelligence/tooling present

The lane also contains:

- focused C++ tests for core policy, Duel policy, trajectory prediction and applied-input telemetry;
- offline Duel log evaluator;
- read-only Reclamation `.rec` replay extractor;
- deterministic transition-dataset builder for later training/analysis;
- Reclamation action/decision telemetry that distinguishes requested action from applied Zero input.

## Validation performed

PASS:

- `run-policy-tests.ps1` â€” Reclamation core policy.
- `run-duel-policy-tests.ps1` â€” Duel state/decision policy.
- `run-trajectory-predictor-tests.ps1` â€” interception predictor.
- `run-applied-input-telemetry-tests.ps1` â€” applied input capture.
- Duel evaluator Python tests â€” 13/13.
- Replay extractor Python tests â€” 7/7.
- Transition dataset Python tests â€” 5/5.
- Visual Studio Release/x64 full `zero.sln` build â€” PASS, producing `x64/Release/zero.exe`.
- `zero.exe --help` â€” PASS and lists `reclamation` as a server target.

A new uncommitted `tests/reclamation/run-all-tests.ps1` runs all focused suites with correct
Python working directories and explicit external-process exit checks.

## Safety / preservation

Before further changes, the complete modified/untracked source state was copied to:

`C:\Projects\reclamation-bot-core-snapshots\bot-core-001-20261003T082508Z.zip`

The snapshot contains base HEAD, Git status, tracked binary patch and copies of all 47 modified or
untracked files. The Git index was not changed.

No commit, push, PR, deploy or production connection was performed in this validation pass.

## What is not proven yet

This is **not** BOT-AGENT-001 completion yet.

Still required in a controlled Reclamation environment:

1. real protocol connection and arena entry;
2. ship request/entry;
3. policy intent becoming real movement and weapon input;
4. server-observed damage against the bot;
5. normal death callback/state;
6. normal respawn/re-entry;
7. clean disconnect/reconnect;
8. no hidden physics/resources beyond an ordinary client;
9. no official rating/stat contamination from bot/training sessions.

## Exact next bot task

Run `tests/reclamation/run-all-tests.ps1`, then establish a disposable/local Reclamation Server
arena that permits a dedicated test bot identity. Exercise one Zero Reclamation client through the
full movement -> fire -> take damage -> die -> respawn lifecycle and capture both client telemetry
and Server evidence. Do not use official ranked queues or mutate production ratings.

## 2026-10-03 evidence addendum - live client embodiment proven

The earlier "not proven yet" section is now historical.

### Real-client lifecycle

Historical isolated Bot Lab evidence recovered from the program archive already showed two Zero Reclamation clients authenticate through normal Subspace/VIE semantics, enter the Reclamation arena, request and enter Warbird, apply real movement and weapon inputs, exchange authoritative Server kills, enter the dead / self-unavailable state, respawn at normal energy and resume targeting, and disconnect cleanly.

A fresh 2026-10-03 loopback run against current Server main repeated the critical path with current duel-v3: successful login, Warbird entry, RAI decision telemetry, RACT policy-stage telemetry, RINPUT post-actuator telemetry and authoritative Server kills.

Therefore the Zero embodiment satisfies the original BOT-AGENT question: it is a real protocol client participating in ordinary Server death/respawn semantics rather than a Server fake-player combat simulation.

### Frozen Duel-v3 baseline

Before rebuilding the corrected source, the current Duel-v3 executable was frozen and used for six 60-second loopback duels. Launch order alternated A/B then B/A. Raw logs and per-run hashes live outside Git under C:\\Projects\\reclamation-build\\bot-baseline-evidence\\.

Authoritative Server outcomes: run-01 0-0; run-02 0-0; run-03 1-1; run-04 0-0; run-05 0-0; run-06 1-1. The previously observed exploratory 3-0 pair was not reproduced and is not a tuning basis.

The corrected evaluator analyzed six duels / twelve explicit player observations and found 4 total authoritative kills, 4 zero-kill runs, both two-kill runs balanced 1-1, 73,477 RACT records paired successfully with subsequent RINPUT records, 0 orphan RACT records, 287 orphan RINPUT records, and 69,136 paired updates whose input masks changed across the actuator boundary. That confirms pre-actuator RACT and post-actuator RINPUT must not be treated as equivalent.

The frozen aggregate summary is stored outside Git at C:\\Projects\\reclamation-build\\bot-baseline-evidence\\baseline-summary.json with SHA-256 A8A66E168F4014CACA3F4EDD1121885CABD599983E468A3B093FD2EF690DB63C.

### Measurement-correctness follow-up

BOT-TELEMETRY-CONTRACT-002 corrects measurement semantics without changing tactical thresholds: RACT carries stage=post_policy_pre_actuator; RINPUT carries stage=post_actuator_pre_game_update; Duel policy tick/state memory uses Zero's 31-bit tick helpers; opponent instability is cleared across target/lifecycle discontinuities; evaluator occupancy prefers gameplay ticks over whole-second wall timestamps; recharge conversion is censored across gaps/death/self-unavailable boundaries; explicit local identity is supported for outcome attribution; RACT/RINPUT are paired by client-log order and orphan/mask-change counts are reported; and zero shot-path confidence is no longer labeled as proof of blocked geometry.

### Current validation

PASS: Reclamation focused C++ tests; Duel evaluator 20/20; replay extractor 7/7; transition dataset 5/5; git diff --check; full Visual Studio Release/x64 zero.sln build.

The next bot task is baseline/report consolidation and the first reviewable Git commit/PR. Tactical Duel constants remain frozen until a separately designed candidate comparison is justified by repeated baseline evidence.
