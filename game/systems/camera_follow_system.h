#pragma once

#include <QVector3D>

namespace Engine::Core {
class World;
}

namespace Render::GL {
class Camera;
}

namespace Game::Systems {
class SelectionSystem;
}

namespace Game::Systems {

class CameraFollowSystem {
public:
  static void update(Engine::Core::World &world, SelectionSystem &selection,
                     Render::GL::Camera &camera);

  static void snapToSelection(Engine::Core::World &world,
                              SelectionSystem &selection,
                              Render::GL::Camera &camera);
};

} // namespace Game::Systems
