#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QSurfaceFormat>

int main(int argc, char *argv[])
{
    QSurfaceFormat format;
    format.setSamples(2); // 2x MSAA для гладких краёв юнитов и зданий
    QSurfaceFormat::setDefaultFormat(format);

    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;
    const QUrl url(QStringLiteral("qrc:/Torchki_and_Zombie/src/Main.qml"));

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.load(url);

    return app.exec();
}
