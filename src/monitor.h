#pragma once

#include <QObject>
#include <QProcess>
#include <QVariantList>

#include "ddc.h"
#include "profile.h"

// One physical monitor and its DDC/CI settings. Which settings it shows comes
// from its profile (profile.h). Every ddcutil call for a monitor goes through
// one queue, because a monitor answers one request at a time and overlapping
// requests fail. Writes to the same setting coalesce, so dragging a slider
// sends the latest value rather than every step.
class Monitor : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString connector READ connector CONSTANT)
    Q_PROPERTY(QString make READ make CONSTANT)
    Q_PROPERTY(QString model READ model CONSTANT)
    Q_PROPERTY(QString serial READ serial CONSTANT)
    Q_PROPERTY(int bus READ bus CONSTANT)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString profileId READ profileId NOTIFY changed)
    Q_PROPERTY(QString profileName READ profileName NOTIFY changed)
    Q_PROPERTY(bool supported READ supported NOTIFY changed)
    Q_PROPERTY(QVariantList controls READ controls NOTIFY changed)

public:
    Monitor(const Ddc::MonitorInfo &info, int bus, const QString &ddcutil, const QString &cacheDirectory,
            const Profiles::Profile &profile, QObject *parent = nullptr);
    ~Monitor() override;

    QString connector() const { return m_info.connector; }
    QString make() const { return m_info.make; }
    QString model() const { return m_info.model; }
    QString serial() const { return m_info.serial; }
    int bus() const { return m_bus; }
    bool ready() const { return m_ready; }
    bool busy() const { return m_process.state() != QProcess::NotRunning || !m_queue.isEmpty(); }
    QString error() const { return m_error; }

    const Profiles::Profile &profile() const { return m_profile; }
    QString profileId() const { return m_profile.id; }
    QString profileName() const { return m_profile.name; }
    bool supported() const { return m_profile.supported; }
    void setProfile(const Profiles::Profile &profile);

    // The controls this monitor shows, in profile order, each as
    // {id, label, type, code, value, max, options, on, off, panel, note}.
    // A control is left out when the monitor doesn't report its code (for
    // the generic profile) or didn't answer for it.
    QVariantList controls() const;
    // One control by id, or an empty map when the monitor doesn't show it.
    QVariantMap control(const QString &id) const;

    // What the monitor's capabilities string lists: whether it has a code,
    // and the values it allows for it (empty for continuous codes).
    bool reports(int code) const { return m_capabilities.contains(code); }
    QList<int> reportedValues(int code) const { return m_capabilities.value(code); }

    int value(int code) const { return m_values.value(code).current; }
    int maximum(int code) const { const int m = m_values.value(code).maximum; return m > 0 ? m : 100; }

    // True when the monitor takes this value for this code: listed in the
    // profile, or (without a profile entry) reported in its capabilities.
    bool accepts(int code, int value) const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setValue(int code, int value);

signals:
    void changed();
    void busyChanged();
    // Emitted when a run of DDC traffic has finished, so the caller can check
    // that the compositor still has the output on.
    void settled();

private:
    enum class Job { Capabilities, Read, Write };
    struct Task { Job job; int code = -1; int value = -1; };

    void enqueue(const Task &task);
    void next();
    void finished(int exitCode, QProcess::ExitStatus status);
    void loadCachedCapabilities();
    QStringList baseArguments() const;
    bool shows(const Profiles::Control &c) const;
    QVariantList options(const Profiles::Control &c) const;
    QVariantMap describe(const Profiles::Control &c) const;

    Ddc::MonitorInfo m_info;
    int m_bus;
    QString m_ddcutil;
    QString m_cacheFile;
    Profiles::Profile m_profile;
    QProcess m_process;
    QList<Task> m_queue;
    Task m_running{Job::Read};
    bool m_trafficSinceIdle = false;
    bool m_ready = false;
    bool m_haveCapabilities = false;
    QString m_error;
    QMap<int, Ddc::Value> m_values;
    QMap<int, QList<int>> m_capabilities;
};
