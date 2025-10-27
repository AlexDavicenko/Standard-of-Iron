#pragma once

#include "../visuals/visual_catalog.h"
#include "map_definition.h"
#include <memory>

namespace Engine::Core {
class World;
using EntityID = unsigned int;
} // namespace Engine::Core

namespace Game::Units {
class UnitFactoryRegistry;
}

namespace Game::Map {

struct MapRuntime {
  std::vector<Engine::Core::EntityID> unit_ids;
};

class MapTransformer {
public:
  static auto applyToWorld(
      const MapDefinition &def, Engine::Core::World &world,
      const Game::Visuals::VisualCatalog *visuals = nullptr) -> MapRuntime;

  static void
  setFactoryRegistry(std::shared_ptr<Game::Units::UnitFactoryRegistry> reg);
  static auto
  getFactoryRegistry() -> std::shared_ptr<Game::Units::UnitFactoryRegistry>;

  static void setLocalOwnerId(int owner_id);
  static auto localOwnerId() -> int;

  static void
  setPlayerTeamOverrides(const std::unordered_map<int, int> &overrides);
  static void clearPlayerTeamOverrides();
};

} // namespace Game::Map
