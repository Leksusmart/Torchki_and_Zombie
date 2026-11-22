#pragma once
#include <QDebug>
#include <QElapsedTimer>
#include <QString>

/**
 * @brief Профайлер шага симуляции для замера производительности подсистем.
 *
 * Вынесен из GameWorld::tick() и peopleUpdateActions(), чтобы не загромождать
 * игровую логику статическими счётчиками времени.
 */
class SimProfiler
{
public:
    static constexpr int REPORT_INTERVAL_TICKS = 120;

    /// Запускает общий таймер тика.
    void beginTick()
    {
        totalTimer_.start();
        stepTimer_.start();
    }

    /// Фиксирует время этапа подготовки зон и таймеров.
    void markZonesAndTimers()
    {
        zonesNs_ += stepTimer_.nsecsElapsed();
        stepTimer_.restart();
    }

    /// Фиксирует время этапа ИИ и движения юнитов.
    void markPeopleActions()
    {
        peopleNs_ += stepTimer_.nsecsElapsed();
        stepTimer_.restart();
    }

    /// Фиксирует время подсчёта занятости и удаления погибших.
    void markPostUnits()
    {
        postUnitsNs_ += stepTimer_.nsecsElapsed();
        stepTimer_.restart();
    }

    /// Фиксирует время обновления эффектов, вертолётов, эвакуации и стен.
    void markSystems()
    {
        systemsNs_ += stepTimer_.nsecsElapsed();
        stepTimer_.restart();
    }

    /// Завершает тик и раз в REPORT_INTERVAL_TICKS выводит сводку в qDebug().
    void endTick(size_t unitCount, int speedMultiplier)
    {
        renderNs_ += stepTimer_.nsecsElapsed();
        totalNs_ += totalTimer_.nsecsElapsed();
        ticks_++;

        if (ticks_ < REPORT_INTERVAL_TICKS)
            return;

        auto toAvgMs = [&](qint64 ns) -> double {
            return (static_cast<double>(ns) / ticks_) / 1'000'000.0;
        };

        qDebug().noquote()
            << QString("=== TICK PROFILE (avg over %1 ticks | units: %2 | speed: %3x) ===")
                   .arg(ticks_)
                   .arg(unitCount)
                   .arg(speedMultiplier)
            << QString("\n  Total tick:         %1 ms (budget: 16.67 ms)")
                   .arg(toAvgMs(totalNs_), 0, 'f', 3)
            << QString("\n  1. Zones & Timers:  %1 ms").arg(toAvgMs(zonesNs_), 0, 'f', 3)
            << QString("\n  2. peopleActions:   %1 ms").arg(toAvgMs(peopleNs_), 0, 'f', 3)
            << QString("\n  3. Post & Counts:   %1 ms").arg(toAvgMs(postUnitsNs_), 0, 'f', 3)
            << QString("\n  4. Subsystems:      %1 ms").arg(toAvgMs(systemsNs_), 0, 'f', 3)
            << QString("\n  5. Render Rebuild:  %1 ms").arg(toAvgMs(renderNs_), 0, 'f', 3);

        ticks_ = 0;
        totalNs_ = 0;
        zonesNs_ = 0;
        peopleNs_ = 0;
        postUnitsNs_ = 0;
        systemsNs_ = 0;
        renderNs_ = 0;
    }

private:
    QElapsedTimer totalTimer_;
    QElapsedTimer stepTimer_;
    int ticks_ = 0;
    qint64 totalNs_ = 0;
    qint64 zonesNs_ = 0;
    qint64 peopleNs_ = 0;
    qint64 postUnitsNs_ = 0;
    qint64 systemsNs_ = 0;
    qint64 renderNs_ = 0;
};
