#include "systems/EvacuationSystem.h"
#include <cmath>
#include "MapGenerator.h"
#include "core/WorldState.h"
#include "systems/NavigationService.h"

void EvacuationSystem::setDependencies(const MapGenerator *generator,
                                       const NavigationService *navigation)
{
    generator_ = generator;
    navigation_ = navigation;
}

void EvacuationSystem::onLevelReset(WorldState &state)
{
    // Задачи:
    // 1. Очистить активные операции эвакуации и обновить модель для QML
    (void) state;
    evacs_.clear();
    evacSlice_ = 0;
    rebuildActiveEvacuationsList();
    changed_ = true;
}

bool EvacuationSystem::consumeChangedFlag()
{
    const bool res = changed_;
    changed_ = false;
    return res;
}

bool EvacuationSystem::startEvacuation(int nodeId,
                                       QPointF evacPos,
                                       const std::vector<EvacRoute> &routes,
                                       WorldState &state)
{
    // Задачи:
    // 1. Проверить, что на выбранном выходе ещё не идёт сбор и есть маршруты конвоев
    // 2. Создать ActiveEvacuation, выбрать ближайший маршрут конвоя и списать 75 ₽
    // 3. Оповестить всех жителей в радиусе 250 м и построить им пути к выходу

    for (const auto &ev : evacs_) {
        if (ev.evacNodeId == nodeId && !ev.completed)
            return false;
    }
    if (routes.empty())
        return false;

    const EvacRoute *bestRoute = &routes[0];
    float bestRouteDist = 1e9F;
    for (const auto &r : routes) {
        const float d = static_cast<float>(
            std::hypot(r.startPos.x() - evacPos.x(), r.startPos.y() - evacPos.y()));
        if (d < bestRouteDist) {
            bestRouteDist = d;
            bestRoute = &r;
        }
    }

    state.budget -= 75.0;

    ActiveEvacuation ev;
    ev.evacNodeId = nodeId;
    ev.evacPos = evacPos;
    ev.timeLeftSec = 30.0F;
    ev.boardedCount = 0;
    ev.completed = false;
    ev.convoyStart = bestRoute->startPos;
    ev.convoyEnd = bestRoute->endPos;
    ev.convoyPos = bestRoute->startPos;
    evacs_.push_back(ev);

    // Оповещаем жителей в радиусе 250 м
    for (auto &u : state.units) {
        if (u.type != UnitType::Citizen && u.type != UnitType::Vaccinated)
            continue;

        float ux = u.x;
        float uy = u.y;
        if (generator_ && u.insideBuildingId >= 0
            && u.insideBuildingId < static_cast<int>(generator_->buildings().size())) {
            ux = generator_->buildings()[u.insideBuildingId].cx;
            uy = generator_->buildings()[u.insideBuildingId].cy;
        }

        const float dx = ux - static_cast<float>(evacPos.x());
        const float dy = uy - static_cast<float>(evacPos.y());
        if (dx * dx + dy * dy <= 250.0F * 250.0F) {
            u.panicTimerSec = 0.0F;
            u.state = UnitState::Evacuating;
            u.targetBuildingId = nodeId;
            if (generator_ && navigation_) {
                u.waypoints = navigation_->buildPathBetween(QPointF(u.x, u.y),
                                                            evacPos,
                                                            *generator_,
                                                            state.navGrid);
            }
        }
    }

    rebuildActiveEvacuationsList();
    changed_ = true;
    return true;
}

void EvacuationSystem::update(float dt, WorldState &state)
{
    // Задачи:
    // 1. Обновить таймеры сбора и движение завершивших посадку конвоев
    // 2. Удалить пустые или дошедшие до финиша операции эвакуации
    // 3. Выполнить периодический (1/15 юнитов за тик) призыв жителей в радиусе 250 м

    bool listDirty = false;

    for (size_t i = 0; i < evacs_.size();) {
        auto &ev = evacs_[i];

        if (!ev.completed) {
            ev.timeLeftSec -= dt;
            listDirty = true;
            if (ev.timeLeftSec <= 0.0F) {
                ev.timeLeftSec = 0.0F;
                ev.completed = true;
                if (ev.passengerOffsets.empty()) {
                    // Никто не успел сесть — закрываем операцию
                    evacs_.erase(evacs_.begin() + i);
                    continue;
                }
            }
            ++i;
        } else {
            // Эвакуация выполнена: конвой движется к конечной точке маршрута
            const float dx = static_cast<float>(ev.convoyEnd.x() - ev.convoyPos.x());
            const float dy = static_cast<float>(ev.convoyEnd.y() - ev.convoyPos.y());
            const float dist = std::hypot(dx, dy);

            if (dist < 4.0F) {
                evacs_.erase(evacs_.begin() + i);
                listDirty = true;
            } else {
                const float convoySpeed = 32.0F;
                ev.convoyPos.rx() += (dx / dist) * convoySpeed * dt;
                ev.convoyPos.ry() += (dy / dist) * convoySpeed * dt;
                ++i;
            }
        }
    }

    if (listDirty) {
        rebuildActiveEvacuationsList();
        changed_ = true;
    }

    callNearbyCitizens(state);
}

