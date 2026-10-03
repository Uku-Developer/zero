// Adapter from Zero's client-observable game state to the Reclamation policy contract.
#include <zero/reclamation/ZeroAdapter.h>

#include <zero/BotController.h>
#include <zero/HeuristicEnergyTracker.h>
#include <zero/game/Clock.h>
#include <zero/game/Game.h>
#include <zero/game/Map.h>
#include <zero/game/WeaponManager.h>

namespace zero {
namespace reclamation {

static PlayerSnapshot CapturePlayer(Game& game, HeuristicEnergyTracker& energy_tracker, Player& player,
                                    const Player* self) {
  PlayerSnapshot result;
  result.id = player.id;
  result.frequency = player.frequency;
  result.ship = player.ship;
  result.position = player.position;
  result.velocity = player.velocity;
  result.heading = player.GetHeading();
  result.energy = energy_tracker.GetEnergy(player);
  result.energy_confidence = player.energy > 0.0f ? 1.0f : 0.5f;
  result.synchronized = game.player_manager.IsSynchronized(player);
  result.respawning = player.IsRespawning();
  if (self) {
    result.line_of_sight_from_self = player.id == self->id ||
                                     !game.GetMap().CastTo(self->position, player.position, self->frequency).hit;
  }

  if (self && player.id == self->id) {
    result.relation = PlayerRelation::Self;
  } else if (self && player.frequency == self->frequency) {
    result.relation = PlayerRelation::Ally;
  } else {
    result.relation = PlayerRelation::Enemy;
  }

  return result;
}

WorldSnapshot CaptureWorld(Game& game, HeuristicEnergyTracker& energy_tracker, float dt) {
  WorldSnapshot world;
  world.tick = GetCurrentTick();
  world.dt = dt;
  world.match.arena_name = game.arena_name;

  Player* self = game.player_manager.GetSelf();
  if (self) {
    world.has_self = true;
    world.self = CapturePlayer(game, energy_tracker, *self, self);
    if (self->ship < 8) {
      auto& ship_settings = game.connection.settings.ShipSettings[self->ship];
      world.combat.max_energy = (float)ship_settings.MaximumEnergy;
      world.combat.bullet_speed = ship_settings.BulletSpeed / 16.0f / 10.0f;
      world.combat.bomb_speed = ship_settings.BombSpeed / 16.0f / 10.0f;
      world.combat.bullet_alive_seconds = game.connection.settings.BulletAliveTime / 100.0f;
      world.combat.ship_radius = ship_settings.GetRadius();
    }
  }

  world.players.reserve(game.player_manager.player_count);
  for (size_t i = 0; i < game.player_manager.player_count; ++i) {
    Player& player = game.player_manager.players[i];
    if (self && player.id == self->id) continue;
    world.players.push_back(CapturePlayer(game, energy_tracker, player, self));
  }

  world.projectiles.reserve(game.weapon_manager.weapon_count);
  for (size_t i = 0; i < game.weapon_manager.weapon_count; ++i) {
    Weapon& weapon = game.weapon_manager.weapons[i];
    ProjectileSnapshot projectile;
    projectile.owner_id = weapon.player_id;
    projectile.frequency = weapon.frequency;
    projectile.type = weapon.data.type;
    projectile.level = weapon.data.level;
    projectile.alternate = weapon.data.alternate != 0;
    projectile.estimated_damage = (float)GetEstimatedWeaponDamage(weapon, game.connection);
    s32 remaining_ticks = TICK_DIFF(weapon.end_tick, world.tick);
    projectile.remaining_seconds = remaining_ticks > 0 ? remaining_ticks / 100.0f : 0.0f;
    projectile.position = weapon.position;
    projectile.velocity = weapon.velocity;
    world.projectiles.push_back(projectile);
  }

  if (self) {
    static const Vector2f kProbeOffsets[] = {
        Vector2f(4, 0),  Vector2f(-4, 0), Vector2f(0, 4),  Vector2f(0, -4),
        Vector2f(4, 4),  Vector2f(4, -4), Vector2f(-4, 4), Vector2f(-4, -4),
    };

    world.geometry.reserve(sizeof(kProbeOffsets) / sizeof(kProbeOffsets[0]));
    for (const Vector2f& offset : kProbeOffsets) {
      GeometryProbe probe;
      probe.position = self->position + offset;
      probe.solid = game.GetMap().IsSolid(probe.position, self->frequency);
      world.geometry.push_back(probe);
    }
  }

  return world;
}

float EvaluateShotPathConfidence(Game& game, const WorldSnapshot& world, const BotAction& action) {
  if (!world.has_self || !action.has_predicted_target_position || action.intercept_time < 0.0f) {
    return -1.0f;
  }

  if (game.GetMap().CastTo(world.self.position, action.predicted_target_position, world.self.frequency).hit) {
    return 0.0f;
  }

  return action.intercept_confidence;
}

void ApplyAction(BotController& controller, InputState& input, const BotAction& action) {
  if (action.has_aim_target) {
    controller.steering.Face(controller.game, action.aim_target);
  }

  if (action.has_move_target) {
    controller.steering.Seek(controller.game, action.move_target, action.desired_distance);
    controller.steering.AvoidWalls(controller.game);
  }

  input.SetAction(InputAction::Bullet, action.fire_bullet);
  input.SetAction(InputAction::Bomb, action.fire_bomb);
}

}  // namespace reclamation
}  // namespace zero
