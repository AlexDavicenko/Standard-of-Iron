#pragma once

#include "../ai_behavior.h"

namespace Game::Systems::AI {

class RetreatBehavior : public AIBehavior {
public:
  void execute(const AISnapshot &snapshot, AIContext &context, float deltaTime,
               std::vector<AICommand> &outCommands) override;

  [[nodiscard]] auto
  should_execute(const AISnapshot &snapshot,
                 const AIContext &context) const -> bool override;

  [[nodiscard]] auto getPriority() const -> BehaviorPriority override {
    return BehaviorPriority::Critical;
  }

  [[nodiscard]] auto canRunConcurrently() const -> bool override {
    return false;
  }

private:
  float m_retreatTimer = 0.0F;
};

} // namespace Game::Systems::AI
