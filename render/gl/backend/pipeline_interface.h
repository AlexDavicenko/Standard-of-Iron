#pragma once

#include "../shader.h"
#include <QOpenGLFunctions_3_3_Core>

namespace Render::GL::BackendPipelines {

class IPipeline : protected QOpenGLFunctions_3_3_Core {
public:
  ~IPipeline() override = default;

  virtual auto initialize() -> bool = 0;

  virtual void shutdown() = 0;

  virtual void cacheUniforms() = 0;

  [[nodiscard]] virtual auto isInitialized() const -> bool = 0;
};

} // namespace Render::GL::BackendPipelines
