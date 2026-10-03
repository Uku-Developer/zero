// Bridges Zero's behavior-tree execution loop to a transport-independent Reclamation policy.
#include <zero/reclamation/PolicyNode.h>

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/game/Clock.h>
#include <zero/game/Logger.h>
#include <zero/reclamation/AppliedInputTelemetry.h>
#include <zero/reclamation/ZeroAdapter.h>

#include <chrono>

namespace zero {
namespace reclamation {

behavior::ExecuteResult PolicyNode::Execute(behavior::ExecuteContext& ctx) {
  if (!ctx.bot || !ctx.bot->game || !ctx.bot->bot_controller || !policy_) {
    return behavior::ExecuteResult::Failure;
  }

  BotController& controller = *ctx.bot->bot_controller;
  auto observe_start = std::chrono::high_resolution_clock::now();
  WorldSnapshot world = CaptureWorld(*ctx.bot->game, controller.energy_tracker, ctx.dt);
  auto observe_end = std::chrono::high_resolution_clock::now();

  auto decide_start = observe_end;
  BotAction action = policy_->Decide(world);
  auto decide_end = std::chrono::high_resolution_clock::now();

  auto apply_start = decide_end;
  float shot_path_confidence = EvaluateShotPathConfidence(*ctx.bot->game, world, action);
  ApplyAction(controller, ctx.bot->input, action);
  auto apply_end = std::chrono::high_resolution_clock::now();

  if (IsReclamationActionTelemetryEnabled()) {
    const AppliedInputSnapshot applied = CaptureAppliedInput(ctx.bot->input);

    // move_x/move_y and aim_x/aim_y are zero-filled and must be ignored when
    // their corresponding has_move/has_aim field is false.
    Log(LogLevel::Info,
        "RACT schema=1 stage=post_policy_pre_actuator tick=%u policy=%.*s/%.*s target=%u has_move=%d move_x=%.3f move_y=%.3f "
        "desired_dist=%.3f has_aim=%d aim_x=%.3f aim_y=%.3f requested_bullet=%d requested_bomb=%d "
        "applied_mask=%u left=%d right=%d forward=%d backward=%d afterburner=%d bullet=%d bomb=%d "
        "shot_path_conf=%.3f state=%.*s reason=%.*s",
        world.tick, (int)policy_->Name().size(), policy_->Name().data(),
        (int)policy_->Version().size(), policy_->Version().data(), (u32)action.target_id,
        action.has_move_target ? 1 : 0, action.has_move_target ? action.move_target.x : 0.0f,
        action.has_move_target ? action.move_target.y : 0.0f, action.desired_distance,
        action.has_aim_target ? 1 : 0, action.has_aim_target ? action.aim_target.x : 0.0f,
        action.has_aim_target ? action.aim_target.y : 0.0f, action.fire_bullet ? 1 : 0,
        action.fire_bomb ? 1 : 0, applied.action_mask, applied.left ? 1 : 0,
        applied.right ? 1 : 0, applied.forward ? 1 : 0, applied.backward ? 1 : 0,
        applied.afterburner ? 1 : 0, applied.bullet ? 1 : 0, applied.bomb ? 1 : 0,
        shot_path_confidence, (int)action.policy_state.size(), action.policy_state.data(),
        (int)action.reason.size(), action.reason.data());
  }

  if (ShouldEmitReclamationDecisionTelemetry(action.state_changed, telemetry_initialized_, world.tick,
                                               last_telemetry_tick_)) {
    BotTelemetry telemetry;
    telemetry.tick = world.tick;
    telemetry.policy_name = policy_->Name();
    telemetry.policy_version = policy_->Version();
    telemetry.self_energy = world.has_self ? world.self.energy : -1.0f;
    telemetry.target_id = action.target_id;
    telemetry.target_distance = action.target_distance;
    telemetry.fire_eligible = action.fire_eligible;
    telemetry.fire_bullet = action.fire_bullet;
    telemetry.shot_path_confidence = shot_path_confidence;
    telemetry.opponent_unstable = action.opponent_unstable;
    telemetry.threat_score = action.threat_score;
    telemetry.energy_advantage = action.energy_advantage;
    telemetry.intercept_time = action.intercept_time;
    telemetry.intercept_confidence = action.intercept_confidence;
    telemetry.state_changed = action.state_changed;
    telemetry.state_age_ticks = action.state_age_ticks;
    telemetry.target_visible = action.target_visible;
    telemetry.policy_state = action.policy_state;
    telemetry.reason = action.reason;

    for (const PlayerSnapshot& player : world.players) {
      if (player.id == action.target_id) {
        telemetry.target_energy = player.energy;
        break;
      }
    }

    telemetry.observe_us = std::chrono::duration_cast<std::chrono::microseconds>(observe_end - observe_start).count();
    telemetry.decide_us = std::chrono::duration_cast<std::chrono::microseconds>(decide_end - decide_start).count();
    telemetry.apply_us = std::chrono::duration_cast<std::chrono::microseconds>(apply_end - apply_start).count();

    Log(LogLevel::Info,
        "RAI tick=%u policy=%.*s/%.*s self_energy=%.1f target=%u target_energy=%.1f target_dist=%.2f primary=%d "
        "fire_eligible=%d fire_decision=%d shot_path_conf=%.3f opponent_unstable=%d threat=%.3f advantage=%.1f "
        "intercept_s=%.3f intercept_conf=%.3f transition=%d state_age=%u visible=%d "
        "state=%.*s reason=%.*s observe_us=%lld decide_us=%lld apply_us=%lld",
        telemetry.tick, (int)telemetry.policy_name.size(), telemetry.policy_name.data(),
        (int)telemetry.policy_version.size(), telemetry.policy_version.data(), telemetry.self_energy,
        (u32)telemetry.target_id, telemetry.target_energy, telemetry.target_distance, telemetry.fire_bullet ? 1 : 0,
        telemetry.fire_eligible ? 1 : 0, telemetry.fire_bullet ? 1 : 0, telemetry.shot_path_confidence,
        telemetry.opponent_unstable ? 1 : 0, telemetry.threat_score, telemetry.energy_advantage,
        telemetry.intercept_time, telemetry.intercept_confidence, telemetry.state_changed ? 1 : 0,
        telemetry.state_age_ticks, telemetry.target_visible ? 1 : 0,
        (int)telemetry.policy_state.size(), telemetry.policy_state.data(),
        (int)telemetry.reason.size(), telemetry.reason.data(), telemetry.observe_us, telemetry.decide_us,
        telemetry.apply_us);
    last_telemetry_tick_ = world.tick;
    telemetry_initialized_ = true;
  }

  return behavior::ExecuteResult::Success;
}

}  // namespace reclamation
}  // namespace zero
