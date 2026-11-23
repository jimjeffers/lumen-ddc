#include "monitor.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

Monitor::Monitor(const Ddc::MonitorInfo &info, int bus, const QString &ddcutil, const QString &cacheDirectory,
                 const Profiles::Profile &profile, QObject *parent)
    : QObject(parent), m_info(info), m_bus(bus), m_ddcutil(ddcutil), m_profile(profile) {
    const QString key = info.serial.isEmpty() ? QString("bus-%1").arg(bus) : info.serial;
    m_cacheFile = cacheDirectory + "/" + key + ".caps";
    connect(&m_process, &QProcess::finished, this, &Monitor::finished);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            m_error = tr("Could not run ddcutil.");
            m_queue.clear();
            emit changed();
            emit busyChanged();
        }
    });
    loadCachedCapabilities();
}

Monitor::~Monitor() {
    // Killing the process emits finished(), which would run the queue (and
    // signal the backend) on a half-destroyed object.
    m_process.disconnect(this);
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
}

QStringList Monitor::baseArguments() const {
    // Addressing the bus directly skips ddcutil's slow detection pass, which
    // also probes every other monitor (including ones behind the hub).
    return {"--bus", QString::number(m_bus), "--sleep-multiplier", "0.5"};
}

void Monitor::loadCachedCapabilities() {
    QFile file(m_cacheFile);
    if (!file.open(QIODevice::ReadOnly))
        return;
    m_capabilities = Ddc::parseCapabilities(QString::fromUtf8(file.readAll()));
    m_haveCapabilities = !m_capabilities.isEmpty();
}

void Monitor::setProfile(const Profiles::Profile &profile) {
    const bool newCodes = [&] {
        for (const auto &c : profile.controls)
            if (!m_profile.controlForCode(c.code))
                return true;
        return false;
    }();
    m_profile = profile;
    emit changed();
    if (newCodes && m_ready)
        enqueue({Job::Read});  // Read the codes the new profile adds.
}

void Monitor::refresh() {
    if (!m_haveCapabilities)
        enqueue({Job::Capabilities});
    enqueue({Job::Read});
}

void Monitor::setValue(int code, int value) {
    if (Profiles::alwaysRefused(code) || m_profile.avoid.contains(code))
        return;  // Resets, and codes the profile marks as harmful, are never sent.
    // Show the new value straight away; the monitor catches up in the queue.
    Ddc::Value v = m_values.value(code);
    if (v.current == value)
        return;
    v.current = value;
    m_values.insert(code, v);
    emit changed();
    enqueue({Job::Write, code, value});
}

void Monitor::enqueue(const Task &task) {
    for (Task &queued : m_queue) {
        if (queued.job == task.job && queued.code == task.code) {
            queued.value = task.value;  // Coalesce: only the latest value matters.
            return;
        }
    }
    m_queue.append(task);
    emit busyChanged();
    next();
}

void Monitor::next() {
    if (m_process.state() != QProcess::NotRunning)
        return;
    if (m_queue.isEmpty()) {
        emit busyChanged();
        if (m_trafficSinceIdle) {
            m_trafficSinceIdle = false;
            emit settled();
        }
        return;
    }
    m_running = m_queue.takeFirst();
    m_trafficSinceIdle = true;
    QStringList args = baseArguments();
    switch (m_running.job) {
    case Job::Capabilities:
        args << "capabilities";
        break;
    case Job::Read: {
        args << "--terse" << "getvcp";
        // A profile for this model is trusted: vendor codes such as BenQ's
        // MoonHalo are often missing from the capabilities string. The
        // generic profile only asks for what the monitor reports.
        for (const auto &c : m_profile.controls)
            if (m_profile.supported || !m_haveCapabilities || m_capabilities.contains(c.code))
                args << QString::number(c.code, 16);
        break;
    }
    case Job::Write:
        // --noverify: the BenQ answers the read-back slowly, and the next
        // refresh shows the true value anyway.
        args << "--noverify" << "setvcp" << QString::number(m_running.code, 16) << QString::number(m_running.value);
        break;
    }
    m_process.start(m_ddcutil, args);
}

