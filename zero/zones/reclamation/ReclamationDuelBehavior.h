#pragma once

#include <zero/behavior/Behavior.h>

namespace zero {
namespace reclamation_zone {

struct ReclamationDuelBehavior final : behavior::Behavior {
  void OnInitialize(behavior::ExecuteContext& ctx) override;
  std::unique_ptr<behavior::BehaviorNode> CreateTree(behavior::ExecuteContext& ctx) override;
};

}  // namespace reclamation_zone
}  // namespace zero
