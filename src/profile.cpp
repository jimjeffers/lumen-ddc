#include "profile.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace Profiles {

// Values may be written as numbers or as strings ("0x30", "48").
static int number(const QJsonValue &v) {
    if (v.isDouble())
        return v.toInt(-1);
    bool ok = false;
    const int n = v.toString().trimmed().toInt(&ok, 0);
    return ok ? n : -1;
}

bool Profile::matches(const QString &make, const QString &model) const {
    for (const Match &m : match) {
        const bool makeOk = m.make.isEmpty() || m.make.compare(make, Qt::CaseInsensitive) == 0;
        if (makeOk && !m.model.isEmpty() && m.model.compare(model.trimmed(), Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

const Control *Profile::control(const QString &controlId) const {
    for (const Control &c : controls)
        if (c.id == controlId)
            return &c;
    return nullptr;
}

const Control *Profile::controlForCode(int code) const {
    for (const Control &c : controls)
        if (c.code == code)
            return &c;
    return nullptr;
}

Profile parse(const QByteArray &json, QString *error) {
    auto fail = [error](const QString &why) {
        if (error)
            *error = why;
        return Profile{};
    };
    QJsonParseError jsonError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &jsonError);
    if (!doc.isObject())
        return fail(jsonError.error != QJsonParseError::NoError ? jsonError.errorString() : "not a JSON object");
    const QJsonObject o = doc.object();

    Profile p;
    p.id = o.value("id").toString().trimmed();
    p.name = o.value("name").toString(p.id);
    p.supported = o.value("supported").toBool(true);
    if (p.id.isEmpty())
        return fail("missing \"id\"");

    for (const QJsonValue &m : o.value("match").toArray()) {
        const QJsonObject mo = m.toObject();
        p.match.append({mo.value("make").toString(), mo.value("model").toString()});
    }

    QStringList seen;
    for (const QJsonValue &item : o.value("controls").toArray()) {
        const QJsonObject co = item.toObject();
        Control c;
        c.id = co.value("id").toString().trimmed();
        c.label = co.value("label").toString(c.id);
        c.type = co.value("type").toString();
        c.code = number(co.value("code"));
        c.panel = co.value("panel").toBool(true);
        c.note = co.value("note").toString();
        if (c.id.isEmpty() || seen.contains(c.id))
            return fail(QString("control %1: missing or repeated \"id\"").arg(c.id));
        if (c.code < 0 || c.code > 0xFF)
            return fail(QString("control %1: \"code\" must be 0x00-0xFF").arg(c.id));
        if (alwaysRefused(c.code))
            return fail(QString("control %1: code 0x%2 is a reset and is never written").arg(c.id).arg(c.code, 2, 16, QChar('0')));
        if (c.type == "toggle") {
            c.on = number(co.value("on"));
            c.off = number(co.value("off"));
            if (c.on < 0 || c.off < 0 || c.on == c.off)
                return fail(QString("control %1: a toggle needs different \"on\" and \"off\" values").arg(c.id));
        } else if (c.type == "choice") {
            for (const QJsonValue &ov : co.value("options").toArray()) {
                const QJsonObject oo = ov.toObject();
                const int v = number(oo.value("value"));
                if (v < 0)
                    return fail(QString("control %1: option without a value").arg(c.id));
                c.options.append({oo.value("label").toString(QString("0x%1").arg(v, 2, 16, QChar('0'))), v});
            }
        } else if (c.type != "slider") {
            return fail(QString("control %1: \"type\" must be slider, choice or toggle").arg(c.id));
        }
        seen.append(c.id);
        p.controls.append(c);
    }
    if (p.controls.isEmpty())
        return fail("no controls");

    for (const QJsonValue &a : o.value("avoid").toArray()) {
        const int code = number(a.isObject() ? a.toObject().value("code") : a);
        if (code >= 0)
            p.avoid.append(code);
    }
    for (const Control &c : p.controls)
        if (p.avoid.contains(c.code))
            return fail(QString("control %1 uses a code the profile avoids").arg(c.id));
    return p;
}

static void loadDirectory(const QString &path, QList<Profile> &profiles, QStringList *errors) {
    const QDir dir(path);
    for (const QString &name : dir.entryList({"*.json"}, QDir::Files, QDir::Name)) {
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        QString why;
        Profile p = parse(file.readAll(), &why);
        if (!p.isValid()) {
            if (errors)
                errors->append(QString("%1: %2").arg(dir.filePath(name), why));
            continue;
        }
        for (int i = 0; i < profiles.size(); ++i)
            if (profiles[i].id == p.id)
                profiles.removeAt(i--);  // A user file replaces the built-in one.
        profiles.append(p);
    }
}

QList<Profile> load(const QString &userDirectory, QStringList *errors) {
    QList<Profile> profiles;
    loadDirectory(":/profiles", profiles, errors);
    if (!userDirectory.isEmpty())
        loadDirectory(userDirectory, profiles, errors);
    return profiles;
}

Profile forMonitor(const QList<Profile> &profiles, const QString &make, const QString &model) {
    const Profile *generic = nullptr;
    for (const Profile &p : profiles) {
        if (p.id == "generic")
            generic = &p;
        else if (p.supported && p.matches(make, model))
            return p;
    }
    if (generic)
        return *generic;
    // Only reachable if the built-in generic profile is missing.
    Profile fallback;
    fallback.id = "generic";
    fallback.name = "Standard DDC/CI";
    fallback.supported = false;
    fallback.controls.append({"brightness", "Brightness", "slider", 0x10});
    return fallback;
}

bool alwaysRefused(int code) {
    return code == 0x04 || code == 0x05 || code == 0x06 || code == 0x08 || code == 0x0A;
}

}  // namespace Profiles
