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
        m_paths.configDirectory = QDir::homePath() + "/.config/lumen";
    if (m_paths.cacheDirectory.isEmpty())
        m_paths.cacheDirectory = QDir::homePath() + "/.cache/lumen";

    m_hotplug.setInterval(3000);
    connect(&m_hotplug, &QTimer::timeout, this, &Backend::discover);

    m_outputCheck.setSingleShot(true);
    m_outputCheck.setInterval(1200);
    connect(&m_outputCheck, &QTimer::timeout, this, &Backend::checkOutputs);

    m_noticeClear.setSingleShot(true);
    m_noticeClear.setInterval(6000);
    connect(&m_noticeClear, &QTimer::timeout, this, [this] { setNotice({}); });

    // Names for picture modes and MoonHalo update live as the file is edited.
    connect(&m_codesWatcher, &QFileSystemWatcher::fileChanged, this, &Backend::reloadCodes);
    connect(&m_codesWatcher, &QFileSystemWatcher::directoryChanged, this, &Backend::reloadCodes);

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
        setError(tr("Hyprland is not running, so lumen cannot tell which monitor is which."));
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
        auto *monitor = new Monitor(info, bus, m_paths.ddcutil, m_paths.cacheDirectory, codesFor(info.model), this);
        connect(monitor, &Monitor::settled, this, [this] { m_outputCheck.start(); });
        monitor->refresh();
        next.append(monitor);
    }
    qDeleteAll(m_monitors);
    m_monitors = next;

    setError(m_monitors.isEmpty() ? tr("No monitors with DDC/CI found.") : QString());
    reloadCodes();
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

void Backend::setValue(int index, int code, int value) {
    if (index < 0 || index >= m_monitors.size())
        return;
    if (!m_linked) {
        m_monitors[index]->setValue(code, value);
        return;
    }
    // Picture modes and halo values are model-specific, so a linked change
    // only goes to monitors that list the same value.
    const QString model = m_monitors[index]->model();
    for (Monitor *m : m_monitors) {
        const bool vendorCode = code == Ddc::DisplayMode || code == m->haloCode();
        if (vendorCode && m->model() != model)
            continue;
        if (m->accepts(code, value))
            m->setValue(code, value);
    }
}

void Backend::step(int index, int code, int delta) {
    if (index < 0 || index >= m_monitors.size())
        return;
    Monitor *source = m_monitors[index];
    int current = -1, maximum = 100;
    switch (code) {
    case Ddc::Brightness: current = source->brightness(); maximum = source->brightnessMax(); break;
    case Ddc::Contrast: current = source->contrast(); maximum = source->contrastMax(); break;
    case Ddc::Volume: current = source->volume(); maximum = source->volumeMax(); break;
    default: return;
    }
    if (current < 0)
        return;
    setValue(index, code, qBound(0, current + delta, maximum));
}

QString Backend::codesFile(int index) const {
    if (index < 0 || index >= m_monitors.size())
        return {};
    Monitor *m = m_monitors[index];
    const QString path = m_paths.configDirectory + "/" + Ddc::modelSlug(m->model()) + ".conf";
    if (QFileInfo::exists(path))
        return path;

    // Start the file with the values this monitor reports, so naming them is
    // a matter of filling in the blanks.
    QString text = QString("# Names for %1 settings that are specific to this model.\n"
                           "# Edit and save; lumen picks up changes immediately.\n#\n"
                           "# Picture modes (VCP 0xDC). Values this monitor reports:\n").arg(m->model());
    for (const QVariant &o : m->modes())
        text += "# mode.NAME=" + o.toMap().value("label").toString() + "\n";
    text += "#\n# Vendor backlight such as BenQ MoonHalo: its VCP code and values.\n"
            "# halo.code=0xNN\n# halo.on=0x01\n# halo.off=0x00\n";
    QDir().mkpath(m_paths.configDirectory);
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        file.write(text.toUtf8());
    return path;
}

Ddc::ModelCodes Backend::codesFor(const QString &model) const {
    QFile file(m_paths.configDirectory + "/" + Ddc::modelSlug(model) + ".conf");
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return Ddc::parseModelCodes(QString::fromUtf8(file.readAll()));
}

void Backend::reloadCodes() {
    // Editors save by replacing the file, which drops it from the watch.
    const auto watched = m_codesWatcher.files() + m_codesWatcher.directories();
    if (!watched.isEmpty())
        m_codesWatcher.removePaths(watched);
    if (QFileInfo::exists(m_paths.configDirectory))
        m_codesWatcher.addPath(m_paths.configDirectory);
    for (Monitor *m : m_monitors) {
        const QString path = m_paths.configDirectory + "/" + Ddc::modelSlug(m->model()) + ".conf";
        if (QFileInfo::exists(path) && !m_codesWatcher.files().contains(path))
            m_codesWatcher.addPath(path);
        m->setModelCodes(codesFor(m->model()));
    }
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
