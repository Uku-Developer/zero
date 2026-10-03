#pragma once

#include <zero/Math.h>

namespace zero {
namespace reclamation {

struct InterceptSolution {
  bool valid = false;
  Vector2f aim_point;
  float time = -1.0f;
  float confidence = 0.0f;
};

InterceptSolution PredictIntercept(const Vector2f& shooter_position, const Vector2f& shooter_velocity,
                                   const Vector2f& target_position, const Vector2f& target_velocity,
                                   float projectile_speed, float max_time);

}  // namespace reclamation
}  // namespace zero
