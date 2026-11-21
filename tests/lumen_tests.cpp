#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "ddc.h"
#include "theme.h"
#include "translator.h"

// Real output captured from a BenQ RD320U.
static const char *kCapabilities =
    "Unparsed capabilities string: (prot(monitor)type(LCD)model(RD320U)cmds(01 02 03 07 0C E3 F3)"
    "vcp(02 04 08 10 12 14(04 05 08 0B) 16 18 1A 52 60(0F 11 12 13) 62 72(50 64 78 8C A0) 7D (00 01 02 07) "
    "DC(0A 0F 12 1F 23 28 30 31 32) E9(01 02 03))mswhql(1)asset_eep(40)mccs_ver(2.2))\n";

static const char *kTerse =
    "VCP 10 C 36 100\n"
    "VCP 12 C 50 100\n"
    "VCP 14 CNC x00 x0b x00 x0b\n"
    "VCP 62 CNC x00 x32 x00 x1e\n"
    "VCP DC SNC x30\n";

static bool write(const QString &path, const QByteArray &data, bool executable = false) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
        return false;
    file.close();
    if (executable)
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    return true;
}

static QString read(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

class LumenTests : public QObject {
    Q_OBJECT

    // A fake system: two BenQs in Hyprland, DRM connectors with i2c buses, and
    // a ddcutil that logs its arguments and answers like the real monitor.
    struct Fake {
        QTemporaryDir dir;
        Backend::Paths paths;
        QString log() const { return read(dir.filePath("ddcutil.log")); }

        Fake(bool rightDisabled = false) {
            const QString monitors = QString(
                R"([{"name":"DP-8","make":"BNQ","model":"BenQ RD320U","serial":"RIGHT000002","x":3072,"disabled":%1},)"
                R"({"name":"DP-2","make":"BNQ","model":"BenQ RD320U","serial":"LEFT0000001","x":0,"disabled":false}])")
                .arg(rightDisabled ? "true" : "false");
            write(dir.filePath("monitors.json"), monitors.toUtf8());
            write(dir.filePath("hyprctl"),
                  "#!/bin/sh\n"
                  "if [ \"$1\" = reload ]; then echo reload >> '" + dir.filePath("hyprctl.log").toUtf8() + "'; "
                  "sed -i 's/\"disabled\":true/\"disabled\":false/' '" + dir.filePath("monitors.json").toUtf8() + "'; exit 0; fi\n"
                  "cat '" + dir.filePath("monitors.json").toUtf8() + "'\n", true);
            write(dir.filePath("caps.txt"), kCapabilities);
            write(dir.filePath("terse.txt"), kTerse);
            write(dir.filePath("ddcutil"),
                  "#!/bin/sh\n"
                  "echo \"$*\" >> '" + dir.filePath("ddcutil.log").toUtf8() + "'\n"
                  "case \"$*\" in\n"
                  "  *capabilities*) cat '" + dir.filePath("caps.txt").toUtf8() + "' ;;\n"
                  "  *getvcp*) cat '" + dir.filePath("terse.txt").toUtf8() + "' ;;\n"
                  "esac\n", true);
            QDir().mkpath(dir.filePath("drm/card1-DP-2/i2c-10"));
            QDir().mkpath(dir.filePath("drm/card1-DP-8/i2c-16"));
            paths.ddcutil = dir.filePath("ddcutil");
            paths.hyprctl = dir.filePath("hyprctl");
            paths.drmRoot = dir.filePath("drm");
            paths.configDirectory = dir.filePath("config");
            paths.cacheDirectory = dir.filePath("cache");
        }
    };

    static bool idle(const Backend &backend) {
        for (Monitor *m : backend.monitorObjects())
            if (m->busy() || !m->ready())
                return false;
        return !backend.monitorObjects().isEmpty();
    }

private slots:
    void parsesTerseValues() {
        const auto v = Ddc::parseTerse(kTerse);
        QCOMPARE(v.value(0x10).current, 36);
        QCOMPARE(v.value(0x10).maximum, 100);
        QCOMPARE(v.value(0x14).current, 0x0b);
        QCOMPARE(v.value(0x62).current, 30);
        QCOMPARE(v.value(0x62).maximum, 50);
        QCOMPARE(v.value(0xDC).current, 0x30);
        QVERIFY(!v.contains(0x60));
    }

    void parsesCapabilitiesWithStraySpaces() {
        const auto caps = Ddc::parseCapabilities(kCapabilities);
        QVERIFY(caps.contains(0x10));
        QCOMPARE(caps.value(0x10), QList<int>{});
        QCOMPARE(caps.value(0x14), (QList<int>{0x04, 0x05, 0x08, 0x0B}));
        QCOMPARE(caps.value(0xDC).size(), 9);
        // "7D (00 01 02 07)" has a space before its list.
        QCOMPARE(caps.value(0x7D), (QList<int>{0x00, 0x01, 0x02, 0x07}));
        QVERIFY(!caps.contains(0x01));  // cmds(...) is not part of vcp(...).
    }

    void parsesHyprlandLeftToRight() {
        Fake fake;
        const auto monitors = Ddc::parseHyprlandMonitors(read(fake.dir.filePath("monitors.json")).toUtf8());
        QCOMPARE(monitors.size(), 2);
        QCOMPARE(monitors[0].serial, QString("LEFT0000001"));
        QCOMPARE(monitors[1].connector, QString("DP-8"));
        QCOMPARE(Ddc::busForConnector(fake.paths.drmRoot, "DP-8"), 16);
        QCOMPARE(Ddc::busForConnector(fake.paths.drmRoot, "HDMI-A-1"), -1);
    }

    void parsesModelCodes() {
        const auto codes = Ddc::parseModelCodes("# comment\nmode.coding-dark=0x30\nmode.movie=0x0a\n"
                                                "halo.code=0xE9\nhalo.off=0x01\nhalo.on=2\ntemp.Paper=0x0b\nbad line\n");
        QCOMPARE(codes.modes.size(), 2);
        QCOMPARE(codes.modes[0].first, QString("coding-dark"));
        QCOMPARE(codes.modes[0].second, 0x30);
        QCOMPARE(codes.haloCode, 0xE9);
        QCOMPARE(codes.halo[1].second, 2);
        QCOMPARE(codes.presetNames.value(0x0b), QString("Paper"));
        QCOMPARE(Ddc::modelSlug("BenQ RD320U"), QString("benq-rd320u"));
    }

    void readsBothMonitorsAndCachesCapabilities() {
        Fake fake;
        Backend backend(fake.paths);
        QTRY_VERIFY(idle(backend));
        QCOMPARE(backend.monitorObjects().size(), 2);
        Monitor *left = backend.monitorObjects()[0];
        QCOMPARE(left->serial(), QString("LEFT0000001"));
        QCOMPARE(left->bus(), 10);
        QCOMPARE(left->brightness(), 36);
        QCOMPARE(left->volumeMax(), 50);
        QCOMPARE(left->modes().size(), 9);
        QCOMPARE(left->colorPresets().size(), 4);
        QVERIFY(QFile::exists(fake.dir.filePath("cache/LEFT0000001.caps")));
        // Never the slow detection pass that probes every monitor.
        QVERIFY(!fake.log().contains("detect"));
        QVERIFY(fake.log().contains("--bus 10"));
    }

    void linkedChangesReachEveryMonitor() {
        Fake fake;
        Backend backend(fake.paths);
        QTRY_VERIFY(idle(backend));
        backend.setValue(0, Ddc::Brightness, 60);
        QTRY_VERIFY(idle(backend));
        QVERIFY(fake.log().contains("--bus 10 --sleep-multiplier 0.5 --noverify setvcp 10 60"));
        QVERIFY(fake.log().contains("--bus 16 --sleep-multiplier 0.5 --noverify setvcp 10 60"));
        QCOMPARE(backend.monitorObjects()[1]->brightness(), 60);
    }

    void unlinkedChangesStayOnOneMonitor() {
        Fake fake;
        Backend backend(fake.paths);
        QTRY_VERIFY(idle(backend));
        backend.setLinked(false);
        backend.setValue(1, Ddc::Contrast, 70);
        QTRY_VERIFY(idle(backend));
        QVERIFY(fake.log().contains("--bus 16 --sleep-multiplier 0.5 --noverify setvcp 12 70"));
        QVERIFY(!fake.log().contains("--bus 10 --sleep-multiplier 0.5 --noverify setvcp 12"));
    }

    void dragCoalescesIntoFewWrites() {
        Fake fake;
        Backend backend(fake.paths);
        QTRY_VERIFY(idle(backend));
        backend.setLinked(false);
        for (int v = 40; v <= 80; ++v)
            backend.setValue(0, Ddc::Brightness, v);
        QTRY_VERIFY(idle(backend));
        const int writes = fake.log().count("setvcp 10");
        QVERIFY2(writes <= 3, qPrintable(QString("%1 writes").arg(writes)));
        QVERIFY(fake.log().contains("setvcp 10 80"));
    }

    void refusesFactoryResets() {
        Fake fake;
        Backend backend(fake.paths);
        QTRY_VERIFY(idle(backend));
        backend.setValue(0, 0x04, 1);
        backend.setValue(0, 0x08, 1);
        QTest::qWait(200);
        QVERIFY(!fake.log().contains("setvcp 4 "));
        QVERIFY(!fake.log().contains("setvcp 8 "));
    }

    void stepsStayInRange() {
        Fake fake;
        Backend backend(fake.paths);
        QTRY_VERIFY(idle(backend));
        backend.setLinked(false);
        backend.step(0, Ddc::Brightness, 500);
        QCOMPARE(backend.monitorObjects()[0]->brightness(), 100);
        backend.step(0, Ddc::Volume, 500);
        QCOMPARE(backend.monitorObjects()[0]->volume(), 50);
    }

    void turnsADisabledOutputBackOn() {
        Fake fake(true);
        Backend backend(fake.paths);
        QTRY_VERIFY(idle(backend));
        backend.setValue(0, Ddc::Brightness, 50);
        QTRY_VERIFY_WITH_TIMEOUT(read(fake.dir.filePath("hyprctl.log")).contains("reload"), 5000);
        QVERIFY(backend.notice().contains("DP-8"));
    }

    void modeNamesComeFromTheModelFile() {
        Fake fake;
        write(fake.dir.filePath("config/benq-rd320u.conf"), "mode.coding-dark=0x30\nhalo.code=0xE9\nhalo.on=0x02\n");
        Backend backend(fake.paths);
        QTRY_VERIFY(idle(backend));
        Monitor *m = backend.monitorObjects()[0];
        QVERIFY(m->haloAvailable());
        bool named = false;
        for (const QVariant &o : m->modes())
            named |= o.toMap().value("label").toString() == "coding-dark";
        QVERIFY(named);
    }

    void createsATemplateNamesFile() {
        Fake fake;
        Backend backend(fake.paths);
        QTRY_VERIFY(idle(backend));
        const QString path = backend.codesFile(0);
        QVERIFY(path.endsWith("benq-rd320u.conf"));
        QVERIFY(read(path).contains("# mode.NAME=0x30"));
    }

    void reportsMissingDdcutil() {
        Fake fake;
        fake.paths.ddcutil = fake.dir.filePath("nope");
        QFile::remove(fake.dir.filePath("ddcutil"));
        Backend backend(fake.paths);
        QTRY_VERIFY(!backend.monitorObjects().isEmpty());
        QTRY_VERIFY(!backend.monitorObjects()[0]->error().isEmpty());
    }

    void themeFallsBack() {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent() {
        QTemporaryDir dir;
        write(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\n");
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void translatorFallsBackToEnglish() {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Brightness").isNull());
    }
};

QTEST_MAIN(LumenTests)
#include "lumen_tests.moc"
