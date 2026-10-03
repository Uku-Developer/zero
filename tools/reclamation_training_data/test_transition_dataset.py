import gzip
import io
import json
import struct
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

from transition_dataset import SCHEMA_VERSION, build, main


def replay(events):
    header = bytearray(92)
    header[:8] = b"asssgame"
    struct.pack_into("<IIIIIIqI", header, 8, 200, 92, len(events), 0, 10, 8025, 0, 0)
    return bytes(header) + gzip.compress(b"".join(events))


def ev(tick, kind, body=b""):
    return struct.pack("<Ih", tick, kind) + body


def enter(pid, name=b"Pilot", ship=1, freq=2):
    return ev(1, 1, struct.pack("<h24s24shh", pid, name + b"\0", b"\0", ship, freq))


def position(tick, pid, x=100, weapon=(0xA5, 0x9E), extra=False):
    packet = bytearray(32 if extra else 22)
    packet[0] = len(packet)
    packet[1] = 5
    struct.pack_into("<IhhBBhhHh", packet, 2, tick, -10, 20, 9, 3, x, 200, 300, 400)
    packet[20:22] = bytes(weapon)
    if extra:
        struct.pack_into("<HHHI", packet, 22, 50, 60, 70, 0x2AAAAA55)
    return ev(tick, 151 if extra else 150, struct.pack("<h", pid) + packet)


class TransitionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()

    def tearDown(self):
        self.temp.cleanup()

    def write(self, name, events):
        path = Path(self.temp.name) / name
        path.write_bytes(replay(events))
        return path

    def test_identity_consecutive_transition_and_extra(self):
        path = self.write("a.rec", [enter(7), position(10, 7), position(13, 7, 101, extra=True)])
        rows, summary = build([path])
        self.assertEqual(len(rows), 1)
        row = rows[0]
        self.assertEqual(row["schema_version"], SCHEMA_VERSION)
        self.assertEqual(row["dt_ticks"], 3)
        self.assertEqual(row["state"]["player"], {"id": 7, "name": "Pilot", "ship": 1, "freq": 2})
        self.assertEqual(row["next_state"]["extra"]["energy"], 50)
        self.assertEqual(summary["segments"], 1)

    def test_no_bridge_across_death(self):
        events = [enter(7), position(10, 7), position(11, 7), ev(12, 5, struct.pack("<hhhh", 8, 7, 3, 0)), position(13, 7), position(14, 7)]
        rows, summary = build([self.write("death.rec", events)])
        self.assertEqual([(r["source"]["state_event_index"], r["source"]["next_state_event_index"]) for r in rows], [(1, 2), (4, 5)])
        self.assertEqual(rows[0]["outcome"]["death"]["killer_player_id"], 8)
        self.assertEqual(summary["segments"], 2)

    def test_no_bridge_across_ship_and_freq_changes(self):
        events = [enter(7), position(10, 7), position(11, 7), ev(12, 3, struct.pack("<hhh", 7, 4, 9)), position(13, 7), position(14, 7), ev(15, 4, struct.pack("<hh", 7, 12)), position(16, 7), position(17, 7)]
        rows, summary = build([self.write("changes.rec", events)])
        self.assertEqual(len(rows), 3)
        self.assertEqual(rows[1]["state"]["player"]["ship"], 4)
        self.assertEqual(rows[1]["state"]["player"]["freq"], 9)
        self.assertEqual(rows[2]["state"]["player"]["freq"], 12)
        self.assertEqual(summary["segments"], 3)

    def test_action_unavailable_weapon_observation_and_nulls(self):
        path = self.write("unknown.rec", [position(10, 3), position(11, 3)])
        row = build([path])[0][0]
        self.assertEqual(row["action"], {"available": False, "value": None, "reason": "not_recorded_observation_only"})
        self.assertEqual(row["state"]["weapon_observation"]["type"], 5)
        self.assertIsNone(row["state"]["player"]["name"])
        self.assertIsNone(row["state"]["player"]["ship"])
        self.assertIsNone(row["reward"])

    def test_deterministic_multifile_order_and_summary(self):
        a = self.write("a.rec", [position(1, 9), position(2, 9)])
        b = self.write("b.rec", [position(1, 2), position(2, 2), position(3, 2)])
        first = build([a, b])
        second = build([a, b])
        self.assertEqual(first, second)
        rows, summary = first
        self.assertEqual([r["source"]["file_index"] for r in rows], [0, 1, 1])
        self.assertEqual(summary, {"schema_version": SCHEMA_VERSION, "record_type": "summary", "files": 2, "players": 2, "transitions": 3, "segments": 2, "unavailable_action_count": 3})
        output = io.StringIO()
        with redirect_stdout(output):
            self.assertEqual(main([str(a), str(b), "--mode", "summary"]), 0)
        self.assertEqual(json.loads(output.getvalue()), summary)


if __name__ == "__main__":
    unittest.main()
