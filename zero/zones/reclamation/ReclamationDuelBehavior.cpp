#include <zero/zones/reclamation/ReclamationDuelBehavior.h>

#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/nodes/ShipNode.h>
#include <zero/reclamation/DuelPolicy.h>
#include <zero/reclamation/PolicyNode.h>

#include <memory>

namespace zero {
namespace reclamation_zone {

void ReclamationDuelBehavior::OnInitialize(behavior::ExecuteContext& ctx) {
  (void)ctx;
}

std::unique_ptr<behavior::BehaviorNode> ReclamationDuelBehavior::CreateTree(behavior::ExecuteContext& ctx) {
  (void)ctx;
  using namespace behavior;

  BehaviorBuilder builder;
  builder
      .Selector()
          .Sequence()
              .InvertChild<ShipQueryNode>("request_ship")
              .Child<ShipRequestNode>("request_ship")
              .End()
          .Composite(std::make_unique<reclamation::PolicyNode>(std::make_unique<reclamation::DuelPolicy>()))
          .End();

  return builder.Build();
}

}  // namespace reclamation_zone
}  // namespace zero
