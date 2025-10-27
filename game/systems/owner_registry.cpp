#include "owner_registry.h"
#include <QDebug>
#include <array>
#include <cstddef>
#include <qjsonarray.h>
#include <qjsonobject.h>
#include <qjsonvalue.h>
#include <qnamespace.h>
#include <qstringliteral.h>
#include <string>
#include <vector>

namespace {

auto owner_typeToString(Game::Systems::OwnerType type) -> QString {
  using Game::Systems::OwnerType;
  switch (type) {
  case OwnerType::Player:
    return QStringLiteral("player");
  case OwnerType::AI:
    return QStringLiteral("ai");
  case OwnerType::Neutral:
  default:
    return QStringLiteral("neutral");
  }
}

auto owner_typeFromString(const QString &value) -> Game::Systems::OwnerType {
  using Game::Systems::OwnerType;
  if (value.compare(QStringLiteral("player"), Qt::CaseInsensitive) == 0) {
    return OwnerType::Player;
  }
  if (value.compare(QStringLiteral("ai"), Qt::CaseInsensitive) == 0) {
    return OwnerType::AI;
  }
  return OwnerType::Neutral;
}

auto colorToJson(const std::array<float, 3> &color) -> QJsonArray {
  QJsonArray array;
  array.append(color[0]);
  array.append(color[1]);
  array.append(color[2]);
  return array;
}

auto colorFromJson(const QJsonArray &array) -> std::array<float, 3> {
  std::array<float, 3> color{0.8F, 0.9F, 1.0F};
  if (array.size() >= 3) {
    color[0] = static_cast<float>(array.at(0).toDouble());
    color[1] = static_cast<float>(array.at(1).toDouble());
    color[2] = static_cast<float>(array.at(2).toDouble());
  }
  return color;
}

} // namespace

