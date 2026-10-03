#include <zero/reclamation/TrajectoryPredictor.h>

#include <cmath>

namespace zero {
namespace reclamation {

InterceptSolution PredictIntercept(const Vector2f& shooter_position, const Vector2f& shooter_velocity,
                                   const Vector2f& target_position, const Vector2f& target_velocity,
                                   float projectile_speed, float max_time) {
  InterceptSolution result;
  if (projectile_speed <= 0.0f || max_time <= 0.0f) return result;

  Vector2f relative_position = target_position - shooter_position;
  Vector2f relative_velocity = target_velocity - shooter_velocity;
  float c = relative_position.Dot(relative_position);
  if (c <= 0.0001f) {
    result.valid = true;
    result.aim_point = target_position;
    result.time = 0.0f;
    result.confidence = 1.0f;
    return result;
  }

  float a = relative_velocity.Dot(relative_velocity) - projectile_speed * projectile_speed;
  float b = 2.0f * relative_velocity.Dot(relative_position);
  float t = -1.0f;

  if (std::fabs(a) <= 0.0001f) {
    if (std::fabs(b) <= 0.0001f) return result;
    t = -c / b;
  } else {
    float discriminant = b * b - 4.0f * a * c;
    if (discriminant < 0.0f) return result;

    float root = std::sqrt(discriminant);
    float t1 = (-b - root) / (2.0f * a);
    float t2 = (-b + root) / (2.0f * a);

    bool t1_valid = t1 >= 0.0f;
    bool t2_valid = t2 >= 0.0f;
    if (t1_valid && t2_valid)
      t = t1 < t2 ? t1 : t2;
    else if (t1_valid)
      t = t1;
    else if (t2_valid)
      t = t2;
  }

  if (t < 0.0f || t > max_time) return result;

  result.valid = true;
  result.time = t;
  result.aim_point = target_position + relative_velocity * t;
  result.confidence = 1.0f - (t / max_time);
  if (result.confidence < 0.0f) result.confidence = 0.0f;
  return result;
}

}  // namespace reclamation
}  // namespace zero
