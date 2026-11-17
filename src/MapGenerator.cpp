#include "MapGenerator.h"
#include <QDebug>
#include <QPainter>
#include <QRandomGenerator>
#include <algorithm>
#include <cmath>
#include <numeric>

bool MapGenerator::generateFromLevel(const LevelDefinition &levelDef)
{
    m_roadWidth = levelDef.roadWidthMeters;
    m_sourceMask.load(levelDef.mapPngPath);
    if (m_sourceMask.isNull()) {
        qWarning() << "MapGenerator: Не удалось загрузить файл карты:" << levelDef.mapPngPath;
        return false;
    }

    m_sourceMask = m_sourceMask.convertToFormat(QImage::Format_ARGB32);
    m_width = m_sourceMask.width();
    m_height = m_sourceMask.height();

    // Берем цвет крайнего левого верхнего пикселя как общий фон воды
    m_waterColor = QColor::fromRgb(m_sourceMask.pixel(0, 0));

    m_cityMask.assign(m_width * m_height, 0);
    m_walkGrid.assign(m_width * m_height, 0);
    m_nodes.clear();
    m_edges.clear();
    m_buildings.clear();

    m_backgroundImage = m_sourceMask.copy();

    const uint32_t CLR_CITY = 0x3A3E48;
    const uint32_t CLR_NODE = 0xFFF200;
    const uint32_t CLR_EVAC = 0xED1C24;

    // Эвакуация
    m_evacRoutes.clear();
    const uint32_t CLR_G1_START = 0x10FFFF;
    const uint32_t CLR_G1_END = 0xA0FFFF;
    const uint32_t CLR_G2_START = 0x1077FF;
    const uint32_t CLR_G2_END = 0x5077FF;
    std::vector<QPointF> g1Starts, g1Ends, g2Starts, g2Ends;
    int evacCounter = 0;

    // 1. Сканируем пиксели ОДИН РАЗ, затираем маркеры и именуем выходы
    for (int y = 0; y < m_height; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(m_sourceMask.constScanLine(y));
        QRgb *bgLine = reinterpret_cast<QRgb *>(m_backgroundImage.scanLine(y));

        for (int x = 0; x < m_width; ++x) {
            uint32_t rgb = line[x] & 0x00FFFFFF;

            if (rgb == CLR_CITY) {
                m_cityMask[y * m_width + x] = 1;
            } else if (rgb == CLR_NODE || rgb == CLR_EVAC) {
                m_cityMask[y * m_width + x] = 1;
                QRgb neighborColor = (x > 0) ? m_sourceMask.pixel(x - 1, y) : qRgb(58, 62, 72);
                if ((neighborColor & 0x00FFFFFF) == CLR_NODE
                    || (neighborColor & 0x00FFFFFF) == CLR_EVAC) {
                    neighborColor = qRgb(58, 62, 72);
                }
                bgLine[x] = neighborColor;

                RoadNode node;
                node.id = static_cast<int>(m_nodes.size());
                node.pos = QPointF(x + 0.5, y + 0.5);
                node.isEvacExit = (rgb == CLR_EVAC);

                if (node.isEvacExit) {
                    if (evacCounter < levelDef.evacNames.size()) {
                        node.evacName = levelDef.evacNames[evacCounter];
                    } else {
                        node.evacName = QString("Evac Point %1").arg(evacCounter + 1);
                    }
                    evacCounter++;
                }

                m_nodes.push_back(node);
            } else if (rgb == CLR_G1_START || rgb == CLR_G1_END || rgb == CLR_G2_START
                       || rgb == CLR_G2_END) {
                QRgb neighborPixel = (x + 1 < m_width) ? m_sourceMask.pixel(x + 1, y)
                                                       : m_waterColor.rgb();
                bgLine[x] = neighborPixel; // Скрываем маркер цветом соседа
                QPointF pt(x + 0.5, y + 0.5);
                if (rgb == CLR_G1_START)
                    g1Starts.push_back(pt);
                else if (rgb == CLR_G1_END)
                    g1Ends.push_back(pt);
                else if (rgb == CLR_G2_START)
                    g2Starts.push_back(pt);
                else if (rgb == CLR_G2_END)
                    g2Ends.push_back(pt);
            }
        }
    }

    if (m_nodes.size() < 2) {
        qWarning() << "MapGenerator: На карте найдено меньше 2 точек дорог!";
        return true;
    }
    // Собираем маршруты эвакуации
    auto pairGroupRoutes =
        [&](int group, const std::vector<QPointF> &starts, const std::vector<QPointF> &ends) {
            if (ends.empty())
                return;
            for (const auto &s : starts) {
                QPointF bestEnd = ends[0];
                float bestDist = 1e9f;
                for (const auto &e : ends) {
                    float d = static_cast<float>(std::hypot(e.x() - s.x(), e.y() - s.y()));
                    if (d < bestDist) {
                        bestDist = d;
                        bestEnd = e;
                    }
                }
                m_evacRoutes.push_back({group, s, bestEnd});
            }
        };
    pairGroupRoutes(1, g1Starts, g1Ends);
    pairGroupRoutes(2, g2Starts, g2Ends);

    // 2. Собираем ВСЕ возможные пары точек (без ограничений по длине и препятствиям)
    struct CandidateEdge
    {
        int u, v;
        float dist;
    };
    std::vector<CandidateEdge> candidates;
    candidates.reserve((m_nodes.size() * (m_nodes.size() - 1)) / 2);

    for (size_t i = 0; i < m_nodes.size(); ++i) {
        for (size_t j = i + 1; j < m_nodes.size(); ++j) {
            float dx = static_cast<float>(m_nodes[i].pos.x() - m_nodes[j].pos.x());
            float dy = static_cast<float>(m_nodes[i].pos.y() - m_nodes[j].pos.y());
            candidates.push_back({static_cast<int>(i), static_cast<int>(j), std::hypot(dx, dy)});
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
        return a.dist < b.dist;
    });

    // Шаг А: Минимальное остовное дерево (MST) — связывает ВСЕ точки в единую сеть без оторванных улиц
    std::vector<int> parent(m_nodes.size());
    std::iota(parent.begin(), parent.end(), 0);
    auto findRoot = [&](int v) {
        while (v != parent[v]) {
            parent[v] = parent[parent[v]];
            v = parent[v];
        }
        return v;
    };

    std::vector<std::vector<float>> nodeAngles(m_nodes.size());
    std::vector<bool> edgeUsed(candidates.size(), false);

    auto addRoadEdge = [&](size_t candIdx) {
        const auto &e = candidates[candIdx];
        m_edges.push_back({e.u, e.v});
        edgeUsed[candIdx] = true;

        float angleUV = std::atan2(static_cast<float>(m_nodes[e.v].pos.y() - m_nodes[e.u].pos.y()),
                                   static_cast<float>(m_nodes[e.v].pos.x() - m_nodes[e.u].pos.x()));
        float angleVU = std::atan2(static_cast<float>(m_nodes[e.u].pos.y() - m_nodes[e.v].pos.y()),
                                   static_cast<float>(m_nodes[e.u].pos.x() - m_nodes[e.v].pos.x()));

        nodeAngles[e.u].push_back(angleUV);
        nodeAngles[e.v].push_back(angleVU);
    };

    for (size_t i = 0; i < candidates.size(); ++i) {
        int ru = findRoot(candidates[i].u);
        int rv = findRoot(candidates[i].v);
        if (ru != rv) {
            parent[ru] = rv;
            addRoadEdge(i);
        }
    }

    // Шаг Б: Добавляем перемычки (4 и 5 связей делаем редкими)
    auto *rng = QRandomGenerator::global();
    std::vector<int> targetDegree(m_nodes.size());
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        int roll = rng->bounded(100);
        if (roll < 45)
            targetDegree[i] = 2; // 45% узлов — 2 связи
        else if (roll < 85)
            targetDegree[i] = 3; // 40% узлов — 3 связи
        else if (roll < 96)
            targetDegree[i] = 4; // 11% узлов — 4 связи
        else
            targetDegree[i] = 5; // 4% узлов  — 5 связей
    }

    auto isAngleValid = [&](int nodeIdx, float newAngle) {
        const float MIN_ANGLE_DIFF = 0.50f; // ~28 градусов минимальный угол между дорогами
        for (float existing : nodeAngles[nodeIdx]) {
            float diff = std::abs(newAngle - existing);
            if (diff > 3.14159f)
                diff = 6.28318f - diff;
            if (diff < MIN_ANGLE_DIFF)
                return false;
        }
        return true;
    };

    for (size_t i = 0; i < candidates.size(); ++i) {
        if (edgeUsed[i])
            continue;

        int u = candidates[i].u;
        int v = candidates[i].v;

        // Ограничиваем длину дополнительных (не обязательных для связности) дорог до 220м,
        // чтобы не тянуть лишние диагонали через весь остров
        if (candidates[i].dist > 220.0f)
            continue;

        if ((static_cast<int>(nodeAngles[u].size()) < targetDegree[u]
             || static_cast<int>(nodeAngles[v].size()) < targetDegree[v])
            && static_cast<int>(nodeAngles[u].size()) < targetDegree[u] + 1
            && static_cast<int>(nodeAngles[v].size()) < targetDegree[v] + 1) {
            float angleUV = std::atan2(static_cast<float>(m_nodes[v].pos.y() - m_nodes[u].pos.y()),
                                       static_cast<float>(m_nodes[v].pos.x() - m_nodes[u].pos.x()));
            float angleVU = std::atan2(static_cast<float>(m_nodes[u].pos.y() - m_nodes[v].pos.y()),
                                       static_cast<float>(m_nodes[u].pos.x() - m_nodes[v].pos.x()));

            if (isAngleValid(u, angleUV) && isAngleValid(v, angleVU)) {
                addRoadEdge(i);
            }
        }
    }

    // Диагностика связей перед отрисовкой
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        if (nodeAngles[i].empty()) {
            qDebug() << "ОШИБКА СВЯЗНОСТИ: Точка id =" << m_nodes[i].id << "в координатах"
                     << m_nodes[i].pos << "не получила ни одной связи!";
        }
    }

    rasterizeRoads(levelDef.roadWidthMeters);
    generateBuildings(levelDef); // Передаем настройки процентов застройки
    return true;
}

