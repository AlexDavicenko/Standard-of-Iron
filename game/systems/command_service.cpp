#include "command_service.h"
#include "../core/component.h"
#include "../core/world.h"
#include "pathfinding.h"
#include "units/spawn_type.h"
#include <QDebug>
#include <QVector3D>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <qvectornd.h>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Game::Systems {

namespace {
constexpr float same_target_threshold_sq = 0.01F;

constexpr float pathfinding_request_cooldown = 1.0F;

constexpr float target_movement_threshold_sq = 4.0F;
} // namespace

std::unique_ptr<Pathfinding> CommandService::s_pathfinder = nullptr;
std::unordered_map<std::uint64_t, CommandService::PendingPathRequest>
    CommandService::s_pendingRequests;
std::unordered_map<Engine::Core::EntityID, std::uint64_t>
    CommandService::s_entityToRequest;
std::mutex CommandService::s_pendingMutex;
std::atomic<std::uint64_t> CommandService::s_nextRequestId{1};

void CommandService::initialize(int worldWidth, int worldHeight) {
  s_pathfinder = std::make_unique<Pathfinding>(worldWidth, worldHeight);
  {
    std::lock_guard<std::mutex> const lock(s_pendingMutex);
    s_pendingRequests.clear();
    s_entityToRequest.clear();
  }
  s_nextRequestId.store(1, std::memory_order_release);

  float const offset_x = -(worldWidth * 0.5F - 0.5F);
  float const offset_z = -(worldHeight * 0.5F - 0.5F);
  s_pathfinder->setGridOffset(offset_x, offset_z);
}

auto CommandService::getPathfinder() -> Pathfinding * {
  return s_pathfinder.get();
}
auto CommandService::worldToGrid(float world_x, float world_z) -> Point {
  if (s_pathfinder) {
    int const grid_x =
        static_cast<int>(std::round(world_x - s_pathfinder->getGridOffsetX()));
    int const grid_z =
        static_cast<int>(std::round(world_z - s_pathfinder->getGridOffsetZ()));
    return {grid_x, grid_z};
  }

  return {static_cast<int>(std::round(world_x)),
          static_cast<int>(std::round(world_z))};
}

auto CommandService::gridToWorld(const Point &gridPos) -> QVector3D {
  if (s_pathfinder) {
    return {static_cast<float>(gridPos.x) + s_pathfinder->getGridOffsetX(),
            0.0F,
            static_cast<float>(gridPos.y) + s_pathfinder->getGridOffsetZ()};
  }
  return {static_cast<float>(gridPos.x), 0.0F, static_cast<float>(gridPos.y)};
}

void CommandService::clearPendingRequest(Engine::Core::EntityID entity_id) {
  std::lock_guard<std::mutex> const lock(s_pendingMutex);
  auto it = s_entityToRequest.find(entity_id);
  if (it == s_entityToRequest.end()) {
    return;
  }

  std::uint64_t const request_id = it->second;
  s_entityToRequest.erase(it);

  auto pending_it = s_pendingRequests.find(request_id);
  if (pending_it == s_pendingRequests.end()) {
    return;
  }

  auto members = pending_it->second.groupMembers;
  s_pendingRequests.erase(pending_it);

  for (auto member_id : members) {
    auto member_entry = s_entityToRequest.find(member_id);
    if (member_entry != s_entityToRequest.end() &&
        member_entry->second == request_id) {
      s_entityToRequest.erase(member_entry);
    }
  }
}

void CommandService::moveUnits(Engine::Core::World &world,
                               const std::vector<Engine::Core::EntityID> &units,
                               const std::vector<QVector3D> &targets) {
  moveUnits(world, units, targets, MoveOptions{});
}

