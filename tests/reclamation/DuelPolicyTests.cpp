#include <zero/reclamation/DuelPolicy.h>
#include <zero/reclamation/TrajectoryPredictor.h>

#include <assert.h>
#include <stdio.h>

using namespace zero;
using namespace zero::reclamation;

static WorldSnapshot MakeWorld(float self_energy = 1200.0f) {
  WorldSnapshot world;
  world.tick = 100;
  world.dt = 0.01f;
  world.has_self = true;
  world.self.id = 1;
  world.self.relation = PlayerRelation::Self;
  world.self.frequency = 100;
  world.self.ship = 0;
  world.self.position = Vector2f(0, 0);
  world.self.velocity = Vector2f(0, 0);
  world.self.heading = Vector2f(1, 0);
  world.self.energy = self_energy;
  world.self.energy_confidence = 1.0f;
  world.self.synchronized = true;
  world.combat.max_energy = 1700.0f;
  world.combat.bullet_speed = 20.0f;
  world.combat.bomb_speed = 15.0f;
  world.combat.bullet_alive_seconds = 2.0f;
  world.combat.ship_radius = 0.9f;
  return world;
}

static PlayerSnapshot MakeEnemy(float x, float energy) {
  PlayerSnapshot player;
  player.id = 2;
  player.relation = PlayerRelation::Enemy;
  player.frequency = 200;
  player.ship = 0;
  player.position = Vector2f(x, 0);
  player.velocity = Vector2f(0, 0);
  player.heading = Vector2f(-1, 0);
  player.energy = energy;
  player.energy_confidence = 1.0f;
  player.synchronized = true;
  player.line_of_sight_from_self = true;
  return player;
}

static ProjectileSnapshot MakeProjectile(WeaponType type, Vector2f position, Vector2f velocity,
                                         float damage = 200.0f, float remaining_seconds = 2.0f) {
  ProjectileSnapshot projectile;
  projectile.owner_id = 2;
  projectile.frequency = 200;
  projectile.type = type;
  projectile.estimated_damage = damage;
  projectile.remaining_seconds = remaining_seconds;
  projectile.position = position;
  projectile.velocity = velocity;
  return projectile;
}

static void TestDuel001PressureLowEnergyOpponent() {
  DuelPolicy policy;
  WorldSnapshot world = MakeWorld(1200.0f);
  world.players.push_back(MakeEnemy(20.0f, 700.0f));

  BotAction action = policy.Decide(world);
  assert(policy.LastState() == DuelState::Pressure);
  assert(action.reason == "duel-pressure");
  assert(action.target_id == 2);
  assert(action.target_distance == 20.0f);
  assert(action.desired_distance == 14.0f);
  assert(action.fire_eligible);
  assert(action.fire_bullet);
}

static void TestDuel002EscapesLethalIncomingThreat() {
  DuelPolicy policy;
  WorldSnapshot world = MakeWorld(300.0f);
  world.players.push_back(MakeEnemy(20.0f, 1000.0f));
  world.projectiles.push_back(MakeProjectile(WeaponType::Bomb, Vector2f(10, 0), Vector2f(-20, 0)));

  BotAction action = policy.Decide(world);
  assert(policy.LastState() == DuelState::Escape);
  assert(action.reason == "duel-escape");
  assert(action.has_move_target);
  assert(action.move_target.x < world.self.position.x);
  assert(!action.fire_bullet);
}

static void TestIncomingThreatDisengagesBeforeRecharge() {
  DuelPolicy policy;
  WorldSnapshot world = MakeWorld(700.0f);
  world.players.push_back(MakeEnemy(20.0f, 1000.0f));
  world.projectiles.push_back(MakeProjectile(WeaponType::Bullet, Vector2f(8, 0), Vector2f(-20, 0)));

  BotAction action = policy.Decide(world);
  assert(policy.LastState() == DuelState::Disengage);
  assert(action.reason == "duel-disengage");
  assert(action.move_target.x < world.self.position.x);
}

static void TestLowEnergyWithoutThreatRecharges() {
  DuelPolicy policy;
  WorldSnapshot world = MakeWorld(500.0f);
  world.players.push_back(MakeEnemy(25.0f, 1000.0f));

  BotAction action = policy.Decide(world);
  assert(policy.LastState() == DuelState::Recharge);
  assert(action.reason == "duel-recharge");
  assert(action.desired_distance == 36.0f);
  assert(!action.fire_bullet);
}

