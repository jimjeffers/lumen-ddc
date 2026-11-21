#include "cli.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QThread>

#include "backend.h"

namespace Cli {

static const QStringList kCommands{"state", "set", "help", "--help", "-h", "--version"};

bool handles(const QStringList &args) {
    return args.size() > 1 && kCommands.contains(args[1]);
}

static void usage(QTextStream &out) {
    out << "Usage:\n"
           "  lumen                              open the window\n"
           "  lumen state                        print every monitor's settings as JSON\n"
           "  lumen set TARGET SETTING VALUE     change a setting\n\n"
           "TARGET   all | <serial> | <connector, e.g. DP-2>\n"
           "SETTING  brightness | contrast | volume | temp | mode | halo | 0xNN\n"
           "VALUE    a number (0x.. accepted), or a name from ~/.config/lumen/<model>.conf\n";
}

// Runs the event loop until every monitor has finished its queued work.
static bool settle(Backend &backend, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        bool done = !backend.monitorObjects().isEmpty() || !backend.error().isEmpty();
        for (Monitor *m : backend.monitorObjects())
            done &= !m->busy() && (m->ready() || !m->error().isEmpty());
        if (done)
            return true;
        QThread::msleep(10);
    }
    return false;
}

static QJsonArray toJson(const QVariantList &options) {
    QJsonArray array;
    for (const QVariant &o : options) {
        const QVariantMap map = o.toMap();
        array.append(QJsonObject{{"value", map.value("value").toInt()}, {"label", map.value("label").toString()}});
    }
    return array;
}

static QJsonObject level(int value, int maximum) {
    return QJsonObject{{"value", value}, {"max", maximum}};
}

static QJsonObject monitorJson(Monitor *m, int index) {
    QJsonObject o{
        {"index", index},
        {"connector", m->connector()},
        {"serial", m->serial()},
        {"model", m->model()},
        {"ready", m->ready()},
        {"error", m->error()},
        {"brightness", level(m->brightness(), m->brightnessMax())},
        {"contrast", level(m->contrast(), m->contrastMax())},
        {"volume", level(m->volume(), m->volumeMax())},
        {"temp", QJsonObject{{"value", m->colorPreset()}, {"options", toJson(m->colorPresets())}}},
        {"mode", QJsonObject{{"value", m->mode()}, {"options", toJson(m->modes())}}},
    };
    if (m->haloAvailable())
        o.insert("halo", QJsonObject{{"code", m->haloCode()}, {"value", m->halo()}, {"options", toJson(m->haloOptions())}});
    return o;
}

static QJsonObject stateJson(Backend &backend) {
    QJsonArray monitors;
    const auto list = backend.monitorObjects();
    for (int i = 0; i < list.size(); ++i)
        monitors.append(monitorJson(list[i], i));
    QJsonObject o{{"monitors", monitors}};
    if (!backend.error().isEmpty())
        o.insert("error", backend.error());
    return o;
}

// Resolves a setting name to its VCP code for one monitor, or -1.
static int codeFor(const QString &setting, Monitor *m) {
    const QString s = setting.toLower();
    if (s == "brightness") return Ddc::Brightness;
    if (s == "contrast") return Ddc::Contrast;
    if (s == "volume") return Ddc::Volume;
    if (s == "temp" || s == "color") return Ddc::ColorPreset;
    if (s == "mode") return Ddc::DisplayMode;
    if (s == "halo") return m->haloAvailable() ? m->haloCode() : -1;
    bool ok = false;
    const int code = setting.toInt(&ok, 0);
    return ok ? code : -1;
}

// Resolves a value: a number, or a label from the monitor's option list.
static int valueFor(const QString &value, int code, Monitor *m) {
    bool ok = false;
    const int number = value.toInt(&ok, 0);
    if (ok)
        return number;
    QVariantList options;
    if (code == Ddc::DisplayMode) options = m->modes();
    else if (code == Ddc::ColorPreset) options = m->colorPresets();
    else if (code == m->haloCode()) options = m->haloOptions();
    for (const QVariant &o : options) {
        const QVariantMap map = o.toMap();
        if (map.value("label").toString().compare(value, Qt::CaseInsensitive) == 0)
            return map.value("value").toInt();
    }
    return -1;
}

int run(const QStringList &args) {
    QTextStream out(stdout), err(stderr);
    const QString command = args.value(1);

    if (command == "--version") {
        out << "lumen " << QCoreApplication::applicationVersion() << "\n";
        return 0;
    }
    if (command == "help" || command == "--help" || command == "-h") {
        usage(out);
        return 0;
    }

    Backend backend;
    if (!settle(backend, 15000)) {
        err << "lumen: monitors did not answer in time\n";
        return 1;
    }

    if (command == "state") {
        out << QJsonDocument(stateJson(backend)).toJson(QJsonDocument::Compact) << "\n";
        return backend.monitorObjects().isEmpty() ? 1 : 0;
    }

    // set TARGET SETTING VALUE
    if (args.size() < 5) {
        usage(err);
        return 1;
    }
    const QString target = args[2], setting = args[3], value = args[4];
    const auto monitors = backend.monitorObjects();
    int index = -1;
    for (int i = 0; i < monitors.size(); ++i)
        if (monitors[i]->serial() == target || monitors[i]->connector() == target)
            index = i;
    if (target != "all" && index < 0) {
        err << "lumen: no monitor matches '" << target << "'\n";
        return 1;
    }
    Monitor *reference = monitors.value(index < 0 ? 0 : index);
    if (!reference) {
        err << "lumen: no monitors found\n";
        return 1;
    }
    const int code = codeFor(setting, reference);
    const int v = code < 0 ? -1 : valueFor(value, code, reference);
    if (code < 0 || v < 0) {
        err << "lumen: unknown setting or value '" << setting << " " << value << "'\n";
        return 1;
    }
    if (code == 0x04 || code == 0x08) {
        err << "lumen: refusing factory/color reset\n";
        return 1;
    }

    backend.setLinked(target == "all");
    backend.setValue(index < 0 ? 0 : index, code, v);
    settle(backend, 15000);
    // Same safeguard as the window: bring back an output Hyprland switched off.
    QThread::msleep(800);
    backend.checkOutputs();

    out << QJsonDocument(stateJson(backend)).toJson(QJsonDocument::Compact) << "\n";
    for (Monitor *m : backend.monitorObjects())
        if (!m->error().isEmpty()) {
            err << "lumen: " << m->connector() << ": " << m->error() << "\n";
            return 1;
        }
    return 0;
}

}  // namespace Cli
