#include <zero/reclamation/TrajectoryPredictor.h>

#include <assert.h>
#include <stdio.h>

using namespace zero;
using namespace zero::reclamation;

static void TestStationaryTargetIntercept() {
  InterceptSolution solution = PredictIntercept(Vector2f(0, 0), Vector2f(0, 0),
                                                Vector2f(20, 0), Vector2f(0, 0),
                                                20.0f, 2.0f);
  assert(solution.valid);
  assert(solution.time > 0.99f && solution.time < 1.01f);
  assert(solution.aim_point.x > 19.99f && solution.aim_point.x < 20.01f);
  assert(solution.confidence > 0.49f && solution.confidence < 0.51f);
}

static void TestLateralTargetIsLed() {
  InterceptSolution solution = PredictIntercept(Vector2f(0, 0), Vector2f(0, 0),
                                                Vector2f(20, 0), Vector2f(0, 5),
                                                20.0f, 2.0f);
  assert(solution.valid);
  assert(solution.time > 1.0f);
  assert(solution.aim_point.y > 5.0f);
}
static void TestFasterEscapingTargetIsUnreachable() {
  InterceptSolution solution = PredictIntercept(Vector2f(0, 0), Vector2f(0, 0),
                                                Vector2f(20, 0), Vector2f(30, 0),
                                                20.0f, 3.0f);
  assert(!solution.valid);
}

static void TestMaxTimeRejectsLateIntercept() {
  InterceptSolution solution = PredictIntercept(Vector2f(0, 0), Vector2f(0, 0),
                                                Vector2f(40, 0), Vector2f(0, 0),
                                                20.0f, 1.5f);
  assert(!solution.valid);
}

int main() {
  TestStationaryTargetIntercept();
  TestLateralTargetIsLed();
  TestFasterEscapingTargetIsUnreachable();
  TestMaxTimeRejectsLateIntercept();

  printf("Reclamation trajectory predictor tests passed.\n");
  return 0;
}
