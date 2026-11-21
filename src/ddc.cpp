#include "ddc.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>

namespace Ddc {

static int hexByte(QString token) {
    if (token.startsWith('x'))
        token.remove(0, 1);
    bool ok = false;
    const int value = token.toInt(&ok, 16);
    return ok ? value : -1;
}

QMap<int, Value> parseTerse(const QString &output) {
    QMap<int, Value> values;
    for (const QString &line : output.split('\n', Qt::SkipEmptyParts)) {
        const QStringList f = line.simplified().split(' ');
        if (f.size() < 4 || f[0] != "VCP")
            continue;
        bool ok = false;
        const int code = f[1].toInt(&ok, 16);
        if (!ok)
            continue;
        Value v;
        if (f[2] == "C" && f.size() >= 5) {
            v.current = f[3].toInt();
            v.maximum = f[4].toInt();
        } else if (f[2] == "SNC") {
            v.current = hexByte(f[3]);
        } else if (f[2] == "CNC" && f.size() >= 7) {
            // Bytes come as mh ml sh sl: the maximum, then the current value.
            v.maximum = hexByte(f[3]) * 256 + hexByte(f[4]);
            v.current = hexByte(f[5]) * 256 + hexByte(f[6]);
        } else {
            continue;  // Unsupported feature or a reply ddcutil flagged as invalid.
        }
        values.insert(code, v);
    }
    return values;
}

QMap<int, QList<int>> parseCapabilities(const QString &output) {
    QMap<int, QList<int>> features;
    const int start = output.indexOf("vcp(");
    if (start < 0)
        return features;

    // Walk the balanced parentheses: "10 12 14(04 05 08 0B) 16 60 (0F 11)".
    // Some monitors put a space before a value list, which ddcutil itself
    // trips over, so a list binds to the code before it regardless.
    int depth = 1, last = -1;  // Already inside "vcp(".

    QString token;
    QList<int> *list = nullptr;
    auto flush = [&] {
        if (token.isEmpty())
            return;
        bool ok = false;
        const int value = token.toInt(&ok, 16);
        token.clear();
        if (!ok)
            return;
        if (depth == 1) {
            last = value;
            features.insert(value, {});
        } else if (depth == 2 && list) {
            list->append(value);
        }
    };
    for (int i = start + 4; i < output.size(); ++i) {
        const QChar c = output[i];
        if (c == '(') {
            flush();
            ++depth;
            list = (depth == 2 && last >= 0) ? &features[last] : nullptr;
        } else if (c == ')') {
            flush();
            if (--depth == 0)
                break;
            list = nullptr;
        } else if (c.isSpace()) {
            flush();
        } else {
            token += c;
        }
    }
    return features;
}

QList<MonitorInfo> parseHyprlandMonitors(const QByteArray &json) {
    QList<MonitorInfo> monitors;
    for (const QJsonValue &item : QJsonDocument::fromJson(json).array()) {
        const QJsonObject o = item.toObject();
        MonitorInfo m;
        m.connector = o.value("name").toString();
        m.make = o.value("make").toString();
        m.model = o.value("model").toString();
        m.serial = o.value("serial").toString();
        m.x = o.value("x").toInt();
        m.disabled = o.value("disabled").toBool();
        if (!m.connector.isEmpty())
            monitors.append(m);
    }
    std::stable_sort(monitors.begin(), monitors.end(),
                     [](const MonitorInfo &a, const MonitorInfo &b) { return a.x < b.x; });
    return monitors;
}

int busForConnector(const QString &drmRoot, const QString &connector) {
    // Each DRM connector directory holds an i2c-N child for its DDC channel.
    const QDir drm(drmRoot);
    for (const QString &card : drm.entryList({"card*-" + connector}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        for (const QString &entry : QDir(drm.filePath(card)).entryList({"i2c-*"}, QDir::Dirs | QDir::NoDotAndDotDot)) {
            bool ok = false;
            const int bus = entry.mid(4).toInt(&ok);
            if (ok)
                return bus;
        }
    }
    return -1;
}

ModelCodes parseModelCodes(const QString &text) {
    ModelCodes codes;
    for (QString line : text.split('\n')) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        const int eq = line.indexOf('=');
        if (eq < 0)
            continue;
        const QString key = line.left(eq).trimmed();
        bool ok = false;
        const int value = line.mid(eq + 1).trimmed().toInt(&ok, 0);
        if (!ok)
            continue;
        if (key == "halo.code")
            codes.haloCode = value;
        else if (key.startsWith("mode."))
            codes.modes.append({key.mid(5), value});
        else if (key.startsWith("halo."))
            codes.halo.append({key.mid(5), value});
        else if (key.startsWith("temp."))
            codes.presetNames.insert(value, key.mid(5));
    }
    return codes;
}

QString modelSlug(const QString &model) {
    QString slug = model.toLower();
    slug.replace(QRegularExpression("[^a-z0-9]+"), "-");
    return slug.remove(QRegularExpression("^-+|-+$"));
}

QString colorPresetLabel(int value) {
    switch (value) {
    case 0x01: return "sRGB";
    case 0x02: return "Native";
    case 0x03: return "4000K";
    case 0x04: return "5000K";
    case 0x05: return "6500K";
    case 0x06: return "7500K";
    case 0x07: return "8200K";
    case 0x08: return "9300K";
    case 0x09: return "10000K";
    case 0x0A: return "11500K";
    case 0x0B: return "User 1";
    case 0x0C: return "User 2";
    case 0x0D: return "User 3";
    default: return QString("0x%1").arg(value, 2, 16, QChar('0'));
    }
}

}  // namespace Ddc
