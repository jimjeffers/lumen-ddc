// lumen — monitor brightness, color and picture modes over DDC/CI.

#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>

#include "backend.h"
#include "cli.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    // Headless commands (state, set) run without a display, for the bar
    // plugin and scripts.
    QStringList args;
    for (int i = 0; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);
    if (Cli::handles(args)) {
        QCoreApplication app(argc, argv);
        app.setApplicationName("lumen");
        app.setApplicationVersion("0.2.0");
        return Cli::run(args);
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName("lumen");
    app.setApplicationVersion("0.2.0");

    // Matches the window to lumen.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("lumen");
    app.setWindowIcon(QIcon::fromTheme("lumen"));

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    Backend backend;

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
