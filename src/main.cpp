#include <QGuiApplication>
#include <QQmlApplicationEngine>
#ifdef Q_OS_ANDROID
#include <QQuickStyle>
#endif
#include <QSurfaceFormat>

/**
 * @brief Точка входа приложения Torchki and Zombie.
 *
 * Задачи:
 * 1. Настроить сглаживание OpenGL/RHI (MSAA 2x) до создания окна
 * 2. Инициализировать QGuiApplication и стиль Basic для Android
 * 3. Загрузить главный QML-модуль Main.qml и запустить цикл событий
 */
int main(int argc, char *argv[])
{
    // 1. Настройка формата поверхности рендера
    QSurfaceFormat format;
    format.setSamples(2);
    QSurfaceFormat::setDefaultFormat(format);

    // 2. Инициализация приложения Qt
    QGuiApplication app(argc, argv);

#ifdef Q_OS_ANDROID
    QQuickStyle::setStyle("Basic");
#endif

    // 3. Запуск QML-движка и загрузка главного окна
    QQmlApplicationEngine engine;
    const QUrl url(QStringLiteral("qrc:/qt/qml/TorchkiAndZombie/src/Main.qml"));

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.load(url);

    return app.exec();
}