void Monitor::finished(int exitCode, QProcess::ExitStatus status) {
    const QString out = QString::fromUtf8(m_process.readAllStandardOutput());
    const QString err = QString::fromUtf8(m_process.readAllStandardError()).trimmed();
    const bool ok = status == QProcess::NormalExit && exitCode == 0;

    switch (m_running.job) {
    case Job::Capabilities: {
        const auto caps = Ddc::parseCapabilities(out);
        if (!caps.isEmpty()) {
            m_capabilities = caps;
            m_haveCapabilities = true;
            QDir().mkpath(QFileInfo(m_cacheFile).absolutePath());
            QSaveFile file(m_cacheFile);  // Written beside, then renamed into place.
            if (file.open(QIODevice::WriteOnly)) {
                file.write(out.toUtf8());
                file.commit();
            }
        }
        break;
    }
    case Job::Read: {
        const auto values = Ddc::parseTerse(out);
        if (values.isEmpty()) {
            m_error = err.isEmpty() ? tr("The monitor did not answer over DDC/CI.") : err.section('\n', 0, 0);
        } else {
            // Keep values the user changed while this read was in flight.
            for (auto it = values.begin(); it != values.end(); ++it) {
                bool pending = false;
                for (const Task &t : m_queue)
                    pending |= t.job == Job::Write && t.code == it.key();
                if (!pending)
                    m_values.insert(it.key(), it.value());
            }
            m_error.clear();
            m_ready = true;
        }
        emit changed();
        break;
    }
    case Job::Write:
        if (!ok) {
            m_error = err.isEmpty() ? tr("The monitor rejected a setting.") : err.section('\n', 0, 0);
            emit changed();
        }
        break;
    }
    next();
}

bool Monitor::shows(const Profiles::Control &c) const {
    if (!m_profile.supported && m_haveCapabilities && !m_capabilities.contains(c.code))
        return false;
    // Until the first read, show everything; afterwards only what answered.
    return !m_ready || value(c.code) >= 0;
}

QVariantList Monitor::options(const Profiles::Control &c) const {
    QVariantList list;
    if (c.type == "toggle") {
        list.append(QVariantMap{{"value", c.on}, {"label", tr("On")}});
        list.append(QVariantMap{{"value", c.off}, {"label", tr("Off")}});
        return list;
    }
    if (c.type != "choice")
        return list;
    QList<int> values;
    QMap<int, QString> names;
    for (const auto &o : c.options) {
        values.append(o.value);
        names.insert(o.value, o.label);
    }
    if (values.isEmpty())
        values = m_capabilities.value(c.code);
    const int current = value(c.code);
    if (current >= 0 && !values.contains(current))
        values.append(current);  // Always show what the monitor is set to.
    for (int v : values) {
        QString label = names.value(v);
        if (label.isEmpty())
            label = c.code == Ddc::ColorPreset ? Ddc::colorPresetLabel(v) : QString("0x%1").arg(v, 2, 16, QChar('0'));
        list.append(QVariantMap{{"value", v}, {"label", label}});
    }
    return list;
}

QVariantMap Monitor::describe(const Profiles::Control &c) const {
    return QVariantMap{
        {"id", c.id},
        {"label", c.label},
        {"type", c.type},
        {"code", c.code},
        {"value", value(c.code)},
        {"max", maximum(c.code)},
        {"options", options(c)},
        {"on", c.on},
        {"off", c.off},
        {"panel", c.panel},
        {"note", c.note},
    };
}

QVariantList Monitor::controls() const {
    QVariantList list;
    for (const auto &c : m_profile.controls)
        if (shows(c))
            list.append(describe(c));
    return list;
}

QVariantMap Monitor::control(const QString &id) const {
    const auto *c = m_profile.control(id);
    return c && shows(*c) ? describe(*c) : QVariantMap{};
}

bool Monitor::accepts(int code, int v) const {
    if (Profiles::alwaysRefused(code) || m_profile.avoid.contains(code))
        return false;
    if (const auto *c = m_profile.controlForCode(code)) {
        if (c->type == "toggle")
            return v == c->on || v == c->off;
        if (c->type == "choice" && !c->options.isEmpty()) {
            for (const auto &o : c->options)
                if (o.value == v)
                    return true;
            return false;
        }
        if (m_profile.supported)
            return true;
    }
    if (m_haveCapabilities && !m_capabilities.contains(code))
        return false;
    const QList<int> allowed = m_capabilities.value(code);
    return allowed.isEmpty() || allowed.contains(v);
}
