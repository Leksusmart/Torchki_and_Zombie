#pragma once
#include "GameWorld.h"
#include <algorithm>
#include <cmath>
#include <queue>

namespace WallHelper {
struct RawSection
{
    float x1 = 0.0F, y1 = 0.0F;
    float x2 = 0.0F, y2 = 0.0F;
    float length = 0.0F;
    int cost = 0;
    int hp = 0;
    float buildTime = 0.0F;
};

// Нарезает отрезок (x1,y1)->(x2,y2) на валидные проходимые участки улиц (обходя здания, воду и другие стены)
inline std::vector<RawSection> extractWallSections(float x1,
                                                   float y1,
                                                   float x2,
                                                   float y2,
                                                   const std::vector<int16_t> &walkGrid,
                                                   const std::vector<uint8_t> &wallMask,
                                                   int mapW,
                                                   int mapH)
{
    std::vector<RawSection> sections;

    if (mapW <= 0 || mapH <= 0)
        return sections;

    float dx = x2 - x1;
    float dy = y2 - y1;
    float totalLen = std::hypot(dx, dy);
    if (totalLen < 6.0F)
        return sections;

    const float maxDragLen = 260.0F;
    if (totalLen > maxDragLen) {
        float scale = maxDragLen / totalLen;
        x2 = x1 + dx * scale;
        y2 = y1 + dy * scale;
        dx = x2 - x1;
        dy = y2 - y1;
        totalLen = maxDragLen;
    }

    float ux = dx / totalLen;
    float uy = dy / totalLen;

    // Проверяем проходимость точки на улице (с допуском 1.5м от края дороги/тротуара)
    auto isCellValidForWall = [&](float px, float py) -> bool {
        const float offsets[3] = {0.0F, -1.5F, 1.5F};

        for (float oy : offsets) {
            for (float ox : offsets) {
                int gx = static_cast<int>(px + ox);
                int gy = static_cast<int>(py + oy);
                if (gx < 0 || gx >= mapW || gy < 0 || gy >= mapH)
                    continue;
                int idx = gy * mapW + gx;
                if (!wallMask.empty() && wallMask[idx] != 0)
                    return false; // Пересечение с уже стоящей стеной запрещено
                int16_t cell = walkGrid[idx];
                if (cell == 1 || cell == 2)
                    return true;
            }
        }
        return false;
    };

    const float step = 1.0F;
    bool inSegment = false;
    float segStartT = 0.0F;

    for (float t = 0.0F; t <= totalLen + 0.01F; t += step) {
        float curT = std::min(t, totalLen);
        float px = x1 + ux * curT;
        float py = y1 + uy * curT;
        bool ok = isCellValidForWall(px, py);

        if (ok && !inSegment) {
            inSegment = true;
            segStartT = curT;
        } else if ((!ok || curT >= totalLen - 0.01F) && inSegment) {
            float segEndT = ok ? curT : std::max(segStartT, curT - step);
            float segLen = segEndT - segStartT;
            inSegment = false;

            // Отбрасываем совсем крошечные огрызки
            if (segLen >= 6.0F) {
                RawSection sec;
                sec.x1 = x1 + ux * segStartT;
                sec.y1 = y1 + uy * segStartT;
                sec.x2 = x1 + ux * segEndT;
                sec.y2 = y1 + uy * segEndT;
                sec.length = segLen;
                sec.cost = 15 + static_cast<int>(std::round(segLen * 0.65F));
                sec.hp = std::max(50, static_cast<int>(std::round(segLen * 5.8F)));
                sec.buildTime = std::max(0.8F, segLen * 0.07F);
                sections.push_back(sec);
            }
        }
    }

    return sections;
}

// Проверка пересечения двух отрезков AB и CD
inline bool segmentsIntersect(QPointF a, QPointF b, QPointF c, QPointF d)
{
    auto ccw = [](QPointF p1, QPointF p2, QPointF p3) {
        return (p3.y() - p1.y()) * (p2.x() - p1.x()) > (p2.y() - p1.y()) * (p3.x() - p1.x());
    };
    return (ccw(a, c, d) != ccw(b, c, d)) && (ccw(a, b, c) != ccw(a, b, d));
}
} // namespace WallHelper

