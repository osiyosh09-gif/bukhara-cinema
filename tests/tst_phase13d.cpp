// Phase 13 (part 4) acceptance tests: hardware video decoding. CI machines
// have no usable video card, so these check the setting, what reaches MLT,
// and that a device that cannot decode falls back to the processor with the
// same picture.
#include "app/MainWindow.h"
#include "app/ProjectController.h"
#include "app/SettingsDialog.h"
#include "core/HwAccel.h"
#include "core/MediaCache.h"
#include "core/MediaProbe.h"
#include "core/Proxy.h"
#include "engine/TimelinePlayer.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>

using namespace bc;

namespace {

bool makeVideo(const QString &path)
{
    QProcess p;
    p.start(QStringLiteral(BC_FFMPEG_EXE),
            {QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
             QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
             QStringLiteral("testsrc=size=640x360:rate=25:duration=2"),
             QStringLiteral("-c:v"), QStringLiteral("mpeg4"), QStringLiteral("-q:v"), QStringLiteral("3"), path});
    return p.waitForFinished(60000) && p.exitCode() == 0 && QFile::exists(path);
}

// Mean absolute difference per channel (0..255).
double difference(const QImage &a, const QImage &b)
{
    if (a.isNull() || a.size() != b.size())
        return 255;
    const QImage x = a.convertToFormat(QImage::Format_RGB888), y = b.convertToFormat(QImage::Format_RGB888);
    double sum = 0;
    qint64 n = 0;
    for (int r = 0; r < x.height(); r += 2) {
        const uchar *p = x.constScanLine(r), *q = y.constScanLine(r);
        for (int i = 0; i < x.width() * 3; ++i, ++n)
            sum += std::abs(int(p[i]) - int(q[i]));
    }
    return n ? sum / n : 255;
}

void waitIdle(ProjectController &ctl)
{
    for (int i = 0; i < 100; ++i) {
        ctl.waitForBackgroundWork();
        bool probing = false;
        for (const MediaItem &m : ctl.project().items)
            probing |= ctl.isProbing(m.id);
        if (!probing)
            return;
        QTest::qWait(50);
    }
}

} // namespace

