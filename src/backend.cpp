#include "backend.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

Backend::Backend(const Paths &paths, QObject *parent) : QObject(parent), m_paths(paths) {
    if (m_paths.ddcutil.isEmpty())
        m_paths.ddcutil = QStandardPaths::findExecutable("ddcutil");
    if (m_paths.hyprctl.isEmpty())
        m_paths.hyprctl = QStandardPaths::findExecutable("hyprctl");
    if (m_paths.configDirectory.isEmpty())
        m_paths.configDirectory = QDir::homePath() + "/.config/lumen-ddc";
    if (m_paths.cacheDirectory.isEmpty())
        m_paths.cacheDirectory = QDir::homePath() + "/.cache/lumen-ddc";

    m_hotplug.setInterval(3000);
    connect(&m_hotplug, &QTimer::timeout, this, &Backend::discover);

    m_outputCheck.setSingleShot(true);
    m_outputCheck.setInterval(1200);
    connect(&m_outputCheck, &QTimer::timeout, this, &Backend::checkOutputs);

    m_noticeClear.setSingleShot(true);
    m_noticeClear.setInterval(6000);
    connect(&m_noticeClear, &QTimer::timeout, this, [this] { setNotice({}); });

    // Profiles in the user's folder apply as soon as they're saved, so
    // writing one for a new monitor is quick to try.
    connect(&m_profileWatcher, &QFileSystemWatcher::fileChanged, this, &Backend::reloadProfiles);
    connect(&m_profileWatcher, &QFileSystemWatcher::directoryChanged, this, &Backend::reloadProfiles);

    reloadProfiles();
    refresh();
    m_hotplug.start();
}

Backend::~Backend() {
    // Monitors go first, while the timers they signal still exist.
    qDeleteAll(m_monitors);
    m_monitors.clear();
}

QByteArray Backend::runHyprctl(const QStringList &args) const {
    if (m_paths.hyprctl.isEmpty())
        return {};
    QProcess p;
    p.start(m_paths.hyprctl, args);
    if (!p.waitForFinished(2000)) {
        p.kill();
        p.waitForFinished(500);
        return {};
    }
    return p.readAllStandardOutput();
}

void Backend::refresh() {
    m_signature.clear();  // Forces a full rebuild.
    discover();
}

void Backend::discover() {
    if (m_paths.ddcutil.isEmpty()) {
        setError(tr("ddcutil is not installed. Install it with: omarchy pkg add ddcutil"));
        return;
    }
    if (m_paths.hyprctl.isEmpty()) {
        setError(tr("Hyprland is not running, so Lumen cannot tell which monitor is which."));
        return;
    }

    const QByteArray json = runHyprctl({"monitors", "all", "-j"});
    if (json.trimmed().isEmpty() && !m_monitors.isEmpty())
        return;  // A slow or failed hyprctl call is not a reason to drop the monitors.
    const auto infos = Ddc::parseHyprlandMonitors(json);
    QString signature;
    for (const auto &i : infos)
        signature += i.connector + "/" + i.serial + "@" + QString::number(i.x) + ";";
    if (signature == m_signature && !m_monitors.isEmpty())
        return;
    const bool firstLoad = m_signature.isEmpty();
    m_signature = signature;

    // Reuse Monitor objects for screens that are still here, so an
    // arrangement change doesn't drop their settings or rerun capabilities.
    QList<Monitor *> next;
    for (const auto &info : infos) {
        const int bus = Ddc::busForConnector(m_paths.drmRoot, info.connector);
        if (bus < 0)
            continue;  // Built-in panels and virtual outputs have no DDC bus.
        Monitor *existing = nullptr;
        for (Monitor *m : m_monitors)
            if (m->connector() == info.connector && m->serial() == info.serial && m->bus() == bus)
                existing = m;
        if (existing) {
            m_monitors.removeOne(existing);
            next.append(existing);
            if (firstLoad)
                existing->refresh();
            continue;
        }
        auto *monitor = new Monitor(info, bus, m_paths.ddcutil, m_paths.cacheDirectory,
                                    Profiles::forMonitor(m_profiles, info.make, info.model), this);
        connect(monitor, &Monitor::settled, this, [this] { m_outputCheck.start(); });
        monitor->refresh();
        next.append(monitor);
    }
    qDeleteAll(m_monitors);
    m_monitors = next;

    setError(m_monitors.isEmpty() ? tr("No monitors with DDC/CI found.") : QString());
    emit monitorsChanged();
}

QVariantList Backend::monitors() const {
    QVariantList list;
    for (Monitor *m : m_monitors)
        list.append(QVariant::fromValue(static_cast<QObject *>(m)));
    return list;
}

void Backend::setLinked(bool linked) {
    if (linked == m_linked)
        return;
    m_linked = linked;
    emit linkedChanged();
}

void Backend::setControl(int index, const QString &id, int value) {
    if (index < 0 || index >= m_monitors.size())
        return;
    Monitor *source = m_monitors[index];
    const auto *control = source->profile().control(id);
    if (!control)
        return;
    const bool sameProfileOnly = control->type != "slider";
    for (Monitor *m : m_monitors) {
        if (m != source && !m_linked)
            continue;
        if (m != source && sameProfileOnly && m->profileId() != source->profileId())
            continue;
        const QVariantMap c = m->control(id);
        if (c.isEmpty())
            continue;
        const int code = c.value("code").toInt();
        int v = value;
        if (control->type == "slider")
            v = qBound(0, value, c.value("max").toInt());
        if (m->accepts(code, v))
            m->setValue(code, v);
    }
}