namespace Game::Systems {

auto OwnerRegistry::instance() -> OwnerRegistry & {
  static OwnerRegistry inst;
  return inst;
}

void OwnerRegistry::clear() {
  m_owners.clear();
  m_owner_idToIndex.clear();
  m_nextOwnerId = 1;
  m_localPlayerId = 1;
}

auto OwnerRegistry::registerOwner(OwnerType type,
                                  const std::string &name) -> int {
  int const owner_id = m_nextOwnerId++;
  OwnerInfo info;
  info.owner_id = owner_id;
  info.type = type;
  info.name = name.empty() ? ("Owner" + std::to_string(owner_id)) : name;

  switch (owner_id) {
  case 1:
    info.color = {0.20F, 0.55F, 1.00F};
    break;
  case 2:
    info.color = {1.00F, 0.30F, 0.30F};
    break;
  case 3:
    info.color = {0.20F, 0.80F, 0.40F};
    break;
  case 4:
    info.color = {1.00F, 0.80F, 0.20F};
    break;
  default:
    info.color = {0.8F, 0.9F, 1.0F};
    break;
  }

  size_t const index = m_owners.size();
  m_owners.push_back(info);
  m_owner_idToIndex[owner_id] = index;

  return owner_id;
}

void OwnerRegistry::registerOwnerWithId(int owner_id, OwnerType type,
                                        const std::string &name) {
  if (m_owner_idToIndex.find(owner_id) != m_owner_idToIndex.end()) {
    return;
  }

  OwnerInfo info;
  info.owner_id = owner_id;
  info.type = type;
  info.name = name.empty() ? ("Owner" + std::to_string(owner_id)) : name;

  switch (owner_id) {
  case 1:
    info.color = {0.20F, 0.55F, 1.00F};
    break;
  case 2:
    info.color = {1.00F, 0.30F, 0.30F};
    break;
  case 3:
    info.color = {0.20F, 0.80F, 0.40F};
    break;
  case 4:
    info.color = {1.00F, 0.80F, 0.20F};
    break;
  default:
    info.color = {0.8F, 0.9F, 1.0F};
    break;
  }

  size_t const index = m_owners.size();
  m_owners.push_back(info);
  m_owner_idToIndex[owner_id] = index;

  if (owner_id >= m_nextOwnerId) {
    m_nextOwnerId = owner_id + 1;
  }
}

void OwnerRegistry::setLocalPlayerId(int player_id) {
  m_localPlayerId = player_id;
}

auto OwnerRegistry::getLocalPlayerId() const -> int { return m_localPlayerId; }

auto OwnerRegistry::isPlayer(int owner_id) const -> bool {
  auto it = m_owner_idToIndex.find(owner_id);
  if (it == m_owner_idToIndex.end()) {
    return false;
  }
  return m_owners[it->second].type == OwnerType::Player;
}

auto OwnerRegistry::isAI(int owner_id) const -> bool {
  auto it = m_owner_idToIndex.find(owner_id);
  if (it == m_owner_idToIndex.end()) {
    return false;
  }
  return m_owners[it->second].type == OwnerType::AI;
}

auto OwnerRegistry::getOwnerType(int owner_id) const -> OwnerType {
  auto it = m_owner_idToIndex.find(owner_id);
  if (it == m_owner_idToIndex.end()) {
    return OwnerType::Neutral;
  }
  return m_owners[it->second].type;
}

auto OwnerRegistry::getOwnerName(int owner_id) const -> std::string {
  auto it = m_owner_idToIndex.find(owner_id);
  if (it == m_owner_idToIndex.end()) {
    return "Unknown";
  }
  return m_owners[it->second].name;
}

auto OwnerRegistry::getAllOwners() const -> const std::vector<OwnerInfo> & {
  return m_owners;
}

auto OwnerRegistry::getPlayerOwnerIds() const -> std::vector<int> {
  std::vector<int> result;
  for (const auto &owner : m_owners) {
    if (owner.type == OwnerType::Player) {
      result.push_back(owner.owner_id);
    }
  }
  return result;
}

auto OwnerRegistry::getAIOwnerIds() const -> std::vector<int> {
  std::vector<int> result;
  for (const auto &owner : m_owners) {
    if (owner.type == OwnerType::AI) {
      result.push_back(owner.owner_id);
    }
  }
  return result;
}

void OwnerRegistry::setOwnerTeam(int owner_id, int team_id) {
  auto it = m_owner_idToIndex.find(owner_id);
  if (it != m_owner_idToIndex.end()) {
    m_owners[it->second].team_id = team_id;
  }
}

auto OwnerRegistry::getOwnerTeam(int owner_id) const -> int {
  auto it = m_owner_idToIndex.find(owner_id);
  if (it == m_owner_idToIndex.end()) {
    return 0;
  }
  return m_owners[it->second].team_id;
}

auto OwnerRegistry::areAllies(int owner_id1, int owner_id2) const -> bool {

  if (owner_id1 == owner_id2) {
    return true;
  }

  int const team1 = getOwnerTeam(owner_id1);
  int const team2 = getOwnerTeam(owner_id2);

  bool const result = (team1 == team2);

  return result;
}

auto OwnerRegistry::areEnemies(int owner_id1, int owner_id2) const -> bool {

  if (owner_id1 == owner_id2) {
    return false;
  }

  if (areAllies(owner_id1, owner_id2)) {
    return false;
  }

  return true;
}

auto OwnerRegistry::getAlliesOf(int owner_id) const -> std::vector<int> {
  std::vector<int> result;
  int const my_team = getOwnerTeam(owner_id);

  if (my_team == 0) {
    return result;
  }

  for (const auto &owner : m_owners) {
    if (owner.owner_id != owner_id && owner.team_id == my_team) {
      result.push_back(owner.owner_id);
    }
  }
  return result;
}

auto OwnerRegistry::getEnemiesOf(int owner_id) const -> std::vector<int> {
  std::vector<int> result;

  for (const auto &owner : m_owners) {
    if (areEnemies(owner_id, owner.owner_id)) {
      result.push_back(owner.owner_id);
    }
  }
  return result;
}

void OwnerRegistry::setOwnerColor(int owner_id, float r, float g, float b) {
  auto it = m_owner_idToIndex.find(owner_id);
  if (it != m_owner_idToIndex.end()) {
    m_owners[it->second].color = {r, g, b};
  }
}

auto OwnerRegistry::getOwnerColor(int owner_id) const -> std::array<float, 3> {
  auto it = m_owner_idToIndex.find(owner_id);
  if (it != m_owner_idToIndex.end()) {
    return m_owners[it->second].color;
  }

  return {0.8F, 0.9F, 1.0F};
}

auto OwnerRegistry::toJson() const -> QJsonObject {
  QJsonObject root;
  root["nextOwnerId"] = m_nextOwnerId;
  root["localPlayerId"] = m_localPlayerId;

  QJsonArray owners_array;
  for (const auto &owner : m_owners) {
    QJsonObject owner_obj;
    owner_obj["owner_id"] = owner.owner_id;
    owner_obj["type"] = owner_typeToString(owner.type);
    owner_obj["name"] = QString::fromStdString(owner.name);
    owner_obj["team_id"] = owner.team_id;
    owner_obj["color"] = colorToJson(owner.color);
    owners_array.append(owner_obj);
  }

  root["owners"] = owners_array;
  return root;
}

void OwnerRegistry::fromJson(const QJsonObject &json) {
  clear();

  m_nextOwnerId = json["nextOwnerId"].toInt(1);
  m_localPlayerId = json["localPlayerId"].toInt(1);

  const auto owners_array = json["owners"].toArray();
  m_owners.reserve(owners_array.size());
  for (const auto &value : owners_array) {
    const auto owner_obj = value.toObject();
    OwnerInfo info;
    info.owner_id = owner_obj["owner_id"].toInt();
    info.type = owner_typeFromString(owner_obj["type"].toString());
    info.name = owner_obj["name"].toString().toStdString();
    info.team_id = owner_obj["team_id"].toInt(0);
    if (owner_obj.contains("color")) {
      info.color = colorFromJson(owner_obj["color"].toArray());
    }

    const size_t index = m_owners.size();
    m_owners.push_back(info);
    m_owner_idToIndex[info.owner_id] = index;
  }

  for (const auto &owner : m_owners) {
    if (owner.owner_id >= m_nextOwnerId) {
      m_nextOwnerId = owner.owner_id + 1;
    }
  }
}

} // namespace Game::Systems
