import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
from tools.reclamation_duel_eval.evaluator import compare_paired, evaluate_text, parse, summarize_files

FIXTURE = """I [2026-01-01 00:00:00] P [0] entered arena
I [2026-01-01 00:00:00] RAI tick=1 target=65535 reason=no-opponent custom=x
I [2026-01-01 00:00:05] RAI tick=501 target=3 reason=duel-recharge self_energy=10.5
I [2026-01-01 00:00:10] RAI tick=1001 target=3 state=aggression transition=1 reason=duel-chase
I [2026-01-01 00:00:11] P (600) killed by Q (600)
"""

class EvaluatorTests(unittest.TestCase):
    def test_parser_normalizes_and_preserves(self):
        rs = parse(FIXTURE)
        self.assertIsNone(rs[1].fields["target"])
        self.assertEqual(rs[1].fields["custom"], "x")
        self.assertEqual(rs[2].fields["self_energy"], 10.5)

    def test_metrics_and_gap_censoring(self):
        m = evaluate_text(FIXTURE)["metrics"]
        self.assertEqual(m["deaths"], 1)
        self.assertEqual(m["state_occupancy_samples"], {"aggression": 1})
        self.assertEqual(m["recharge_observed_s"], 5.0)
        self.assertIsNotNone(m["transition_rate"])

    def test_zero_transition_and_escape_samples(self):
        text = """I [2026-01-01 00:00:00] P [0] entered arena
I [2026-01-01 00:00:01] RAI target=1 state=escape reason=duel-escape transition=0
I [2026-01-01 00:00:02] RAI target=1 state=escape reason=duel-escape transition=1
I [2026-01-01 00:00:03] RAI target=1 state=neutral reason=duel-neutral transition=0
"""
        m = evaluate_text(text)["metrics"]
        self.assertEqual(m["transition_count"], 1)
        self.assertEqual(m["escape_samples"], 2)

    def test_time_weighted_occupancy(self):
        text = """I [2026-01-01 00:00:00] RAI target=1 state=recharge reason=duel-recharge
I [2026-01-01 00:00:02] RAI target=1 state=recharge reason=duel-recharge
I [2026-01-01 00:00:03] RAI target=1 state=duel-chase reason=duel-chase
"""
        m = evaluate_text(text)["metrics"]
        self.assertEqual(m["state_occupancy_seconds"]["recharge"], 3)
        self.assertAlmostEqual(m["state_occupancy_fractions"]["recharge"], 1)

    def test_recharge_episode_conversion(self):
        text = """I [2026-01-01 00:00:00] RAI target=1 state=recharge reason=duel-recharge
I [2026-01-01 00:00:01] RAI target=1 state=recharge reason=duel-recharge
I [2026-01-01 00:00:02] RAI target=1 state=rush reason=duel-rush
I [2026-01-01 00:00:03] RAI target=1 state=neutral reason=duel-neutral
"""
        m = evaluate_text(text)["metrics"]
        self.assertEqual(m["recharge_exits"], 1)
        self.assertEqual(m["recharge_to_aggression"], 1)
        self.assertEqual(m["recharge_to_aggression_rate"], 1)

    def test_missing_transition_reason_fractions_and_open_recharge(self):
        text = """I [2026-01-01 00:00:00] RAI target=1 reason=duel-chase
I [2026-01-01 00:00:01] RAI target=1 reason=duel-recharge
I [2026-01-01 00:00:02] RAI target=1 reason=duel-recharge
"""
        m = evaluate_text(text)["metrics"]
        self.assertIsNone(m["transition_count"])
        self.assertIsNone(m["transition_rate"])
        self.assertAlmostEqual(m["reason_occupancy_fractions"]["duel-chase"], 0.5)
        self.assertAlmostEqual(m["reason_occupancy_fractions"]["duel-recharge"], 0.5)
        self.assertEqual(m["recharge_episodes"], 1)
        self.assertEqual(m["recharge_exits"], 0)
        self.assertIsNone(m["recharge_to_aggression_rate"])

    def test_batch_is_run_based_not_sample_pooled(self):
        one = "I [2026-01-01 00:00:00] RAI target=1\nI [2026-01-01 00:00:01] RAI target=1\n"
        two = "I [2026-01-01 00:00:00] RAI target=1\nI [2026-01-01 00:00:01] RAI target=1\nI [2026-01-01 00:00:02] RAI target=1\n"
        with TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "one.log").write_text(one)
            (root / "two.log").write_text(two)
            result = summarize_files([root])
        self.assertEqual(result["summary"]["run_count"], 2)
        self.assertEqual(result["summary"]["metrics"]["rai_samples"]["count"], 2)
        self.assertEqual(result["summary"]["metrics"]["rai_samples"]["mean"], 2.5)

    def test_pairing_and_unmatched_files(self):
        with TemporaryDirectory() as temp:
            root = Path(temp); base = root / "base"; cand = root / "cand"
            base.mkdir(); cand.mkdir()
            text = "I [2026-01-01 00:00:00] RAI target=1\nI [2026-01-01 00:00:01] RAI target=1\n"
            (base / "same.log").write_text(text)
            (cand / "same.log").write_text(text + "I [2026-01-01 00:00:02] RAI target=1\n")
            (base / "base-only.log").write_text(text)
            (cand / "candidate-only.log").write_text(text)
            result = compare_paired([base], [cand])
        self.assertEqual(result["pair_count"], 1)
        self.assertEqual(result["matched"][0]["basename"], "same.log")
        self.assertEqual([x["basename"] for x in result["unmatched"]["baseline"]], ["base-only.log"])
        self.assertEqual([x["basename"] for x in result["unmatched"]["candidate"]], ["candidate-only.log"])
        self.assertEqual(result["metrics"]["rai_samples"]["delta"]["mean"], 1)

    def test_pair_missing_metric_is_not_zero(self):
        base = "I [2026-01-01 00:00:00] RAI target=1\nI [2026-01-01 00:00:01] RAI target=1 transition=1\n"
        candidate = "I [2026-01-01 00:00:00] RAI target=1\nI [2026-01-01 00:00:01] RAI target=1\n"
        with TemporaryDirectory() as temp:
            root = Path(temp); base_dir = root / "base"; candidate_dir = root / "candidate"
            base_dir.mkdir(); candidate_dir.mkdir()
            (base_dir / "same.log").write_text(base); (candidate_dir / "same.log").write_text(candidate)
            result = compare_paired([base_dir], [candidate_dir])
        self.assertEqual(result["metrics"]["transition_count"]["pair_count"], 0)
        self.assertIsNone(result["metrics"]["transition_count"]["baseline"])
        self.assertIsNone(result["metrics"]["transition_count"]["candidate"])
        self.assertIsNone(result["metrics"]["transition_count"]["delta"])

    def test_pair_direction_counts_are_neutral_numeric_labels(self):
        baseline = "I [2026-01-01 00:00:00] RAI target=1\nI [2026-01-01 00:00:01] RAI target=1\n"
        candidate = "I [2026-01-01 00:00:00] RAI target=1\nI [2026-01-01 00:00:02] RAI target=1\n"
        with TemporaryDirectory() as temp:
            root = Path(temp); base_dir = root / "base"; candidate_dir = root / "candidate"
            base_dir.mkdir(); candidate_dir.mkdir()
            (base_dir / "increase.log").write_text(baseline)
            (candidate_dir / "increase.log").write_text(candidate)
            result = compare_paired([base_dir], [candidate_dir])
        counts = result["metrics"]["observed_match_duration_s"]["direction_counts"]
        self.assertEqual(counts, {"candidate_increases": 1, "candidate_decreases": 0, "ties": 0})

    def test_directory_inputs_ignore_non_log_files_case_insensitively(self):
        text = "I [2026-01-01 00:00:00] RAI target=1\n"
        with TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "run.LOG").write_text(text)
            (root / "README.md").write_text(text)
            (root / "data.json").write_text(text)
            result = summarize_files([root])
        self.assertEqual(result["files"], [str(root / "run.LOG")])
        self.assertEqual(result["summary"]["run_count"], 1)

    def test_new_duel_telemetry_metrics_are_presence_aware(self):
        text = """I [2026-01-01 00:00:00] RAI target=1 target_dist=10 fire_eligible=1 fire_decision=1 shot_path_conf=0 opponent_unstable=0
I [2026-01-01 00:00:01] RAI target=1 target_dist=20 fire_eligible=1 fire_decision=0 shot_path_conf=0.75 opponent_unstable=1
I [2026-01-01 00:00:02] RAI target=1 target_dist=-5 fire_eligible=0 fire_decision=1 shot_path_conf=-1 opponent_unstable=0
I [2026-01-01 00:00:03] RAI target=1 fire_decision=1
"""
        m = evaluate_text(text)["metrics"]
        self.assertEqual((m["target_distance_observations"], m["target_distance_mean"], m["target_distance_median"]), (2, 15, 15))
        self.assertEqual((m["fire_eligibility_observations"], m["fire_eligibility_truthy_samples"]), (3, 2))
        self.assertAlmostEqual(m["fire_decision_rate_given_eligible"], 0.5)
        self.assertEqual(m["shot_path_confidence_zero_samples"], 1)
        self.assertAlmostEqual(m["shot_path_confidence_positive_fraction"], 0.5)
        self.assertEqual((m["opponent_instability_observations"], m["opponent_instability_truthy_samples"]), (3, 1))

    def test_legacy_logs_leave_new_metrics_unavailable(self):
        m = evaluate_text("I [2026-01-01 00:00:00] RAI target=1 primary=1\n")["metrics"]
        for key in ("target_distance_mean", "fire_eligibility_fraction", "fire_decision_fraction", "fire_decision_rate_given_eligible", "shot_path_confidence_mean", "opponent_instability_fraction"):
            self.assertIsNone(m[key])

    def test_same_second_records_use_gameplay_ticks_for_duration(self):
        text = """I [2026-01-01 00:00:00] RAI tick=100 target=1 state=recharge reason=duel-recharge
I [2026-01-01 00:00:00] RAI tick=200 target=1 state=recharge reason=duel-recharge
I [2026-01-01 00:00:00] RAI tick=300 target=1 state=duel-chase reason=duel-chase
"""
        m = evaluate_text(text)["metrics"]
        self.assertAlmostEqual(m["observed_match_duration_s"], 2.0)
        self.assertAlmostEqual(m["state_occupancy_seconds"]["recharge"], 2.0)

    def test_recharge_conversion_is_censored_by_gap(self):
        text = """I [2026-01-01 00:00:00] RAI tick=100 target=1 state=recharge reason=duel-recharge
I [2026-01-01 00:00:01] RAI tick=200 target=1 state=recharge reason=duel-recharge
I [2026-01-01 00:00:20] RAI tick=2000 target=1 state=rush reason=duel-rush
"""
        m = evaluate_text(text)["metrics"]
        self.assertEqual(m["recharge_episodes"], 1)
        self.assertEqual(m["recharge_exits"], 0)
        self.assertAlmostEqual(m["recharge_censored_gap_s"], 18.0)
        self.assertIsNone(m["recharge_to_aggression_rate"])

    def test_recharge_conversion_is_censored_by_local_death(self):
        text = """I [2026-01-01 00:00:00] P [1] entered arena
I [2026-01-01 00:00:00] RAI tick=100 target=2 state=recharge reason=duel-recharge
I [2026-01-01 00:00:01] RAI tick=200 target=2 state=recharge reason=duel-recharge
I [2026-01-01 00:00:01] P (600) killed by Q (600)
I [2026-01-01 00:00:01] RAI tick=220 target=65535 state=duel-neutral reason=self-unavailable
I [2026-01-01 00:00:02] RAI tick=300 target=2 state=rush reason=duel-rush
"""
        m = evaluate_text(text, local_name="P")["metrics"]
        self.assertEqual(m["deaths"], 1)
        self.assertEqual(m["recharge_episodes"], 1)
        self.assertEqual(m["recharge_exits"], 0)

    def test_explicit_local_identity_overrides_first_entered_player(self):
        text = """I [2026-01-01 00:00:00] Q [2] entered arena
I [2026-01-01 00:00:00] P [1] entered arena
I [2026-01-01 00:00:01] P (600) killed by Q (600)
"""
        inferred = evaluate_text(text)["metrics"]
        explicit = evaluate_text(text, local_name="P")["metrics"]
        self.assertEqual(inferred["local_name"], "Q")
        self.assertEqual(inferred["kills"], 1)
        self.assertEqual(explicit["local_identity_source"], "explicit")
        self.assertEqual(explicit["kills"], 0)
        self.assertEqual(explicit["deaths"], 1)

    def test_ract_rinput_pairing_uses_log_order(self):
        text = """I [2026-01-01 00:00:00] RACT schema=1 stage=post_policy_pre_actuator tick=100 applied_mask=64
I [2026-01-01 00:00:00] RINPUT schema=1 tick=101 applied_mask=69
I [2026-01-01 00:00:00] RINPUT schema=1 tick=102 applied_mask=0
I [2026-01-01 00:00:00] RACT schema=1 stage=post_policy_pre_actuator tick=103 applied_mask=0
"""
        m = evaluate_text(text)["metrics"]
        self.assertEqual(m["ract_records"], 2)
        self.assertEqual(m["rinput_records"], 2)
        self.assertEqual(m["ract_rinput_pairs"], 1)
        self.assertEqual(m["ract_rinput_mask_change_pairs"], 1)
        self.assertEqual(m["ract_orphans"], 1)
        self.assertEqual(m["rinput_orphans"], 1)

    def test_tick_rollover_uses_31_bit_gameplay_semantics(self):
        text = """I [2026-01-01 00:00:00] RAI tick=2147483600 target=1 state=neutral reason=duel-neutral
I [2026-01-01 00:00:00] RAI tick=50 target=1 state=neutral reason=duel-neutral
"""
        m = evaluate_text(text)["metrics"]
        self.assertAlmostEqual(m["observed_match_duration_s"], 0.98)

    def test_half_range_or_backward_tick_is_not_observed_duration(self):
        text = """I [2026-01-01 00:00:00] RAI tick=100 target=1 state=neutral reason=duel-neutral
I [2026-01-01 00:00:00] RAI tick=1073741924 target=1 state=neutral reason=duel-neutral
I [2026-01-01 00:00:00] RAI tick=90 target=1 state=neutral reason=duel-neutral
"""
        m = evaluate_text(text)["metrics"]
        self.assertEqual(m["observed_match_duration_s"], 0)
        self.assertEqual(m["state_occupancy_seconds"], {})

if __name__ == "__main__": unittest.main()
