#include "systems/WallSystem.h"
#include <algorithm>
#include <cmath>
#include "core/NavGrid.h"
#include "core/WorldState.h"
#include "systems/EffectsSystem.h"

namespace WallHelper {

std::vector<RawSection> extractWallSections(float x1,
                                            float y1,
                                            float x2,
                                            float y2,
                                            const std::vector<int16_t> &walkGrid,
                                            const NavGrid &navGrid)
{
    // Задачи:
    // 1. Ограничить максимальную длину протяжки стены до 260 м
    // 2. Прошагать по отрезку с шагом 1 м, разбивая его на валидные участки дорог/тропинок
    // 3. Отбросить слишком короткие сегменты (< 6 м) и рассчитать цену, HP и время постройки

    std::vector<RawSection> sections;
    const int mapW = navGrid.width();
    const int mapH = navGrid.height();
    if (mapW <= 0 || mapH <= 0)
        return sections;

    float dx = x2 - x1;
    float dy = y2 - y1;
    float totalLen = std::hypot(dx, dy);
    if (totalLen < 6.0F)
        return sections;

    const float maxDragLen = 260.0F;
    if (totalLen > maxDragLen) {
        const float scale = maxDragLen / totalLen;
        x2 = x1 + dx * scale;
        y2 = y1 + dy * scale;
        dx = x2 - x1;
        dy = y2 - y1;
        totalLen = maxDragLen;
    }

    const float ux = dx / totalLen;
    const float uy = dy / totalLen;

    auto isCellValidForWall = [&](float px, float py) -> bool {
        const float offsets[3] = {0.0F, -1.5F, 1.5F};
        for (float oy : offsets) {
            for (float ox : offsets) {
                const int gx = static_cast<int>(px + ox);
                const int gy = static_cast<int>(py + oy);
                if (!navGrid.inBounds(gx, gy))
                    continue;
                // Запрещаем пересечение с уже стоящей стеной/препятствием
                if (navGrid.hasFlag(gx, gy, NavCellFlag::BlocksMovement))
                    return false;
                const int16_t cell = walkGrid[static_cast<size_t>(gy) * mapW + gx];
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
        const float curT = std::min(t, totalLen);
        const float px = x1 + ux * curT;
        const float py = y1 + uy * curT;
        const bool ok = isCellValidForWall(px, py);

        if (ok && !inSegment) {
            inSegment = true;
            segStartT = curT;
        } else if ((!ok || curT >= totalLen - 0.01F) && inSegment) {
            const float segEndT = ok ? curT : std::max(segStartT, curT - step);
            const float segLen = segEndT - segStartT;
            inSegment = false;

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

bool segmentsIntersect(QPointF a, QPointF b, QPointF c, QPointF d)
{
    auto ccw = [](QPointF p1, QPointF p2, QPointF p3) {
        return (p3.y() - p1.y()) * (p2.x() - p1.x()) > (p2.y() - p1.y()) * (p3.x() - p1.x());
    };
    return (ccw(a, c, d) != ccw(b, c, d)) && (ccw(a, b, c) != ccw(a, b, d));
}

} // namespace WallHelper

void WallSystem::onLevelReset(WorldState &state)
{
    // Задачи:
    // 1. Очистить все стены, буферы геометрии и выделение при перезапуске уровня
    (void) state;
    walls_.clear();
    geometry_.clear();
    wallProgressBars_.clear();
    selectedWallId_ = -1;
    nextWallId_ = 1;
    updateSelectedWallInfo();
    mapDirty_ = true;
    overlaysDirty_ = true;
    selectionDirty_ = true;
}

bool WallSystem::consumeMapDirtyFlag()
{
    const bool res = mapDirty_;
    mapDirty_ = false;
    return res;
}

bool WallSystem::consumeOverlaysDirtyFlag()
{
    const bool res = overlaysDirty_;
    overlaysDirty_ = false;
    return res;
}

bool WallSystem::consumeSelectionDirtyFlag()
{
    const bool res = selectionDirty_;
    selectionDirty_ = false;
    return res;
}

QVariantMap WallSystem::previewWall(float x1,
                                    float y1,
                                    float x2,
                                    float y2,
                                    double budget,
                                    const std::vector<int16_t> &walkGrid,
                                    const NavGrid &navGrid) const
{
    // Задачи:
    // 1. Ограничить длину протяжки и нарезать отрезок на валидные секции
    // 2. Подсчитать суммарную стоимость, диапазон HP и время строительства для QML

    QVariantMap res;
    float dx = x2 - x1;
    float dy = y2 - y1;
    float rawLen = std::hypot(dx, dy);

    const float maxDragLen = 260.0F;
    if (rawLen > maxDragLen && rawLen > 0.001F) {
        const float scale = maxDragLen / rawLen;
        x2 = x1 + dx * scale;
        y2 = y1 + dy * scale;
        dx = x2 - x1;
        dy = y2 - y1;
        rawLen = maxDragLen;
    }

    const auto sections = WallHelper::extractWallSections(x1, y1, x2, y2, walkGrid, navGrid);

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

    const bool valid = (!sections.empty()) && (budget >= totalCost);
    const float len = std::max(4.0F, rawLen);
    const float angle = (rawLen > 0.01F) ? std::atan2(dy, dx) : 0.0F;

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

bool WallSystem::canPlaceWallSegment(float x1,
                                     float y1,
                                     float x2,
                                     float y2,
                                     const std::vector<int16_t> &walkGrid,
                                     const NavGrid &navGrid) const
{
    return !WallHelper::extractWallSections(x1, y1, x2, y2, walkGrid, navGrid).empty();
}

bool WallSystem::placeWall(float x1,
                           float y1,
                           float x2,
                           float y2,
                           const std::vector<int16_t> &walkGrid,
                           WorldState &state)
{
    // Задачи:
    // 1. Получить валидные секции и проверить достаточность бюджета
    // 2. Создать QuarantineWall в фазе строительства и обновить геометрию и оверлеи

    const auto sections = WallHelper::extractWallSections(x1, y1, x2, y2, walkGrid, state.navGrid);
    if (sections.empty())
        return false;

    int totalCost = 0;
    for (const auto &s : sections)
        totalCost += s.cost;

    if (state.budget < totalCost)
        return false;

    state.budget -= totalCost;

    for (const auto &s : sections) {
        QuarantineWall wall;
        wall.id = nextWallId_++;
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
        walls_.push_back(wall);
    }

    rebuildGeometry();
    rebuildOverlays();
    mapDirty_ = true;
    overlaysDirty_ = true;
    return true;
}

void WallSystem::applyToNavGrid(NavGrid &grid) const
{
    // Задачи:
    // 1. Растеризовать все достроенные стены в NavGrid флагами BlocksMovement и BlocksSight

    for (const auto &wall : walls_) {
        if (wall.isConstructing || wall.hp <= 0)
            continue;

        const float cosA = std::cos(wall.angleRad);
        const float sinA = std::sin(wall.angleRad);
        const float hw = wall.length * 0.5F;
        const float hh = wall.thickness * 0.5F;

        for (float ly = -hh; ly <= hh; ly += 0.5F) {
            for (float lx = -hw; lx <= hw; lx += 0.5F) {
                const int gx = static_cast<int>(wall.cx + lx * cosA - ly * sinA);
                const int gy = static_cast<int>(wall.cy + lx * sinA + ly * cosA);
                grid.addFlag(gx, gy, NavCellFlag::BlocksMovement);
                grid.addFlag(gx, gy, NavCellFlag::BlocksSight);
            }
        }
    }
}

void WallSystem::collectRenderData(
    std::array<RenderLayerData, static_cast<size_t>(RenderLayerId::Count)> &layers) const
{
    // Задачи:
    // 1. Передать геометрию всех стен в слой RenderLayerId::Obstacles
    auto &layer = layers[static_cast<size_t>(RenderLayerId::Obstacles)];
    layer.isStaticMapLayer = true;
    layer.quads.insert(layer.quads.end(), geometry_.begin(), geometry_.end());
}

bool WallSystem::findBlockingObstacle(
    float x, float y, float dirX, float dirY, float maxDist, DestructibleHit &outHit) const
{
    // Задачи:
    // 1. Проверить точку впереди на расстоянии maxDist по вектору (dirX, dirY)
    // 2. Найти ближайшую активную стену, до поверхности которой <= maxDist

    if (walls_.empty())
        return false;

    const float probeX = x + dirX * maxDist;
    const float probeY = y + dirY * maxDist;
    float bestSurfaceDist = maxDist + 0.5F;
    bool found = false;

    for (const auto &w : walls_) {
        if (w.isConstructing || w.hp <= 0)
            continue;

        const float vx = w.x2 - w.x1;
        const float vy = w.y2 - w.y1;
        const float lenSq = vx * vx + vy * vy;
        const float t = (lenSq > 1e-4F)
                            ? std::clamp(((probeX - w.x1) * vx + (probeY - w.y1) * vy) / lenSq,
                                         0.0F,
                                         1.0F)
                            : 0.0F;
        const float projX = w.x1 + vx * t;
        const float projY = w.y1 + vy * t;

        const float distToCenter = std::hypot(projX - x, projY - y);
        const float surfaceDist = std::max(0.0F, distToCenter - w.thickness * 0.5F);

        if (surfaceDist <= bestSurfaceDist) {
            bestSurfaceDist = surfaceDist;
            outHit.obstacleId = w.id;
            outHit.distToSurface = surfaceDist;
            outHit.hitPoint = QPointF(projX, projY);
            found = true;
        }
    }

    return found;
}

bool WallSystem::applyDamage(int obstacleId, int damage, QPointF hitPos)
{
    // Задачи:
    // 1. Найти стену по ID, уменьшить HP и включить белый контур атаки на 3 секунды
    // 2. Заспавнить искры удара через EffectsSystem

    for (auto &w : walls_) {
        if (w.id != obstacleId || w.isConstructing || w.hp <= 0)
            continue;

        w.hp = std::max(0, w.hp - damage);
        w.underAttackTimerSec = 3.0F;
        if (effects_) {
            effects_->spawnBurstParticles(static_cast<float>(hitPos.x()),
                                          static_cast<float>(hitPos.y()),
                                          3,
                                          255,
                                          170,
                                          70);
        }
        return true;
    }
    return false;
}

bool WallSystem::blocksSegment(QPointF a, QPointF b) const
{
    for (const auto &wall : walls_) {
        if (wall.isConstructing || wall.hp <= 0)
            continue;
        const float ux = std::cos(wall.angleRad) * 3.0F;
        const float uy = std::sin(wall.angleRad) * 3.0F;
        const QPointF wa(wall.x1 - ux, wall.y1 - uy);
        const QPointF wb(wall.x2 + ux, wall.y2 + uy);
        if (WallHelper::segmentsIntersect(a, b, wa, wb))
            return true;
    }
    return false;
}

void WallSystem::update(float dt, WorldState &state)
{
    // Задачи:
    // 1. Обновить таймеры подсветки атаки, строительства и сноса стен
    // 2. Удалить разрушенные или снесённые стены со спавном частиц
    // 3. При завершении строительства вытолкнуть юнитов за пределы стены

    if (walls_.empty())
        return;

    bool geomDirty = false;
    bool maskDirty = false;
    bool overlaysDirty = false;

    for (size_t i = 0; i < walls_.size();) {
        auto &w = walls_[i];

        if (w.underAttackTimerSec > 0.0F) {
            w.underAttackTimerSec = std::max(0.0F, w.underAttackTimerSec - dt);
            geomDirty = true;
        }

        // 1. Разрушена зомби
        if (w.hp <= 0) {
            if (effects_) {
                effects_->spawnBurstParticles(w.cx, w.cy, 14, 160, 165, 175);
                effects_->spawnPulseWave(w.cx, w.cy, 28.0F, 220, 110, 110);
            }
            if (selectedWallId_ == w.id) {
                selectedWallId_ = -1;
                updateSelectedWallInfo();
                selectionDirty_ = true;
            }
            walls_.erase(walls_.begin() + i);
            geomDirty = true;
            maskDirty = true;
            overlaysDirty = true;
            continue;
        }

        // 2. Завершение строительства
        if (w.isConstructing) {
            w.buildTimeLeft -= dt;
            overlaysDirty = true;
            if (w.buildTimeLeft <= 0.0F) {
                w.buildTimeLeft = 0.0F;
                w.isConstructing = false;
                geomDirty = true;
                maskDirty = true;
                pushUnitsOutOfWall(w, state.units);
            }
        }

        // 3. Завершение сноса
        if (w.isTearingDown) {
            w.teardownTimeLeft -= dt;
            overlaysDirty = true;
            if (w.teardownTimeLeft <= 0.0F) {
                if (effects_) {
                    effects_->spawnBurstParticles(w.cx, w.cy, 14, 170, 175, 185);
                    effects_->spawnPulseWave(w.cx, w.cy, 30.0F, 180, 185, 195);
                }
                if (selectedWallId_ == w.id) {
                    selectedWallId_ = -1;
                    updateSelectedWallInfo();
                    selectionDirty_ = true;
                }
                walls_.erase(walls_.begin() + i);
                geomDirty = true;
                maskDirty = true;
                overlaysDirty = true;
                continue;
            }
        }

        ++i;
    }

    if (maskDirty) {
        state.navGrid.clearDynamicObstacles();
        applyToNavGrid(state.navGrid);
    }
    if (geomDirty) {
        rebuildGeometry();
        mapDirty_ = true;
    }
    if (overlaysDirty) {
        rebuildOverlays();
        overlaysDirty_ = true;
    }
    if (selectedWallId_ != -1) {
        updateSelectedWallInfo();
        selectionDirty_ = true;
    }
}

void WallSystem::pushUnitsOutOfWall(const QuarantineWall &wall, std::vector<Unit> &units) const
{
    const float cosA = std::cos(-wall.angleRad);
    const float sinA = std::sin(-wall.angleRad);
    const float nx = -std::sin(wall.angleRad);
    const float ny = std::cos(wall.angleRad);

    for (auto &u : units) {
        const float dx = u.x - wall.cx;
        const float dy = u.y - wall.cy;
        const float lx = dx * cosA - dy * sinA;
        const float ly = dx * sinA + dy * cosA;
        if (std::abs(lx) <= wall.length * 0.5F + 1.5F
            && std::abs(ly) <= wall.thickness * 0.5F + 1.5F) {
            const float sign = (ly >= 0.0F) ? 1.0F : -1.0F;
            const float shift = (wall.thickness * 0.5F + 3.0F) - std::abs(ly);
            u.x += nx * sign * shift;
            u.y += ny * sign * shift;
        }
    }
}

void WallSystem::rebuildGeometry()
{
    // Задачи:
    // 1. Построить прямоугольники со скруглёнными фасками для каждой стены в зависимости от её фазы
    // 2. Отрисовать внутреннюю полосу прочности пропорционально текущему HP

    geometry_.clear();

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
        const float bv = std::min({bevel, len * 0.25F, thick * 0.25F});
        if (bv <= 0.2F) {
            geometry_.push_back({cx, cy, len, thick, angleRad, r, g, b, a});
            return;
        }
        geometry_.push_back({cx, cy, len - 2.0F * bv, thick, angleRad, r, g, b, a});
        geometry_.push_back({cx, cy, len, thick - 2.0F * bv, angleRad, r, g, b, a});
        geometry_.push_back({cx, cy, len - bv, thick - bv, angleRad, r, g, b, a});
    };

    for (const auto &w : walls_) {
        const float innerLen = std::max(1.0F, w.length - 2.0F);
        const float innerThick = std::max(1.0F, w.thickness - 2.0F);
        const float stripeThick = w.thickness * 0.34F;

        if (w.isConstructing) {
            addChamferedRect(w.cx, w.cy, w.length, w.thickness, w.angleRad, 1.4F, 229, 62, 62, 190);
            addChamferedRect(w.cx, w.cy, innerLen, innerThick, w.angleRad, 1.0F, 110, 45, 50, 140);
        } else if (w.isTearingDown) {
            addChamferedRect(w.cx, w.cy, w.length, w.thickness, w.angleRad, 1.4F, 203, 213, 224, 255);
            addChamferedRect(w.cx, w.cy, innerLen, innerThick, w.angleRad, 1.0F, 65, 72, 84, 255);
            geometry_.push_back({w.cx, w.cy, innerLen, stripeThick, w.angleRad, 160, 174, 192, 255});
        } else {
            const bool attacked = (w.underAttackTimerSec > 0.0F);
            const uint8_t br = attacked ? 255 : 245;
            const uint8_t bg = attacked ? 255 : 101;
            const uint8_t bb = attacked ? 255 : 101;

            addChamferedRect(w.cx, w.cy, w.length, w.thickness, w.angleRad, 1.4F, br, bg, bb, 255);
            addChamferedRect(w.cx, w.cy, innerLen, innerThick, w.angleRad, 1.0F, 44, 48, 56, 255);

            const float hpRatio = (w.maxHp > 0)
                                      ? std::clamp(static_cast<float>(w.hp) / w.maxHp, 0.0F, 1.0F)
                                      : 0.0F;
            if (hpRatio > 0.0F) {
                const float barLen = std::max(1.0F, innerLen * hpRatio);
                const float offsetFromCenter = -(innerLen - barLen) * 0.5F;
                const float barCx = w.cx + std::cos(w.angleRad) * offsetFromCenter;
                const float barCy = w.cy + std::sin(w.angleRad) * offsetFromCenter;
                geometry_.push_back({barCx, barCy, barLen, stripeThick, w.angleRad, 229, 62, 62, 255});
            }
        }
    }
}

void WallSystem::rebuildOverlays()
{
    QVariantList list;
    for (const auto &w : walls_) {
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
    wallProgressBars_ = list;
}

bool WallSystem::selectWallAt(float worldX, float worldY)
{
    int foundId = -1;
    for (const auto &w : walls_) {
        if (w.isConstructing)
            continue;
        const float dx = worldX - w.cx;
        const float dy = worldY - w.cy;
        const float cosA = std::cos(-w.angleRad);
        const float sinA = std::sin(-w.angleRad);
        const float lx = dx * cosA - dy * sinA;
        const float ly = dx * sinA + dy * cosA;
        if (std::abs(lx) <= w.length * 0.5F + 2.0F && std::abs(ly) <= w.thickness * 0.5F + 2.0F) {
            foundId = w.id;
            break;
        }
    }
    if (selectedWallId_ != foundId) {
        selectedWallId_ = foundId;
        updateSelectedWallInfo();
        selectionDirty_ = true;
    }
    return (foundId != -1);
}

void WallSystem::updateSelectedWallInfo()
{
    QVariantMap info;
    for (const auto &w : walls_) {
        if (w.id == selectedWallId_ && !w.isConstructing) {
            info["valid"] = true;
            info["id"] = w.id;
            info["hp"] = w.hp;
            info["maxHp"] = w.maxHp;
            info["cx"] = w.cx;
            info["cy"] = w.cy;
            info["isTearingDown"] = w.isTearingDown;
            selectedWallInfo_ = info;
            return;
        }
    }
    info["valid"] = false;
    selectedWallInfo_ = info;
}

bool WallSystem::startWallTeardown(int wallId, WorldState &state)
{
    // Задачи:
    // 1. Проверить бюджет (>= 200 ₽) и перевести стену в состояние сноса на 10 секунд

    if (state.budget < 200.0)
        return false;

    for (auto &w : walls_) {
        if (w.id == wallId && !w.isConstructing && !w.isTearingDown) {
            state.budget -= 200.0;
            w.isTearingDown = true;
            w.teardownTimeTotal = 10.0F;
            w.teardownTimeLeft = 10.0F;
            selectedWallId_ = -1;
            updateSelectedWallInfo();
            rebuildGeometry();
            rebuildOverlays();
            selectionDirty_ = true;
            overlaysDirty_ = true;
            mapDirty_ = true;
            return true;
        }
    }
    return false;
}
