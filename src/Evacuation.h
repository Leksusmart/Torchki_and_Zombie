#pragma once
#include "GameWorld.h"

void GameWorld::updateEvacuations(float dt)
{
    bool listDirty = false;

    for (size_t i = 0; i < g_evacs.size();) {
        auto &ev = g_evacs[i];

        if (!ev.completed) {
            ev.timeLeftSec -= dt;
            listDirty = true;
            if (ev.timeLeftSec <= 0.0f) {
                ev.timeLeftSec = 0.0f;
                ev.completed = true;
                if (ev.passengerOffsets.empty()) {
                    // Никто не успел сесть — закрываем операцию
                    g_evacs.erase(g_evacs.begin() + i);
                    continue;
                }
            }
            ++i;
        } else {
            // Эвакуация выполнена: конвой движется к конечной точке маршрута
            float dx = static_cast<float>(ev.convoyEnd.x() - ev.convoyPos.x());
            float dy = static_cast<float>(ev.convoyEnd.y() - ev.convoyPos.y());
            float dist = std::hypot(dx, dy);

            if (dist < 4.0f) {
                // Конвой достиг финишной точки (#A0FFFF / #5077FF) и исчезает
                g_evacs.erase(g_evacs.begin() + i);
                listDirty = true;
            } else {
                float convoySpeed = 32.0f;
                ev.convoyPos.rx() += (dx / dist) * convoySpeed * dt;
                ev.convoyPos.ry() += (dy / dist) * convoySpeed * dt;
                ++i;
            }
        }
    }

    if (listDirty) {
        rebuildActiveEvacuationsList();
        emit evacuationsChanged();
    }
}

void GameWorld::rebuildActiveEvacuationsList()
{
    QVariantList list;
    for (const auto &ev : g_evacs) {
        if (ev.completed)
            continue; // После завершения круг 100м и таймер убираются
        QVariantMap m;
        m["x"] = ev.evacPos.x();
        m["y"] = ev.evacPos.y();
        m["timeLeft"] = static_cast<int>(std::ceil(ev.timeLeftSec));
        m["boarded"] = ev.boardedCount;
        list.append(m);
    }
    g_activeEvacuationsList = list;
}

void GameWorld::rebuildEvacPoints()
{
    QVariantList list;
    for (const auto &node : g_generator.nodes()) {
        if (node.isEvacExit) {
            QVariantMap item;
            item["id"] = node.id;
            item["x"] = node.pos.x();
            item["y"] = node.pos.y();
            item["name"] = node.evacName;
            item["evacuatedCount"] = 0; // Задел для будущей статистики эвакуации
            list.append(item);
        }
    }
    g_evacPoints = list;
}