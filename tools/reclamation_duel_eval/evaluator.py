from __future__ import annotations

import json
import re
from collections import Counter, defaultdict
from dataclasses import asdict, dataclass, field
from datetime import datetime
from pathlib import Path
from statistics import mean, median
from typing import Any, Iterable

PREFIX = re.compile(r"^\w+ \[(?P<ts>[^]]+)\] (?P<body>.*)$")
KV = re.compile(r"(?P<key>[A-Za-z_][\w.-]*)=(?P<value>\"[^\"]*\"|'[^']*'|[^\s]+)")
ENTER = re.compile(r"^(?P<name>.+?) \[(?P<id>\d+)\] entered arena$")
LEAVE = re.compile(r"^(?P<name>.+?) \[(?P<id>\d+)\] left arena$")
KILL = re.compile(r"^(?P<victim>.+?) \(\d+\) killed by (?P<killer>.+?) \(\d+\)$")

NUMERIC = re.compile(r"^-?(?:\d+(?:\.\d*)?|\.\d+)$")
TICK_MODULUS = 1 << 31
TICK_HALF_RANGE = 1 << 30
TICK_SECONDS = 0.01
RAI_MAX_CONTINUOUS_GAP_TICKS = 750

def _value(s: str) -> Any:
    s = s.strip("\"'")
    if NUMERIC.match(s):
        return float(s) if "." in s else int(s)
    return s

@dataclass
class Record:
    timestamp: str
    epoch: float
    kind: str
    fields: dict[str, Any] = field(default_factory=dict)
    raw: str = ""

def parse(text: str) -> list[Record]:
    records = []
    for line in text.splitlines():
        m = PREFIX.match(line.strip())
        if not m:
            continue
        ts = m.group("ts")
        try: epoch = datetime.strptime(ts, "%Y-%m-%d %H:%M:%S").timestamp()
        except ValueError: continue
        body = m.group("body")
        fields = {x.group("key"): _value(x.group("value")) for x in KV.finditer(body)}
        if body.startswith("RAI "):
            fields["target"] = None if fields.get("target") == 65535 else fields.get("target")
            kind = "rai"
        elif body.startswith("RACT "):
            kind = "ract"
        elif body.startswith("RINPUT "):
            kind = "rinput"
        elif (x := ENTER.match(body)):
            fields.update(name=x.group("name"), player_id=int(x.group("id"))); kind = "arena_enter"
        elif (x := LEAVE.match(body)):
            fields.update(name=x.group("name"), player_id=int(x.group("id"))); kind = "arena_leave"
        elif (x := KILL.match(body)):
            fields.update(victim=x.group("victim"), killer=x.group("killer")); kind = "kill"
        else:
            kind = "other"
        records.append(Record(ts, epoch, kind, fields, line))
    return records

def _tick_diff(newer: int, older: int) -> int:
    delta = (newer - older) & (TICK_MODULUS - 1)
    return delta - TICK_MODULUS if delta >= TICK_HALF_RANGE else delta

def _interval_duration(a: Record, b: Record) -> tuple[float | None, bool]:
    a_tick = a.fields.get("tick")
    b_tick = b.fields.get("tick")
    if isinstance(a_tick, int) and isinstance(b_tick, int):
        delta_ticks = _tick_diff(b_tick, a_tick)
        if delta_ticks <= 0:
            return None, True
        return delta_ticks * TICK_SECONDS, delta_ticks > RAI_MAX_CONTINUOUS_GAP_TICKS

    delta_seconds = b.epoch - a.epoch
    if delta_seconds <= 0:
        return None, True
    return delta_seconds, delta_seconds > (RAI_MAX_CONTINUOUS_GAP_TICKS * TICK_SECONDS)

def _is_local_lifecycle_break(record: Record, local_name: str | None) -> bool:
    if record.kind == "rai" and record.fields.get("reason") == "self-unavailable":
        return True
    if record.kind in ("arena_enter", "arena_leave"):
        return local_name is None or record.fields.get("name") == local_name
    if record.kind == "kill" and local_name is not None:
        return record.fields.get("victim") == local_name
    return False