void CommandService::moveUnits(Engine::Core::World &world,
                               const std::vector<Engine::Core::EntityID> &units,
                               const std::vector<QVector3D> &targets,
                               const MoveOptions &options) {
  if (units.size() != targets.size()) {
    return;
  }

  if (options.groupMove && units.size() > 1) {
    moveGroup(world, units, targets, options);
    return;
  }

  for (size_t i = 0; i < units.size(); ++i) {
    auto *e = world.getEntity(units[i]);
    if (e == nullptr) {
      continue;
    }

    auto *hold_mode = e->getComponent<Engine::Core::HoldModeComponent>();
    if ((hold_mode != nullptr) && hold_mode->active) {

      hold_mode->active = false;
      hold_mode->exitCooldown = hold_mode->standUpDuration;
    }

    auto *atk = e->getComponent<Engine::Core::AttackComponent>();
    if ((atk != nullptr) && atk->inMeleeLock) {

      continue;
    }

    auto *transform = e->getComponent<Engine::Core::TransformComponent>();
    if (transform == nullptr) {
      continue;
    }

    auto *mv = e->getComponent<Engine::Core::MovementComponent>();
    if (mv == nullptr) {
      mv = e->addComponent<Engine::Core::MovementComponent>();
    }
    if (mv == nullptr) {
      continue;
    }

    if (options.clearAttackIntent) {
      e->removeComponent<Engine::Core::AttackTargetComponent>();
    }

    const float target_x = targets[i].x();
    const float target_z = targets[i].z();

    bool matched_pending = false;
    if (mv->pathPending) {
      std::lock_guard<std::mutex> const lock(s_pendingMutex);
      auto request_it = s_entityToRequest.find(units[i]);
      if (request_it != s_entityToRequest.end()) {
        auto pending_it = s_pendingRequests.find(request_it->second);
        if (pending_it != s_pendingRequests.end()) {
          float const pdx = pending_it->second.target.x() - target_x;
          float const pdz = pending_it->second.target.z() - target_z;
          if (pdx * pdx + pdz * pdz <= same_target_threshold_sq) {
            pending_it->second.options = options;
            matched_pending = true;
          }
        } else {
          s_entityToRequest.erase(request_it);
        }
      }
    }

    mv->goalX = target_x;
    mv->goalY = target_z;

    if (matched_pending) {
      continue;
    }

    bool should_suppress_path_request = false;
    if (mv->timeSinceLastPathRequest < pathfinding_request_cooldown) {

      float const last_goal_dx = mv->lastGoalX - target_x;
      float const last_goal_dz = mv->lastGoalY - target_z;
      float const goal_movement_sq =
          last_goal_dx * last_goal_dx + last_goal_dz * last_goal_dz;

      if (goal_movement_sq < target_movement_threshold_sq) {
        should_suppress_path_request = true;

        if (mv->hasTarget || mv->pathPending) {
          continue;
        }
      }
    }

    if (!mv->pathPending) {
      bool const current_target_matches = mv->hasTarget && mv->path.empty();
      if (current_target_matches) {
        float const dx = mv->target_x - target_x;
        float const dz = mv->target_y - target_z;
        if (dx * dx + dz * dz <= same_target_threshold_sq) {
          continue;
        }
      }

      if (!mv->path.empty()) {
        const auto &last_waypoint = mv->path.back();
        float const dx = last_waypoint.first - target_x;
        float const dz = last_waypoint.second - target_z;
        if (dx * dx + dz * dz <= same_target_threshold_sq) {
          continue;
        }
      }
    }

    if (s_pathfinder) {
      Point const start =
          worldToGrid(transform->position.x, transform->position.z);
      Point const end = worldToGrid(targets[i].x(), targets[i].z());

      if (start == end) {
        mv->target_x = target_x;
        mv->target_y = target_z;
        mv->hasTarget = true;
        mv->path.clear();
        mv->pathPending = false;
        mv->pendingRequestId = 0;
        mv->vx = 0.0F;
        mv->vz = 0.0F;
        clearPendingRequest(units[i]);
        continue;
      }

      int const dx = std::abs(end.x - start.x);
      int const dz = std::abs(end.y - start.y);
      bool use_direct_path = (dx + dz) <= CommandService::DIRECT_PATH_THRESHOLD;
      if (!options.allowDirectFallback) {
        use_direct_path = false;
      }

      if (use_direct_path) {

        mv->target_x = target_x;
        mv->target_y = target_z;
        mv->hasTarget = true;
        mv->path.clear();
        mv->pathPending = false;
        mv->pendingRequestId = 0;
        mv->vx = 0.0F;
        mv->vz = 0.0F;
        clearPendingRequest(units[i]);

        mv->timeSinceLastPathRequest = 0.0F;
        mv->lastGoalX = target_x;
        mv->lastGoalY = target_z;
      } else {

        bool skip_new_request = false;
        {
          std::lock_guard<std::mutex> const lock(s_pendingMutex);
          auto existing_it = s_entityToRequest.find(units[i]);
          if (existing_it != s_entityToRequest.end()) {
            auto pending_it = s_pendingRequests.find(existing_it->second);
            if (pending_it != s_pendingRequests.end()) {
              float const dx = pending_it->second.target.x() - target_x;
              float const dz = pending_it->second.target.z() - target_z;
              if (dx * dx + dz * dz <= same_target_threshold_sq) {
                pending_it->second.options = options;
                skip_new_request = true;
              } else {
                s_pendingRequests.erase(pending_it);
                s_entityToRequest.erase(existing_it);
              }
            } else {
              s_entityToRequest.erase(existing_it);
            }
          }
        }

        if (skip_new_request) {
          continue;
        }

        mv->path.clear();
        mv->hasTarget = false;
        mv->vx = 0.0F;
        mv->vz = 0.0F;
        mv->pathPending = true;

        std::uint64_t const request_id =
            s_nextRequestId.fetch_add(1, std::memory_order_relaxed);
        mv->pendingRequestId = request_id;

        {
          std::lock_guard<std::mutex> const lock(s_pendingMutex);
          s_pendingRequests[request_id] = {
              units[i], targets[i], options, {}, {}};
          s_entityToRequest[units[i]] = request_id;
        }

        s_pathfinder->submitPathRequest(request_id, start, end);

        mv->timeSinceLastPathRequest = 0.0F;
        mv->lastGoalX = target_x;
        mv->lastGoalY = target_z;
      }
    } else {

      mv->target_x = target_x;
      mv->target_y = target_z;
      mv->hasTarget = true;
      mv->path.clear();
      mv->pathPending = false;
      mv->pendingRequestId = 0;
      mv->vx = 0.0F;
      mv->vz = 0.0F;
      clearPendingRequest(units[i]);
    }
  }
}