static void TestRushConvertsCloseLowTarget() {
  DuelPolicy policy;
  WorldSnapshot world = MakeWorld(1200.0f);
  world.players.push_back(MakeEnemy(8.0f, 350.0f));

  BotAction action = policy.Decide(world);
  assert(policy.LastState() == DuelState::Rush);
  assert(action.reason == "duel-rush");
  assert(action.desired_distance == 0.0f);
  assert(action.fire_bullet);
}

static void TestFinishConvertsCriticalTarget() {
  DuelPolicy policy;
  WorldSnapshot world = MakeWorld(1200.0f);
  world.players.push_back(MakeEnemy(20.0f, 200.0f));

  BotAction action = policy.Decide(world);
  assert(policy.LastState() == DuelState::Finish);
  assert(action.reason == "duel-finish");
  assert(action.desired_distance == 5.0f);
}

static void TestFarOpponentChases() {
  DuelPolicy policy;
  WorldSnapshot world = MakeWorld(1200.0f);
  world.players.push_back(MakeEnemy(70.0f, 1200.0f));

  BotAction action = policy.Decide(world);
  assert(policy.LastState() == DuelState::Chase);
  assert(action.reason == "duel-chase");
  assert(action.desired_distance == 18.0f);
}

static void TestOrdinaryEngagementIsNeutral() {
  DuelPolicy policy;
  WorldSnapshot world = MakeWorld(1200.0f);
  world.players.push_back(MakeEnemy(30.0f, 1200.0f));

  BotAction action = policy.Decide(world);
  assert(policy.LastState() == DuelState::Neutral);
  assert(action.reason == "duel-neutral");
  assert(action.desired_distance == 22.0f);
}

static void TestNoOpponentIsSafeIdle() {
  DuelPolicy policy;
  WorldSnapshot world = MakeWorld();

  BotAction action = policy.Decide(world);
  assert(policy.LastState() == DuelState::Neutral);
  assert(action.target_id == kInvalidPlayerId);
  assert(action.reason == "no-opponent");
}

static void TestExpiredProjectileIsIgnored() {
  DuelPolicy policy;
  WorldSnapshot world = MakeWorld(1200.0f);
  world.players.push_back(MakeEnemy(30.0f, 1200.0f));
  world.projectiles.push_back(MakeProjectile(WeaponType::Bomb, Vector2f(10, 0), Vector2f(-20, 0), 1200.0f, 0.0f));

  BotAction action = policy.Decide(world);
  assert(policy.LastState() == DuelState::Neutral);
  assert(action.threat_score == 0.0f);
}

static void TestCumulativeDamageTriggersDisengage() {
  DuelPolicy policy;
  WorldSnapshot world = MakeWorld(1000.0f);
  world.players.push_back(MakeEnemy(30.0f, 1200.0f));
  world.projectiles.push_back(MakeProjectile(WeaponType::Bullet, Vector2f(5, 0), Vector2f(-20, 0), 200.0f));
  world.projectiles.push_back(MakeProjectile(WeaponType::Bullet, Vector2f(8, 0), Vector2f(-20, 0), 200.0f));

  BotAction action = policy.Decide(world);
  assert(policy.LastState() == DuelState::Disengage);
  assert(action.threat_score >= 0.25f);
}

static void TestInterceptTelemetryUsesPhysicsPredictor() {
  DuelPolicy policy;
  WorldSnapshot world = MakeWorld(1200.0f);
  PlayerSnapshot enemy = MakeEnemy(20.0f, 1200.0f);
  enemy.velocity = Vector2f(0, 5);
  world.players.push_back(enemy);

  BotAction action = policy.Decide(world);
  assert(action.intercept_time > 1.0f);
  assert(action.intercept_confidence > 0.0f);
  assert(action.aim_target.y > enemy.position.y);
}

static void TestBlockedLineOfSightSuppressesFire() {
  DuelPolicy policy;
  WorldSnapshot world = MakeWorld(1200.0f);
  PlayerSnapshot enemy = MakeEnemy(20.0f, 700.0f);
  enemy.line_of_sight_from_self = false;
  world.players.push_back(enemy);

  BotAction action = policy.Decide(world);
  assert(policy.LastState() == DuelState::Pressure);
  assert(!action.fire_eligible);
  assert(!action.fire_bullet);
}