class TestPhase13d : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_video;

    // A one-clip project of the test video.
    Project project(ProjectController &ctl)
    {
        if (ctl.project().items.isEmpty()) {
            ctl.importFiles({m_video});
            waitIdle(ctl);
            ctl.placeMedia({ctl.project().items.first().id}, 0, 0);
        }
        return ctl.project();
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("BukharaCinemaTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_phase13d"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        QSettings().setValue(QStringLiteral("proxy/auto"), false);
        QDir(MediaCache::root()).removeRecursively();
        QVERIFY(m_dir.isValid());
        m_video = m_dir.filePath(QStringLiteral("clip.mkv"));
        QVERIFY(makeVideo(m_video));
    }

    void setting()
    {
        QCOMPARE(HwAccel::setting(), QStringLiteral("auto"));
        QVERIFY(!HwAccel::candidates().isEmpty());
        for (const QString &d : HwAccel::available())
            QVERIFY(HwAccel::candidates().contains(d));
        QCOMPARE(HwAccel::decoder(), HwAccel::available().value(0));
        qInfo() << "devices here:" << HwAccel::available() << "encoders:" << HwAccel::encoders();

        HwAccel::setSetting(QStringLiteral("off"));
        QVERIFY(HwAccel::decoder().isEmpty());
        HwAccel::applyToEngine();
        QVERIFY(qgetenv("MLT_AVFORMAT_HWACCEL").isEmpty());

        const QString forced = HwAccel::candidates().first();
        HwAccel::setSetting(forced);
        QCOMPARE(HwAccel::decoder(), forced);
        HwAccel::applyToEngine();
        QCOMPARE(QString::fromLatin1(qgetenv("MLT_AVFORMAT_HWACCEL")), forced);

        HwAccel::setSetting(QStringLiteral("auto"));
        HwAccel::applyToEngine();
        QCOMPARE(QString::fromLatin1(qgetenv("MLT_AVFORMAT_HWACCEL")), HwAccel::decoder());
    }

    void proxyFallsBack()
    {
        const QString target = m_dir.filePath(QStringLiteral("proxy.mkv"));
        QString err, decoder = QStringLiteral("x");

        // An unknown device: the processor decodes.
        HwAccel::setSetting(QStringLiteral("nosuchcard"));
        QVERIFY2(Proxy::make(m_video, target, &err, {}, nullptr, &decoder), qPrintable(err));
        QVERIFY(decoder.isEmpty());
        MediaInfo info;
        QVERIFY(MediaProbe::probe(target, info, nullptr));
        QCOMPARE(info.height, 360);
        QVERIFY(std::abs(info.durationUs - 2000000) < 100000);

        // Every candidate of this system: on the card if it works, else the
        // processor; the proxy is complete either way.
        for (const QString &type : HwAccel::candidates()) {
            HwAccel::setSetting(type);
            QFile::remove(target);
            QVERIFY2(Proxy::make(m_video, target, &err, {}, nullptr, &decoder), qPrintable(type + ": " + err));
            QVERIFY(decoder.isEmpty() || decoder == type);
            QVERIFY(MediaProbe::probe(target, info, nullptr));
            QCOMPARE(info.height, 360);
            QVERIFY(std::abs(info.durationUs - 2000000) < 100000);
        }
        HwAccel::setSetting(QStringLiteral("auto"));
    }

    void playbackFallsBack()
    {
        ProjectController ctl;
        if (!ctl.canPlay())
            QSKIP("video engine not available");
        const Project p = project(ctl);
        QVERIFY(!p.timeline.tracks.isEmpty());

        HwAccel::setSetting(QStringLiteral("off"));
        HwAccel::applyToEngine();
        TimelinePlayer cpu;
        cpu.setProject(p);
        const QImage reference = cpu.renderFrame(20);
        QVERIFY(!reference.isNull());

        // A forced device (a working card or not): the same picture.
        for (const QString &type : HwAccel::candidates()) {
            HwAccel::setSetting(type);
            HwAccel::applyToEngine();
            TimelinePlayer player;
            player.setProject(p);
            const QImage img = player.renderFrame(20);
            const double d = difference(img, reference);
            QVERIFY2(d < 6, qPrintable(QStringLiteral("%1: %2").arg(type).arg(d)));
        }
        HwAccel::setSetting(QStringLiteral("auto"));
        HwAccel::applyToEngine();
    }

    void settingsDialog()
    {
        HwAccel::setSetting(QStringLiteral("auto"));
        SettingsDialog dlg;
        auto *combo = dlg.findChild<QComboBox *>(QStringLiteral("gpuDecodeCombo"));
        auto *info = dlg.findChild<QLabel *>(QStringLiteral("gpuInfo"));
        QVERIFY(combo && info);
        QCOMPARE(combo->currentData().toString(), QStringLiteral("auto"));
        QVERIFY(!info->text().isEmpty());
        combo->setCurrentIndex(combo->findData(QStringLiteral("off")));
        dlg.apply();
        QCOMPARE(HwAccel::setting(), QStringLiteral("off"));
        QVERIFY(qgetenv("MLT_AVFORMAT_HWACCEL").isEmpty());

        // A device type set by hand stays selected.
        HwAccel::setSetting(QStringLiteral("cuda"));
        SettingsDialog again;
        combo = again.findChild<QComboBox *>(QStringLiteral("gpuDecodeCombo"));
        QCOMPARE(combo->currentData().toString(), QStringLiteral("cuda"));
        HwAccel::setSetting(QStringLiteral("auto"));
    }
};

QTEST_MAIN(TestPhase13d)
#include "tst_phase13d.moc"