void EvacuationSystem::callNearbyCitizens(WorldState &state)
{
    // Задачи:
    // 1. Проверить 1/15 часть юнитов за тик для равномерной нагрузки на FPS
    // 2. Направить спокойных/паникующих жителей в радиусе 250 м к активному выходу

    if (evacs_.empty() || !generator_ || !navigation_)
        return;

    evacSlice_ = (evacSlice_ + 1) % 15;

    for (const auto &ev : evacs_) {
        if (ev.completed)
            continue;

        for (size_t i = static_cast<size_t>(evacSlice_); i < state.units.size(); i += 15) {
            auto &u = state.units[i];
            if (u.type != UnitType::Citizen && u.type != UnitType::Vaccinated)
                continue;
            if ((u.state == UnitState::Evacuating || u.state == UnitState::PanicEvacuating)
                && u.targetBuildingId == ev.evacNodeId)
                continue;
            if (u.state == UnitState::Walking && u.stateTimerSec > 0.0F)
                continue;

            const float dx = u.x - static_cast<float>(ev.evacPos.x());
            const float dy = u.y - static_cast<float>(ev.evacPos.y());
            if (dx * dx + dy * dy <= 250.0F * 250.0F) {
                u.state = (u.panicTimerSec > 0.0F) ? UnitState::PanicEvacuating
                                                   : UnitState::Evacuating;
                u.targetBuildingId = ev.evacNodeId;
                u.waypoints = navigation_->buildPathBetween(QPointF(u.x, u.y),
                                                            ev.evacPos,
                                                            *generator_,
                                                            state.navGrid);
            }
        }
    }
}

void EvacuationSystem::collectRenderData(
    std::array<RenderLayerData, static_cast<size_t>(RenderLayerId::Count)> &layers) const
{
    // Задачи:
    // 1. Добавить точки эвакуированных пассажиров в слой RenderLayerId::Units
    //    (круг на месте посадки вытягивается в овал при движении конвоя)

    auto &unitLayer = layers[static_cast<size_t>(RenderLayerId::Units)];

    for (const auto &ev : evacs_) {
        const float dx = static_cast<float>(ev.convoyEnd.x() - ev.convoyStart.x());
        const float dy = static_cast<float>(ev.convoyEnd.y() - ev.convoyStart.y());
        const float angle = std::atan2(dy, dx);
        const float cosA = std::cos(angle);
        const float sinA = std::sin(angle);

        const float stretchAlong = ev.completed ? 1.85F : 1.0F;
        const float squashAcross = ev.completed ? 0.65F : 1.0F;

        for (const auto &off : ev.passengerOffsets) {
            const float lx = static_cast<float>(off.x()) * stretchAlong;
            const float ly = static_cast<float>(off.y()) * squashAcross;

            const float wx = static_cast<float>(ev.convoyPos.x()) + (lx * cosA - ly * sinA);
            const float wy = static_cast<float>(ev.convoyPos.y()) + (lx * sinA + ly * cosA);

            unitLayer.circles.push_back({wx, wy, 2.0F, 2.0F, 0.0F, 240, 245, 255, 255});
        }
    }
}

ActiveEvacuation *EvacuationSystem::findEvacuation(int nodeId)
{
    for (auto &ev : evacs_) {
        if (ev.evacNodeId == nodeId)
            return &ev;
    }
    return nullptr;
}

void EvacuationSystem::rebuildActiveEvacuationsList()
{
    QVariantList list;
    for (const auto &ev : evacs_) {
        if (ev.completed)
            continue;
        QVariantMap m;
        m["x"] = ev.evacPos.x();
        m["y"] = ev.evacPos.y();
        m["timeLeft"] = static_cast<int>(std::ceil(ev.timeLeftSec));
        m["boarded"] = ev.boardedCount;
        list.append(m);
    }
    activeEvacuationsList_ = list;
}

void EvacuationSystem::rebuildEvacPoints(const std::vector<RoadNode> &nodes)
{
    // Задачи:
    // 1. Собрать список всех эвакуационных выходов карты для QML
    QVariantList list;
    for (const auto &node : nodes) {
        if (node.isEvacExit) {
            QVariantMap item;
            item["id"] = node.id;
            item["x"] = node.pos.x();
            item["y"] = node.pos.y();
            item["name"] = node.evacName;
            item["evacuatedCount"] = 0;
            list.append(item);
        }
    }
    evacPointsList_ = list;
}