void CommandService::moveGroup(Engine::Core::World &world,
                               const std::vector<Engine::Core::EntityID> &units,
                               const std::vector<QVector3D> &targets,
                               const MoveOptions &options) {
  struct MemberInfo {
    Engine::Core::EntityID id;
    Engine::Core::Entity *entity;
    Engine::Core::TransformComponent *transform;
    Engine::Core::MovementComponent *movement;
    QVector3D target;
    bool isEngaged;
    float speed;
    Game::Units::SpawnType spawn_type;
    float distanceToTarget;
  };

  std::vector<MemberInfo> members;
  members.reserve(units.size());

  for (size_t i = 0; i < units.size(); ++i) {
    auto *entity = world.getEntity(units[i]);
    if (entity == nullptr) {
      continue;
    }

    auto *hold_mode = entity->getComponent<Engine::Core::HoldModeComponent>();
    if ((hold_mode != nullptr) && hold_mode->active) {
      hold_mode->active = false;
      hold_mode->exitCooldown = hold_mode->standUpDuration;
    }

    auto *transform = entity->getComponent<Engine::Core::TransformComponent>();
    if (transform == nullptr) {
      continue;
    }

    auto *movement = entity->getComponent<Engine::Core::MovementComponent>();
    if (movement == nullptr) {
      movement = entity->addComponent<Engine::Core::MovementComponent>();
    }
    if (movement == nullptr) {
      continue;
    }

    bool engaged =
        entity->getComponent<Engine::Core::AttackTargetComponent>() != nullptr;

    if (options.clearAttackIntent) {
      entity->removeComponent<Engine::Core::AttackTargetComponent>();
      engaged = false;
    }

    auto *unit_component = entity->getComponent<Engine::Core::UnitComponent>();
    float const member_speed = (unit_component != nullptr)
                                   ? std::max(0.1F, unit_component->speed)
                                   : 1.0F;
    Game::Units::SpawnType const spawn_type =
        (unit_component != nullptr) ? unit_component->spawn_type
                                    : Game::Units::SpawnType::Archer;

    members.push_back({units[i], entity, transform, movement, targets[i],
                       engaged, member_speed, spawn_type, 0.0F});
  }

  if (members.empty()) {
    return;
  }

  if (members.size() == 1) {
    std::vector<Engine::Core::EntityID> const single_unit = {members[0].id};
    std::vector<QVector3D> const single_target = {members[0].target};
    MoveOptions single_options = options;
    single_options.groupMove = false;
    moveUnits(world, single_unit, single_target, single_options);
    return;
  }

  std::vector<MemberInfo> moving_members;
  std::vector<MemberInfo> engaged_members;

  for (const auto &member : members) {
    if (member.isEngaged) {
      engaged_members.push_back(member);
    } else {
      moving_members.push_back(member);
    }
  }

  if (moving_members.empty()) {
    return;
  }

  if (s_pathfinder) {
    bool any_target_invalid = false;
    for (const auto &member : moving_members) {
      Point const target_grid =
          worldToGrid(member.target.x(), member.target.z());

      if (target_grid.x < 0 || target_grid.y < 0) {
        any_target_invalid = true;
        break;
      }

      if (!s_pathfinder->isWalkable(target_grid.x, target_grid.y)) {
        any_target_invalid = true;
        break;
      }
    }

    if (any_target_invalid) {
      return;
    }
  }

  members = moving_members;

  if (members.empty()) {
    return;
  }

  QVector3D target_centroid(0.0F, 0.0F, 0.0F);
  QVector3D position_centroid(0.0F, 0.0F, 0.0F);
  float speed_sum = 0.0F;
  for (auto &member : members) {
    target_centroid += member.target;
    position_centroid += QVector3D(member.transform->position.x, 0.0F,
                                   member.transform->position.z);
    speed_sum += member.speed;
  }

  target_centroid /= static_cast<float>(members.size());
  position_centroid /= static_cast<float>(members.size());

  float target_distance_sum = 0.0F;
  float max_target_distance = 0.0F;
  float centroid_distance_sum = 0.0F;
  for (auto &member : members) {
    QVector3D const current_pos(member.transform->position.x, 0.0F,
                                member.transform->position.z);
    float const to_target = (current_pos - member.target).length();
    float const to_centroid = (current_pos - position_centroid).length();

    member.distanceToTarget = to_target;
    target_distance_sum += to_target;
    centroid_distance_sum += to_centroid;
    max_target_distance = std::max(max_target_distance, to_target);
  }

  float const avg_target_distance =
      members.empty()
          ? 0.0F
          : target_distance_sum / static_cast<float>(members.size());
  float const avg_scatter =
      members.empty()
          ? 0.0F
          : centroid_distance_sum / static_cast<float>(members.size());
  float const avg_speed =
      members.empty() ? 0.0F : speed_sum / static_cast<float>(members.size());

  float const near_threshold =
      std::clamp(avg_target_distance * 0.5F, 4.0F, 12.0F);
  if (max_target_distance <= near_threshold) {
    MoveOptions direct_options = options;
    direct_options.groupMove = false;

    std::vector<Engine::Core::EntityID> direct_ids;
    std::vector<QVector3D> direct_targets;
    direct_ids.reserve(members.size());
    direct_targets.reserve(members.size());

    for (const auto &member : members) {
      direct_ids.push_back(member.id);
      direct_targets.push_back(member.target);
    }

    moveUnits(world, direct_ids, direct_targets, direct_options);
    return;
  }

  float const scatter_threshold = std::max(avg_scatter, 2.5F);

  std::vector<MemberInfo> regroup_members;
  std::vector<MemberInfo> direct_members;
  regroup_members.reserve(members.size());
  direct_members.reserve(members.size());

  for (const auto &member : members) {
    QVector3D const current_pos(member.transform->position.x, 0.0F,
                                member.transform->position.z);
    float const to_target = member.distanceToTarget;
    float const to_centroid = (current_pos - position_centroid).length();
    bool const near_destination = to_target <= near_threshold;
    bool const far_from_group = to_centroid > scatter_threshold * 1.5F;
    bool const fast_unit =
        member.speed >= avg_speed + 0.5F ||
        member.spawn_type == Game::Units::SpawnType::MountedKnight;

    bool should_advance = near_destination;
    if (!should_advance && fast_unit && to_target <= near_threshold * 1.5F) {
      should_advance = true;
    }
    if (!should_advance && far_from_group &&
        to_target <= near_threshold * 2.0F) {
      should_advance = true;
    }

    if (should_advance) {
      direct_members.push_back(member);
    } else {
      regroup_members.push_back(member);
    }
  }

  if (!direct_members.empty()) {
    MoveOptions direct_options = options;
    direct_options.groupMove = false;

    std::vector<Engine::Core::EntityID> direct_ids;
    std::vector<QVector3D> direct_targets;
    direct_ids.reserve(direct_members.size());
    direct_targets.reserve(direct_members.size());

    for (const auto &member : direct_members) {
      direct_ids.push_back(member.id);
      direct_targets.push_back(member.target);
    }

    moveUnits(world, direct_ids, direct_targets, direct_options);
  }

  if (regroup_members.size() <= 1) {
    if (!regroup_members.empty()) {
      MoveOptions direct_options = options;
      direct_options.groupMove = false;
      std::vector<Engine::Core::EntityID> const single_ids = {
          regroup_members.front().id};
      std::vector<QVector3D> const single_targets = {
          regroup_members.front().target};
      moveUnits(world, single_ids, single_targets, direct_options);
    }
    return;
  }

  members = std::move(regroup_members);

  QVector3D average(0.0F, 0.0F, 0.0F);
  for (const auto &member : members) {
    average += member.target;
  }
  average /= static_cast<float>(members.size());

  std::size_t leader_index = 0;
  float best_dist_sq = std::numeric_limits<float>::infinity();
  for (std::size_t i = 0; i < members.size(); ++i) {
    float const dist_sq = (members[i].target - average).lengthSquared();
    if (dist_sq < best_dist_sq) {
      best_dist_sq = dist_sq;
      leader_index = i;
    }
  }

  auto &leader = members[leader_index];
  QVector3D const leader_target = leader.target;

  std::vector<MemberInfo *> units_needing_new_path;
  constexpr float same_goal_threshold_sq = 4.0F;

  for (auto &member : members) {
    auto *mv = member.movement;

    mv->goalX = member.target.x();
    mv->goalY = member.target.z();

    clearPendingRequest(member.id);
    mv->target_x = member.transform->position.x;
    mv->target_y = member.transform->position.z;
    mv->hasTarget = false;
    mv->vx = 0.0F;
    mv->vz = 0.0F;
    mv->path.clear();
    mv->pathPending = false;
    mv->pendingRequestId = 0;
    units_needing_new_path.push_back(&member);
  }

  if (units_needing_new_path.empty()) {
    return;
  }

  if (!s_pathfinder) {
    for (auto *member : units_needing_new_path) {
      member->movement->target_x = member->target.x();
      member->movement->target_y = member->target.z();
      member->movement->hasTarget = true;
    }
    return;
  }

  Point const start =
      worldToGrid(leader.transform->position.x, leader.transform->position.z);
  Point const end = worldToGrid(leader_target.x(), leader_target.z());

  if (start == end) {
    for (auto *member : units_needing_new_path) {
      member->movement->target_x = member->target.x();
      member->movement->target_y = member->target.z();
      member->movement->hasTarget = true;
    }
    return;
  }

  int const dx = std::abs(end.x - start.x);
  int const dz = std::abs(end.y - start.y);
  bool use_direct_path = (dx + dz) <= CommandService::DIRECT_PATH_THRESHOLD;
  if (!options.allowDirectFallback) {
    use_direct_path = false;
  }

  if (use_direct_path) {
    for (auto *member : units_needing_new_path) {
      member->movement->target_x = member->target.x();
      member->movement->target_y = member->target.z();
      member->movement->hasTarget = true;

      member->movement->timeSinceLastPathRequest = 0.0F;
      member->movement->lastGoalX = member->target.x();
      member->movement->lastGoalY = member->target.z();
    }
    return;
  }

  std::uint64_t const request_id =
      s_nextRequestId.fetch_add(1, std::memory_order_relaxed);

  for (auto *member : units_needing_new_path) {
    member->movement->pathPending = true;
    member->movement->pendingRequestId = request_id;

    member->movement->timeSinceLastPathRequest = 0.0F;
    member->movement->lastGoalX = member->target.x();
    member->movement->lastGoalY = member->target.z();
  }

  PendingPathRequest pending;
  pending.entity_id = leader.id;
  pending.target = leader_target;
  pending.options = options;
  pending.groupMembers.reserve(units_needing_new_path.size());
  pending.groupTargets.reserve(units_needing_new_path.size());
  for (const auto *member : units_needing_new_path) {
    pending.groupMembers.push_back(member->id);
    pending.groupTargets.push_back(member->target);
  }

  {
    std::lock_guard<std::mutex> const lock(s_pendingMutex);
    s_pendingRequests[request_id] = std::move(pending);
    for (const auto *member : units_needing_new_path) {
      s_entityToRequest[member->id] = request_id;
    }
  }

  s_pathfinder->submitPathRequest(request_id, start, end);
}

