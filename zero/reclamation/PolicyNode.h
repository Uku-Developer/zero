#pragma once

#include <zero/behavior/BehaviorTree.h>
#include <zero/reclamation/Core.h>

#include <memory>

namespace zero {
namespace reclamation {

class PolicyNode final : public behavior::BehaviorNode {
 public:
  explicit PolicyNode(std::unique_ptr<Policy> policy) : policy_(std::move(policy)) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override;

 private:
  std::unique_ptr<Policy> policy_;
  bool telemetry_initialized_ = false;
  u32 last_telemetry_tick_ = 0;
};

}  // namespace reclamation
}  // namespace zero
