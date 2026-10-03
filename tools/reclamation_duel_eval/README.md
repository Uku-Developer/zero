# Offline Duel evaluation

Run a single client log with an explicit local identity whenever outcomes matter:

`python -m tools.reclamation_duel_eval.evaluator ReclaimV3A.log --local-name ReclaimV3A --json`

Without `--local-name`, the evaluator preserves the legacy first-arena-enter inference and reports
`local_identity_source=inferred_first_arena_enter`. Do not use inferred identity for baseline
kill/death conclusions when both clients appear in one log.

Batch evaluation accepts an explicit list of files and/or directories. Directories are read
non-recursively and only regular files ending in `.log` (case-insensitive) are included:

`python -m tools.reclamation_duel_eval.evaluator --batch run-a.log run-b.log logs/ --json`

Batch mode still treats each client file as one observation. A multi-client duel is the experimental
unit for tuning; pair the two client observations with authoritative server outcomes outside batch
mode until a run-manifest interface is added.

Paired evaluation matches baseline and candidate files by identical basename. Unmatched files are
listed and are never pooled:

`python -m tools.reclamation_duel_eval.evaluator --baseline baseline/ --candidate candidate/ --json`

## Time and lifecycle accounting

When RAI records contain gameplay ticks, durations use Zero's 31-bit tick semantics at 10 ms per
tick. This preserves same-second transitions that wall-clock log timestamps cannot represent and
handles tick rollover. Timestamp-only legacy logs remain supported at lower precision.

A continuous RAI segment ends at:

- a local arena enter/leave boundary;
- a local death when explicit identity is available;
- `reason=self-unavailable`;
- a non-forward/repeated tick;
- a RAI gap larger than 750 gameplay ticks (7.5 seconds).

State/reason `*_seconds` metrics are time-weighted only across continuously observed segments.
RAI sample fractions are transition-triggered plus periodic evidence and must not be presented as
time-weighted exposure.

Recharge episodes cannot convert across a lifecycle boundary or censored gap. Only continuously
observed completed exits contribute to `recharge_exits` and
`recharge_to_aggression_rate`.

## Action telemetry

The parser recognizes RAI, RACT and RINPUT records.

- **RACT schema 1** is emitted after policy `ApplyAction` and before Zero's actuator. New records
  carry `stage=post_policy_pre_actuator`. The legacy field name `applied_mask` is retained for
  compatibility, but it is a pre-actuator InputState snapshot. Movement goals live in
  `has_move/move_x/move_y`.
- **RINPUT schema 1** is captured immediately after `actuator.Update` and before
  `game->Update`; new records carry `stage=post_actuator_pre_game_update`. It is final
  controller-stage client input for that update, not proof that a weapon spawned, a packet was
  transmitted, or server damage occurred.

RACT and RINPUT are paired strictly by log order within one client process: a RACT pairs with the
next RINPUT before another RACT. The evaluator reports pair count, orphan counts and pairs whose
input mask changed. Do not join different clients by tick and do not assume the two record ticks
must be identical.

## Current metrics and limitations

Target distance uses nonnegative observations only. Fire eligibility/decision and opponent
instability report presence-aware sample counts/fractions. `shot_path_conf` reports count, mean,
median, `shot_path_confidence_zero_samples`, and positive fraction. A zero confidence sample does
not by itself prove blocked geometry; a valid intercept at the prediction horizon can also produce
zero confidence.

Exact shot/hit/damage attribution still requires replay/server evidence. Missing telemetry remains
unavailable rather than being coerced to zero.