void CommandService::processPathResults(Engine::Core::World &world) {
  if (!s_pathfinder) {
    return;
  }

  auto results = s_pathfinder->fetchCompletedPaths();
  if (results.empty()) {
    return;
  }

  for (auto &result : results) {
    PendingPathRequest request_info;
    bool found = false;

    {
      std::lock_guard<std::mutex> const lock(s_pendingMutex);
      auto pending_it = s_pendingRequests.find(result.request_id);
      if (pending_it != s_pendingRequests.end()) {
        request_info = pending_it->second;
        s_pendingRequests.erase(pending_it);

        found = true;
      }
    }

    if (!found) {
      continue;
    }

    const auto &path_points = result.path;

    const float skip_threshold_sq = CommandService::WAYPOINT_SKIP_THRESHOLD_SQ;
    const bool has_path = path_points.size() > 1;

    auto apply_to_member = [&](Engine::Core::EntityID member_id,
                               const QVector3D &target,
                               const QVector3D &offset) {
      auto *member_entity = world.getEntity(member_id);
      if (member_entity == nullptr) {
        return;
      }

      auto *movement_component =
          member_entity->getComponent<Engine::Core::MovementComponent>();
      if (movement_component == nullptr) {
        return;
      }

      auto *member_transform =
          member_entity->getComponent<Engine::Core::TransformComponent>();
      if (member_transform == nullptr) {
        return;
      }

      if (!movement_component->pathPending ||
          movement_component->pendingRequestId != result.request_id) {
        movement_component->pathPending = false;
        movement_component->pendingRequestId = 0;
        return;
      }

      movement_component->pathPending = false;
      movement_component->pendingRequestId = 0;
      movement_component->path.clear();
      movement_component->goalX = target.x();
      movement_component->goalY = target.z();
      movement_component->vx = 0.0F;
      movement_component->vz = 0.0F;

      if (has_path) {
        movement_component->path.reserve(path_points.size() - 1);
        for (size_t idx = 1; idx < path_points.size(); ++idx) {
          QVector3D const world_pos = gridToWorld(path_points[idx]);
          movement_component->path.emplace_back(world_pos.x() + offset.x(),
                                                world_pos.z() + offset.z());
        }

        while (!movement_component->path.empty()) {
          float const dx = movement_component->path.front().first -
                           member_transform->position.x;
          float const dz = movement_component->path.front().second -
                           member_transform->position.z;
          if (dx * dx + dz * dz <= skip_threshold_sq) {
            movement_component->path.erase(movement_component->path.begin());
          } else {
            break;
          }
        }

        if (!movement_component->path.empty()) {
          movement_component->target_x = movement_component->path.front().first;
          movement_component->target_y =
              movement_component->path.front().second;
          movement_component->hasTarget = true;
          return;
        }
      }

      if (request_info.options.allowDirectFallback) {
        movement_component->target_x = target.x();
        movement_component->target_y = target.z();
        movement_component->hasTarget = true;
      } else {
        movement_component->hasTarget = false;
      }
    };

    auto remove_entry = [&](Engine::Core::EntityID id) {
      auto entry = s_entityToRequest.find(id);
      if (entry != s_entityToRequest.end() &&
          entry->second == result.request_id) {
        s_entityToRequest.erase(entry);
      }
    };

    {
      std::lock_guard<std::mutex> const lock(s_pendingMutex);
      remove_entry(request_info.entity_id);
      for (auto member_id : request_info.groupMembers) {
        remove_entry(member_id);
      }
    }

    QVector3D leader_target = request_info.target;
    std::vector<Engine::Core::EntityID> processed;
    processed.reserve(request_info.groupMembers.size() + 1);

    auto add_member = [&](Engine::Core::EntityID id, const QVector3D &target) {
      if (std::find(processed.begin(), processed.end(), id) !=
          processed.end()) {
        return;
      }
      QVector3D const offset = target - leader_target;
      apply_to_member(id, target, offset);
      processed.push_back(id);
    };

    add_member(request_info.entity_id, leader_target);

    if (!request_info.groupMembers.empty()) {
      const std::size_t count = request_info.groupMembers.size();
      for (std::size_t idx = 0; idx < count; ++idx) {
        auto member_id = request_info.groupMembers[idx];
        QVector3D const target = (idx < request_info.groupTargets.size())
                                     ? request_info.groupTargets[idx]
                                     : leader_target;
        add_member(member_id, target);
      }
    }
  }
}

