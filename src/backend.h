#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <QVariantList>

#include "monitor.h"

// Finds the monitors Hyprland shows, gives each a Monitor, and keeps them in
// step when "linked". Paths to tools and directories are injectable so tests
// can run against fakes.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList monitors READ monitors NOTIFY monitorsChanged)
    Q_PROPERTY(bool linked READ linked WRITE setLinked NOTIFY linkedChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)

public:
    struct Paths {
        QString ddcutil;      // empty: found on PATH
        QString hyprctl;      // empty: found on PATH
        QString drmRoot = "/sys/class/drm";
        QString configDirectory;  // empty: ~/.config/lumen
        QString cacheDirectory;   // empty: ~/.cache/lumen
    };

    explicit Backend(QObject *parent = nullptr) : Backend(Paths{}, parent) {}
    explicit Backend(const Paths &paths, QObject *parent = nullptr);
    ~Backend() override;

    QVariantList monitors() const;
    QList<Monitor *> monitorObjects() const { return m_monitors; }
    bool linked() const { return m_linked; }
    void setLinked(bool linked);
    QString error() const { return m_error; }
    QString notice() const { return m_notice; }
    QString configDirectory() const { return m_paths.configDirectory; }

    // Rediscovers monitors and rereads every setting.
    Q_INVOKABLE void refresh();
    // Sets a value on one monitor, or on every monitor that accepts it when linked.
    Q_INVOKABLE void setValue(int index, int code, int value);
    // Nudges a continuous setting (brightness, contrast, volume) by delta.
    Q_INVOKABLE void step(int index, int code, int delta);
    // The per-model names file, created with a template on first use.
    Q_INVOKABLE QString codesFile(int index) const;
    // Turns back on any output of ours that Hyprland switched off.
    void checkOutputs();

signals:
    void monitorsChanged();
    void linkedChanged();
    void errorChanged();
    void noticeChanged();

private:
    void discover();
    void setError(const QString &error);
    void setNotice(const QString &notice);
    Ddc::ModelCodes codesFor(const QString &model) const;
    void reloadCodes();
    QByteArray runHyprctl(const QStringList &args) const;

    Paths m_paths;
    QList<Monitor *> m_monitors;
    QString m_signature;
    bool m_linked = true;
    QString m_error, m_notice;
    QTimer m_hotplug;       // Notices monitors appearing or leaving.
    QTimer m_outputCheck;   // Runs shortly after writes settle.
    QTimer m_noticeClear;
    QFileSystemWatcher m_codesWatcher;
};
