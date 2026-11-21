#pragma once

#include <QObject>
#include <QProcess>
#include <QVariantList>

#include "ddc.h"

// One physical monitor and its DDC/CI settings. Every ddcutil call for a
// monitor goes through one queue, because a monitor answers one request at a
// time and overlapping requests fail. Writes to the same setting coalesce, so
// dragging a slider sends the latest value rather than every step.
class Monitor : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString connector READ connector CONSTANT)
    Q_PROPERTY(QString model READ model CONSTANT)
    Q_PROPERTY(QString serial READ serial CONSTANT)
    Q_PROPERTY(int bus READ bus CONSTANT)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(int brightness READ brightness NOTIFY changed)
    Q_PROPERTY(int brightnessMax READ brightnessMax NOTIFY changed)
    Q_PROPERTY(int contrast READ contrast NOTIFY changed)
    Q_PROPERTY(int contrastMax READ contrastMax NOTIFY changed)
    Q_PROPERTY(int volume READ volume NOTIFY changed)
    Q_PROPERTY(int volumeMax READ volumeMax NOTIFY changed)
    Q_PROPERTY(int colorPreset READ colorPreset NOTIFY changed)
    Q_PROPERTY(QVariantList colorPresets READ colorPresets NOTIFY changed)
    Q_PROPERTY(int mode READ mode NOTIFY changed)
    Q_PROPERTY(QVariantList modes READ modes NOTIFY changed)
    Q_PROPERTY(bool haloAvailable READ haloAvailable NOTIFY changed)
    Q_PROPERTY(int haloCode READ haloCode NOTIFY changed)
    Q_PROPERTY(int halo READ halo NOTIFY changed)
    Q_PROPERTY(QVariantList haloOptions READ haloOptions NOTIFY changed)

public:
    Monitor(const Ddc::MonitorInfo &info, int bus, const QString &ddcutil, const QString &cacheDirectory,
            const Ddc::ModelCodes &codes, QObject *parent = nullptr);
    ~Monitor() override;

    QString connector() const { return m_info.connector; }
    QString model() const { return m_info.model; }
    QString serial() const { return m_info.serial; }
    int bus() const { return m_bus; }
    bool ready() const { return m_ready; }
    bool busy() const { return m_process.state() != QProcess::NotRunning || !m_queue.isEmpty(); }
    QString error() const { return m_error; }

    int brightness() const { return valueOf(Ddc::Brightness); }
    int brightnessMax() const { return maxOf(Ddc::Brightness); }
    int contrast() const { return valueOf(Ddc::Contrast); }
    int contrastMax() const { return maxOf(Ddc::Contrast); }
    int volume() const { return valueOf(Ddc::Volume); }
    int volumeMax() const { return maxOf(Ddc::Volume); }
    int colorPreset() const { return valueOf(Ddc::ColorPreset); }
    QVariantList colorPresets() const;
    int mode() const { return valueOf(Ddc::DisplayMode); }
    QVariantList modes() const;
    bool haloAvailable() const { return m_codes.haloCode >= 0; }
    int halo() const { return m_codes.haloCode >= 0 ? valueOf(m_codes.haloCode) : -1; }
    QVariantList haloOptions() const;
    int haloCode() const { return m_codes.haloCode; }

    // True when the monitor reports the feature and, for lists, the value.
    bool accepts(int code, int value) const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setValue(int code, int value);
    void setModelCodes(const Ddc::ModelCodes &codes);

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
    int valueOf(int code) const { return m_values.value(code).current; }
    int maxOf(int code) const { const int m = m_values.value(code).maximum; return m > 0 ? m : 100; }
    QStringList baseArguments() const;
    QVariantList options(int code, const QMap<int, QString> &names) const;

    Ddc::MonitorInfo m_info;
    int m_bus;
    QString m_ddcutil;
    QString m_cacheFile;
    Ddc::ModelCodes m_codes;
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
