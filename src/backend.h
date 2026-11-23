#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <QVariantList>

#include "monitor.h"

// Finds the monitors Hyprland shows, gives each a Monitor with the profile
// for its model, and keeps them in step when "linked". Paths to tools and
// directories are injectable so tests can run against fakes.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList monitors READ monitors NOTIFY monitorsChanged)
    Q_PROPERTY(bool linked READ linked WRITE setLinked NOTIFY linkedChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(QStringList profileErrors READ profileErrors NOTIFY profilesChanged)
    Q_PROPERTY(QString contributeUrl READ contributeUrl CONSTANT)

public:
    struct Paths {
        QString ddcutil;      // empty: found on PATH
        QString hyprctl;      // empty: found on PATH
        QString drmRoot = "/sys/class/drm";
        QString configDirectory;  // empty: ~/.config/lumen-ddc (profiles in profiles/)
        QString cacheDirectory;   // empty: ~/.cache/lumen-ddc
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
    QString profileDirectory() const { return m_paths.configDirectory + "/profiles"; }
    QStringList profileErrors() const { return m_profileErrors; }
    // Where to read about adding a monitor that has no profile yet.
    static QString contributeUrl() { return "https://github.com/jimjeffers/lumen-ddc/blob/main/docs/adding-a-monitor.md"; }

    // Rediscovers monitors and rereads every setting.
    Q_INVOKABLE void refresh();
    // Sets a control (by id, such as "brightness" or "mode") on one monitor,
    // or, when linked, on every monitor that has it. Choices and toggles mean
    // different things on different models, so those only reach monitors
    // with the same profile.
    Q_INVOKABLE void setControl(int index, const QString &id, int value);
    // Sets a raw VCP code on one monitor, or on every monitor that accepts it
    // when linked. Used for codes a profile doesn't name yet.
    Q_INVOKABLE void setValue(int index, int code, int value);
    // Nudges a slider control by delta.
    Q_INVOKABLE void step(int index, const QString &id, int delta);
    // A profile file in the user's profile folder for this monitor's model:
    // a copy of its profile, or a starting point for a model without one.
    // Lumen reloads it whenever it's saved.
    Q_INVOKABLE QString profileFile(int index);
    // Turns back on any output of ours that Hyprland switched off.
    void checkOutputs();

signals:
    void monitorsChanged();
    void linkedChanged();
    void errorChanged();
    void noticeChanged();
    void profilesChanged();

private:
    void discover();
    void setError(const QString &error);
    void setNotice(const QString &notice);
    void reloadProfiles();
    void watchProfiles();
    QByteArray runHyprctl(const QStringList &args) const;

    Paths m_paths;
    QList<Monitor *> m_monitors;
    QString m_signature;
    bool m_linked = true;
    QString m_error, m_notice;
    QTimer m_hotplug;       // Notices monitors appearing or leaving.
    QTimer m_outputCheck;   // Runs shortly after writes settle.
    QTimer m_noticeClear;
    QList<Profiles::Profile> m_profiles;
    QStringList m_profileErrors;
    QFileSystemWatcher m_profileWatcher;
};
