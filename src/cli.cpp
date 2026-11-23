#include "cli.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>

#include "backend.h"

namespace Cli {

static const QStringList kCommands{"state", "set", "scan", "profile", "help", "--help", "-h", "--version"};

bool handles(const QStringList &args) {
    return args.size() > 1 && kCommands.contains(args[1]);
}

static void usage(QTextStream &out) {
    out << "Usage:\n"
           "  lumen-ddc                          open the window\n"
           "  lumen-ddc state                    print every monitor's settings as JSON\n"
           "  lumen-ddc set TARGET CONTROL VALUE  change a setting\n"
           "  lumen-ddc scan MONITOR             print every VCP code the monitor answers\n"
           "  lumen-ddc profile MONITOR          create or find the monitor's profile file\n\n"
           "TARGET   all | MONITOR\n"
           "MONITOR  <serial> | <connector, e.g. DP-2>\n"
           "CONTROL  a control id from `lumen-ddc state` (brightness, mode, …) or a VCP code (0xNN)\n"
           "VALUE    a number (0x.. accepted), an option label, or on/off for toggles\n\n"
           "Adding a monitor: " << Backend::contributeUrl() << "\n";
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

static QJsonObject controlJson(const QVariantMap &c) {
    QJsonObject o{
        {"id", c.value("id").toString()},
        {"label", c.value("label").toString()},
        {"type", c.value("type").toString()},
        {"code", c.value("code").toInt()},
        {"value", c.value("value").toInt()},
    };
    if (c.value("type") == "slider")
        o.insert("max", c.value("max").toInt());
    if (c.value("type") != "slider") {
        QJsonArray options;
        for (const QVariant &v : c.value("options").toList()) {
            const QVariantMap m = v.toMap();
            options.append(QJsonObject{{"value", m.value("value").toInt()}, {"label", m.value("label").toString()}});
        }
        o.insert("options", options);
    }
    if (!c.value("panel").toBool())
        o.insert("panel", false);
    if (!c.value("note").toString().isEmpty())
        o.insert("note", c.value("note").toString());
    return o;
}

static QJsonObject monitorJson(Monitor *m, int index) {
    QJsonArray controls;
    for (const QVariant &c : m->controls())
        controls.append(controlJson(c.toMap()));
    return QJsonObject{
        {"index", index},
        {"connector", m->connector()},
        {"serial", m->serial()},
        {"make", m->make()},
        {"model", m->model()},
        {"ready", m->ready()},
        {"error", m->error()},
        {"profile", QJsonObject{{"id", m->profileId()}, {"name", m->profileName()}, {"supported", m->supported()}}},
        {"controls", controls},
    };
}

static QJsonObject stateJson(Backend &backend) {
    QJsonArray monitors;
    const auto list = backend.monitorObjects();
    for (int i = 0; i < list.size(); ++i)
        monitors.append(monitorJson(list[i], i));
    QJsonObject o{{"monitors", monitors}, {"contribute", Backend::contributeUrl()}};
    if (!backend.error().isEmpty())
        o.insert("error", backend.error());
    if (!backend.profileErrors().isEmpty())
        o.insert("profileErrors", QJsonArray::fromStringList(backend.profileErrors()));
    return o;
}

// Labels match loosely: "coding dark", "Coding-Dark" and "codingdark" are one.
static QString loose(const QString &s) {
    return s.toLower().remove(QRegularExpression("[^a-z0-9]"));
}

// Resolves a value for a control: a number, an option label, or on/off.
static int valueFor(const QString &value, const QVariantMap &control) {
    bool ok = false;
    const int number = value.toInt(&ok, 0);
    if (ok)
        return number;
    if (control.value("type") == "toggle") {
        const QString v = loose(value);
        if (v == "on" || v == "true" || v == "1") return control.value("on").toInt();
        if (v == "off" || v == "false" || v == "0") return control.value("off").toInt();
    }
    for (const QVariant &o : control.value("options").toList()) {
        const QVariantMap map = o.toMap();
        if (loose(map.value("label").toString()) == loose(value))
            return map.value("value").toInt();
    }
    return -1;
}

static int indexOf(const QList<Monitor *> &monitors, const QString &target) {
    for (int i = 0; i < monitors.size(); ++i)
        if (monitors[i]->serial() == target || monitors[i]->connector() == target)
            return i;
    return -1;
}

// `lumen-ddc scan` talks to one bus directly and never starts the Backend, whose
// own reads would collide with the scan on the same bus.
static int scan(const QString &target, QTextStream &out, QTextStream &err) {
    const QString hyprctl = QStandardPaths::findExecutable("hyprctl");
    const QString ddcutil = QStandardPaths::findExecutable("ddcutil");
    if (hyprctl.isEmpty() || ddcutil.isEmpty()) {
        err << "lumen-ddc: scan needs hyprctl and ddcutil\n";
        return 1;
    }
    QProcess h;
    h.start(hyprctl, {"monitors", "all", "-j"});
    h.waitForFinished(3000);
    for (const auto &info : Ddc::parseHyprlandMonitors(h.readAllStandardOutput())) {
        if (info.serial != target && info.connector != target)
            continue;
        const int bus = Ddc::busForConnector("/sys/class/drm", info.connector);
        if (bus < 0) {
            err << "lumen-ddc: " << info.connector << " has no DDC bus\n";
            return 1;
        }
        err << "Reading every VCP code from " << info.make << " " << info.model << " on " << info.connector
            << " (about 10 seconds)…\n";
        QProcess d;
        d.start(ddcutil, {"--bus", QString::number(bus), "--sleep-multiplier", "0.5", "--terse", "getvcp", "scan"});
        if (!d.waitForFinished(120000)) {
            d.kill();
            err << "lumen-ddc: ddcutil did not finish\n";
            return 1;
        }
        for (const QString &line : QString::fromUtf8(d.readAllStandardOutput()).split('\n'))
            if (line.startsWith("VCP ") && !line.contains(" ERR"))
                out << line << "\n";
        return 0;
    }
    err << "lumen-ddc: no monitor matches '" << target << "'\n";
    return 1;
}

int run(const QStringList &args) {
    QTextStream out(stdout), err(stderr);
    const QString command = args.value(1);

    if (command == "--version") {
        out << "lumen-ddc " << QCoreApplication::applicationVersion() << "\n";
        return 0;
    }
    if (command == "help" || command == "--help" || command == "-h") {
        usage(out);
        return 0;
    }

    if (command == "scan") {
        if (args.size() < 3) {
            usage(err);
            return 1;
        }
        return scan(args[2], out, err);
    }

    Backend backend;
    if (!settle(backend, 15000)) {
        err << "lumen-ddc: monitors did not answer in time\n";
        return 1;
    }
    for (const QString &e : backend.profileErrors())
        err << "lumen-ddc: skipped profile " << e << "\n";

    if (command == "state") {
        out << QJsonDocument(stateJson(backend)).toJson(QJsonDocument::Compact) << "\n";
        return backend.monitorObjects().isEmpty() ? 1 : 0;
    }

    const auto monitors = backend.monitorObjects();

    if (command == "profile") {
        const int index = args.size() > 2 ? indexOf(monitors, args[2]) : (monitors.size() == 1 ? 0 : -1);
        if (index < 0) {
            err << "lumen-ddc: name a monitor by serial or connector\n";
            return 1;
        }
        out << backend.profileFile(index) << "\n";
        return 0;
    }

    // set TARGET CONTROL VALUE
    if (args.size() < 5) {
        usage(err);
        return 1;
    }
    const QString target = args[2], control = args[3], value = args[4];
    const int index = indexOf(monitors, target);
    if (target != "all" && index < 0) {
        err << "lumen-ddc: no monitor matches '" << target << "'\n";
        return 1;
    }
    Monitor *reference = monitors.value(index < 0 ? 0 : index);
    if (!reference) {
        err << "lumen-ddc: no monitors found\n";
        return 1;
    }
    backend.setLinked(target == "all");

    const QVariantMap named = reference->control(control);
    if (!named.isEmpty()) {
        const int v = valueFor(value, named);
        if (v < 0) {
            err << "lumen-ddc: '" << value << "' is not a value of " << control << "\n";
            return 1;
        }
        backend.setControl(index < 0 ? 0 : index, control, v);
    } else {
        bool ok = false;
        const int code = control.toInt(&ok, 0);
        bool valueOk = false;
        const int v = value.toInt(&valueOk, 0);
        if (!ok || code < 0 || code > 0xFF) {
            err << "lumen-ddc: " << reference->connector() << " has no control '" << control << "'\n";
            return 1;
        }
        if (!valueOk || v < 0) {
            err << "lumen-ddc: a raw VCP code needs a number\n";
            return 1;
        }
        if (Profiles::alwaysRefused(code) || reference->profile().avoid.contains(code)) {
            err << "lumen-ddc: refusing to write 0x" << QString::number(code, 16) << " (a reset, or marked harmful in the profile)\n";
            return 1;
        }
        backend.setValue(index < 0 ? 0 : index, code, v);
    }
    settle(backend, 15000);
    // Same safeguard as the window: bring back an output Hyprland switched off.
    QThread::msleep(800);
    backend.checkOutputs();

    out << QJsonDocument(stateJson(backend)).toJson(QJsonDocument::Compact) << "\n";
    for (Monitor *m : backend.monitorObjects())
        if (!m->error().isEmpty()) {
            err << "lumen-ddc: " << m->connector() << ": " << m->error() << "\n";
            return 1;
        }
    return 0;
}

}  // namespace Cli
