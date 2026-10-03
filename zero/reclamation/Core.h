#pragma once

#include <zero/game/Player.h>

#include <string>
#include <string_view>
#include <vector>

namespace zero {
namespace reclamation {

enum class PlayerRelation {
  Self,
  Ally,
  Enemy,
};

struct PlayerSnapshot {
  PlayerId id = kInvalidPlayerId;
  PlayerRelation relation = PlayerRelation::Enemy;
  u16 frequency = 0;
  u8 ship = 8;
  Vector2f position;
  Vector2f velocity;
  Vector2f heading;
  float energy = 0.0f;
  float energy_confidence = 0.0f;
  bool synchronized = false;
  bool respawning = false;
  bool line_of_sight_from_self = false;
};

struct ProjectileSnapshot {
  PlayerId owner_id = kInvalidPlayerId;
  u16 frequency = 0;
  WeaponType type = WeaponType::None;
  u16 level = 0;
  bool alternate = false;
  float estimated_damage = 0.0f;
  float remaining_seconds = 0.0f;
  Vector2f position;
  Vector2f velocity;
};

struct GeometryProbe {
  Vector2f position;
  bool solid = false;
};

struct CombatSnapshot {
  float max_energy = 0.0f;
  float bullet_speed = 0.0f;
  float bomb_speed = 0.0f;
  float bullet_alive_seconds = 0.0f;
  float ship_radius = 0.0f;
};

struct MatchContext {
  std::string arena_name;
};

struct WorldSnapshot {
  u32 tick = 0;
  float dt = 0.0f;
  MatchContext match;
  bool has_self = false;
  PlayerSnapshot self;
  CombatSnapshot combat;
  std::vector<PlayerSnapshot> players;
  std::vector<ProjectileSnapshot> projectiles;
  std::vector<GeometryProbe> geometry;
};

struct BotAction {
  PlayerId target_id = kInvalidPlayerId;
  bool has_move_target = false;
  Vector2f move_target;
  float desired_distance = 0.0f;
  bool has_aim_target = false;
  Vector2f aim_target;
  bool fire_bullet = false;
  bool fire_bomb = false;
  bool fire_eligible = false;
  float target_distance = -1.0f;
  float threat_score = 0.0f;
  float energy_advantage = 0.0f;
  float intercept_time = -1.0f;
  float intercept_confidence = 0.0f;
  bool has_predicted_target_position = false;
  Vector2f predicted_target_position;
  bool opponent_unstable = false;
  bool state_changed = false;
  u32 state_age_ticks = 0;
  bool target_visible = false;
  std::string_view policy_state = "idle";
  std::string_view reason = "idle";
};

struct BotTelemetry {
  u32 tick = 0;
  std::string_view policy_name;
  std::string_view policy_version;
  float self_energy = -1.0f;
  PlayerId target_id = kInvalidPlayerId;
  float target_energy = -1.0f;
  float target_distance = -1.0f;
  bool fire_eligible = false;
  bool fire_bullet = false;
  float shot_path_confidence = -1.0f;
  bool opponent_unstable = false;
  float threat_score = 0.0f;
  float energy_advantage = 0.0f;
  float intercept_time = -1.0f;
  float intercept_confidence = 0.0f;
  bool state_changed = false;
  u32 state_age_ticks = 0;
  bool target_visible = false;
  std::string_view policy_state = "idle";
  std::string_view reason = "idle";
  long long observe_us = 0;
  long long decide_us = 0;
  long long apply_us = 0;
};

class Policy {
 public:
  virtual ~Policy() = default;

  virtual std::string_view Name() const = 0;
  virtual std::string_view Version() const = 0;
  virtual BotAction Decide(const WorldSnapshot& world) = 0;
};

}  // namespace reclamation
}  // namespace zero
