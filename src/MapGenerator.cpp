#include "MapGenerator.h"
#include <QDebug>
#include <QPainter>
#include <QRandomGenerator>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <queue>

bool MapGenerator::generateFromLevel(const LevelDefinition &newLevel)
{
    level = newLevel;
    image.load(level.mapPngPath);
    if (image.isNull()) {
        qWarning() << "MapGenerator: Не удалось загрузить файл карты:" << level.mapPngPath;
        return false;
    }

    image = image.convertToFormat(QImage::Format_ARGB32);
    mapWidth = image.width();
    mapHeight = image.height();

    // Берем цвет крайнего левого верхнего пикселя как общий фон
    mapOutborderColor = QColor::fromRgb(image.pixel(0, 0));

    cityMap.assign(mapWidth * mapHeight, 0);
    walkMap.assign(mapWidth * mapHeight, 0);
    nodesList.clear();
    edgesList.clear();
    buildingsList.clear();

    backgroundImage = image.copy();

    // Эвакуация
    evacRoutesList.clear();
    std::vector<QPointF> g1Starts, g1Ends, g2Starts, g2Ends;
    int amountOfEvacuationPoints = 0;

    // 1. создаём карту города, скрываем маркеры, инициализируем узлы,
    // заполняем эвакуационные маршруты
    for (int y = 0; y < mapHeight; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        QRgb *bgLine = reinterpret_cast<QRgb *>(backgroundImage.scanLine(y));

        for (int x = 0; x < mapWidth; ++x) {
            uint32_t pixelColor = line[x] & 0x00FFFFFF;

            if (pixelColor == level.COLOR_CITY) {
                cityMap[y * mapWidth + x] = 1;
            } else if (pixelColor == level.COLOR_NODE || pixelColor == level.COLOR_EVAC) {
                cityMap[y * mapWidth + x] = 1;
                // затираем цветом города
                QRgb neighborColor = (x > 0) ? image.pixel(x - 1, y) : level.COLOR_CITY;
                if ((neighborColor & 0x00FFFFFF) == level.COLOR_NODE
                    || (neighborColor & 0x00FFFFFF) == level.COLOR_EVAC) {
                    neighborColor = level.COLOR_CITY;
                }
                bgLine[x] = neighborColor;

                // инициализируем узлы
                RoadNode node;
                node.id = static_cast<int>(nodesList.size());
                node.pos = QPointF(x + 0.5, y + 0.5);
                node.isEvacExit = (pixelColor == level.COLOR_EVAC);
                if (node.isEvacExit) {
                    if (amountOfEvacuationPoints < level.evacNames.size()) {
                        node.evacName = level.evacNames[amountOfEvacuationPoints];
                    } else {
                        // Если не хватило имён на эвакуационные выходы
                        node.evacName = QString("Эвакуационный выход %1")
                                            .arg(amountOfEvacuationPoints + 1);
                    }
                    amountOfEvacuationPoints++;
                }
                nodesList.push_back(node);

            } else if (pixelColor == level.COLOR_G1_START || pixelColor == level.COLOR_G1_END
                       || pixelColor == level.COLOR_G2_START || pixelColor == level.COLOR_G2_END) {
                QRgb neighborPixel = (x + 1 < mapWidth) ? image.pixel(x + 1, y)
                                                        : mapOutborderColor.rgb();
                bgLine[x] = neighborPixel; // Скрываем маркер цветом соседа
                QPointF pt(x + 0.5, y + 0.5);
                if (pixelColor == level.COLOR_G1_START)
                    g1Starts.push_back(pt);
                else if (pixelColor == level.COLOR_G1_END)
                    g1Ends.push_back(pt);
                else if (pixelColor == level.COLOR_G2_START)
                    g2Starts.push_back(pt);
                else if (pixelColor == level.COLOR_G2_END)
                    g2Ends.push_back(pt);
            }
        }
    }

    if (nodesList.size() < 2) {
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
                evacRoutesList.push_back({group, s, bestEnd});
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
    candidates.reserve((nodesList.size() * (nodesList.size() - 1)) / 2);

    for (size_t i = 0; i < nodesList.size(); ++i) {
        for (size_t j = i + 1; j < nodesList.size(); ++j) {
            float dx = static_cast<float>(nodesList[i].pos.x() - nodesList[j].pos.x());
            float dy = static_cast<float>(nodesList[i].pos.y() - nodesList[j].pos.y());
            candidates.push_back({static_cast<int>(i), static_cast<int>(j), std::hypot(dx, dy)});
        }
    }

    // Вспомогательная функция расчёта приоритета (меньше = выше приоритет)
    auto calcEdgeCost = [&](const CandidateEdge &edge) -> float {
        if ((level.preferStraightRoads / 100.0f) <= 0.001f || edge.dist <= 0.001f)
            return edge.dist;

        float dx = std::abs(
            static_cast<float>(nodesList[edge.u].pos.x() - nodesList[edge.v].pos.x()));
        float dy = std::abs(
            static_cast<float>(nodesList[edge.u].pos.y() - nodesList[edge.v].pos.y()));

        // 0.0 для углов 0°/90°/180°/270°, до ~0.707 для диагонали 45°
        float diagFactor = std::min(dx, dy) / edge.dist;

        // Штраф увеличивает "виртуальную" длину диагональных дорог
        float penalty = edge.dist * diagFactor * ((level.preferStraightRoads / 100.0f) * 2.5f);
        return edge.dist + penalty;
    };

    std::sort(candidates.begin(),
              candidates.end(),
              [&](const CandidateEdge &a, const CandidateEdge &b) {
                  return calcEdgeCost(a) < calcEdgeCost(b);
              });

    // Считаем количество дорог на перекрёстках
    auto *rng = QRandomGenerator::global();
    std::vector<int> targetDegree(nodesList.size());
    for (size_t i = 0; i < nodesList.size(); ++i) {
        int roll = rng->bounded(100);
        char chance1 = level.chance1;
        char chance2 = level.chance2 + chance1;
        char chance3 = level.chance3 + chance2;
        char chance4 = level.chance4 + chance3;
        char chance5 = level.chance5 + chance4;
        if (roll < chance1)
            targetDegree[i] = 1;
        else if (roll < chance2)
            targetDegree[i] = 2;
        else if (roll < chance3)
            targetDegree[i] = 3;
        else if (roll < chance4)
            targetDegree[i] = 4;
        else if (roll < chance5)
            targetDegree[i] = 5;
        else {
            targetDegree[i] = 0;
        }
    }

    std::vector<std::vector<float>> nodeAngles(nodesList.size());
    std::vector<bool> edgeUsed(candidates.size(), false);

    // 1. Геометрическая проверка пересечения двух отрезков AB и CD (без учёта общих концов)
    auto segmentsIntersect = [](QPointF a, QPointF b, QPointF c, QPointF d) -> bool {
        auto crossProduct = [](QPointF p1, QPointF p2, QPointF p3) -> double {
            return (p2.x() - p1.x()) * (p3.y() - p1.y()) - (p2.y() - p1.y()) * (p3.x() - p1.x());
        };

        double d1 = crossProduct(c, d, a);
        double d2 = crossProduct(c, d, b);
        double d3 = crossProduct(a, b, c);
        double d4 = crossProduct(a, b, d);

        // Строгое пересечение: концы каждого отрезка лежат по разные стороны от другого
        if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0))
            && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0))) {
            return true;
        }
        return false;
    };

    // 2. Проверка, не пересекает ли ребро (u, v) уже построенные дороги из edgesList
    auto isCrossingExistingRoads = [&](int u, int v) -> bool {
        QPointF a = nodesList[u].pos;
        QPointF b = nodesList[v].pos;

        for (const auto &edge : edgesList) {
            // Если у дорог есть общий перекрёсток — это нормальное примыкание, а не пересечение
            if (edge.fromId == u || edge.fromId == v || edge.toId == u || edge.toId == v) {
                continue;
            }
            QPointF c = nodesList[edge.fromId].pos;
            QPointF d = nodesList[edge.toId].pos;

            if (segmentsIntersect(a, b, c, d)) {
                return true;
            }
        }
        return false;
    };

    auto addRoadEdge = [&](size_t candIdx) {
        const auto &e = candidates[candIdx];

        targetDegree[e.u]--;
        targetDegree[e.v]--;

        edgesList.push_back({e.u, e.v});
        edgeUsed[candIdx] = true;

        float angleUV = std::atan2(static_cast<float>(nodesList[e.v].pos.y()
                                                      - nodesList[e.u].pos.y()),
                                   static_cast<float>(nodesList[e.v].pos.x()
                                                      - nodesList[e.u].pos.x()));
        float angleVU = std::atan2(static_cast<float>(nodesList[e.u].pos.y()
                                                      - nodesList[e.v].pos.y()),
                                   static_cast<float>(nodesList[e.u].pos.x()
                                                      - nodesList[e.v].pos.x()));

        nodeAngles[e.u].push_back(angleUV);
        nodeAngles[e.v].push_back(angleVU);
    };

    // Шаг А: Минимальное остовное дерево (MST)
    if (level.shouldUnionRoads) {
        std::vector<int> parent(nodesList.size());
        std::iota(parent.begin(), parent.end(), 0);
        auto findRoot = [&](int v) {
            while (v != parent[v]) {
                parent[v] = parent[parent[v]];
                v = parent[v];
            }
            return v;
        };
        for (size_t i = 0; i < candidates.size(); ++i) {
            int u = candidates[i].u;
            int v = candidates[i].v;
            int ru = findRoot(u);
            int rv = findRoot(v);

            // Добавляем проверку на отсутствие пересечений
            if (ru != rv && !isCrossingExistingRoads(u, v)) {
                parent[ru] = rv;
                addRoadEdge(i);
            }
        }
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

        // Проверяем, что у узлов есть свободные слоты (> 0)
        if (targetDegree[u] > 0 && targetDegree[v] > 0) {
            float angleUV = std::atan2(static_cast<float>(nodesList[v].pos.y()
                                                          - nodesList[u].pos.y()),
                                       static_cast<float>(nodesList[v].pos.x()
                                                          - nodesList[u].pos.x()));
            float angleVU = std::atan2(static_cast<float>(nodesList[u].pos.y()
                                                          - nodesList[v].pos.y()),
                                       static_cast<float>(nodesList[u].pos.x()
                                                          - nodesList[v].pos.x()));

            // Добавлена проверка !isCrossingExistingRoads(u, v)
            if (isAngleValid(u, angleUV) && isAngleValid(v, angleVU)
                && !isCrossingExistingRoads(u, v)) {
                addRoadEdge(i);
            }
        }
    }

    // Диагностика связей перед отрисовкой
    for (size_t i = 0; i < nodesList.size(); ++i) {
        if (nodeAngles[i].empty()) {
            qDebug() << "ОШИБКА СВЯЗНОСТИ: Точка id =" << nodesList[i].id << "в координатах"
                     << nodesList[i].pos << "не получила ни одной связи!";
        }
    }
    rasterizeRoads(level.roadWidthMeters);
    generateBuildings();
    buildBuildingInteriorsAndPaths();
    return true;
}

