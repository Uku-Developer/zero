# Reclamation transition dataset v1

`transition_dataset.py` is a Python-stdlib-only offline converter layered on the
accepted `reclamation_replay_extract` parser. It accepts one or more `.rec` files
and writes deterministic JSONL state transitions:

```console
python tools/reclamation_training_data/transition_dataset.py a.rec b.rec
python tools/reclamation_training_data/transition_dataset.py a.rec b.rec --mode summary
```

Every record uses `schema_version: "reclamation-transition-v1"`. A transition
joins consecutive normalized position observations for one player inside one
lifecycle segment. Enter/re-enter, leave, death, ship change, and frequency
change break continuity. Segment indices are zero-based per player and file.
Input file order is preserved, followed by extractor event order.

`state` and `next_state` have a fixed shape. Unknown identity or state values are
`null`; they are never guessed. WeaponData is retained as
`weapon_observation`, while `action.available` is always false because replay
positions do not contain keyboard/action labels. `reward` is null. Kill records
may conservatively annotate the last still-active transition of the killer and
victim, but no scalar reward or intent is inferred.

`dt_ticks` is unsigned 32-bit server-tick elapsed time (and therefore handles a
single tick-counter wrap). Source file and event indices provide traceability.
Summary mode reports file count, unique `(file, player_id)` count, observed
segments, transitions, and unavailable actions. Segments with only one position
are counted even though they yield no transition.