void MapGenerator::rasterizeRoads(float roadWidth)
{
    QPainter bgPainter(&m_backgroundImage);
    bgPainter.setRenderHint(QPainter::Antialiasing, false);
    QPen roadPen(QColor(24, 27, 33), roadWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    bgPainter.setPen(roadPen);

    QImage walkMask(m_width, m_height, QImage::Format_Grayscale8);
    walkMask.fill(0);
    QPainter walkPainter(&walkMask);
    walkPainter.setRenderHint(QPainter::Antialiasing, false);
    QPen maskPen(Qt::white, roadWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    walkPainter.setPen(maskPen);

    for (const auto &edge : m_edges) {
        QPointF p1 = m_nodes[edge.fromId].pos;
        QPointF p2 = m_nodes[edge.toId].pos;
        bgPainter.drawLine(p1, p2);
        walkPainter.drawLine(p1, p2);
    }

    bgPainter.end();
    walkPainter.end();

    // Проходимо всё, где нарисована дорога (даже если она прошла через сквер)
    for (int y = 0; y < m_height; ++y) {
        const uint8_t *line = walkMask.constScanLine(y);
        for (int x = 0; x < m_width; ++x) {
            if (line[x] > 128) {
                m_walkGrid[y * m_width + x] = 1;
            }
        }
    }
}

QPointF MapGenerator::findEntranceOnRoad(float cx, float cy) const
{
    QPointF buildingCenter(cx, cy);
    QPointF bestPoint = buildingCenter;
    float bestDist = 1e9f;

    for (const auto &edge : m_edges) {
        QPointF a = m_nodes[edge.fromId].pos;
        QPointF b = m_nodes[edge.toId].pos;

        // Проекция центра здания на отрезок дороги AB
        QPointF ab = b - a;
        float lenSq = static_cast<float>(ab.x() * ab.x() + ab.y() * ab.y());
        float t = 0.0f;
        if (lenSq > 0.001f) {
            t = static_cast<float>(
                ((buildingCenter.x() - a.x()) * ab.x() + (buildingCenter.y() - a.y()) * ab.y())
                / lenSq);
            t = std::clamp(t, 0.05f, 0.95f); // Не ставим дверь на самый центр перекрёстка
        }

        QPointF proj = a + ab * t;
        float d = static_cast<float>(std::hypot(proj.x() - cx, proj.y() - cy));
        if (d < bestDist) {
            bestDist = d;
            bestPoint = proj;
        }
    }
    return bestPoint;
}

bool MapGenerator::tryPlaceBuilding(
    int x, int y, float bw, float bh, BuildingType type, std::vector<uint8_t> &occupied)
{
    if (occupied[y * m_width + x] != 0)
        return false;

    // Выравниваем здание по направлению ближайшей дороги
    float angle = 0.0f;
    float bestDist = 1e9f;
    for (const auto &edge : m_edges) {
        QPointF p1 = m_nodes[edge.fromId].pos;
        QPointF p2 = m_nodes[edge.toId].pos;
        QPointF mid = (p1 + p2) * 0.5;
        float d = static_cast<float>(std::hypot(mid.x() - x, mid.y() - y));
        if (d < bestDist) {
            bestDist = d;
            angle = std::atan2(static_cast<float>(p2.y() - p1.y()),
                               static_cast<float>(p2.x() - p1.x()));
        }
    }

    float cosA = std::cos(angle);
    float sinA = std::sin(angle);
    // Учитываем +2px рамки (border) и +1px зазора между соседними зданиями
    float hw = bw * 0.5f + 3.0f;
    float hh = bh * 0.5f + 3.0f;

    // Проверяем, помещается ли здание целиком в свободную зону застройки
    for (float ly = -hh; ly <= hh; ly += 2.0f) {
        for (float lx = -hw; lx <= hw; lx += 2.0f) {
            int px = static_cast<int>(x + lx * cosA - ly * sinA);
            int py = static_cast<int>(y + lx * sinA + ly * cosA);
            if (px < 0 || px >= m_width || py < 0 || py >= m_height
                || occupied[py * m_width + px] != 0) {
                return false;
            }
        }
    }

    // Помечаем территорию под зданием как занятую
    for (float ly = -hh; ly <= hh; ly += 1.0f) {
        for (float lx = -hw; lx <= hw; lx += 1.0f) {
            int px = static_cast<int>(x + lx * cosA - ly * sinA);
            int py = static_cast<int>(y + lx * sinA + ly * cosA);
            if (px >= 0 && px < m_width && py >= 0 && py < m_height) {
                occupied[py * m_width + px] = 1;
            }
        }
    }

    auto *rng = QRandomGenerator::global();
    BuildingObject b;
    b.id = static_cast<int>(m_buildings.size());
    b.type = type;
    b.cx = static_cast<float>(x);
    b.cy = static_cast<float>(y);
    b.width = bw;
    b.height = bh;
    b.angleRad = angle;
    b.area = bw * bh;
    b.maxHealth = std::max(20, static_cast<int>(b.area / 5.0f));
    b.health = b.maxHealth;
    b.entrancePos = findEntranceOnRoad(b.cx, b.cy);

    // Вместимость зависит от площади и типа здания
    if (type == BuildingType::Factory) {
        b.capacity = static_cast<int>(b.area / 24.0f); // Много рабочих мест на заводе
    } else if (type == BuildingType::Office) {
        b.capacity = static_cast<int>(b.area / 30.0f); // Плотная офисная застройка
    } else {
        b.capacity = static_cast<int>(b.area / 40.0f); // Жилой
    }

    // Визуально все здания одинаковы по цвету
    uint8_t shade = static_cast<uint8_t>(85 + rng->bounded(35));
    b.r = shade;
    b.g = static_cast<uint8_t>(shade + 4);
    b.b = static_cast<uint8_t>(shade + 10);

    m_buildings.push_back(b);
    return true;
}

void MapGenerator::generateBuildings(const LevelDefinition &levelDef)
{
    auto *rng = QRandomGenerator::global();
    std::vector<uint8_t> occupied(m_width * m_height, 1);

    for (int i = 0; i < m_width * m_height; ++i) {
        if (m_cityMask[i] == 1 && m_walkGrid[i] == 0) {
            occupied[i] = 0;
        }
    }

    int factoriesCount = 0;
    int officesCount = 0;
    int residentialCount = 0;

    // Базовый размер жилого дома ~12x12м (площадь ~144 м²)
    // Проход 1: ЗАВОДЫ (площадь в 4-6 раз больше жилого -> ~580 - 860 м², стороны ~24-32м)
    for (int y = 18; y < m_height - 18; y += 22) {
        for (int x = 18; x < m_width - 18; x += 22) {
            if (rng->bounded(100) < levelDef.factoryChancePercent) {
                float bw = static_cast<float>(24 + rng->bounded(8)); // 24..31 м
                float bh = static_cast<float>(22 + rng->bounded(7)); // 22..28 м
                if (tryPlaceBuilding(x, y, bw, bh, BuildingType::Factory, occupied)) {
                    factoriesCount++;
                }
            }
        }
    }

    // Проход 2: ОФИСЫ (площадь от 1x до 3x жилого -> ~160 - 430 м², стороны ~14-24м)
    for (int y = 12; y < m_height - 12; y += 16) {
        for (int x = 12; x < m_width - 12; x += 16) {
            if (rng->bounded(100) < levelDef.officeChancePercent) {
                float bw = static_cast<float>(15 + rng->bounded(9)); // 15..23 м
                float bh = static_cast<float>(13 + rng->bounded(7)); // 13..19 м
                if (tryPlaceBuilding(x, y, bw, bh, BuildingType::Office, occupied)) {
                    officesCount++;
                }
            }
        }
    }

    // Проход 3: ЖИЛЫЕ ДОМА (базовый размер 10..14 м, заполняют все оставшиеся места)
    for (int y = 8; y < m_height - 8; y += 12) {
        for (int x = 8; x < m_width - 8; x += 12) {
            if (rng->bounded(100) < levelDef.houseChancePercent) {
                float bw = static_cast<float>(10 + rng->bounded(5)); // 10..14 м
                float bh = static_cast<float>(10 + rng->bounded(5)); // 10..14 м
                if (tryPlaceBuilding(x, y, bw, bh, BuildingType::Residential, occupied)) {
                    residentialCount++;
                }
            }
        }
    }

    qDebug() << "Генерация зданий завершена:"
             << "Заводов =" << factoriesCount << "| Офисов =" << officesCount
             << "| Жилых =" << residentialCount << "| Всего =" << m_buildings.size();
}
