#include "systems/HelicopterSystem.h"
#include <QRandomGenerator>
#include <algorithm>
#include <cmath>
#include "core/WorldState.h"
#include "systems/EffectsSystem.h"

void HelicopterSystem::onLevelReset(WorldState &state)
{
    // Задачи:
    // 1. Очистить активные вертолёты и данные рендера для QML
    (void) state;
    helicopters_.clear();
    helicoptersList_.clear();
    dropZonesList_.clear();
    changed_ = true;
}

bool HelicopterSystem::consumeChangedFlag()
{
    const bool res = changed_;
    changed_ = false;
    return res;
}

void HelicopterSystem::callDropHelicopter(float targetX, float targetY)
{
    // Задачи:
    // 1. Выбрать случайное направление подлёта за 800 м от точки высадки
    // 2. Инициализировать параметры вертолёта и добавить его в симуляцию
    // 3. Обновить данные рендера для QML

    auto *rng = QRandomGenerator::global();
    const float angle = static_cast<float>(rng->generateDouble() * 6.2831853);
    const float dirX = std::cos(angle);
    const float dirY = std::sin(angle);

    DropHelicopter heli;
    heli.targetPos = QPointF(targetX, targetY);
    heli.spawnPos = QPointF(targetX - dirX * 800.0F, targetY - dirY * 800.0F);
    heli.pos = heli.spawnPos;
    heli.dirX = dirX;
    heli.dirY = dirY;
    heli.angleRad = angle;
    heli.hoverTimerSec = 1.0F;
    heli.phase = DropHelicopter::Phase::Approaching;

    helicopters_.push_back(heli);
    rebuildRenderData();
    changed_ = true;
}

void HelicopterSystem::update(float dt, WorldState &state)
{
    // Задачи:
    // 1. Обновить вращение винтов всех вертолётов
    // 2. Выполнить шаг текущей фазы полёта (подлёт / зависание / отлёт)
    // 3. Удалить улетевшие за 800 м вертолёты и пересобрать данные QML

    if (helicopters_.empty())
        return;

    const float rotorSpeedRad = 22.0F;

    for (size_t i = 0; i < helicopters_.size();) {
        DropHelicopter &heli = helicopters_[i];

        // 1. Вращение лопастей
        heli.rotorAngleRad += rotorSpeedRad * dt;
        if (heli.rotorAngleRad > 6.2831853F)
            heli.rotorAngleRad -= 6.2831853F;

        // 2. Обработка фазы полёта
        bool removeHeli = false;
        if (heli.phase == DropHelicopter::Phase::Approaching) {
            moveApproaching(heli, dt);
        } else if (heli.phase == DropHelicopter::Phase::Hovering) {
            processHovering(heli, dt, state);
        } else if (heli.phase == DropHelicopter::Phase::Departing) {
            removeHeli = moveDeparting(heli, dt);
        }

        // 3. Удаление после выхода за пределы карты (800 м)
        if (removeHeli) {
            helicopters_.erase(helicopters_.begin() + i);
        } else {
            ++i;
        }
    }

    rebuildRenderData();
    changed_ = true;
}

void HelicopterSystem::moveApproaching(DropHelicopter &heli, float dt)
{
    // Задачи:
    // 1. Вычислить дистанцию до точки высадки
    // 2. Обеспечить плавное замедление за 50 м до цели
    // 3. Перевести вертолёт в фазу зависания при достижении точки

    const float dx = static_cast<float>(heli.targetPos.x() - heli.pos.x());
    const float dy = static_cast<float>(heli.targetPos.y() - heli.pos.y());
    const float dist = std::hypot(dx, dy);

    const float cruiseSpeed = 135.0F;
    const float minLandingSpeed = 12.0F;
    float speed = cruiseSpeed;
    if (dist < 50.0F) {
        const float t = std::clamp(dist / 50.0F, 0.0F, 1.0F);
        speed = minLandingSpeed + (cruiseSpeed - minLandingSpeed) * t;
    }

    const float step = speed * dt;
    if (dist <= step || dist < 1.0F) {
        heli.pos = heli.targetPos;
        heli.phase = DropHelicopter::Phase::Hovering;
    } else {
        heli.pos.rx() += heli.dirX * step;
        heli.pos.ry() += heli.dirY * step;
    }
}

void HelicopterSystem::processHovering(DropHelicopter &heli, float dt, WorldState &state)
{
    // Задачи:
    // 1. Удерживать вертолёт в точке зависания заданное время
    // 2. Высадить отряд десантников и перевести вертолёт в фазу отлёта

    heli.hoverTimerSec -= dt;
    if (heli.hoverTimerSec > 0.0F)
        return;

    spawnParatrooperSquad(heli.targetPos, state);
    heli.phase = DropHelicopter::Phase::Departing;
    heli.traveledAfterDrop = 0.0F;
}