QVariantMap GameWorld::previewWall(float x1, float y1, float x2, float y2) const
{
    QVariantMap res;
    float dx = x2 - x1;
    float dy = y2 - y1;
    float rawLen = distanceBetween(dx, dy);

    const float maxDragLen = 260.0F;
    if (rawLen > maxDragLen && rawLen > 0.001F) {
        float scale = maxDragLen / rawLen;
        x2 = x1 + dx * scale;
        y2 = y1 + dy * scale;
        dx = x2 - x1;
        dy = y2 - y1;
        rawLen = maxDragLen;
    }

    auto sections = WallHelper::extractWallSections(x1,
                                                    y1,
                                                    x2,
                                                    y2,
                                                    g_generator.walkGrid(),
                                                    g_wallMask,
                                                    g_generator.width(),
                                                    g_generator.height());

    int totalCost = 0;
    int minHp = 1000000;
    int maxHp = 0;
    float maxBuildTime = 0.0F;

    for (const auto &s : sections) {
        totalCost += s.cost;
        minHp = std::min(minHp, s.hp);
        maxHp = std::max(maxHp, s.hp);
        maxBuildTime = std::max(maxBuildTime, s.buildTime);
    }

    if (sections.empty()) {
        minHp = 0;
        maxHp = 0;
    }

    bool valid = (!sections.empty()) && (g_budget >= totalCost);
    float len = std::max(4.0F, rawLen);
    float angle = (rawLen > 0.01F) ? std::atan2(dy, dx) : 0.0F;

    res["valid"] = valid;
    res["x1"] = x1;
    res["y1"] = y1;
    res["x2"] = x2;
    res["y2"] = y2;
    res["cx"] = (x1 + x2) * 0.5F;
    res["cy"] = (y1 + y2) * 0.5F;
    res["length"] = len;
    res["angleDeg"] = angle * 57.2957795F;
    res["sectionCount"] = static_cast<int>(sections.size());
    res["cost"] = totalCost;
    res["minHp"] = minHp;
    res["maxHp"] = maxHp;
    res["buildTime"] = maxBuildTime;
    return res;
}

bool GameWorld::canPlaceWallSegment(float x1, float y1, float x2, float y2) const
{
    auto sections = WallHelper::extractWallSections(x1,
                                                    y1,
                                                    x2,
                                                    y2,
                                                    g_generator.walkGrid(),
                                                    g_wallMask,
                                                    g_generator.width(),
                                                    g_generator.height());
    return !sections.empty();
}

bool GameWorld::placeWall(float x1, float y1, float x2, float y2)
{
    auto sections = WallHelper::extractWallSections(x1,
                                                    y1,
                                                    x2,
                                                    y2,
                                                    g_generator.walkGrid(),
                                                    g_wallMask,
                                                    g_generator.width(),
                                                    g_generator.height());
    if (sections.empty())
        return false;

    int totalCost = 0;
    for (const auto &s : sections)
        totalCost += s.cost;

    if (g_budget < totalCost)
        return false;

    g_budget -= totalCost;

    for (const auto &s : sections) {
        QuarantineWall wall;
        wall.id = g_nextWallId++;
        wall.x1 = s.x1;
        wall.y1 = s.y1;
        wall.x2 = s.x2;
        wall.y2 = s.y2;
        wall.cx = (s.x1 + s.x2) * 0.5F;
        wall.cy = (s.y1 + s.y2) * 0.5F;
        wall.length = s.length;
        wall.thickness = 9.0F;
        wall.angleRad = std::atan2(s.y2 - s.y1, s.x2 - s.x1);
        wall.hp = s.hp;
        wall.maxHp = s.hp;
        wall.isConstructing = true;
        wall.buildTimeTotal = s.buildTime;
        wall.buildTimeLeft = s.buildTime;
        g_walls.push_back(wall);
    }

    rebuildWallsGeometry();
    rebuildWallOverlays();
    emit wallsChanged();
    emit statsChanged();
    emit mapUpdated();
    return true;
}

