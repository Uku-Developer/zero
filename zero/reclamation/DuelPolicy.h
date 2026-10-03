#pragma once

#include <zero/reclamation/Core.h>

namespace zero {
namespace reclamation {

enum class DuelState {
  Neutral,
  Pressure,
  Rush,
  Disengage,
  Recharge,
  Chase,
  Escape,
  Finish,
};

const char* ToString(DuelState state);

struct DuelPolicyConfig {
  float pressure_target_energy = 800.0f;
  float rush_target_energy = 400.0f;
  float finish_target_energy = 250.0f;
  float recharge_self_energy = 600.0f;
  float recharge_enter_fraction = 0.35f;
  float recharge_exit_fraction = 0.45f;
  float disengage_self_energy = 800.0f;
  float disengage_self_fraction = 0.47f;
  float escape_self_energy = 350.0f;
  float escape_self_fraction = 0.21f;
  float pressure_energy_advantage = 300.0f;
  float rush_distance = 12.0f;
  float finish_distance = 30.0f;
  float pressure_distance = 28.0f;
  float chase_distance = 50.0f;
  float fire_distance = 85.0f;
  float threat_horizon_seconds = 0.75f;
  float disengage_threat_score = 0.25f;
  float escape_threat_score = 0.85f;
  float max_intercept_seconds = 2.0f;
};

class DuelPolicy final : public Policy {
 public:
  explicit DuelPolicy(DuelPolicyConfig config = {}) : config_(config) {}

  std::string_view Name() const override { return "duel"; }
  std::string_view Version() const override { return "duel-v3"; }
  BotAction Decide(const WorldSnapshot& world) override;

  DuelState LastState() const { return last_state_; }

 private:
  struct ThreatSummary {
    bool incoming = false;
    bool explosive = false;
    float time_to_impact = -1.0f;
    float projected_damage = 0.0f;
    float score = 0.0f;
  };

  const PlayerSnapshot* SelectTarget(const WorldSnapshot& world) const;
  ThreatSummary AnalyzeThreat(const WorldSnapshot& world) const;
  DuelState SelectState(const WorldSnapshot& world, const PlayerSnapshot& target, float distance,
                        const ThreatSummary& threat) const;
  BotAction BuildAction(const WorldSnapshot& world, const PlayerSnapshot& target, float distance,
                        DuelState state, bool opponent_unstable) const;
  bool UpdateState(DuelState state, u32 tick);
  u32 StateAge(u32 tick) const;
  void ResetOpponentMemory();
  bool UpdateOpponentMemory(const PlayerSnapshot& target, u32 tick);

  DuelPolicyConfig config_;
  DuelState last_state_ = DuelState::Neutral;
  bool state_initialized_ = false;
  u32 state_enter_tick_ = 0;
  bool opponent_memory_initialized_ = false;
  PlayerId observed_target_id_ = kInvalidPlayerId;
  Vector2f observed_target_velocity_;
  bool observed_target_tick_initialized_ = false;
  u32 observed_target_tick_ = 0;
  bool unstable_until_initialized_ = false;
  u32 unstable_until_tick_ = 0;
};

}  // namespace reclamation
}  // namespace zero
