#include <zero/reclamation/DuelPolicy.h>
#include <zero/reclamation/TrajectoryPredictor.h>
#include <zero/game/Clock.h>

#include <cfloat>
#include <cmath>

namespace zero {
namespace reclamation {

const char* ToString(DuelState state) {
  switch (state) {
    case DuelState::Neutral: return "duel-neutral";
    case DuelState::Pressure: return "duel-pressure";
    case DuelState::Rush: return "duel-rush";
    case DuelState::Disengage: return "duel-disengage";
    case DuelState::Recharge: return "duel-recharge";
    case DuelState::Chase: return "duel-chase";
    case DuelState::Escape: return "duel-escape";
    case DuelState::Finish: return "duel-finish";
  }

  return "duel-neutral";
}

bool DuelPolicy::UpdateState(DuelState state, u32 tick) {
  bool changed = !state_initialized_ || state != last_state_;
  if (changed) {
    last_state_ = state;
    state_enter_tick_ = tick;
    state_initialized_ = true;
  }
  return changed;
}

u32 DuelPolicy::StateAge(u32 tick) const {
  if (!state_initialized_) return 0;
  const s32 age = TICK_DIFF(tick, state_enter_tick_);
  return age > 0 ? static_cast<u32>(age) : 0;
}

void DuelPolicy::ResetOpponentMemory() {
  opponent_memory_initialized_ = false;
  observed_target_id_ = kInvalidPlayerId;
  observed_target_velocity_ = Vector2f();
  observed_target_tick_initialized_ = false;
  observed_target_tick_ = 0;
  unstable_until_initialized_ = false;
  unstable_until_tick_ = 0;
}

bool DuelPolicy::UpdateOpponentMemory(const PlayerSnapshot& target, u32 tick) {
  if (!opponent_memory_initialized_ || observed_target_id_ != target.id) {
    ResetOpponentMemory();
    opponent_memory_initialized_ = true;
    observed_target_id_ = target.id;
  }

  const s32 observed_age = TICK_DIFF(tick, observed_target_tick_);
  const bool recent_same_target = observed_target_tick_initialized_ && observed_age >= 0 && observed_age <= 1000;
  if (recent_same_target) {
    float previous_speed_sq = observed_target_velocity_.LengthSq();
    float current_speed_sq = target.velocity.LengthSq();
    if (previous_speed_sq > 1.0f && current_speed_sq > 1.0f) {
      Vector2f previous_direction = observed_target_velocity_;
      Vector2f current_direction = target.velocity;
      previous_direction.Normalize();
      current_direction.Normalize();
      if (previous_direction.Dot(current_direction) < -0.25f) {
        unstable_until_tick_ = MAKE_TICK(tick + 500);
        unstable_until_initialized_ = true;
      }
    }
  }

  observed_target_velocity_ = target.velocity;
  observed_target_tick_ = tick;
  observed_target_tick_initialized_ = true;

  const bool unstable = unstable_until_initialized_ && TICK_GTE(unstable_until_tick_, tick);
  if (!unstable) unstable_until_initialized_ = false;
  return unstable;
}

const PlayerSnapshot* DuelPolicy::SelectTarget(const WorldSnapshot& world) const {
  const PlayerSnapshot* target = nullptr;
  float best_distance_sq = FLT_MAX;

  for (const PlayerSnapshot& player : world.players) {
    if (player.relation != PlayerRelation::Enemy || !player.synchronized || player.ship >= 8 || player.respawning) {
      continue;
    }

    float distance_sq = world.self.position.DistanceSq(player.position);
    if (distance_sq < best_distance_sq) {
      best_distance_sq = distance_sq;
      target = &player;
    }
  }

  return target;
}

DuelPolicy::ThreatSummary DuelPolicy::AnalyzeThreat(const WorldSnapshot& world) const {
  ThreatSummary result;
  float earliest = FLT_MAX;
  float self_energy = world.self.energy > 1.0f ? world.self.energy : 1.0f;
  float ship_radius = world.combat.ship_radius > 0.0f ? world.combat.ship_radius : 0.9f;

  for (const ProjectileSnapshot& projectile : world.projectiles) {
    if (projectile.frequency == world.self.frequency) continue;

    bool explosive = projectile.type == WeaponType::Bomb || projectile.type == WeaponType::ProximityBomb ||
                     projectile.type == WeaponType::Thor || projectile.type == WeaponType::Burst;
    bool direct = projectile.type == WeaponType::Bullet || projectile.type == WeaponType::BouncingBullet || explosive;
    if (!direct) continue;

    Vector2f relative_position = projectile.position - world.self.position;
    Vector2f relative_velocity = projectile.velocity - world.self.velocity;
    float speed_sq = relative_velocity.LengthSq();
    float danger_radius = explosive ? 5.0f : ship_radius * 2.0f;
    if (danger_radius < 1.5f) danger_radius = 1.5f;

    if (speed_sq <= 0.0001f) {
      if (!explosive || relative_position.LengthSq() > danger_radius * danger_radius ||
          projectile.remaining_seconds == 0.0f) {
        continue;
      }

      float damage = projectile.estimated_damage > 0.0f ? projectile.estimated_damage : 1000.0f;
      result.incoming = true;
      result.explosive = true;
      result.projected_damage += damage;
      if (0.0f < earliest) earliest = 0.0f;
      continue;
    }

    float time_to_closest = -relative_position.Dot(relative_velocity) / speed_sq;
    if (time_to_closest < 0.0f || time_to_closest > config_.threat_horizon_seconds) continue;
    if (projectile.remaining_seconds >= 0.0f && time_to_closest > projectile.remaining_seconds) continue;

    Vector2f closest = relative_position + relative_velocity * time_to_closest;
    if (closest.LengthSq() > danger_radius * danger_radius) continue;

    float damage = projectile.estimated_damage > 0.0f ? projectile.estimated_damage : (explosive ? 1000.0f : 200.0f);
    float urgency = 1.0f - (time_to_closest / config_.threat_horizon_seconds) * 0.5f;
    if (urgency < 0.5f) urgency = 0.5f;

    result.incoming = true;
    result.explosive = result.explosive || explosive;
    result.projected_damage += damage * urgency;
    if (time_to_closest < earliest) earliest = time_to_closest;
  }

  if (result.incoming) {
    result.time_to_impact = earliest;
    result.score = result.projected_damage / self_energy;
  }
  return result;
}

DuelState DuelPolicy::SelectState(const WorldSnapshot& world, const PlayerSnapshot& target, float distance,
                                  const ThreatSummary& threat) const {
  bool target_energy_known = target.energy_confidence > 0.0f;
  float energy_advantage = world.self.energy - target.energy;
  bool max_energy_known = world.combat.max_energy > 1.0f;
  float max_energy = world.combat.max_energy;
  float recharge_enter = max_energy_known && config_.recharge_enter_fraction > 0.0f
                             ? max_energy * config_.recharge_enter_fraction
                             : config_.recharge_self_energy;
  float recharge_exit = max_energy_known && config_.recharge_exit_fraction > 0.0f
                            ? max_energy * config_.recharge_exit_fraction
                            : config_.recharge_self_energy;
  float disengage_energy = max_energy_known && config_.disengage_self_fraction > 0.0f
                               ? max_energy * config_.disengage_self_fraction
                               : config_.disengage_self_energy;
  float escape_energy = max_energy_known && config_.escape_self_fraction > 0.0f
                            ? max_energy * config_.escape_self_fraction
                            : config_.escape_self_energy;

  if (threat.incoming &&
      (threat.score >= config_.escape_threat_score ||
       (threat.explosive && world.self.energy <= escape_energy))) {
    return DuelState::Escape;
  }

  if (threat.incoming &&
      (threat.score >= config_.disengage_threat_score || world.self.energy <= disengage_energy)) {
    return DuelState::Disengage;
  }

  if (state_initialized_ && last_state_ == DuelState::Recharge && world.self.energy < recharge_exit) {
    return DuelState::Recharge;
  }

  if (world.self.energy <= recharge_enter) {
    return DuelState::Recharge;
  }

  if (target_energy_known && target.energy <= config_.finish_target_energy && distance <= config_.finish_distance &&
      energy_advantage >= config_.pressure_energy_advantage) {
    return DuelState::Finish;
  }

  if (target_energy_known && target.energy <= config_.rush_target_energy && distance <= config_.rush_distance) {
    return DuelState::Rush;
  }

  if (target_energy_known && target.energy <= config_.pressure_target_energy &&
      (energy_advantage >= config_.pressure_energy_advantage || distance <= config_.pressure_distance)) {
    return DuelState::Pressure;
  }

  if (distance >= config_.chase_distance) {
    return DuelState::Chase;
  }

  return DuelState::Neutral;
}

BotAction DuelPolicy::BuildAction(const WorldSnapshot& world, const PlayerSnapshot& target, float distance,
                                  DuelState state, bool opponent_unstable) const {
  BotAction action;

  float fallback_lead = distance / 220.0f;
  if (fallback_lead > 0.40f) fallback_lead = 0.40f;
  if (opponent_unstable && fallback_lead > 0.10f) fallback_lead = 0.10f;
  Vector2f relative_target_velocity = target.velocity - world.self.velocity;
  Vector2f aim_point = target.position + relative_target_velocity * fallback_lead;
  Vector2f predicted_target_position = target.position + target.velocity * fallback_lead;
  action.intercept_time = fallback_lead;
  action.intercept_confidence = opponent_unstable ? 0.10f : 0.25f;

  float max_intercept_time = config_.max_intercept_seconds;
  if (world.combat.bullet_alive_seconds > 0.0f && world.combat.bullet_alive_seconds < max_intercept_time) {
    max_intercept_time = world.combat.bullet_alive_seconds;
  }

  if (!opponent_unstable && world.combat.bullet_speed > 0.0f && max_intercept_time > 0.0f) {
    InterceptSolution intercept = PredictIntercept(world.self.position, world.self.velocity, target.position,
                                                   target.velocity, world.combat.bullet_speed, max_intercept_time);
    if (intercept.valid) {
      aim_point = intercept.aim_point;
      predicted_target_position = target.position + target.velocity * intercept.time;
      action.intercept_time = intercept.time;
      action.intercept_confidence = intercept.confidence;
    }
  }

  Vector2f to_target = target.position - world.self.position;
  Vector2f target_direction = to_target;
  if (target_direction.LengthSq() > 0.0f) target_direction.Normalize();

  action.target_id = target.id;
  action.target_distance = distance;
  action.target_visible = target.line_of_sight_from_self;
  action.has_predicted_target_position = true;
  action.predicted_target_position = predicted_target_position;
  action.opponent_unstable = opponent_unstable;
  action.policy_state = ToString(state);
  action.has_aim_target = true;
  action.aim_target = aim_point;
  action.has_move_target = true;
  action.move_target = aim_point;
  action.reason = ToString(state);

  switch (state) {
    case DuelState::Pressure: action.desired_distance = 14.0f; break;
    case DuelState::Rush: action.desired_distance = 0.0f; break;
    case DuelState::Recharge: action.desired_distance = 36.0f; break;
    case DuelState::Chase: action.desired_distance = 18.0f; break;
    case DuelState::Finish: action.desired_distance = 5.0f; break;
    case DuelState::Disengage:
      action.move_target = world.self.position - target_direction * 22.0f;
      action.desired_distance = 0.0f;
      break;
    case DuelState::Escape:
      action.move_target = world.self.position - target_direction * 35.0f;
      action.desired_distance = 0.0f;
      break;
    case DuelState::Neutral: action.desired_distance = 22.0f; break;
  }

  bool suppress_fire = state == DuelState::Recharge || state == DuelState::Escape ||
                       (state == DuelState::Disengage && world.self.energy < 500.0f);
  Vector2f to_aim = aim_point - world.self.position;
  action.fire_eligible = !suppress_fire && target.line_of_sight_from_self &&
                         distance <= config_.fire_distance && to_aim.LengthSq() > 0.0f;
  if (action.fire_eligible) {
    to_aim.Normalize();
    float threshold = state == DuelState::Rush ? 0.960f : (state == DuelState::Finish ? 0.975f : 0.985f);
    action.fire_bullet = world.self.heading.Dot(to_aim) >= threshold;
  }

  return action;
}

BotAction DuelPolicy::Decide(const WorldSnapshot& world) {
  BotAction action;
  if (!world.has_self || world.self.ship >= 8 || world.self.respawning) {
    ResetOpponentMemory();
    action.state_changed = UpdateState(DuelState::Neutral, world.tick);
    action.state_age_ticks = StateAge(world.tick);
    action.policy_state = ToString(DuelState::Neutral);
    action.reason = "self-unavailable";
    return action;
  }

  const PlayerSnapshot* target = SelectTarget(world);
  if (!target) {
    ResetOpponentMemory();
    action.state_changed = UpdateState(DuelState::Neutral, world.tick);
    action.state_age_ticks = StateAge(world.tick);
    action.policy_state = ToString(DuelState::Neutral);
    action.reason = "no-opponent";
    return action;
  }

  float distance = sqrtf(world.self.position.DistanceSq(target->position));
  ThreatSummary threat = AnalyzeThreat(world);
  DuelState selected_state = SelectState(world, *target, distance, threat);
  bool state_changed = UpdateState(selected_state, world.tick);
  bool opponent_unstable = UpdateOpponentMemory(*target, world.tick);

  BotAction result = BuildAction(world, *target, distance, last_state_, opponent_unstable);
  result.threat_score = threat.score;
  result.energy_advantage = world.self.energy - target->energy;
  result.state_changed = state_changed;
  result.state_age_ticks = StateAge(world.tick);
  return result;
}

}  // namespace reclamation
}  // namespace zero
