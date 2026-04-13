#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "ConfigManager.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("G600 Config");
    app.setOrganizationName("g600d");

    QQmlApplicationEngine engine;
    ConfigManager config;
    engine.rootContext()->setContextProperty("config", &config);
    engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));

    if (engine.rootObjects().isEmpty()) return -1;
    return app.exec();
}