void GameWorld::rasterizeAllWallsToGrid()
{
    int w = g_generator.width();
    int h = g_generator.height();
    g_wallMask.assign(w * h, 0);

    for (const auto &wall : g_walls) {
        if (wall.isConstructing || wall.hp <= 0)
            continue;

        float cosA = std::cos(wall.angleRad);
        float sinA = std::sin(wall.angleRad);
        float hw = wall.length * 0.5F;
        float hh = wall.thickness * 0.5F;

        for (float ly = -hh; ly <= hh; ly += 0.5F) {
            for (float lx = -hw; lx <= hw; lx += 0.5F) {
                int gx = static_cast<int>(wall.cx + lx * cosA - ly * sinA);
                int gy = static_cast<int>(wall.cy + lx * sinA + ly * cosA);
                if (gx >= 0 && gx < w && gy >= 0 && gy < h) {
                    g_wallMask[gy * w + gx] = 1;
                }
            }
        }
    }

    // 1. Обновляем веса рёбер графа дорог с учётом построенных стен (занимает < 0.5 мс)
    const auto &nodes = g_generator.nodes();
    const auto &edges = g_generator.edges();
    const int n = static_cast<int>(nodes.size());
    if (n == 0)
        return;

    g_roadAdj.assign(n, {});
    for (const auto &e : edges) {
        QPointF p1 = nodes[e.fromId].pos;
        QPointF p2 = nodes[e.toId].pos;
        float dist = static_cast<float>(distanceBetween(p1.x() - p2.x(), p1.y() - p2.y()));

        bool blockedByWall = false;
        for (const auto &wall : g_walls) {
            if (wall.isConstructing || wall.hp <= 0)
                continue;
            float ux = std::cos(wall.angleRad) * 3.0F;
            float uy = std::sin(wall.angleRad) * 3.0F;
            QPointF wa(wall.x1 - ux, wall.y1 - uy);
            QPointF wb(wall.x2 + ux, wall.y2 + uy);
            if (WallHelper::segmentsIntersect(p1, p2, wa, wb)) {
                blockedByWall = true;
                break;
            }
        }

        float weight = blockedByWall ? 1e8F : dist;
        g_roadAdj[e.fromId].push_back({e.toId, weight});
        g_roadAdj[e.toId].push_back({e.fromId, weight});
    }

    // 2. Быстро перестраиваем маршруты только тем юнитам, чей путь пересекает активную стену
    if (g_walls.empty())
        return;

    for (auto &u : g_units) {
        if (u.hp <= 0 || u.waypoints.empty() || u.type == UnitType::Zombie)
            continue;

        bool pathBlocked = false;
        QPointF prevPt(u.x, u.y);
        for (const auto &wp : u.waypoints) {
            for (const auto &wall : g_walls) {
                if (wall.isConstructing || wall.hp <= 0)
                    continue;
                float ux = std::cos(wall.angleRad) * 3.0F;
                float uy = std::sin(wall.angleRad) * 3.0F;
                QPointF wa(wall.x1 - ux, wall.y1 - uy);
                QPointF wb(wall.x2 + ux, wall.y2 + uy);
                if (WallHelper::segmentsIntersect(prevPt, wp, wa, wb)) {
                    pathBlocked = true;
                    break;
                }
            }
            if (pathBlocked)
                break;
            prevPt = wp;
        }

        if (pathBlocked) {
            QPointF dest = u.waypoints.back();
            u.waypoints = buildPathBetween(QPointF(u.x, u.y), dest);
        }
    }
}

