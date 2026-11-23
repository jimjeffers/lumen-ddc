#pragma once

#include <QList>
#include <QMap>
#include <QString>

// Pure parsing for ddcutil output and Hyprland's monitor list. Nothing here touches a device, so all of it is tested.
namespace Ddc {

// MCCS feature codes lumen refers to by name.
enum Code : int {
    Brightness = 0x10,
    ColorPreset = 0x14,
};

struct Value {
    int current = -1;
    int maximum = -1;
};

// Parses `ddcutil --terse getvcp ...` lines ("VCP 10 C 36 100",
// "VCP 14 CNC x00 x0b x00 x0b", "VCP DC SNC x30") into code -> value.
QMap<int, Value> parseTerse(const QString &output);

// Parses the vcp(...) list of a raw MCCS capabilities string into
// code -> allowed values (empty for continuous features such as brightness).
QMap<int, QList<int>> parseCapabilities(const QString &output);

struct MonitorInfo {
    QString connector;  // DP-2
    QString make;       // BNQ
    QString model;      // BenQ RD320U
    QString serial;     // RIGHT000002
    int x = 0;
    bool disabled = false;
};

// Parses `hyprctl monitors all -j`, ordered left to right.
QList<MonitorInfo> parseHyprlandMonitors(const QByteArray &json);

// Finds the /dev/i2c-N bus a DRM connector exposes for DDC, or -1.
int busForConnector(const QString &drmRoot, const QString &connector);

// "BenQ RD320U" -> "benq-rd320u", the file name of a model's profile.
QString modelSlug(const QString &model);

// A human label for a 0x14 color preset value, per MCCS.
QString colorPresetLabel(int value);

}  // namespace Ddc