static void TestStateTransitionTelemetry() {
  DuelPolicy policy;
  WorldSnapshot world = MakeWorld(1200.0f);
  world.players.push_back(MakeEnemy(20.0f, 700.0f));

  BotAction first = policy.Decide(world);
  assert(first.state_changed);
  assert(first.state_age_ticks == 0);

  world.tick = 125;
  BotAction same = policy.Decide(world);
  assert(!same.state_changed);
  assert(same.state_age_ticks == 25);

  world.tick = 130;
  world.self.energy = 500.0f;
  BotAction changed = policy.Decide(world);
  assert(changed.state_changed);
  assert(changed.state_age_ticks == 0);
  assert(policy.LastState() == DuelState::Recharge);
}

static void TestRechargeHysteresisPreventsThresholdOscillation() {
  DuelPolicy policy;
  WorldSnapshot world = MakeWorld(590.0f);
  world.players.push_back(MakeEnemy(30.0f, 1200.0f));

  BotAction entered = policy.Decide(world);
  assert(policy.LastState() == DuelState::Recharge);
  assert(entered.state_changed);

  world.tick = 150;
  world.self.energy = 650.0f;
  BotAction held = policy.Decide(world);
  assert(policy.LastState() == DuelState::Recharge);
  assert(!held.state_changed);
  assert(held.reason == "duel-recharge");

  world.tick = 200;
  world.self.energy = 780.0f;
  BotAction exited = policy.Decide(world);
  assert(policy.LastState() == DuelState::Neutral);
  assert(exited.state_changed);
}

static void TestRechargeThresholdScalesWithMaxEnergy() {
  DuelPolicy policy;
  WorldSnapshot world = MakeWorld(680.0f);
  world.combat.max_energy = 2000.0f;
  world.players.push_back(MakeEnemy(30.0f, 1200.0f));

  BotAction action = policy.Decide(world);
  assert(policy.LastState() == DuelState::Recharge);
  assert(action.reason == "duel-recharge");
}

static void TestRechargeFallsBackToAbsoluteThresholdWithoutMaxEnergy() {
  DuelPolicy policy;
  WorldSnapshot world = MakeWorld(590.0f);
  world.combat.max_energy = 0.0f;
  world.players.push_back(MakeEnemy(30.0f, 1200.0f));

  BotAction action = policy.Decide(world);
  assert(policy.LastState() == DuelState::Recharge);
  assert(action.reason == "duel-recharge");
}

static void TestStationaryExplosiveThreatIsNotIgnored() {
  DuelPolicy policy;
  WorldSnapshot world = MakeWorld(700.0f);
  world.players.push_back(MakeEnemy(30.0f, 1200.0f));
  world.projectiles.push_back(MakeProjectile(WeaponType::ProximityBomb, Vector2f(3, 0), Vector2f(0, 0), 900.0f));

  BotAction action = policy.Decide(world);
  assert(action.threat_score > 0.0f);
  assert(policy.LastState() == DuelState::Escape || policy.LastState() == DuelState::Disengage);
}

static void TestPredictedTargetPositionPreservesWorldFrame() {
  DuelPolicy policy;
  WorldSnapshot world = MakeWorld(1200.0f);
  world.self.velocity = Vector2f(3, 0);
  PlayerSnapshot enemy = MakeEnemy(20.0f, 1200.0f);
  enemy.velocity = Vector2f(0, 5);
  world.players.push_back(enemy);

  BotAction action = policy.Decide(world);
  assert(action.has_predicted_target_position);
  assert(action.intercept_time >= 0.0f);
  Vector2f reconstructed = action.aim_target + world.self.velocity * action.intercept_time;
  assert(action.predicted_target_position.DistanceSq(reconstructed) < 0.0001f);
}

static void TestRecentOpponentReversalReducesLeadConfidence() {
  DuelPolicy policy;
  WorldSnapshot world = MakeWorld(1200.0f);
  PlayerSnapshot enemy = MakeEnemy(20.0f, 1200.0f);
  enemy.velocity = Vector2f(0, 5);
  world.players.push_back(enemy);

  BotAction stable = policy.Decide(world);
  assert(stable.intercept_confidence > 0.10f);

  world.tick = 150;
  world.players[0].velocity = Vector2f(0, -5);
  BotAction reversed = policy.Decide(world);
  assert(reversed.opponent_unstable);
  assert(reversed.intercept_confidence == 0.10f);
  assert(reversed.intercept_time <= 0.10f);
}