void GameWorld::updateWalls(float dt)
{
    if (g_walls.empty())
        return;

    bool geomDirty = false;
    bool maskDirty = false;
    bool overlaysDirty = false;

    for (size_t i = 0; i < g_walls.size();) {
        auto &w = g_walls[i];

        // 0. Таймер подсветки белого контура при атаке зомби
        if (w.underAttackTimerSec > 0.0F) {
            w.underAttackTimerSec = std::max(0.0F, w.underAttackTimerSec - dt);
            geomDirty = true; // Обновляем полоску HP и белый контур
        }

        // 1. Разрушена зомби
        if (w.hp <= 0) {
            spawnBurstParticles(w.cx, w.cy, 14, 160, 165, 175);
            spawnPulseWave(w.cx, w.cy, 28.0F, 220, 110, 110);
            if (g_selectedWallId == w.id) {
                g_selectedWallId = -1;
                updateSelectedWallInfo();
                emit selectedWallChanged();
            }
            g_walls.erase(g_walls.begin() + i);
            geomDirty = true;
            maskDirty = true;
            overlaysDirty = true;
            continue;
        }

        // 2. Идёт строительство
        if (w.isConstructing) {
            w.buildTimeLeft -= dt;
            overlaysDirty = true;
            if (w.buildTimeLeft <= 0.0F) {
                w.buildTimeLeft = 0.0F;
                w.isConstructing = false;
                geomDirty = true;
                maskDirty = true;

                float cosA = std::cos(-w.angleRad);
                float sinA = std::sin(-w.angleRad);
                float nx = -std::sin(w.angleRad);
                float ny = std::cos(w.angleRad);
                for (auto &u : g_units) {
                    float dx = u.x - w.cx;
                    float dy = u.y - w.cy;
                    float lx = dx * cosA - dy * sinA;
                    float ly = dx * sinA + dy * cosA;
                    if (std::abs(lx) <= w.length * 0.5F + 1.5F
                        && std::abs(ly) <= w.thickness * 0.5F + 1.5F) {
                        float sign = (ly >= 0.0F) ? 1.0F : -1.0F;
                        float shift = (w.thickness * 0.5F + 3.0F) - std::abs(ly);
                        u.x += nx * sign * shift;
                        u.y += ny * sign * shift;
                    }
                }
            }
        }

        // 3. Идёт снос (Take Down)
        if (w.isTearingDown) {
            w.teardownTimeLeft -= dt;
            overlaysDirty = true;
            if (w.teardownTimeLeft <= 0.0F) {
                spawnBurstParticles(w.cx, w.cy, 14, 170, 175, 185);
                spawnPulseWave(w.cx, w.cy, 30.0F, 180, 185, 195);
                if (g_selectedWallId == w.id) {
                    g_selectedWallId = -1;
                    updateSelectedWallInfo();
                    emit selectedWallChanged();
                }
                g_walls.erase(g_walls.begin() + i);
                geomDirty = true;
                maskDirty = true;
                overlaysDirty = true;
                continue;
            }
        }

        ++i;
    }

    if (maskDirty)
        rasterizeAllWallsToGrid();
    if (geomDirty) {
        rebuildWallsGeometry();
        emit mapUpdated();
    }
    if (overlaysDirty) {
        rebuildWallOverlays();
        emit wallsChanged();
    }
    if (g_selectedWallId != -1) {
        updateSelectedWallInfo();
        emit selectedWallChanged();
    }
}

void GameWorld::rebuildWallsGeometry()
{
    g_wallsGeometry.clear();
    g_wallsCapsGeometry.clear();

    // Рисуем прямоугольник со слегка скруглёнными краями (фаска 1.5м без перекрытия слоёв)
    auto addChamferedRect = [&](float cx,
                                float cy,
                                float len,
                                float thick,
                                float angleRad,
                                float bevel,
                                uint8_t r,
                                uint8_t g,
                                uint8_t b,
                                uint8_t a) {
        float bv = std::min({bevel, len * 0.25F, thick * 0.25F});
        if (bv <= 0.2F) {
            g_wallsGeometry.push_back({cx, cy, len, thick, angleRad, r, g, b, a});
            return;
        }
        // Центральный прямоугольник + 2 чуть более узких торцевых пояска = эффект скруглённых углов
        g_wallsGeometry.push_back({cx, cy, len - 2.0F * bv, thick, angleRad, r, g, b, a});
        g_wallsGeometry.push_back({cx, cy, len, thick - 2.0F * bv, angleRad, r, g, b, a});
        g_wallsGeometry.push_back({cx, cy, len - bv, thick - bv, angleRad, r, g, b, a});
    };

    for (const auto &w : g_walls) {
        float innerLen = std::max(1.0F, w.length - 2.0F);
        float innerThick = std::max(1.0F, w.thickness - 2.0F);
        float stripeThick = w.thickness * 0.34F;

        if (w.isConstructing) {
            addChamferedRect(w.cx, w.cy, w.length, w.thickness, w.angleRad, 1.4F, 229, 62, 62, 190);
            addChamferedRect(w.cx, w.cy, innerLen, innerThick, w.angleRad, 1.0F, 110, 45, 50, 140);
        } else if (w.isTearingDown) {
            addChamferedRect(w.cx, w.cy, w.length, w.thickness, w.angleRad, 1.4F, 203, 213, 224, 255);
            addChamferedRect(w.cx, w.cy, innerLen, innerThick, w.angleRad, 1.0F, 65, 72, 84, 255);
            g_wallsGeometry
                .push_back({w.cx, w.cy, innerLen, stripeThick, w.angleRad, 160, 174, 192, 255});
        } else {
            bool attacked = (w.underAttackTimerSec > 0.0F);
            uint8_t br = attacked ? 255 : 245;
            uint8_t bg = attacked ? 255 : 101;
            uint8_t bb = attacked ? 255 : 101;

            // 1. Внешний контур со скруглёнными углами
            addChamferedRect(w.cx, w.cy, w.length, w.thickness, w.angleRad, 1.4F, br, bg, bb, 255);

            // 2. Внутренний тёмный корпус со скруглёнными углами
            addChamferedRect(w.cx, w.cy, innerLen, innerThick, w.angleRad, 1.0F, 44, 48, 56, 255);

            // 3. Внутренняя красная полоса здоровья
            float hpRatio = (w.maxHp > 0)
                                ? std::clamp(static_cast<float>(w.hp) / w.maxHp, 0.0F, 1.0F)
                                : 0.0F;
            if (hpRatio > 0.0F) {
                float barLen = std::max(1.0F, innerLen * hpRatio);
                float offsetFromCenter = -(innerLen - barLen) * 0.5F;
                float barCx = w.cx + std::cos(w.angleRad) * offsetFromCenter;
                float barCy = w.cy + std::sin(w.angleRad) * offsetFromCenter;

                g_wallsGeometry
                    .push_back({barCx, barCy, barLen, stripeThick, w.angleRad, 229, 62, 62, 255});
            }
        }
    }
}

