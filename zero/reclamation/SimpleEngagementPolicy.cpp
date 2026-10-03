// Subspace Reclamation in-game bot policy. This only selects simulated game opponents.
#include <zero/reclamation/SimpleEngagementPolicy.h>

#include <cfloat>

namespace zero {
namespace reclamation {

BotAction SimpleEngagementPolicy::Decide(const WorldSnapshot& world) {
  BotAction action;
  if (!world.has_self || world.self.ship >= 8 || world.self.respawning) {
    action.reason = "self-unavailable";
    return action;
  }

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

  if (!target) {
    action.reason = "no-opponent";
    return action;
  }

  float distance = sqrtf(best_distance_sq);
  float lead_time = distance / 200.0f;
  if (lead_time > 0.35f) lead_time = 0.35f;

  Vector2f aim_point = target->position + target->velocity * lead_time;
  action.target_id = target->id;
  action.has_aim_target = true;
  action.aim_target = aim_point;
  action.has_move_target = true;
  action.move_target = aim_point;
  action.desired_distance = 18.0f;
  action.reason = "engage-nearest-opponent";

  Vector2f to_aim = aim_point - world.self.position;
  if (to_aim.LengthSq() > 0.0f) {
    to_aim.Normalize();
    float alignment = world.self.heading.Dot(to_aim);
    action.fire_bullet = distance <= 80.0f && alignment >= 0.985f;
  }

  return action;
}

}  // namespace reclamation
}  // namespace zero
