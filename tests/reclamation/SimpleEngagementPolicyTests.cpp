#include <zero/reclamation/SimpleEngagementPolicy.h>

#include <assert.h>
#include <stdio.h>

using namespace zero;
using namespace zero::reclamation;

static WorldSnapshot MakeWorld() {
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
  world.self.energy = 1000.0f;
  world.self.synchronized = true;
  return world;
}

static PlayerSnapshot MakePlayer(PlayerId id, PlayerRelation relation, Vector2f position) {
  PlayerSnapshot player;
  player.id = id;
  player.relation = relation;
  player.frequency = relation == PlayerRelation::Ally ? 100 : 200;
  player.ship = 0;
  player.position = position;
  player.velocity = Vector2f(0, 0);
  player.heading = Vector2f(-1, 0);
  player.energy = 800.0f;
  player.synchronized = true;
  return player;
}

static void TestUnavailableSelfIsIdle() {
  SimpleEngagementPolicy policy;
  WorldSnapshot world;
  BotAction action = policy.Decide(world);

  assert(action.target_id == kInvalidPlayerId);
  assert(!action.fire_bullet);
  assert(action.reason == "self-unavailable");
}

static void TestNearestValidOpponentWins() {
  SimpleEngagementPolicy policy;
  WorldSnapshot world = MakeWorld();
  world.players.push_back(MakePlayer(2, PlayerRelation::Ally, Vector2f(2, 0)));
  PlayerSnapshot ignored = MakePlayer(3, PlayerRelation::Enemy, Vector2f(4, 0));
  ignored.synchronized = false;
  world.players.push_back(ignored);
  world.players.push_back(MakePlayer(4, PlayerRelation::Enemy, Vector2f(20, 0)));

  BotAction action = policy.Decide(world);

  assert(action.target_id == 4);
  assert(action.has_aim_target);
  assert(action.has_move_target);
  assert(action.fire_bullet);
  assert(action.desired_distance == 18.0f);
  assert(action.reason == "engage-nearest-opponent");
}

static void TestMisalignmentPreventsShot() {
  SimpleEngagementPolicy policy;
  WorldSnapshot world = MakeWorld();
  world.players.push_back(MakePlayer(7, PlayerRelation::Enemy, Vector2f(0, 20)));

  BotAction action = policy.Decide(world);

  assert(action.target_id == 7);
  assert(!action.fire_bullet);
}

static void TestLeadUsesObservedVelocity() {
  SimpleEngagementPolicy policy;
  WorldSnapshot world = MakeWorld();
  PlayerSnapshot opponent = MakePlayer(9, PlayerRelation::Enemy, Vector2f(20, 0));
  opponent.velocity = Vector2f(10, 0);
  world.players.push_back(opponent);

  BotAction action = policy.Decide(world);

  assert(action.target_id == 9);
  assert(action.aim_target.x > opponent.position.x);
}

static void TestSpectatorIsIgnored() {
  SimpleEngagementPolicy policy;
  WorldSnapshot world = MakeWorld();
  PlayerSnapshot spectator = MakePlayer(11, PlayerRelation::Enemy, Vector2f(5, 0));
  spectator.ship = 8;
  world.players.push_back(spectator);

  BotAction action = policy.Decide(world);
  assert(action.target_id == kInvalidPlayerId);
  assert(action.reason == "no-opponent");
}

int main() {
  TestUnavailableSelfIsIdle();
  TestNearestValidOpponentWins();
  TestMisalignmentPreventsShot();
  TestLeadUsesObservedVelocity();
  TestSpectatorIsIgnored();

  printf("Reclamation core policy tests passed.\n");
  return 0;
}
