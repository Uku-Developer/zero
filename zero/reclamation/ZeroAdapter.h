#pragma once

#include <zero/reclamation/Core.h>

namespace zero {

struct BotController;
struct Game;
struct HeuristicEnergyTracker;
struct InputState;

namespace reclamation {

WorldSnapshot CaptureWorld(Game& game, HeuristicEnergyTracker& energy_tracker, float dt);
float EvaluateShotPathConfidence(Game& game, const WorldSnapshot& world, const BotAction& action);
void ApplyAction(BotController& controller, InputState& input, const BotAction& action);

}  // namespace reclamation
}  // namespace zero