void MapGenerator::rasterizeRoads(float roadmapWidth)
{
    QPainter bgPainter(&backgroundImage);
    bgPainter.setRenderHint(QPainter::Antialiasing, false);
    QPen roadPen(QColor(24, 27, 33), roadmapWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    bgPainter.setPen(roadPen);

    QImage walkMask(mapWidth, mapHeight, QImage::Format_Grayscale8);
    walkMask.fill(0);
    QPainter walkPainter(&walkMask);
    walkPainter.setRenderHint(QPainter::Antialiasing, false);
    QPen maskPen(Qt::white, roadmapWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    walkPainter.setPen(maskPen);

    for (const auto &edge : edgesList) {
        QPointF p1 = nodesList[edge.fromId].pos;
        QPointF p2 = nodesList[edge.toId].pos;
        bgPainter.drawLine(p1, p2);
        walkPainter.drawLine(p1, p2);
    }

    bgPainter.end();
    walkPainter.end();

    // Проходимо всё, где нарисована дорога (даже если она прошла через сквер)
    for (int y = 0; y < mapHeight; ++y) {
        const uint8_t *line = walkMask.constScanLine(y);
        for (int x = 0; x < mapWidth; ++x) {
            if (line[x] > 128) {
                walkMap[y * mapWidth + x] = 1;
            }
        }
    }
}

bool MapGenerator::tryPlaceBuilding(
    int x, int y, float bw, float bh, BuildingType type, std::vector<uint8_t> &occupied)
{
    if (occupied[y * mapWidth + x] != 0)
        return false;

    // Выравниваем здание по направлению ближайшей дороги
    float angle = 0.0f;
    float bestDist = 1e9f;
    for (const auto &edge : edgesList) {
        QPointF p1 = nodesList[edge.fromId].pos;
        QPointF p2 = nodesList[edge.toId].pos;
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
            if (px < 0 || px >= mapWidth || py < 0 || py >= mapHeight
                || occupied[py * mapWidth + px] != 0) {
                return false;
            }
        }
    }

    // Помечаем территорию под зданием как занятую, а корпус здания как непросматриваемый (cityMap = 2)
    float solidHw = bw * 0.5f;
    float solidHh = bh * 0.5f;
    for (float ly = -hh; ly <= hh; ly += 1.0f) {
        for (float lx = -hw; lx <= hw; lx += 1.0f) {
            int px = static_cast<int>(x + lx * cosA - ly * sinA);
            int py = static_cast<int>(y + lx * sinA + ly * cosA);
            if (px >= 0 && px < mapWidth && py >= 0 && py < mapHeight) {
                occupied[py * mapWidth + px] = 1;
                if (std::abs(lx) <= solidHw && std::abs(ly) <= solidHh) {
                    cityMap[py * mapWidth + px] = 2;
                }
            }
        }
    }

    auto *rng = QRandomGenerator::global();
    BuildingObject b;
    b.id = static_cast<int>(buildingsList.size());
    b.type = type;
    b.cx = static_cast<float>(x);
    b.cy = static_cast<float>(y);
    b.width = bw;
    b.height = bh;
    b.angleRad = angle;
    b.area = bw * bh;
    b.maxHealth = std::max(20, static_cast<int>(b.area / 5.0f));
    b.health = b.maxHealth;
    // Вместимость зависит от площади и типа здания
    if (type == BuildingType::Factory) {
        b.capacity = static_cast<int>(b.area
                                      / level.factoryCapacity); // Много рабочих мест на заводе
    } else if (type == BuildingType::Office) {
        b.capacity = static_cast<int>(b.area / level.officeCapacity); // Плотная офисная застройка
    } else {
        b.capacity = static_cast<int>(b.area / level.houseCapacity); // Жилой
    }

    // Выбираем случайный цвет крыши из палитры уровня
    if (!level.roofColors.empty()) {
        uint32_t hexColor = level
                                .roofColors[rng->bounded(static_cast<int>(level.roofColors.size()))];
        b.r = (hexColor >> 16) & 0xFF;
        b.g = (hexColor >> 8) & 0xFF;
        b.b = hexColor & 0xFF;
    } else {
        uint8_t shade = static_cast<uint8_t>(85 + rng->bounded(35));
        b.r = shade;
        b.g = static_cast<uint8_t>(shade + 4);
        b.b = static_cast<uint8_t>(shade + 10);
    }

    buildingsList.push_back(b);
    return true;
}

void MapGenerator::generateBuildings()
{
    auto *rng = QRandomGenerator::global();
    std::vector<uint8_t> occupied(mapWidth * mapHeight, 1);

    int freePixels = 0;
    for (int i = 0; i < mapWidth * mapHeight; ++i) {
        if (cityMap[i] == 1 && walkMap[i] == 0) {
            occupied[i] = 0;
            freePixels++;
        }
    }

    int totalPercent = std::clamp(static_cast<int>(level.factoriesPercent + level.officesPercent
                                                   + level.housesPercent),
                                  0,
                                  100);
    if (totalPercent <= 0 || freePixels == 0)
        return;

    // 1. Вычисляем среднюю площадь (с учётом рамок +6px) для каждого типа зданий
    auto calcAvgFootprint = [](const std::vector<unsigned short int> &s) -> float {
        float avgW = (s[2] + s[3]) * 0.5F + 6.0F;
        float avgH = (s[4] + s[5]) * 0.5F + 6.0F;
        return avgW * avgH;
    };

    float areaF = calcAvgFootprint(level.factory);
    float areaO = calcAvgFootprint(level.office);
    float areaH = calcAvgFootprint(level.house);

    float pF = level.factoriesPercent / 100.0F;
    float pO = level.officesPercent / 100.0F;
    float pH = level.housesPercent / 100.0F;
    float pEmpty = (100 - totalPercent) / 100.0F;

    // Коэффициент упаковки (~0.62 учитывает потери на повороты зданий и узкие углы у дорог)
    const float PACKING_EFFICIENCY = 0.62F;
    float usableArea = freePixels * PACKING_EFFICIENCY;

    // Средняя площадь одного слота (включая пустые слоты, которые забирают место как средний дом)
    float avgSlotArea = (pF * areaF) + (pO * areaO) + ((pH + pEmpty) * areaH);
    int estimatedTotalSlots = static_cast<int>(usableArea / std::max(1.0F, avgSlotArea));

    // Целевое количество зданий каждого типа
    int targetFactories = static_cast<int>(std::round(estimatedTotalSlots * pF));
    int targetOffices = static_cast<int>(std::round(estimatedTotalSlots * pO));
    int targetHouses = static_cast<int>(std::round(estimatedTotalSlots * pH));

    // Вспомогательная лямбда спавна с проверкой отступов
    auto trySpawnType = [&](BuildingType type, int x, int y) -> bool {
        const auto &s = (type == BuildingType::Factory)
                            ? level.factory
                            : ((type == BuildingType::Office) ? level.office : level.house);

        int margin = s[0];
        if (x < margin || x >= mapWidth - margin || y < margin || y >= mapHeight - margin)
            return false;

        float bw = static_cast<float>(s[2] + rng->bounded(s[3] - s[2] + 1));
        float bh = static_cast<float>(s[4] + rng->bounded(s[5] - s[4] + 1));

        return tryPlaceBuilding(x, y, bw, bh, type, occupied);
    };

    // Генерируем перемешанную сетку точек-кандидатов с плотным шагом
    int step = std::max<int>(6, level.house[1] / 2);
    std::vector<QPoint> spawnPoints;
    for (int y = 6; y < mapHeight - 6; y += step) {
        for (int x = 6; x < mapWidth - 6; x += step) {
            if (occupied[y * mapWidth + x] == 0) {
                spawnPoints.emplace_back(x, y);
            }
        }
    }
    std::shuffle(spawnPoints.begin(), spawnPoints.end(), *rng);

    // ЭТАП 1: Сначала ставим самые крупные здания — ФАБРИКИ
    int factoriesCount = 0;
    for (const QPoint &pt : spawnPoints) {
        if (factoriesCount >= targetFactories)
            break;
        if (occupied[pt.y() * mapWidth + pt.x()] == 0) {
            if (trySpawnType(BuildingType::Factory, pt.x(), pt.y())) {
                factoriesCount++;
            }
        }
    }

    // ЭТАП 2: Затем ставим средние здания — ОФИСЫ
    int officesCount = 0;
    for (const QPoint &pt : spawnPoints) {
        if (officesCount >= targetOffices)
            break;
        if (occupied[pt.y() * mapWidth + pt.x()] == 0) {
            if (trySpawnType(BuildingType::Office, pt.x(), pt.y())) {
                officesCount++;
            }
        }
    }

    // ЭТАП 3: В последнюю очередь заполняем оставшиеся места — ЖИЛЫМИ ДОМАМИ
    // Лимит targetHouses автоматически оставит заданный процент (100 - totalPercent) пустым
    int housesCount = 0;
    for (const QPoint &pt : spawnPoints) {
        if (housesCount >= targetHouses)
            break;
        if (occupied[pt.y() * mapWidth + pt.x()] == 0) {
            if (trySpawnType(BuildingType::Residential, pt.x(), pt.y())) {
                housesCount++;
            }
        }
    }

    // Гарантируем строгое совпадение ID с индексом в массиве
    for (size_t i = 0; i < buildingsList.size(); ++i) {
        buildingsList[i].id = static_cast<int>(i);
    }

    int finalTotal = factoriesCount + officesCount + housesCount;
    qDebug() << "Итого построено:" << factoriesCount << "Фабрик |" << officesCount << "Офисов |"
             << housesCount << "Домов | Всего:" << finalTotal;
}

unsigned int MapGenerator::generateBuilding(BuildingType type,
                                            std::vector<unsigned short int> settings,
                                            std::vector<uint8_t> &occupied)
{
    auto *rng = QRandomGenerator::global();
    unsigned short int marginOfCityMap = settings[0]; // Отступ от края карты
    unsigned short int marginOfSpawnBuildings
        = settings[1]; // Расстояние между потенциальными точками спавна домов.
    unsigned short int widthMin = settings[2];
    unsigned short int widthMax = settings[3];
    unsigned short int lengthMin = settings[4];
    unsigned short int lengthMax = settings[5];

    unsigned int buildingCounter = 0;
    for (int y = marginOfCityMap; y < mapHeight - marginOfCityMap; y += marginOfSpawnBuildings) {
        for (int x = marginOfCityMap; x < mapWidth - marginOfCityMap; x += marginOfSpawnBuildings) {
            float buildingWidth = static_cast<float>(widthMin
                                                     + rng->bounded(widthMax - widthMin + 1));
            float buildingLength = static_cast<float>(lengthMin
                                                      + rng->bounded(lengthMax - lengthMin + 1));

            if (tryPlaceBuilding(x, y, buildingWidth, buildingLength, type, occupied))
                ++buildingCounter;
        }
    }
    return buildingCounter;
}

void MapGenerator::removeBuilding(BuildingType type,
                                  int amountToRemove,
                                  std::vector<uint8_t> &occupied)
{
    for (int i = 0; i < amountToRemove; ++i) {
        // Находим нужное здание
        int randId = 0;
        int cycleCounter = 0;
        do {
            ++cycleCounter;
            randId = QRandomGenerator::global()->bounded(buildingsList.size());
        } while (cycleCounter < 100000 && buildingsList[randId].type != type);

        if (buildingsList[randId].type != type)
            return;

        // Загружаем настройки для очистки
        float angle = buildingsList[randId].angleRad;
        float cosA = std::cos(angle);
        float sinA = std::sin(angle);
        float hw = buildingsList[randId].width * 0.5F + 3.0F;
        float hh = buildingsList[randId].height * 0.5F + 3.0F;

        // Помечаем территорию под зданием как занятую, а корпус здания как непросматриваемый (cityMap = 2)
        float solidHw = buildingsList[i].width * 0.5f;
        float solidHh = buildingsList[i].height * 0.5f;

        // Очищаем занятую территорию под зданием
        for (float ly = -hh; ly <= hh; ly += 1.0F) {
            for (float lx = -hw; lx <= hw; lx += 1.0F) {
                int px = static_cast<int>(buildingsList[randId].cx + lx * cosA - ly * sinA);
                int py = static_cast<int>(buildingsList[randId].cy + lx * sinA + ly * cosA);
                if (px >= 0 && px < mapWidth && py >= 0 && py < mapHeight) {
                    occupied[py * mapWidth + px] = 0;
                    cityMap[py * mapWidth + px] = 1;
                    if (std::abs(lx) <= solidHw && std::abs(ly) <= solidHh) {
                        cityMap[py * mapWidth + px] = 1;
                    }
                }
            }
        }

        // Удаляем здание
        buildingsList.erase(buildingsList.begin() + randId);
    }
}

void MapGenerator::buildBuildingInteriorsAndPaths()
{
    if (buildingsList.empty())
        return;

    // 1. Заполняем пол каждого здания в walkMap (10 + b.id) и задаём цвет пола
    for (auto &b : buildingsList) {
        if (b.type == BuildingType::Factory)
            b.floorColor = level.COLOR_FLOOR_FACTORY;
        else if (b.type == BuildingType::Office)
            b.floorColor = level.COLOR_FLOOR_OFFICE;
        else
            b.floorColor = level.COLOR_FLOOR_HOUSE;

        float cosA = std::cos(b.angleRad);
        float sinA = std::sin(b.angleRad);
        float innerHw = std::max(1.0F, b.width * 0.5F - 1.5F);
        float innerHh = std::max(1.0F, b.height * 0.5F - 1.5F);

        for (float ly = -innerHh; ly <= innerHh; ly += 0.5F) {
            for (float lx = -innerHw; lx <= innerHw; lx += 0.5F) {
                int px = static_cast<int>(b.cx + lx * cosA - ly * sinA);
                int py = static_cast<int>(b.cy + lx * sinA + ly * cosA);
                if (px >= 0 && px < mapWidth && py >= 0 && py < mapHeight) {
                    walkMap[py * mapWidth + px] = static_cast<int16_t>(10 + b.id);
                }
            }
        }
    }

    // Маска корпусов зданий (чтобы тропинки огибали дома, а не резали их насквозь)
    std::vector<uint8_t> buildingSolidMask(mapWidth * mapHeight, 0);
    for (const auto &b : buildingsList) {
        float cosA = std::cos(b.angleRad);
        float sinA = std::sin(b.angleRad);
        float hw = b.width * 0.5F + 1.0F;
        float hh = b.height * 0.5F + 1.0F;

        for (float ly = -hh; ly <= hh; ly += 0.5F) {
            for (float lx = -hw; lx <= hw; lx += 0.5F) {
                int px = static_cast<int>(b.cx + lx * cosA - ly * sinA);
                int py = static_cast<int>(b.cy + lx * sinA + ly * cosA);
                if (px >= 0 && px < mapWidth && py >= 0 && py < mapHeight) {
                    buildingSolidMask[py * mapWidth + px] = 1;
                }
            }
        }
    }

    // Сортируем от ближних к дорогам зданий к дальним
    auto distToNearestEdge = [&](const BuildingObject &b) -> double {
        double best = 1e9;
        for (const auto &edge : edgesList) {
            QPointF p1 = nodesList[edge.fromId].pos;
            QPointF p2 = nodesList[edge.toId].pos;
            QPointF mid = (p1 + p2) * 0.5;
            best = std::min(best, std::hypot(mid.x() - b.cx, mid.y() - b.cy));
        }
        return best;
    };

    std::vector<int> buildOrder(buildingsList.size());
    std::iota(buildOrder.begin(), buildOrder.end(), 0);
    std::sort(buildOrder.begin(), buildOrder.end(), [&](int a, int b) {
        return distToNearestEdge(buildingsList[a]) < distToNearestEdge(buildingsList[b]);
    });

    QPainter bgPainter(&backgroundImage);
    bgPainter.setRenderHint(QPainter::Antialiasing, false);
    QPen pathPen(QColor(24, 27, 33),
                 level.roadWidthMetersToBuilding,
                 Qt::SolidLine,
                 Qt::RoundCap,
                 Qt::RoundJoin);
    bgPainter.setPen(pathPen);

    auto isClearGroundLine = [&](QPointF p1, QPointF p2) -> bool {
        int x1 = static_cast<int>(p1.x()), y1 = static_cast<int>(p1.y());
        int x2 = static_cast<int>(p2.x()), y2 = static_cast<int>(p2.y());
        int dx = std::abs(x2 - x1), dy = std::abs(y2 - y1);
        int sx = (x1 < x2) ? 1 : -1, sy = (y1 < y2) ? 1 : -1;
        int err = dx - dy;

        while (true) {
            if (x1 < 0 || x1 >= mapWidth || y1 < 0 || y1 >= mapHeight)
                return false;
            int idx = y1 * mapWidth + x1;
            if (cityMap[idx] == 0 || buildingSolidMask[idx] == 1)
                return false;
            if (x1 == x2 && y1 == y2)
                break;
            int e2 = 2 * err;
            if (e2 > -dy) {
                err -= dy;
                x1 += sx;
            }
            if (e2 < dx) {
                err += dx;
                y1 += sy;
            }
        }
        return true;
    };

    auto rasterizeSinglePathToWalkMap = [&](const std::vector<QPointF> &pts) {
        float halfW = level.roadWidthMetersToBuilding * 0.5F;
        int r = std::max(1, static_cast<int>(std::ceil(halfW)));

        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            QPointF p1 = pts[i];
            QPointF p2 = pts[i + 1];
            float segLen = static_cast<float>(std::hypot(p2.x() - p1.x(), p2.y() - p1.y()));
            int steps = std::max(1, static_cast<int>(std::ceil(segLen * 2.0F)));

            for (int s = 0; s <= steps; ++s) {
                float t = static_cast<float>(s) / steps;
                int cx = static_cast<int>(p1.x() + (p2.x() - p1.x()) * t);
                int cy = static_cast<int>(p1.y() + (p2.y() - p1.y()) * t);

                for (int dy = -r; dy <= r; ++dy) {
                    for (int dx = -r; dx <= r; ++dx) {
                        if (dx * dx + dy * dy > r * r)
                            continue;
                        int px = cx + dx;
                        int py = cy + dy;
                        if (px >= 0 && px < mapWidth && py >= 0 && py < mapHeight) {
                            int idx = py * mapWidth + px;
                            if (cityMap[idx] == 1 && walkMap[idx] == 0
                                && buildingSolidMask[idx] == 0) {
                                walkMap[idx] = 2;
                            }
                        }
                    }
                }
            }
        }
    };

    std::vector<int> visited(mapWidth * mapHeight, -1);
    std::vector<int> parent(mapWidth * mapHeight, -1);
    int searchRunId = 0;

    // 2. Перебираем 4 стороны каждого здания и строим тропинку от лучшей стороны
    for (int bIdx : buildOrder) {
        auto &b = buildingsList[bIdx];

        float cosA = std::cos(b.angleRad);
        float sinA = std::sin(b.angleRad);
        float hw = b.width * 0.5F;
        float hh = b.height * 0.5F;

        struct DoorCandidate
        {
            float localDoorX, localDoorY;
            float localOutX, localOutY;
            float sideAngleOffset;
        };

        DoorCandidate sides[4] = {{hw, 0.0F, hw + 2.5F, 0.0F, 0.0F},
                                  {-hw, 0.0F, -hw - 2.5F, 0.0F, 0.0F},
                                  {0.0F, hh, 0.0F, hh + 2.5F, 1.5707963F},
                                  {0.0F, -hh, 0.0F, -hh - 2.5F, 1.5707963F}};

        int bestSide = -1;
        std::vector<QPointF> bestSmoothPath;
        float bestPathLength = 1e9F;

        const int dxs[8] = {1, -1, 0, 0, 1, 1, -1, -1};
        const int dys[8] = {0, 0, 1, -1, 1, -1, 1, -1};

        for (int s = 0; s < 4; ++s) {
            QPointF doorPt(b.cx + sides[s].localDoorX * cosA - sides[s].localDoorY * sinA,
                           b.cy + sides[s].localDoorX * sinA + sides[s].localDoorY * cosA);
            QPointF outPt(b.cx + sides[s].localOutX * cosA - sides[s].localOutY * sinA,
                          b.cy + sides[s].localOutX * sinA + sides[s].localOutY * cosA);

            int ox = static_cast<int>(outPt.x());
            int oy = static_cast<int>(outPt.y());
            if (ox < 0 || ox >= mapWidth || oy < 0 || oy >= mapHeight)
                continue;

            int oIdx = oy * mapWidth + ox;
            if (cityMap[oIdx] == 0 || buildingSolidMask[oIdx] == 1)
                continue;

            if (walkMap[oIdx] == 1 || walkMap[oIdx] == 2) {
                float len = static_cast<float>(
                    std::hypot(outPt.x() - doorPt.x(), outPt.y() - doorPt.y()));
                if (len < bestPathLength) {
                    bestPathLength = len;
                    bestSide = s;
                    bestSmoothPath = {outPt, doorPt};
                }
                continue;
            }

            searchRunId++;
            std::queue<int> q;
            q.push(oIdx);
            visited[oIdx] = searchRunId;
            parent[oIdx] = -1;

            int foundCell = -1;
            while (!q.empty()) {
                int curr = q.front();
                q.pop();

                if (walkMap[curr] == 1 || walkMap[curr] == 2) {
                    foundCell = curr;
                    break;
                }

                int cx = curr % mapWidth;
                int cy = curr / mapWidth;

                for (int dir = 0; dir < 8; ++dir) {
                    int nx = cx + dxs[dir];
                    int ny = cy + dys[dir];
                    if (nx < 0 || nx >= mapWidth || ny < 0 || ny >= mapHeight)
                        continue;

                    int nCell = ny * mapWidth + nx;
                    if (visited[nCell] == searchRunId)
                        continue;

                    if (cityMap[nCell] == 1 && buildingSolidMask[nCell] == 0) {
                        visited[nCell] = searchRunId;
                        parent[nCell] = curr;
                        q.push(nCell);
                    }
                }
            }

            if (foundCell != -1) {
                std::vector<QPointF> rawPath;
                for (int c = foundCell; c != -1; c = parent[c]) {
                    rawPath.emplace_back((c % mapWidth) + 0.5, (c / mapWidth) + 0.5);
                }
                rawPath.push_back(outPt);
                rawPath.push_back(doorPt);

                std::vector<QPointF> smoothPath;
                smoothPath.push_back(rawPath.front());
                size_t anchor = 0;

                while (anchor + 1 < rawPath.size()) {
                    size_t furthest = anchor + 1;
                    for (size_t test = rawPath.size() - 1; test > anchor + 1; --test) {
                        if (isClearGroundLine(rawPath[anchor], rawPath[test])) {
                            furthest = test;
                            break;
                        }
                    }
                    smoothPath.push_back(rawPath[furthest]);
                    anchor = furthest;
                }
                float totalLen = 0.0F;
                for (size_t i = 0; i + 1 < smoothPath.size(); ++i) {
                    totalLen += static_cast<float>(
                        std::hypot(smoothPath[i + 1].x() - smoothPath[i].x(),
                                   smoothPath[i + 1].y() - smoothPath[i].y()));
                }
                if (totalLen < bestPathLength) {
                    bestPathLength = totalLen;
                    bestSide = s;
                    bestSmoothPath = std::move(smoothPath);
                }
            }
        }
        if (bestSide != -1 && bestSmoothPath.size() >= 2) {
            b.doorCenter = bestSmoothPath.back();
            b.doorAngleRad = b.angleRad + sides[bestSide].sideAngleOffset;
            b.pathFromRoad = bestSmoothPath;
            // Пробиваем дверной проём в стене шириной в тропинку и помечаем его как walkMap = 2
            QPointF outPt = bestSmoothPath[bestSmoothPath.size() - 2];
            float ddx = static_cast<float>(outPt.x()) - b.cx;
            float ddy = static_cast<float>(outPt.y()) - b.cy;
            float dLen = std::hypot(ddx, ddy);
            // Половина ширины проёма вдоль стены
            float halfDoorW = level.roadWidthMetersToBuilding * 0.5F;
            float perpX = (dLen > 0.01F) ? (-ddy / dLen) : 0.0F;
            float perpY = (dLen > 0.01F) ? (ddx / dLen) : 0.0F;
            if (dLen > 0.01F) {
                for (float t = 0.0F; t <= dLen; t += 0.5F) {
                    float baseX = b.cx + (ddx / dLen) * t;
                    float baseY = b.cy + (ddy / dLen) * t;
                    for (float wOff = -halfDoorW; wOff <= halfDoorW; wOff += 0.5F) {
                        int px = static_cast<int>(baseX + perpX * wOff);
                        int py = static_cast<int>(baseY + perpY * wOff);
                        if (px >= 0 && px < mapWidth && py >= 0 && py < mapHeight) {
                            int idx = py * mapWidth + px;
                            // Проём в стене сразу принадлежит полу здания (10 + b.id),
                            // чтобы юнит мгновенно входил в здание при пересечении порога!
                            if (walkMap[idx] == 0) {
                                walkMap[idx] = static_cast<int16_t>(10 + b.id);
                            }
                        }
                    }
                }
            }
            rasterizeSinglePathToWalkMap(bestSmoothPath);
            for (size_t i = 0; i + 1 < bestSmoothPath.size(); ++i) {
                bgPainter.drawLine(bestSmoothPath[i], bestSmoothPath[i + 1]);
            }
        }
    }
    bgPainter.end();
}