void CommandService::attack_target(
    Engine::Core::World &world,
    const std::vector<Engine::Core::EntityID> &units,
    Engine::Core::EntityID target_id, bool shouldChase) {
  if (target_id == 0) {
    return;
  }
  for (auto unit_id : units) {
    auto *e = world.getEntity(unit_id);
    if (e == nullptr) {
      continue;
    }

    auto *hold_mode = e->getComponent<Engine::Core::HoldModeComponent>();
    if ((hold_mode != nullptr) && hold_mode->active) {

      hold_mode->active = false;
      hold_mode->exitCooldown = hold_mode->standUpDuration;
    }

    auto *attack_target =
        e->getComponent<Engine::Core::AttackTargetComponent>();
    if (attack_target == nullptr) {
      attack_target = e->addComponent<Engine::Core::AttackTargetComponent>();
    }
    if (attack_target == nullptr) {
      continue;
    }

    attack_target->target_id = target_id;
    attack_target->shouldChase = shouldChase;

    if (!shouldChase) {
      continue;
    }

    auto *target_ent = world.getEntity(target_id);
    if (target_ent == nullptr) {
      continue;
    }

    auto *t_trans =
        target_ent->getComponent<Engine::Core::TransformComponent>();
    auto *att_trans = e->getComponent<Engine::Core::TransformComponent>();
    if ((t_trans == nullptr) || (att_trans == nullptr)) {
      continue;
    }

    QVector3D const target_pos(t_trans->position.x, 0.0F, t_trans->position.z);
    QVector3D const attacker_pos(att_trans->position.x, 0.0F,
                                 att_trans->position.z);

    QVector3D desired_pos = target_pos;

    float range = 2.0F;
    bool is_ranged_unit = false;
    if (auto *atk = e->getComponent<Engine::Core::AttackComponent>()) {
      range = std::max(0.1F, atk->range);
      if (atk->canRanged && atk->range > atk->meleeRange * 1.5F) {
        is_ranged_unit = true;
      }
    }

    QVector3D direction = target_pos - attacker_pos;
    float const distance = direction.length();
    if (distance > 0.001F) {
      direction /= distance;
      if (target_ent->hasComponent<Engine::Core::BuildingComponent>()) {
        float const scale_x = t_trans->scale.x;
        float const scale_z = t_trans->scale.z;
        float const target_radius = std::max(scale_x, scale_z) * 0.5F;
        float const desired_distance =
            target_radius + std::max(range - 0.2F, 0.2F);
        if (distance > desired_distance + 0.15F) {
          desired_pos = target_pos - direction * desired_distance;
        }
      } else {
        float desired_distance = std::max(range - 0.2F, 0.2F);
        if (is_ranged_unit) {
          desired_distance = range * 0.85F;
        }
        if (distance > desired_distance + 0.15F) {
          desired_pos = target_pos - direction * desired_distance;
        }
      }
    }

    CommandService::MoveOptions opts;
    opts.clearAttackIntent = false;
    opts.allowDirectFallback = true;
    std::vector<Engine::Core::EntityID> const unit_ids = {unit_id};
    std::vector<QVector3D> const move_targets = {desired_pos};
    CommandService::moveUnits(world, unit_ids, move_targets, opts);

    auto *mv = e->getComponent<Engine::Core::MovementComponent>();
    if (mv == nullptr) {
      mv = e->addComponent<Engine::Core::MovementComponent>();
    }
    if (mv != nullptr) {

      mv->target_x = desired_pos.x();
      mv->target_y = desired_pos.z();
      mv->goalX = desired_pos.x();
      mv->goalY = desired_pos.z();
      mv->hasTarget = true;
      mv->path.clear();
    }
  }
}

} // namespace Game::Systems