def _rai_segments(records: list[Record], local_name: str | None) -> list[list[Record]]:
    segments: list[list[Record]] = []
    current: list[Record] = []
    for record in records:
        if _is_local_lifecycle_break(record, local_name):
            if current:
                segments.append(current)
                current = []
            continue
        if record.kind != "rai":
            continue
        if current:
            _, censored = _interval_duration(current[-1], record)
            if censored:
                segments.append(current)
                current = []
        current.append(record)
    if current:
        segments.append(current)
    return segments

def _intervals(records: list[Record], local_name: str | None):
    result = []
    previous: Record | None = None
    for record in records:
        if _is_local_lifecycle_break(record, local_name):
            previous = None
            continue
        if record.kind != "rai":
            continue
        if previous is not None:
            duration, censored = _interval_duration(previous, record)
            if duration is not None:
                result.append((previous, duration, censored))
        previous = record
    return result

def _pair_action_inputs(records: list[Record]) -> dict[str, int]:
    pending: Record | None = None
    pairs = 0
    orphan_ract = 0
    orphan_rinput = 0
    mask_changes = 0
    for record in records:
        if record.kind == "ract":
            if pending is not None:
                orphan_ract += 1
            pending = record
        elif record.kind == "rinput":
            if pending is None:
                orphan_rinput += 1
                continue
            pairs += 1
            if pending.fields.get("applied_mask") != record.fields.get("applied_mask"):
                mask_changes += 1
            pending = None
    if pending is not None:
        orphan_ract += 1
    return {
        "ract_records": sum(r.kind == "ract" for r in records),
        "rinput_records": sum(r.kind == "rinput" for r in records),
        "ract_rinput_pairs": pairs,
        "ract_orphans": orphan_ract,
        "rinput_orphans": orphan_rinput,
        "ract_rinput_mask_change_pairs": mask_changes,
    }

def _is_recharge(r):
    return r.fields.get("state") == "recharge" or r.fields.get("reason") == "duel-recharge"

def _is_aggression(r):
    value = f"{r.fields.get('state', '')} {r.fields.get('reason', '')}".lower()
    return any(x in value for x in ("pressure", "rush", "finish", "chase"))

def _numeric_samples(rai, field_name, *, nonnegative=False):
    values = [float(r.fields[field_name]) for r in rai
              if isinstance(r.fields.get(field_name), (int, float))
              and not isinstance(r.fields.get(field_name), bool)
              and (not nonnegative or float(r.fields[field_name]) >= 0)]
    return values

def _boolean_samples(rai, field_name):
    values = [r.fields[field_name] for r in rai if field_name in r.fields]
    return len(values), sum(bool(value) for value in values)

