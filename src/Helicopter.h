#pragma once
#include <QRandomGenerator>
#include "GameWorld.h"
#include <cmath>

void GameWorld::callDropHelicopter(float targetX, float targetY)
{
    // Задачи:
    // 1. Выбрать случайное направление подлёта за 800м от точки высадки
    // 2. Инициализировать параметры вертолёта и добавить его в симуляцию
    // 3. Обновить данные рендера для QML

    // 1. Направление и точка спавна (800м от цели)
    auto *rng = QRandomGenerator::global();
    float angle = static_cast<float>(rng->generateDouble() * 6.2831853);
    float dirX = std::cos(angle);
    float dirY = std::sin(angle);

    DropHelicopter heli;
    heli.targetPos = QPointF(targetX, targetY);
    heli.spawnPos = QPointF(targetX - dirX * 800.0F, targetY - dirY * 800.0F);
    heli.pos = heli.spawnPos;
    heli.dirX = dirX;
    heli.dirY = dirY;
    heli.angleRad = angle;
    heli.hoverTimerSec = 2.0F;
    heli.phase = DropHelicopter::Phase::Approaching;

    // 2. Добавление в активный список
    g_helicopters.push_back(heli);

    // 3. Оповещение QML
    rebuildHelicoptersRenderData();
    emit helicoptersChanged();
}

void GameWorld::updateHelicopters(float dt)
{
    // Задачи:
    // 1. Обновить вращение винтов всех вертолётов
    // 2. Выполнить шаг текущей фазы полёта (подлёт / зависание / отлёт)
    // 3. Удалить улетевшие за 800м вертолёты и обновить QML

    if (g_helicopters.empty())
        return;

    const float rotorSpeedRad = 22.0F;

    for (size_t i = 0; i < g_helicopters.size();) {
        DropHelicopter &heli = g_helicopters[i];

        // 1. Вращение лопастей
        heli.rotorAngleRad += rotorSpeedRad * dt;
        if (heli.rotorAngleRad > 6.2831853F)
            heli.rotorAngleRad -= 6.2831853F;

        // 2. Обработка фазы полёта
        bool removeHeli = false;
        if (heli.phase == DropHelicopter::Phase::Approaching) {
            moveApproachingHelicopter(heli, dt);
        } else if (heli.phase == DropHelicopter::Phase::Hovering) {
            processHoveringHelicopter(heli, dt);
        } else if (heli.phase == DropHelicopter::Phase::Departing) {
            removeHeli = moveDepartingHelicopter(heli, dt);
        }

        // 3. Удаление после выхода за пределы карты (800м)
        if (removeHeli) {
            g_helicopters.erase(g_helicopters.begin() + i);
        } else {
            ++i;
        }
    }

    rebuildHelicoptersRenderData();
    emit helicoptersChanged();
}

void GameWorld::moveApproachingHelicopter(DropHelicopter &heli, float dt)
{
    // Задачи:
    // 1. Вычислить дистанцию до точки высадки
    // 2. Плавное замедление за 50м до цели
    // 3. Переход в фазу зависания при достижении точки

    // 1. Дистанция до цели
    float dx = static_cast<float>(heli.targetPos.x() - heli.pos.x());
    float dy = static_cast<float>(heli.targetPos.y() - heli.pos.y());
    float dist = std::hypot(dx, dy);

    // 2. Расчёт скорости с торможением за 50м
    const float cruiseSpeed = 135.0F;
    const float minLandingSpeed = 18.0F;
    float speed = cruiseSpeed;
    if (dist < 50.0F) {
        float t = std::clamp(dist / 50.0F, 0.0F, 1.0F);
        speed = minLandingSpeed + (cruiseSpeed - minLandingSpeed) * t;
    }

    // 3. Перемещение и проверка прибытия
    float step = speed * dt;
    if (dist <= step || dist < 1.0F) {
        heli.pos = heli.targetPos;
        heli.phase = DropHelicopter::Phase::Hovering;
        heli.hoverTimerSec = 2.0F;
    } else {
        heli.pos.rx() += heli.dirX * step;
        heli.pos.ry() += heli.dirY * step;
    }
}