void Backend::setValue(int index, int code, int value) {
    if (index < 0 || index >= m_monitors.size())
        return;
    Monitor *source = m_monitors[index];
    if (!m_linked) {
        // One monitor, by request: written as given, so a contributor can
        // try codes the monitor doesn't advertise. Resets and codes the
        // profile avoids are still refused in Monitor::setValue.
        source->setValue(code, value);
        return;
    }
    // A code the profile treats as a choice or toggle is model-specific.
    const auto *control = source->profile().controlForCode(code);
    const bool sameProfileOnly = control && control->type != "slider";
    for (Monitor *m : m_monitors) {
        if (sameProfileOnly && m->profileId() != source->profileId())
            continue;
        if (m->accepts(code, value))
            m->setValue(code, value);
    }
}

void Backend::step(int index, const QString &id, int delta) {
    if (index < 0 || index >= m_monitors.size())
        return;
    const QVariantMap c = m_monitors[index]->control(id);
    if (c.isEmpty() || c.value("type") != "slider" || c.value("value").toInt() < 0)
        return;
    setControl(index, id, qBound(0, c.value("value").toInt() + delta, c.value("max").toInt()));
}

QString Backend::profileFile(int index) {
    if (index < 0 || index >= m_monitors.size())
        return {};
    Monitor *m = m_monitors[index];
    const QString slug = m->supported() ? m->profileId() : Ddc::modelSlug(m->model());
    const QString path = profileDirectory() + "/" + slug + ".json";
    if (QFileInfo::exists(path))
        return path;

    QByteArray text;
    if (m->supported()) {
        // A copy of the built-in profile, to adjust locally.
        QFile builtIn(":/profiles/" + slug + ".json");
        if (builtIn.open(QIODevice::ReadOnly))
            text = builtIn.readAll();
    }
    if (text.isEmpty()) {
        // A starting point: the standard controls, plus a picture-mode
        // choice when the monitor reports one (0xDC), with its values to name.
        auto hex = [](int v) { return QString("0x%1").arg(v, 2, 16, QChar('0')).toUpper().replace("0X", "0x"); };
        auto control = [&](const QString &id, const QString &label, const QString &type, int code) {
            return QJsonObject{{"id", id}, {"label", label}, {"type", type}, {"code", hex(code)}};
        };
        QJsonArray controls;
        // Picture modes are often 0xDC; list the values the monitor reports
        // so they only need names.
        const QList<int> modes = m->reportedValues(0xDC);
        if (!modes.isEmpty()) {
            QJsonArray options;
            for (int v : modes)
                options.append(QJsonObject{{"label", "Name for " + hex(v)}, {"value", hex(v)}});
            QJsonObject mode = control("mode", "Picture mode", "choice", 0xDC);
            mode.insert("options", options);
            controls.append(mode);
        }
        controls.append(control("brightness", "Brightness", "slider", 0x10));
        controls.append(control("contrast", "Contrast", "slider", 0x12));
        controls.append(control("volume", "Speaker volume", "slider", 0x62));
        QJsonObject temp = control("temp", "Color temperature", "choice", 0x14);
        temp.insert("panel", false);
        controls.append(temp);
        QJsonObject profile{
            {"id", slug},
            {"name", m->model()},
            {"match", QJsonArray{QJsonObject{{"make", m->make()}, {"model", m->model()}}}},
            {"controls", controls},
        };
        text = QJsonDocument(profile).toJson(QJsonDocument::Indented);
    }
    QDir().mkpath(profileDirectory());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        file.write(text);
    watchProfiles();
    return path;
}

void Backend::reloadProfiles() {
    QStringList errors;
    m_profiles = Profiles::load(profileDirectory(), &errors);
    m_profileErrors = errors;
    for (Monitor *m : m_monitors)
        m->setProfile(Profiles::forMonitor(m_profiles, m->make(), m->model()));
    watchProfiles();
    emit profilesChanged();
}

void Backend::watchProfiles() {
    // Editors save by replacing the file, which drops it from the watch.
    const auto watched = m_profileWatcher.files() + m_profileWatcher.directories();
    if (!watched.isEmpty())
        m_profileWatcher.removePaths(watched);
    const QDir dir(profileDirectory());
    if (!dir.exists())
        return;
    m_profileWatcher.addPath(dir.path());
    for (const QString &name : dir.entryList({"*.json"}, QDir::Files))
        m_profileWatcher.addPath(dir.filePath(name));
}

void Backend::checkOutputs() {
    // DDC traffic through a Thunderbolt hub can make the monitor send a
    // hotplug pulse, after which Hyprland sometimes leaves the output off.
    // Reloading Hyprland's config turns it back on.
    const QJsonArray outputs = QJsonDocument::fromJson(runHyprctl({"monitors", "all", "-j"})).array();
    for (const QJsonValue &o : outputs) {
        if (!o.toObject().value("disabled").toBool())
            continue;
        const QString name = o.toObject().value("name").toString();
        for (Monitor *m : m_monitors) {
            if (m->connector() == name) {
                runHyprctl({"reload"});
                setNotice(tr("%1 switched off after a change; turned it back on.").arg(name));
                return;
            }
        }
    }
}

void Backend::setError(const QString &error) {
    if (error == m_error)
        return;
    m_error = error;
    emit errorChanged();
}

void Backend::setNotice(const QString &notice) {
    if (notice == m_notice)
        return;
    m_notice = notice;
    emit noticeChanged();
    if (!notice.isEmpty())
        m_noticeClear.start();
}