def aggregate(records: list[Record], local_name: str | None = None) -> dict[str, Any]:
    rai = [r for r in records if r.kind == "rai"]
    reasons = Counter(str(r.fields["reason"]) for r in rai if "reason" in r.fields)
    states = Counter(str(r.fields["state"]) for r in rai if "state" in r.fields)
    inferred_name = next((r.fields.get("name") for r in records if r.kind == "arena_enter"), None)
    identity_source = "explicit" if local_name else ("inferred_first_arena_enter" if inferred_name else "unavailable")
    local_name = local_name or inferred_name
    intervals = _intervals(records, local_name)
    observed_intervals = [(a,d) for a,d,c in intervals if not c]
    recharge = sum(d for a,d in observed_intervals if _is_recharge(a))
    recharge_gaps = sum(d for a,d,c in intervals if c and _is_recharge(a))
    recharge_samples = sum(_is_recharge(a) for a,d,c in intervals if not c)

    recharge_episodes = 0
    recharge_exits = 0
    conversions = 0
    for segment in _rai_segments(records, local_name):
        i = 0
        while i < len(segment):
            if not _is_recharge(segment[i]):
                i += 1
                continue
            recharge_episodes += 1
            j = i + 1
            while j < len(segment) and _is_recharge(segment[j]):
                j += 1
            if j < len(segment):
                recharge_exits += 1
                conversions += _is_aggression(segment[j])
            i = j
    recharge_to_aggression = conversions / recharge_exits if recharge_exits else None
    transition_available = any("transition" in r.fields for r in rai)
    transition_count = sum(bool(r.fields.get("transition")) for r in rai) if transition_available else None
    threats = [float(r.fields["threat"]) for r in rai if isinstance(r.fields.get("threat"), (int,float))]
    conf = [float(r.fields["intercept_conf"]) for r in rai if isinstance(r.fields.get("intercept_conf"), (int,float)) and float(r.fields["intercept_conf"]) >= 0]
    adv = [float(r.fields["advantage"]) for r in rai if isinstance(r.fields.get("advantage"), (int,float))]
    duration = sum(d for _, d in observed_intervals)
    kill_records = [r for r in records if r.kind == "kill"]
    action_input = _pair_action_inputs(records)
    sample_occupancy = dict(states); reason_sample_occupancy = dict(reasons)
    state_seconds = defaultdict(float); reason_seconds = defaultdict(float)
    for a,d in observed_intervals:
        if a.fields.get("state") is not None: state_seconds[str(a.fields["state"])] += d
        if a.fields.get("reason") is not None: reason_seconds[str(a.fields["reason"])] += d
    engagement_duration = sum(d for a, d in observed_intervals if a.fields.get("target") is not None)
    state_total = sum(state_seconds.values())
    reason_total = sum(reason_seconds.values())
    escape_samples = sum(("escape" in str(r.fields.get("state", "")).lower() or "escape" in str(r.fields.get("reason", "")).lower()) for r in rai)
    positive_threat = sum(x > 0 for x in threats); dangerous_threat = sum(x >= .25 for x in threats)
    target_distances = _numeric_samples(rai, "target_dist", nonnegative=True)
    eligible_count, eligible_truthy = _boolean_samples(rai, "fire_eligible")
    decision_count, decision_truthy = _boolean_samples(rai, "fire_decision")
    paired_fire = [r for r in rai if "fire_eligible" in r.fields and "fire_decision" in r.fields]
    eligible_paired = sum(bool(r.fields["fire_eligible"]) for r in paired_fire)
    decision_paired = sum(bool(r.fields["fire_decision"]) and bool(r.fields["fire_eligible"]) for r in paired_fire)
    shot_conf = _numeric_samples(rai, "shot_path_conf", nonnegative=True)
    unstable_count, unstable_truthy = _boolean_samples(rai, "opponent_unstable")
    return {"records": len(records), "rai_samples": len(rai), "arena_enters": sum(r.kind == "arena_enter" for r in records), "arena_leaves": sum(r.kind == "arena_leave" for r in records),
            "local_name": local_name, "local_identity_source": identity_source,
            "kills": sum(r.fields.get("killer") == local_name for r in kill_records) if local_name else None, "deaths": sum(r.fields.get("victim") == local_name for r in kill_records) if local_name else None,
            "observed_match_duration_s": duration, "observed_engagement_duration_s": engagement_duration, "state_occupancy_samples": sample_occupancy, "reason_occupancy_samples": reason_sample_occupancy,
            "state_occupancy_seconds": dict(state_seconds), "reason_occupancy_seconds": dict(reason_seconds),
            "state_occupancy_fractions": {k:v/state_total for k,v in state_seconds.items()} if state_total else {}, "reason_occupancy_fractions": {k:v/reason_total for k,v in reason_seconds.items()} if reason_total else {},
            "recharge_observed_s": recharge, "recharge_censored_gap_s": recharge_gaps, "recharge_intervals": recharge_samples,
            "transition_rate": (transition_count / duration if transition_available and duration else None), "transition_count": transition_count,
            "transition_measurement": "truthy transition fields per continuously observed gameplay second" if transition_available else None,
            "threat_exposure_samples": len(threats), "threat_positive_samples": positive_threat, "threat_dangerous_samples": dangerous_threat, "threat_positive_fraction": positive_threat/len(threats) if threats else None, "threat_dangerous_fraction": dangerous_threat/len(threats) if threats else None, "threat_mean": (sum(threats)/len(threats) if threats else None),
            "disengage_samples": reasons.get("duel-disengage", 0), "escape_samples": escape_samples,
            "intercept_confidence_mean": (sum(conf)/len(conf) if conf else None), "energy_advantage_mean": (sum(adv)/len(adv) if adv else None),
            "target_distance_observations": len(target_distances), "target_distance_mean": (mean(target_distances) if target_distances else None), "target_distance_median": (median(target_distances) if target_distances else None),
            "fire_eligibility_observations": eligible_count, "fire_eligibility_truthy_samples": eligible_truthy, "fire_eligibility_fraction": (eligible_truthy / eligible_count if eligible_count else None),
            "fire_decision_observations": decision_count, "fire_decision_truthy_samples": decision_truthy, "fire_decision_fraction": (decision_truthy / decision_count if decision_count else None),
            "fire_decision_rate_given_eligible": (decision_paired / eligible_paired if eligible_paired else None),
            "shot_path_confidence_observations": len(shot_conf), "shot_path_confidence_mean": (mean(shot_conf) if shot_conf else None), "shot_path_confidence_median": (median(shot_conf) if shot_conf else None), "shot_path_confidence_zero_samples": sum(x == 0 for x in shot_conf), "shot_path_confidence_positive_fraction": (sum(x > 0 for x in shot_conf) / len(shot_conf) if shot_conf else None),
            "opponent_instability_observations": unstable_count, "opponent_instability_truthy_samples": unstable_truthy, "opponent_instability_fraction": (unstable_truthy / unstable_count if unstable_count else None),
            "recharge_episodes": recharge_episodes, "recharge_exits": recharge_exits, "recharge_to_aggression": conversions, "recharge_to_aggression_conversion": recharge_to_aggression, "recharge_to_aggression_rate": recharge_to_aggression,
            **action_input,
            "limitations": ["RAI sample fractions are transition-triggered plus periodic evidence, not time-weighted exposure unless a *_seconds metric is used.", "RINPUT is client input after actuator.Update, not proof that a weapon spawned, a packet transmitted, or the server applied damage.", "RACT schema 1 retains the legacy applied_mask field name but stage=post_policy_pre_actuator identifies it as a pre-actuator InputState snapshot.", "shot_path_conf is geometry-gated prediction confidence, not full bounce/fuse/detonation modeling.", "Durations use 10ms gameplay ticks when available and censor lifecycle discontinuities and detected gaps."]}

