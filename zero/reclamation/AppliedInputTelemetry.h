#pragma once

#include <zero/game/Clock.h>
#include <zero/game/InputState.h>

#include <cstdlib>
#include <string_view>

namespace zero {
namespace reclamation {

struct AppliedInputSnapshot {
  u32 action_mask = 0;
  bool left = false;
  bool right = false;
  bool forward = false;
  bool backward = false;
  bool afterburner = false;
  bool bullet = false;
  bool bomb = false;
};

inline AppliedInputSnapshot CaptureAppliedInput(const InputState& input) {
  AppliedInputSnapshot snapshot;
  snapshot.action_mask = input.actions;
  snapshot.left = input.IsDown(InputAction::Left);
  snapshot.right = input.IsDown(InputAction::Right);
  snapshot.forward = input.IsDown(InputAction::Forward);
  snapshot.backward = input.IsDown(InputAction::Backward);
  snapshot.afterburner = input.IsDown(InputAction::Afterburner);
  snapshot.bullet = input.IsDown(InputAction::Bullet);
  snapshot.bomb = input.IsDown(InputAction::Bomb);
  return snapshot;
}

inline bool IsReclamationActionTelemetryValueEnabled(std::string_view value) {
  if (value.size() == 1 && value[0] == '1') return true;

  auto ascii_lower = [](char value) {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
  };
  auto equals_case_insensitive = [&](std::string_view expected) {
    if (value.size() != expected.size()) return false;
    for (size_t i = 0; i < value.size(); ++i) {
      if (ascii_lower(value[i]) != expected[i]) return false;
    }
    return true;
  };

  return equals_case_insensitive("true") || equals_case_insensitive("on");
}

inline bool IsReclamationActionTelemetryEnabled() {
  const char* value = std::getenv("RECLAMATION_ACTION_TELEMETRY");
  return value && IsReclamationActionTelemetryValueEnabled(value);
}

inline bool ShouldEmitReclamationDecisionTelemetry(bool state_changed, bool initialized,
                                                    u32 current_tick, u32 last_tick,
                                                    u32 interval_ticks = 500) {
  return state_changed || !initialized || TICK_DIFF(current_tick, last_tick) >= static_cast<s32>(interval_ticks);
}

}  // namespace reclamation
}  // namespace zero
