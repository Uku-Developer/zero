#pragma once

#include <zero/reclamation/Core.h>

namespace zero {
namespace reclamation {

class SimpleEngagementPolicy final : public Policy {
 public:
  std::string_view Name() const override { return "simple-engagement"; }
  std::string_view Version() const override { return "core-v1"; }
  BotAction Decide(const WorldSnapshot& world) override;
};

}  // namespace reclamation
}  // namespace zero