def evaluate_text(text: str, local_name: str | None = None) -> dict[str, Any]:
    rs = parse(text); return {"metrics": aggregate(rs, local_name=local_name), "record_kinds": dict(Counter(r.kind for r in rs))}

def evaluate_file(path: str | Path, local_name: str | None = None) -> dict[str, Any]:
    p = Path(path); out = evaluate_text(p.read_text(encoding="utf-8", errors="replace"), local_name=local_name); out["file"] = str(p); return out

def _expand_paths(paths: Iterable[str | Path]) -> list[Path]:
    """Expand an explicit collection of files and directories deterministically."""
    result = []
    for value in paths:
        path = Path(value)
        if path.is_dir():
            result.extend(sorted((p for p in path.iterdir() if p.is_file() and p.suffix.lower() == ".log"), key=lambda p: p.name))
        else:
            result.append(path)
    return sorted(result, key=lambda p: str(p))

def _numeric_leaves(value: Any, prefix: str = "") -> dict[str, float]:
    leaves = {}
    if isinstance(value, dict):
        for key in sorted(value):
            child = f"{prefix}.{key}" if prefix else str(key)
            leaves.update(_numeric_leaves(value[key], child))
    elif isinstance(value, (int, float)) and not isinstance(value, bool):
        leaves[prefix] = float(value)
    return leaves

def _numeric_summary(values: list[float]) -> dict[str, Any] | None:
    if not values:
        return None
    return {"count": len(values), "mean": mean(values), "median": median(values),
            "min": min(values), "max": max(values)}

def _batch_summary(runs: list[dict[str, Any]]) -> dict[str, Any]:
    values = defaultdict(list)
    for run in runs:
        for key, number in _numeric_leaves(run["metrics"]).items():
            values[key].append(number)
    return {key: _numeric_summary(values[key]) for key in sorted(values)}

def summarize_files(paths: Iterable[str | Path]) -> dict[str, Any]:
    """Evaluate each supplied file/run independently and summarize run metrics."""
    files = _expand_paths(paths)
    runs = [evaluate_file(path) for path in files]
    return {"mode": "batch", "files": [str(p) for p in files], "runs": runs,
            "summary": {"run_count": len(runs), "metrics": _batch_summary(runs)}}

def _by_basename(paths: Iterable[str | Path]) -> dict[str, Path]:
    files = _expand_paths(paths)
    result = {}
    for path in files:
        name = path.name
        if name in result:
            raise ValueError(f"duplicate basename in paired inputs: {name}")
        result[name] = path
    return result