bool HelicopterSystem::moveDeparting(DropHelicopter &heli, float dt)
{
    // Задачи:
    // 1. Плавно ускорять вертолёт по прямой от точки высадки
    // 2. Вернуть true, когда вертолёт отлетит на 800 м

    const float cruiseSpeed = 145.0F;
    const float accelFactor = std::clamp(0.25F + (heli.traveledAfterDrop / 60.0F) * 0.75F,
                                         0.25F,
                                         1.0F);
    const float step = cruiseSpeed * accelFactor * dt;

    heli.pos.rx() += heli.dirX * step;
    heli.pos.ry() += heli.dirY * step;
    heli.traveledAfterDrop += step;

    return (heli.traveledAfterDrop >= 800.0F);
}

void HelicopterSystem::spawnParatrooperSquad(QPointF dropPos, WorldState &state)
{
    // Задачи:
    // 1. Создать 3 солдат с небольшим разбросом вокруг точки высадки (с проверкой NavGrid)
    // 2. Задать им срок службы 90 секунд и добавить в общий список юнитов

    auto *rng = QRandomGenerator::global();
    const QPointF offsets[3] = {QPointF(-3.0, -2.0), QPointF(3.0, -1.5), QPointF(0.0, 3.0)};

    for (int i = 0; i < 3; ++i) {
        const QPointF candidate(dropPos.x() + offsets[i].x(), dropPos.y() + offsets[i].y());
        const QPointF validPos = state.navGrid.isWalkable(static_cast<float>(candidate.x()),
                                                          static_cast<float>(candidate.y()))
                                     ? candidate
                                     : dropPos;

        Unit s;
        s.id = state.nextUnitId++;
        s.x = static_cast<float>(validPos.x());
        s.y = static_cast<float>(validPos.y());
        s.vx = 0.0F;
        s.vy = 0.0F;
        s.type = UnitType::Soldier;
        s.hp = 3;
        s.lifeTimerSec = 90.0F;
        s.isRetreating = false;
        s.onEvacBridge = false;
        s.laneOffset = static_cast<float>((rng->generateDouble() - 0.5) * 0.5);

        state.units.push_back(s);
    }

    if (effects_) {
        effects_->spawnPulseWave(static_cast<float>(dropPos.x()),
                                 static_cast<float>(dropPos.y()),
                                 18.0F,
                                 80,
                                 180,
                                 255);
    }
}

void HelicopterSystem::rebuildRenderData()
{
    // Задачи:
    // 1. Сформировать данные положения и поворота каждого вертолёта для QML
    // 2. Рассчитать точное ETA прибытия до высадки (с учётом участка торможения)

    QVariantList helis;
    QVariantList zones;

    for (const auto &h : helicopters_) {
        QVariantMap hm;
        hm["x"] = h.pos.x();
        hm["y"] = h.pos.y();
        hm["angleDeg"] = h.angleRad * 57.2957795F;
        hm["rotorDeg"] = h.rotorAngleRad * 57.2957795F;
        helis.append(hm);

        if (h.phase != DropHelicopter::Phase::Departing) {
            const float dist = static_cast<float>(
                std::hypot(h.targetPos.x() - h.pos.x(), h.targetPos.y() - h.pos.y()));
            int arriveSec = 0;
            if (h.phase == DropHelicopter::Phase::Hovering) {
                arriveSec = static_cast<int>(std::ceil(h.hoverTimerSec));
            } else {
                const float maxSpeed = 135.0F;
                const float minSpeed = 12.0F;
                const float slowDist = 50.0F;
                float travelTime = 0.0F;

                if (dist > slowDist) {
                    const float travelTimeFast = (dist - slowDist) / maxSpeed;
                    const float travelTimeSlow = (slowDist / (maxSpeed - minSpeed))
                                                 * std::log(maxSpeed / minSpeed);
                    travelTime = travelTimeFast + travelTimeSlow;
                } else if (dist > 0.0F) {
                    const float t = std::clamp(dist / slowDist, 0.0F, 1.0F);
                    const float startSpeed = minSpeed + (maxSpeed - minSpeed) * t;
                    if (startSpeed > minSpeed) {
                        travelTime = (dist / (startSpeed - minSpeed))
                                     * std::log(startSpeed / minSpeed);
                    } else {
                        travelTime = dist / minSpeed;
                    }
                }
                arriveSec = static_cast<int>(std::ceil(travelTime + h.hoverTimerSec));
            }

            QVariantMap zm;
            zm["x"] = h.targetPos.x();
            zm["y"] = h.targetPos.y();
            zm["eta"] = arriveSec;
            zones.append(zm);
        }
    }

    helicoptersList_ = helis;
    dropZonesList_ = zones;
}