void GameWorld::rebuildWallOverlays()
{
    QVariantList list;
    for (const auto &w : g_walls) {
        if (!w.isConstructing && !w.isTearingDown)
            continue;
        QVariantMap item;
        item["cx"] = w.cx;
        item["cy"] = w.cy;
        item["isConstructing"] = w.isConstructing;
        if (w.isConstructing) {
            item["timeLeft"] = QString::number(std::max(0.0F, w.buildTimeLeft), 'f', 1);
            item["progress"] = 1.0F - (w.buildTimeLeft / std::max(0.01F, w.buildTimeTotal));
        } else {
            item["timeLeft"] = QString::number(std::max(0.0F, w.teardownTimeLeft), 'f', 1);
            item["progress"] = 1.0F - (w.teardownTimeLeft / std::max(0.01F, w.teardownTimeTotal));
        }
        list.append(item);
    }
    g_wallProgressBarsList = list;
}
bool GameWorld::selectWallAt(float worldX, float worldY)
{
    int foundId = -1;
    for (const auto &w : g_walls) {
        if (w.isConstructing)
            continue;
        float dx = worldX - w.cx;
        float dy = worldY - w.cy;
        float cosA = std::cos(-w.angleRad);
        float sinA = std::sin(-w.angleRad);
        float lx = dx * cosA - dy * sinA;
        float ly = dx * sinA + dy * cosA;
        if (std::abs(lx) <= w.length * 0.5F + 2.0F && std::abs(ly) <= w.thickness * 0.5F + 2.0F) {
            foundId = w.id;
            break;
        }
    }
    if (g_selectedWallId != foundId) {
        g_selectedWallId = foundId;
        updateSelectedWallInfo();
        emit selectedWallChanged();
    }
    return (foundId != -1);
}
void GameWorld::updateSelectedWallInfo()
{
    QVariantMap info;
    for (const auto &w : g_walls) {
        if (w.id == g_selectedWallId && !w.isConstructing) {
            info["valid"] = true;
            info["id"] = w.id;
            info["hp"] = w.hp;
            info["maxHp"] = w.maxHp;
            info["cx"] = w.cx;
            info["cy"] = w.cy;
            info["isTearingDown"] = w.isTearingDown;
            g_selectedWallInfo = info;
            return;
        }
    }
    info["valid"] = false;
    g_selectedWallInfo = info;
}
bool GameWorld::startWallTeardown(int wallId)
{
    if (g_budget < 200.0)
        return false;
    for (auto &w : g_walls) {
        if (w.id == wallId && !w.isConstructing && !w.isTearingDown) {
            g_budget -= 200.0;
            w.isTearingDown = true;
            w.teardownTimeTotal = 10.0F;
            w.teardownTimeLeft = 10.0F;
            g_selectedWallId = -1;
            updateSelectedWallInfo();
            rebuildWallsGeometry();
            rebuildWallOverlays();
            emit selectedWallChanged();
            emit wallsChanged();
            emit statsChanged();
            emit mapUpdated();
            return true;
        }
    }
    return false;
}
