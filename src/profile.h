#pragma once

#include <QList>
#include <QString>

// A monitor profile says which controls lumen shows for a model and what
// their VCP codes and values mean. Profiles are JSON files: the ones in
// profiles/ are built in, and files in ~/.config/lumen-ddc/profiles/ add new
// models or replace a built-in profile with the same id. Monitors without a
// profile get "generic", the standard MCCS controls, filtered to what the
// monitor reports. docs/adding-a-monitor.md describes the format.
namespace Profiles {

struct Option {
    QString label;
    int value = -1;
};

struct Control {
    QString id;     // brightness, mode, halo, …; the name `lumen-ddc set` takes
    QString label;
    QString type;   // slider | choice | toggle
    int code = -1;
    QList<Option> options;  // choice: empty means "what the monitor reports"
    int on = -1, off = -1;  // toggle
    bool panel = true;      // false: only in the window, not the bar panel
    QString note;
};

struct Match {
    QString make;   // EDID manufacturer as Hyprland reports it, e.g. BNQ; empty matches any
    QString model;  // e.g. "BenQ RD320U"
};

struct Profile {
    QString id;
    QString name;
    bool supported = true;  // false only for the generic fallback
    QList<Match> match;
    QList<Control> controls;
    QList<int> avoid;       // codes that are never written

    bool isValid() const { return !id.isEmpty() && !controls.isEmpty(); }
    bool matches(const QString &make, const QString &model) const;
    const Control *control(const QString &id) const;
    const Control *controlForCode(int code) const;
};

// Parses one profile. On failure returns an invalid profile and, when given,
// a reason in *error.
Profile parse(const QByteArray &json, QString *error = nullptr);

// Built-in profiles, then the ones in userDirectory (replacing built-ins with
// the same id). Files that fail to parse are skipped and reported in *errors.
QList<Profile> load(const QString &userDirectory, QStringList *errors = nullptr);

// The profile for a monitor, or the generic one.
Profile forMonitor(const QList<Profile> &profiles, const QString &make, const QString &model);

// Codes never written to any monitor: factory and color resets.
bool alwaysRefused(int code);

}  // namespace Profiles
