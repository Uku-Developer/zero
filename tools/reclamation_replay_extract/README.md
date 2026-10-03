# Offline Reclamation replay extractor

`replay_extract.py` is a Python-stdlib-only, read-only `.rec` parser for offline AI analysis. It follows the current server sources in `Subspace-Reclamation-Server/src/Replay/FileFormat` and `ReplayModule.cs`, with packet layouts from `src/Packets/Game/PlayerPositionPackets.cs`. The file header is exactly 92 bytes (`asssgame`, six 32-bit fields, an int64, a 32-bit checksum, and two 24-byte strings); its comment area ends at `offset`, followed by a gzip event stream.

It emits JSONL (`header` plus one record per event) or `--mode summary`. Supported and safely sized event types are all current enum values: lifecycle, chat, legacy ASSS position, packet, brick/ball events, crown, static/carry flags, security seed, attach/turret, and the two current Afluxion position wrappers. Legacy Position records are 22, 24, or 32 bytes after the event header; their packet time field contains player id, so client time is unavailable. The 24-byte tail is preserved as opaque bytes. Unknown types and invalid/truncated lengths fail explicitly.

The normalized position record is trajectory-friendly, but it does not invent absent data. C2S position + WeaponData exposes observed ship state and weapon events, not exact keyboard inputs; keyboard inputs are not recorded. Coordinates, velocities, client time, and server tick are observations; they are not proof of intent or causality. The tool has no runtime, policy, or competitive authority.

Example: `python replay_extract.py recording.rec > replay.jsonl` or `python replay_extract.py recording.rec --mode summary`.
