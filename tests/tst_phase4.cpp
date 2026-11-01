// Phase 4 acceptance tests: MP4 export.
#include "app/ExportDialog.h"
#include "app/Language.h"
#include "app/MainWindow.h"
#include "app/ProjectController.h"
#include "core/MediaProbe.h"
#include "engine/Exporter.h"
#include "engine/TimelinePlayer.h"

#include <QAction>
#include <QFile>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QProcess>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using namespace bc;
using Kind = Timeline::Kind;

namespace {

bool makeVideo(const QString &path, int seconds = 4)
{
    QProcess p;
    p.start(QStringLiteral(BC_FFMPEG_EXE),
            {QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
             QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
             QStringLiteral("testsrc=size=320x240:rate=25:duration=%1").arg(seconds),
             QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
             QStringLiteral("sine=frequency=440:sample_rate=48000:duration=%1").arg(seconds),
             QStringLiteral("-c:v"), QStringLiteral("mpeg4"), QStringLiteral("-g"), QStringLiteral("50"),
             QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("-shortest"), path});
    return p.waitForFinished(60000) && p.exitCode() == 0 && QFile::exists(path);
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

class TestPhase4 : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_video;

    void loadSmallProject(ProjectController &ctl)
    {
        SequenceSettings seq = ctl.project().sequence;
        seq.width = 640; // small frames keep the test fast
        seq.height = 360;
        ctl.setSequence(seq);
        QCOMPARE(ctl.importFiles({m_video}), 1);
        waitIdle(ctl);
        QVERIFY(!ctl.placeMedia({ctl.project().items.first().id}, 0, 0).isEmpty());
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        QVERIFY(m_dir.isValid());
        m_video = m_dir.filePath(QStringLiteral("tone.mp4"));
        QVERIFY2(makeVideo(m_video), "ffmpeg could not create test media");
        Language::instance().setLanguage(QStringLiteral("uz_Cyrl"), false);
    }

    void encodersAreFound()
    {
        const QList<MediaProbe::VideoEncoder> list = MediaProbe::usableVideoEncoders();
        QVERIFY(!list.isEmpty());
        QCOMPARE(list.last().codec, QStringLiteral("mpeg4")); // always-working fallback is last
        for (const auto &e : list)
            QVERIFY2(e.codec != QLatin1String("libx264"), "x264 (GPL) must never be used");
        qInfo() << "encoders:" << [&] {
            QStringList n;
            for (const auto &e : list)
                n << e.codec;
            return n.join(QLatin1Char(','));
        }();
    }

    void bitRateFollowsFrameSize()
    {
        SequenceSettings hd;
        SequenceSettings small = hd;
        small.width = 1280;
        small.height = 720;
        QVERIFY(Exporter::suggestedKbps(hd, 1) > Exporter::suggestedKbps(small, 1));
        QVERIFY(Exporter::suggestedKbps(hd, 0) > Exporter::suggestedKbps(hd, 1));
        QVERIFY(Exporter::suggestedKbps(hd, 2) < Exporter::suggestedKbps(hd, 1));
    }

    void exportWritesPlayableMp4()
    {
        ProjectController ctl;
        QVERIFY2(ctl.canPlay(), qPrintable(ctl.engineError()));
        loadSmallProject(ctl);
        ctl.addTitle(0); // 5 s title: the sequence is 125 frames long
        const qint64 total = ctl.timeline().duration();
        QCOMPARE(total, 125);

        ExportSettings s;
        s.path = m_dir.filePath(QStringLiteral("out.mp4"));
        s.encoder = MediaProbe::usableVideoEncoders().first();
        s.videoKbps = 2000;
        QSignalSpy done(&ctl, &ProjectController::exportFinished);
        QSignalSpy progress(&ctl, &ProjectController::exportProgress);
        QString error;
        QVERIFY2(ctl.startExport(s, &error), qPrintable(error));
        QVERIFY(ctl.isExporting());
        QVERIFY(done.wait(120000));
        QVERIFY2(done.first().at(0).toBool(), qPrintable(done.first().at(1).toString()));
        QVERIFY(!ctl.isExporting());
        QVERIFY(progress.count() >= 2);
        QCOMPARE(progress.last().at(0).toLongLong(), total);

        MediaInfo info;
        QVERIFY2(MediaProbe::probe(s.path, info, &error), qPrintable(error));
        QCOMPARE(info.width, 640);
        QCOMPARE(info.height, 360);
        QVERIFY(info.hasAudio());
        QCOMPARE(info.audioCodec, QStringLiteral("aac"));
        QVERIFY2(qAbs(info.durationUs - 5000000) < 200000, qPrintable(QString::number(info.durationUs)));
        QVERIFY(info.videoCodec == QLatin1String("h264") || info.videoCodec == QLatin1String("mpeg4"));

        // The exported picture matches the preview (same graph): compare a frame.
        const QImage file = MediaProbe::frameAt(s.path, 1000000, 640, 360);
        TimelinePlayer player;
        player.setPreviewLimit(QSize(640, 360));
        player.setProject(ctl.project());
        const QImage preview = player.renderFrame(25).convertToFormat(QImage::Format_RGB32);
        QVERIFY(!file.isNull() && !preview.isNull());
        QCOMPARE(file.size(), preview.size());
        double diff = 0;
        for (int y = 0; y < 360; y += 8)
            for (int x = 0; x < 640; x += 8) {
                const QColor a = file.pixelColor(x, y), b = preview.pixelColor(x, y);
                diff += qAbs(a.red() - b.red()) + qAbs(a.green() - b.green()) + qAbs(a.blue() - b.blue());
            }
        diff /= (360 / 8) * (640 / 8) * 3;
        QVERIFY2(diff < 20, qPrintable(QString::number(diff))); // only compression differences
    }

    void cancelRemovesFile()
    {
        ProjectController ctl;
        loadSmallProject(ctl);
        ExportSettings s;
        s.path = m_dir.filePath(QStringLiteral("cancel.mp4"));
        s.encoder = MediaProbe::usableVideoEncoders().last();
        QSignalSpy done(&ctl, &ProjectController::exportFinished);
        QString error;
        QVERIFY2(ctl.startExport(s, &error), qPrintable(error));
        ctl.cancelExport();
        QCOMPARE(done.count(), 1);
        QVERIFY(!done.first().at(0).toBool());
        QVERIFY(done.first().at(1).toString().isEmpty());
        QVERIFY(!QFile::exists(s.path));
    }

    void emptySequenceIsRefused()
    {
        ProjectController ctl;
        ExportSettings s;
        s.path = m_dir.filePath(QStringLiteral("empty.mp4"));
        s.encoder = MediaProbe::usableVideoEncoders().last();
        QString error;
        QVERIFY(!ctl.startExport(s, &error));
        QVERIFY(!error.isEmpty());
    }
    void exportActionAndDialog()
    {
        MainWindow w;
        ProjectController *ctl = w.controller();
        auto *act = w.findChild<QAction *>(QStringLiteral("actExport"));
        QVERIFY(act && !act->isEnabled()); // empty timeline
        QVERIFY(!act->property("bc_planned").toBool());
        loadSmallProject(*ctl);
        QVERIFY(act->isEnabled());
        QVERIFY(w.findChild<QAction *>(QStringLiteral("tbExport"))->isEnabled());

        ExportDialog dialog(ctl, &w);
        QVERIFY(dialog.path().endsWith(QLatin1String(".mp4")));
        dialog.setPath(m_dir.filePath(QStringLiteral("dialog")));        // ".mp4" is added
        QSignalSpy done(ctl, &ProjectController::exportFinished);
        QVERIFY(dialog.startExport());
        QVERIFY(!dialog.findChild<QPushButton *>(QStringLiteral("exportStart"))->isEnabled());
        QVERIFY(done.wait(120000));
        QVERIFY(done.first().at(0).toBool());
        QVERIFY(QFile::exists(m_dir.filePath(QStringLiteral("dialog.mp4"))));
        QCOMPARE(dialog.findChild<QProgressBar *>(QStringLiteral("exportProgress"))->value(), 1000);
        QVERIFY(dialog.findChild<QLabel *>(QStringLiteral("exportStatus"))->text().contains(QLatin1String("dialog.mp4")));
        QVERIFY(dialog.findChild<QPushButton *>(QStringLiteral("exportStart"))->isEnabled());

        // Closing during an export cancels it and keeps the dialog open.
        dialog.setPath(m_dir.filePath(QStringLiteral("dialog2.mp4")));
        QVERIFY(dialog.startExport());
        dialog.findChild<QPushButton *>(QStringLiteral("exportClose"))->click(); // "Cancel export"
        QVERIFY(!ctl->isExporting());
        QVERIFY(!QFile::exists(m_dir.filePath(QStringLiteral("dialog2.mp4"))));
    }
};

QTEST_MAIN(TestPhase4)
#include "tst_phase4.moc"