static void TestOpponentInstabilityDoesNotLeakAcrossTargetSwitch() {
  DuelPolicy policy;
  WorldSnapshot world = MakeWorld(1200.0f);
  PlayerSnapshot enemy = MakeEnemy(20.0f, 1200.0f);
  enemy.velocity = Vector2f(0, 5);
  world.players.push_back(enemy);

  BotAction first = policy.Decide(world);
  assert(!first.opponent_unstable);

  world.tick = 150;
  world.players[0].velocity = Vector2f(0, -5);
  BotAction reversed = policy.Decide(world);
  assert(reversed.opponent_unstable);

  world.tick = 160;
  world.players[0].id = 3;
  BotAction switched = policy.Decide(world);
  assert(!switched.opponent_unstable);
  assert(switched.target_id == 3);
  assert(switched.intercept_confidence > 0.10f);
}

static void TestOpponentMemoryResetsAcrossUnavailableLifecycle() {
  DuelPolicy policy;
  WorldSnapshot world = MakeWorld(1200.0f);
  PlayerSnapshot enemy = MakeEnemy(20.0f, 1200.0f);
  enemy.velocity = Vector2f(0, 5);
  world.players.push_back(enemy);

  policy.Decide(world);
  world.tick = 150;
  world.players[0].velocity = Vector2f(0, -5);
  assert(policy.Decide(world).opponent_unstable);

  world.tick = 160;
  world.has_self = false;
  BotAction unavailable = policy.Decide(world);
  assert(unavailable.reason == "self-unavailable");

  world.tick = 170;
  world.has_self = true;
  BotAction resumed = policy.Decide(world);
  assert(!resumed.opponent_unstable);
}

static void TestOpponentMemoryHandlesTickZeroAndRollover() {
  DuelPolicy zero_policy;
  WorldSnapshot zero_world = MakeWorld(1200.0f);
  zero_world.tick = 0;
  PlayerSnapshot zero_enemy = MakeEnemy(20.0f, 1200.0f);
  zero_enemy.velocity = Vector2f(0, 5);
  zero_world.players.push_back(zero_enemy);
  zero_policy.Decide(zero_world);

  zero_world.tick = 10;
  zero_world.players[0].velocity = Vector2f(0, -5);
  assert(zero_policy.Decide(zero_world).opponent_unstable);

  DuelPolicy wrap_policy;
  WorldSnapshot wrap_world = MakeWorld(1200.0f);
  wrap_world.tick = 0x7FFFFFF0u;
  PlayerSnapshot wrap_enemy = MakeEnemy(20.0f, 1200.0f);
  wrap_enemy.velocity = Vector2f(0, 5);
  wrap_world.players.push_back(wrap_enemy);
  BotAction before_wrap = wrap_policy.Decide(wrap_world);
  assert(before_wrap.state_age_ticks == 0);

  wrap_world.tick = 20;
  wrap_world.players[0].velocity = Vector2f(0, -5);
  BotAction after_wrap = wrap_policy.Decide(wrap_world);
  assert(after_wrap.opponent_unstable);
  assert(after_wrap.state_age_ticks == 36);
}

int main() {
  TestDuel001PressureLowEnergyOpponent();
  TestDuel002EscapesLethalIncomingThreat();
  TestIncomingThreatDisengagesBeforeRecharge();
  TestLowEnergyWithoutThreatRecharges();
  TestRushConvertsCloseLowTarget();
  TestFinishConvertsCriticalTarget();
  TestFarOpponentChases();
  TestOrdinaryEngagementIsNeutral();
  TestNoOpponentIsSafeIdle();
  TestExpiredProjectileIsIgnored();
  TestCumulativeDamageTriggersDisengage();
  TestInterceptTelemetryUsesPhysicsPredictor();
  TestBlockedLineOfSightSuppressesFire();
  TestStateTransitionTelemetry();
  TestRechargeHysteresisPreventsThresholdOscillation();
  TestRechargeThresholdScalesWithMaxEnergy();
  TestRechargeFallsBackToAbsoluteThresholdWithoutMaxEnergy();
  TestStationaryExplosiveThreatIsNotIgnored();
  TestPredictedTargetPositionPreservesWorldFrame();
  TestRecentOpponentReversalReducesLeadConfidence();
  TestOpponentInstabilityDoesNotLeakAcrossTargetSwitch();
  TestOpponentMemoryResetsAcrossUnavailableLifecycle();
  TestOpponentMemoryHandlesTickZeroAndRollover();

  printf("Reclamation Duel policy tests passed.\n");
  return 0;
}
