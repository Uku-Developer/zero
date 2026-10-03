#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/game/Logger.h>
#include <zero/zones/ZoneController.h>
#include <zero/zones/reclamation/ReclamationBehavior.h>
#include <zero/zones/reclamation/ReclamationDuelBehavior.h>

#include <memory>

namespace zero {
namespace reclamation_zone {

struct ReclamationController final : ZoneController {
  bool IsZone(Zone zone) override { return zone == Zone::Reclamation; }
  void CreateBehaviors(const char* arena_name) override;
};

static ReclamationController controller;

void ReclamationController::CreateBehaviors(const char* arena_name) {
  (void)arena_name;
  Log(LogLevel::Info, "Registering Reclamation core and duel behaviors.");

  bot->bot_controller->energy_tracker.estimate_type = EnergyHeuristicType::Average;
  auto& repo = bot->bot_controller->behaviors;
  repo.Add("core", std::make_unique<ReclamationBehavior>());
  repo.Add("duel", std::make_unique<ReclamationDuelBehavior>());
  SetBehavior("core");
}

// End Reclamation game-zone controller scope.
}  // namespace reclamation_zone
}  // namespace zero
