#include <QGuiApplication>
#include <QQmlApplicationEngine>
#ifdef Q_OS_ANDROID
#include <QQuickStyle>
#endif
#include <QSurfaceFormat>

int main(int argc, char *argv[])
{
    QSurfaceFormat format;
    format.setSamples(2);
    QSurfaceFormat::setDefaultFormat(format);

    QGuiApplication app(argc, argv);
	
    #ifdef Q_OS_ANDROID
		QQuickStyle::setStyle("Basic");
	#endif


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