void GameWorld::processHoveringHelicopter(DropHelicopter &heli, float dt)
{
    // Задачи:
    // 1. Удерживать вертолёт в точке 2 секунды
    // 2. Высадить 3 десантников и перевести вертолёт в фазу отлёта

    // 1. Отсчёт 2 секунд зависания
    heli.hoverTimerSec -= dt;
    if (heli.hoverTimerSec > 0.0F)
        return;

    // 2. Высадка 3 солдат и старт отлёта
    spawnParatrooperSquad(heli.targetPos);
    heli.phase = DropHelicopter::Phase::Departing;
    heli.traveledAfterDrop = 0.0F;
}

bool GameWorld::moveDepartingHelicopter(DropHelicopter &heli, float dt)
{
    // Задачи:
    // 1. Ускорять вертолёт по прямой от точки высадки
    // 2. Вернуть true, когда вертолёт отлетит на 800м

    // 1. Разгон по прямой
    const float cruiseSpeed = 145.0F;
    float accelFactor = std::clamp(0.25F + (heli.traveledAfterDrop / 60.0F) * 0.75F, 0.25F, 1.0F);
    float step = cruiseSpeed * accelFactor * dt;

    heli.pos.rx() += heli.dirX * step;
    heli.pos.ry() += heli.dirY * step;
    heli.traveledAfterDrop += step;

    // 2. Проверка дистанции исчезновения (800м)
    return (heli.traveledAfterDrop >= 800.0F);
}

void GameWorld::spawnParatrooperSquad(QPointF dropPos)
{
    // Задачи:
    // 1. Создать 3 солдат с лёгким разбросом вокруг точки высадки
    // 2. Задать им время службы 90 секунд и добавить в общий список юнитов

    auto *rng = QRandomGenerator::global();
    const QPointF offsets[3] = {QPointF(-3.0, -2.0), QPointF(3.0, -1.5), QPointF(0.0, 3.0)};

    // 1. Спавн 3 бойцов
    for (int i = 0; i < 3; ++i) {
        QPointF candidate(dropPos.x() + offsets[i].x(), dropPos.y() + offsets[i].y());
        QPointF validPos = isWalkable(static_cast<float>(candidate.x()),
                                      static_cast<float>(candidate.y()))
                               ? candidate
                               : dropPos;

        Unit s;
        s.x = static_cast<float>(validPos.x());
        s.y = static_cast<float>(validPos.y());
        s.vx = 0.0F;
        s.vy = 0.0F;
        s.type = UnitType::Soldier;
        s.hp = 3;
        s.lifeTimerSec = 90.0F; // Срок службы десанта — 90 секунд
        s.isRetreating = false;
        s.onEvacBridge = false;
        s.laneOffset = static_cast<float>((rng->generateDouble() - 0.5) * 0.5);

        // 2. Добавление в мир
        g_units.push_back(s);
    }

    spawnPulseWave(static_cast<float>(dropPos.x()),
                   static_cast<float>(dropPos.y()),
                   18.0F,
                   80,
                   180,
                   255);
}

void GameWorld::rebuildHelicoptersRenderData()
{
    // Задачи:
    // 1. Сформировать данные всех активных вертолётов для QML
    // 2. Сформировать данные кругов высадки (пока вертолёт летит или завис)

    QVariantList helis;
    QVariantList zones;

    for (const auto &h : g_helicopters) {
        // 1. Вертолёт
        QVariantMap hm;
        hm["x"] = h.pos.x();
        hm["y"] = h.pos.y();
        hm["angleDeg"] = h.angleRad * 57.2957795F;
        hm["rotorDeg"] = h.rotorAngleRad * 57.2957795F;
        helis.append(hm);

        // 2. Маркер зоны посадки (пока десант не высажен)
        if (h.phase != DropHelicopter::Phase::Departing) {
            float dist = static_cast<float>(
                std::hypot(h.targetPos.x() - h.pos.x(), h.targetPos.y() - h.pos.y()));
            int etaSec = (h.phase == DropHelicopter::Phase::Hovering)
                             ? static_cast<int>(std::ceil(h.hoverTimerSec))
                             : static_cast<int>(std::ceil((dist / 135.0F) + 2.0F));

            QVariantMap zm;
            zm["x"] = h.targetPos.x();
            zm["y"] = h.targetPos.y();
            zm["eta"] = std::max(1, etaSec);
            zones.append(zm);
        }
    }

    g_helicoptersList = helis;
    g_dropZonesList = zones;
}