def compare_paired(baseline: Iterable[str | Path], candidate: Iterable[str | Path]) -> dict[str, Any]:
    """Compare independently evaluated baseline/candidate runs matched by basename."""
    baseline_map = _by_basename(baseline)
    candidate_map = _by_basename(candidate)
    names = sorted(set(baseline_map) & set(candidate_map))
    baseline_only = sorted(set(baseline_map) - set(candidate_map))
    candidate_only = sorted(set(candidate_map) - set(baseline_map))
    pairs = [(evaluate_file(baseline_map[name]), evaluate_file(candidate_map[name])) for name in names]
    metric_names = sorted(set().union(*(_numeric_leaves(pair[0]["metrics"]).keys() | _numeric_leaves(pair[1]["metrics"]).keys() for pair in pairs))) if pairs else []
    metrics = {}
    for name in metric_names:
        observations = []
        for base, cand in pairs:
            b = _numeric_leaves(base["metrics"]).get(name)
            c = _numeric_leaves(cand["metrics"]).get(name)
            if b is not None and c is not None:
                observations.append((b, c))
        base_values = [b for b, c in observations]
        candidate_values = [c for b, c in observations]
        deltas = [c - b for b, c in observations]
        metrics[name] = {"pair_count": len(observations), "baseline": _numeric_summary(base_values),
                         "candidate": _numeric_summary(candidate_values), "delta": _numeric_summary(deltas),
                         "direction_counts": {"candidate_increases": sum(c > b for b, c in observations),
                                               "candidate_decreases": sum(c < b for b, c in observations),
                                               "ties": sum(c == b for b, c in observations)}}
    return {"mode": "paired", "pair_count": len(names),
            "matched": [{"basename": name, "baseline": str(baseline_map[name]), "candidate": str(candidate_map[name])} for name in names],
            "unmatched": {"baseline": [{"basename": name, "file": str(baseline_map[name])} for name in baseline_only],
                          "candidate": [{"basename": name, "file": str(candidate_map[name])} for name in candidate_only]},
            "metrics": metrics}

def main(argv=None) -> int:
    import argparse
    ap = argparse.ArgumentParser(description="Offline Reclamation Duel evaluator")
    ap.add_argument("files", nargs="*", type=Path)
    ap.add_argument("--batch", nargs="+", type=Path, help="explicit files and/or directories to summarize")
    ap.add_argument("--baseline", nargs="+", type=Path, help="baseline files and/or directories for paired comparison")
    ap.add_argument("--candidate", nargs="+", type=Path, help="candidate files and/or directories for paired comparison")
    ap.add_argument("--local-name", help="explicit local player identity; valid only with one positional client log")
    ap.add_argument("--json", action="store_true")
    ns = ap.parse_args(argv)
    if bool(ns.baseline) != bool(ns.candidate): ap.error("--baseline and --candidate must be used together")
    if ns.baseline and (ns.files or ns.batch): ap.error("paired mode cannot be combined with positional files or --batch")
    if ns.batch and ns.files: ap.error("--batch cannot be combined with positional files")
    if ns.local_name and (ns.batch or ns.baseline or len(ns.files) != 1): ap.error("--local-name requires exactly one positional client log")
    if ns.baseline: output = compare_paired(ns.baseline, ns.candidate)
    elif ns.batch: output = summarize_files(ns.batch)
    elif ns.local_name: output = [evaluate_file(ns.files[0], local_name=ns.local_name)]
    else: output = [evaluate_file(p) for p in ns.files]
    if ns.json: print(json.dumps(output, indent=2, sort_keys=True))
    elif isinstance(output, dict) and output["mode"] == "batch":
        print(f"batch: runs={output['summary']['run_count']} metrics={len(output['summary']['metrics'])}")
    elif isinstance(output, dict):
        print(f"paired: pairs={output['pair_count']} unmatched-baseline={len(output['unmatched']['baseline'])} unmatched-candidate={len(output['unmatched']['candidate'])} metrics={len(output['metrics'])}")
    else:
        for x in output:
            m=x["metrics"]; print(f"{x['file']}: RAI={m['rai_samples']} duration={m['observed_match_duration_s']:.1f}s kills={m['kills']} recharge={m['recharge_observed_s']:.1f}s gaps={m['recharge_censored_gap_s']:.1f}s")
    return 0

if __name__ == "__main__": raise SystemExit(main())
