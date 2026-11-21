#pragma once

#include <QList>
#include <QMap>
#include <QString>

// Pure parsing for ddcutil output, Hyprland's monitor list, and lumen's
// per-model code files. Nothing here touches a device, so all of it is tested.
namespace Ddc {

// MCCS feature codes lumen shows controls for.
enum Code : int {
    Brightness = 0x10,
    Contrast = 0x12,
    ColorPreset = 0x14,
    Volume = 0x62,
    DisplayMode = 0xDC,
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

// Names for vendor-specific values, read from a per-model file:
//   mode.coding=0x30   halo.code=0xE9   halo.on=0x01   temp.user=0x0b
struct ModelCodes {
    QList<QPair<QString, int>> modes;  // in file order
    int haloCode = -1;
    QList<QPair<QString, int>> halo;
    QMap<int, QString> presetNames;    // overrides for 0x14 labels
};
ModelCodes parseModelCodes(const QString &text);

// "BenQ RD320U" -> "benq-rd320u", the file name of a model's code file.
QString modelSlug(const QString &model);

// A human label for a 0x14 color preset value, per MCCS.
QString colorPresetLabel(int value);

}  // namespace Ddc
