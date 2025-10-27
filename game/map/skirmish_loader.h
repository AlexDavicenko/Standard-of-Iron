#pragma once

#include "map_definition.h"
#include <QString>
#include <QVariantList>
#include <QVector3D>
#include <functional>
#include <memory>
#include <utility>

namespace Engine::Core {
class World;
using EntityID = unsigned int;
} // namespace Engine::Core

namespace Render::GL {
class Renderer;
class Camera;
class GroundRenderer;
class TerrainRenderer;
class BiomeRenderer;
class FogRenderer;
class StoneRenderer;
class PlantRenderer;
class PineRenderer;
class FireCampRenderer;
class RiverRenderer;
class RiverbankRenderer;
class BridgeRenderer;
} // namespace Render::GL

namespace Game::Map {

struct SkirmishLoadResult {
  bool ok = false;
  QString map_name;
  QString errorMessage;
  Engine::Core::EntityID playerUnitId = 0;
  float camFov = 45.0F;
  float camNear = 0.1F;
  float camFar = 1000.0F;
  int grid_width = 50;
  int grid_height = 50;
  float tile_size = 1.0F;
  int max_troops_per_player = 50;
  VictoryConfig victoryConfig;
  QVector3D focusPosition;
  bool hasFocusPosition = false;
};

class SkirmishLoader {
public:
  using OwnersUpdatedCallback = std::function<void()>;
  using VisibilityMaskReadyCallback = std::function<void()>;

  SkirmishLoader(Engine::Core::World &world, Render::GL::Renderer &renderer,
                 Render::GL::Camera &camera);

  void setGroundRenderer(Render::GL::GroundRenderer *ground) {
    m_ground = ground;
  }
  void setTerrainRenderer(Render::GL::TerrainRenderer *terrain) {
    m_terrain = terrain;
  }
  void setBiomeRenderer(Render::GL::BiomeRenderer *biome) { m_biome = biome; }
  void setRiverRenderer(Render::GL::RiverRenderer *river) { m_river = river; }
  void setRiverbankRenderer(Render::GL::RiverbankRenderer *riverbank) {
    m_riverbank = riverbank;
  }
  void setBridgeRenderer(Render::GL::BridgeRenderer *bridge) {
    m_bridge = bridge;
  }
  void setFogRenderer(Render::GL::FogRenderer *fog) { m_fog = fog; }
  void setStoneRenderer(Render::GL::StoneRenderer *stone) { m_stone = stone; }
  void setPlantRenderer(Render::GL::PlantRenderer *plant) { m_plant = plant; }
  void setPineRenderer(Render::GL::PineRenderer *pine) { m_pine = pine; }
  void setFireCampRenderer(Render::GL::FireCampRenderer *firecamp) {
    m_firecamp = firecamp;
  }

  void setOnOwnersUpdated(OwnersUpdatedCallback callback) {
    m_onOwnersUpdated = std::move(callback);
  }

  void setOnVisibilityMaskReady(VisibilityMaskReadyCallback callback) {
    m_onVisibilityMaskReady = std::move(callback);
  }

  auto start(const QString &map_path, const QVariantList &playerConfigs,
             int selectedPlayerId,
             int &outSelectedPlayerId) -> SkirmishLoadResult;

private:
  void resetGameState();
  Engine::Core::World &m_world;
  Render::GL::Renderer &m_renderer;
  Render::GL::Camera &m_camera;
  Render::GL::GroundRenderer *m_ground = nullptr;
  Render::GL::TerrainRenderer *m_terrain = nullptr;
  Render::GL::BiomeRenderer *m_biome = nullptr;
  Render::GL::RiverRenderer *m_river = nullptr;
  Render::GL::RiverbankRenderer *m_riverbank = nullptr;
  Render::GL::BridgeRenderer *m_bridge = nullptr;
  Render::GL::FogRenderer *m_fog = nullptr;
  Render::GL::StoneRenderer *m_stone = nullptr;
  Render::GL::PlantRenderer *m_plant = nullptr;
  Render::GL::PineRenderer *m_pine = nullptr;
  Render::GL::FireCampRenderer *m_firecamp = nullptr;
  OwnersUpdatedCallback m_onOwnersUpdated;
  VisibilityMaskReadyCallback m_onVisibilityMaskReady;
};

} // namespace Game::Map
