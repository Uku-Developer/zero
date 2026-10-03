#!/usr/bin/env python3
"""Build observation-only state transitions from Reclamation replay files."""

import argparse
import json
import sys
from pathlib import Path


SCHEMA_VERSION = "reclamation-transition-v1"

# Import the accepted parser rather than interpreting .rec bytes here.
EXTRACTOR_DIR = Path(__file__).resolve().parents[1] / "reclamation_replay_extract"
if str(EXTRACTOR_DIR) not in sys.path:
    sys.path.insert(0, str(EXTRACTOR_DIR))

from replay_extract import ReplayError, parse  # noqa: E402


def _state(event, identity):
    """Return a fixed-shape state, retaining unknown fields as JSON null."""
    return {
        "server_tick": event.get("server_tick"),
        "x": event.get("x"),
        "y": event.get("y"),
        "x_speed": event.get("x_speed"),
        "y_speed": event.get("y_speed"),
        "rotation": event.get("rotation"),
        "energy": event.get("energy"),
        "status": event.get("status"),
        "bounty": event.get("bounty"),
        "weapon_observation": event.get("weapon"),
        "extra": event.get("extra"),
        "player": {
            "id": event.get("player_id"),
            "name": identity.get("name"),
            "ship": identity.get("ship"),
            "freq": identity.get("freq"),
        },
    }


def build(files):
    """Return (transitions, summary) for paths in the supplied order."""
    transitions = []
    players = set()
    segment_count = 0

    for file_index, path in enumerate(files):
        _header, events = parse(path)
        identities = {}
        previous = {}
        segment_ordinals = {}
        active_segments = {}
        last_transition = {}

        def reset(player_id):
            previous.pop(player_id, None)
            active_segments.pop(player_id, None)
            last_transition.pop(player_id, None)

        for event in events:
            event_type = event["type"]
            if event_type == "Enter":
                player_id = event["player_id"]
                reset(player_id)
                identities[player_id] = {
                    "name": event.get("name"),
                    "ship": event.get("ship"),
                    "freq": event.get("freq"),
                }
                continue
            if event_type == "Leave":
                player_id = event["player_id"]
                reset(player_id)
                identities.pop(player_id, None)
                continue
            if event_type in ("ShipChange", "FreqChange"):
                player_id = event["player_id"]
                reset(player_id)
                identity = identities.setdefault(
                    player_id, {"name": None, "ship": None, "freq": None}
                )
                if event_type == "ShipChange":
                    identity["ship"] = event.get("ship")
                    identity["freq"] = event.get("freq")
                else:
                    identity["freq"] = event.get("freq")
                continue
            if event_type == "Kill":
                killed = event.get("killed")
                killer = event.get("killer")
                if killed in last_transition:
                    transitions[last_transition[killed]]["outcome"]["death"] = {
                        "event_index": event["event_index"],
                        "server_tick": event["server_tick"],
                        "killer_player_id": killer,
                        "points": event.get("points"),
                        "flags": event.get("flags"),
                    }
                if killer in last_transition:
                    transitions[last_transition[killer]]["outcome"]["kill"] = {
                        "event_index": event["event_index"],
                        "server_tick": event["server_tick"],
                        "killed_player_id": killed,
                        "points": event.get("points"),
                        "flags": event.get("flags"),
                    }
                reset(killed)
                continue
            if "normalized_position" not in event:
                continue

            player_id = event["player_id"]
            players.add((file_index, player_id))
            identity = identities.setdefault(
                player_id, {"name": None, "ship": None, "freq": None}
            )
            if player_id not in active_segments:
                ordinal = segment_ordinals.get(player_id, 0)
                segment_ordinals[player_id] = ordinal + 1
                active_segments[player_id] = ordinal
                segment_count += 1

            prior = previous.get(player_id)
            if prior is not None:
                prior_event, prior_identity = prior
                state = _state(prior_event, prior_identity)
                next_state = _state(event, dict(identity))
                transition = {
                    "schema_version": SCHEMA_VERSION,
                    "record_type": "transition",
                    "transition_index": len(transitions),
                    "source": {
                        "file": str(path),
                        "file_index": file_index,
                        "state_event_index": prior_event["event_index"],
                        "next_state_event_index": event["event_index"],
                    },
                    "player_id": player_id,
                    "segment_index": active_segments[player_id],
                    "dt_ticks": (
                        event["server_tick"] - prior_event["server_tick"]
                    ) & 0xFFFFFFFF,
                    "state": state,
                    "next_state": next_state,
                    "action": {
                        "available": False,
                        "value": None,
                        "reason": "not_recorded_observation_only",
                    },
                    "outcome": {"death": None, "kill": None},
                    "reward": None,
                }
                transitions.append(transition)
                last_transition[player_id] = len(transitions) - 1
            previous[player_id] = (event, dict(identity))

    summary = {
        "schema_version": SCHEMA_VERSION,
        "record_type": "summary",
        "files": len(files),
        "players": len(players),
        "transitions": len(transitions),
        "segments": segment_count,
        "unavailable_action_count": len(transitions),
    }
    return transitions, summary


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("files", nargs="+")
    parser.add_argument("--mode", choices=("jsonl", "summary"), default="jsonl")
    args = parser.parse_args(argv)
    try:
        transitions, summary = build(args.files)
    except (OSError, ReplayError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    records = [summary] if args.mode == "summary" else transitions
    for record in records:
        print(json.dumps(record, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
