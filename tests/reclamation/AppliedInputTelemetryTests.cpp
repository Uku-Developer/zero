#include <zero/reclamation/AppliedInputTelemetry.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

using namespace zero;
using namespace zero::reclamation;

static void SetTelemetryEnvironment(const char* value) {
#if defined(_WIN32)
  _putenv_s("RECLAMATION_ACTION_TELEMETRY", value ? value : "");
#else
  if (value) {
    setenv("RECLAMATION_ACTION_TELEMETRY", value, 1);
  } else {
    unsetenv("RECLAMATION_ACTION_TELEMETRY");
  }
#endif
}

static void TestAppliedSnapshotReflectsInputWithoutMutation() {
  InputState input;
  input.SetAction(InputAction::Left, true);
  input.SetAction(InputAction::Forward, true);
  input.SetAction(InputAction::Afterburner, true);
  input.SetAction(InputAction::Bullet, true);
  input.SetAction(InputAction::Mine, true);
  const u32 original_actions = input.actions;

  const AppliedInputSnapshot snapshot = CaptureAppliedInput(input);

  assert(snapshot.action_mask == original_actions);
  assert(snapshot.left);
  assert(!snapshot.right);
  assert(snapshot.forward);
  assert(!snapshot.backward);
  assert(snapshot.afterburner);
  assert(snapshot.bullet);
  assert(!snapshot.bomb);
  assert(input.actions == original_actions);
}

static void TestExplicitTelemetryValues() {
  assert(!IsReclamationActionTelemetryValueEnabled(""));
  assert(!IsReclamationActionTelemetryValueEnabled("0"));
  assert(!IsReclamationActionTelemetryValueEnabled("false"));
  assert(!IsReclamationActionTelemetryValueEnabled("FALSE"));
  assert(!IsReclamationActionTelemetryValueEnabled("off"));
  assert(!IsReclamationActionTelemetryValueEnabled("OFF"));
  assert(!IsReclamationActionTelemetryValueEnabled("yes"));
  assert(!IsReclamationActionTelemetryValueEnabled(" 1"));
  assert(IsReclamationActionTelemetryValueEnabled("1"));
  assert(IsReclamationActionTelemetryValueEnabled("true"));
  assert(IsReclamationActionTelemetryValueEnabled("TRUE"));
  assert(IsReclamationActionTelemetryValueEnabled("TrUe"));
  assert(IsReclamationActionTelemetryValueEnabled("on"));
  assert(IsReclamationActionTelemetryValueEnabled("ON"));
}

static void TestEnvironmentCheckDoesNotMutateInput() {
  InputState input;
  input.SetAction(InputAction::Right, true);
  input.SetAction(InputAction::Bomb, true);
  const u32 original_actions = input.actions;

  SetTelemetryEnvironment(nullptr);
  assert(!IsReclamationActionTelemetryEnabled());
  assert(input.actions == original_actions);
  SetTelemetryEnvironment("");
  assert(!IsReclamationActionTelemetryEnabled());
  SetTelemetryEnvironment("0");
  assert(!IsReclamationActionTelemetryEnabled());
  SetTelemetryEnvironment("false");
  assert(!IsReclamationActionTelemetryEnabled());
  SetTelemetryEnvironment("off");
  assert(!IsReclamationActionTelemetryEnabled());
  SetTelemetryEnvironment("1");
  assert(IsReclamationActionTelemetryEnabled());
  SetTelemetryEnvironment("TRUE");
  assert(IsReclamationActionTelemetryEnabled());
  SetTelemetryEnvironment("On");
  assert(IsReclamationActionTelemetryEnabled());
  assert(input.actions == original_actions);

  SetTelemetryEnvironment(nullptr);
}

static void TestDecisionTelemetryScheduleInitializationAndInterval() {
  assert(ShouldEmitReclamationDecisionTelemetry(false, false, 0, 0));
  assert(ShouldEmitReclamationDecisionTelemetry(true, true, 100, 100));
  assert(!ShouldEmitReclamationDecisionTelemetry(false, true, 599, 100));
  assert(ShouldEmitReclamationDecisionTelemetry(false, true, 600, 100));
}

static void TestDecisionTelemetryScheduleHandlesRolloverAndBackwardTicks() {
  const u32 last = 0x7FFFFF00u;
  const u32 rollover_current = MAKE_TICK(last + 500);
  assert(ShouldEmitReclamationDecisionTelemetry(false, true, rollover_current, last));
  assert(!ShouldEmitReclamationDecisionTelemetry(false, true, 900, 1000));
}

int main() {
  TestAppliedSnapshotReflectsInputWithoutMutation();
  TestExplicitTelemetryValues();
  TestEnvironmentCheckDoesNotMutateInput();
  TestDecisionTelemetryScheduleInitializationAndInterval();
  TestDecisionTelemetryScheduleHandlesRolloverAndBackwardTicks();

  printf("Reclamation applied-input telemetry tests passed.\n");
  return 0;
}
