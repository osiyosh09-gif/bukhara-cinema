// Phase 5 (part 2) acceptance tests: proxy files, media cache, collecting a
// project into one folder.
#include "app/CacheDialog.h"
#include "app/CollectDialog.h"
#include "app/MainWindow.h"
#include "app/ProjectController.h"
#include "core/Collector.h"
#include "core/MediaCache.h"
#include "core/MediaProbe.h"
#include "core/Proxy.h"
#include "engine/TimelinePlayer.h"

#include <QAction>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using namespace bc;

namespace {

bool ffmpeg(const QStringList &args)
{
    QProcess p;
    p.start(QStringLiteral(BC_FFMPEG_EXE), QStringList{QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error")} + args);
    return p.waitForFinished(120000) && p.exitCode() == 0;
}

// A video with a moving picture (testsrc has a running counter) and a tone.
bool makeVideo(const QString &path, const QString &size, int seconds)
{
    return ffmpeg({QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                   QStringLiteral("testsrc=size=%1:rate=25:duration=%2").arg(size).arg(seconds),
                   QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                   QStringLiteral("sine=frequency=440:sample_rate=48000:duration=%1").arg(seconds),
                   QStringLiteral("-c:v"), QStringLiteral("mpeg4"), QStringLiteral("-q:v"), QStringLiteral("4"),
                   QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("-shortest"), path})
           && QFile::exists(path);
}

// Mean colour difference (0..255) of two images of the same size.
double difference(const QImage &a0, const QImage &b0)
{
    const QImage a = a0.convertToFormat(QImage::Format_RGB32);
    const QImage b = b0.convertToFormat(QImage::Format_RGB32).scaled(a.size());
    double d = 0;
    int n = 0;
    for (int y = 0; y < a.height(); y += 4)
        for (int x = 0; x < a.width(); x += 4) {
            const QColor p = a.pixelColor(x, y), q = b.pixelColor(x, y);
            d += qAbs(p.red() - q.red()) + qAbs(p.green() - q.green()) + qAbs(p.blue() - q.blue());
            n += 3;
        }
    return n ? d / n : 255;
}

QColor average(const QImage &img0)
{
    const QImage img = img0.convertToFormat(QImage::Format_RGB32).scaled(16, 16);
    long r = 0, g = 0, b = 0;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            const QColor c = img.pixelColor(x, y);
            r += c.red();
            g += c.green();
            b += c.blue();
        }
    return QColor(int(r / 256), int(g / 256), int(b / 256));
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

int clipCount(const Timeline &t)
{
    int n = 0;
    for (const TimelineTrack &tr : t.tracks)
        n += int(tr.clips.size());
    return n;
}

} // namespace

class TestPhase5b : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_big;   // 2560x1440: gets a proxy
    QString m_small; // 320x240

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("BukharaCinemaTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_phase5b"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        QDir(MediaCache::root()).removeRecursively();
        QVERIFY(m_dir.isValid());
        m_big = m_dir.filePath(QStringLiteral("big.mp4"));
        m_small = m_dir.filePath(QStringLiteral("small.mp4"));
        QVERIFY(makeVideo(m_big, QStringLiteral("2560x1440"), 3));
        QVERIFY(makeVideo(m_small, QStringLiteral("320x240"), 3));
    }

    // --- proxies

    void proxyIsSmallAndInStep()
    {
        MediaInfo orig;
        QVERIFY(MediaProbe::probe(m_big, orig, nullptr));
        QVERIFY(Proxy::recommended(orig));
        MediaInfo small;
        QVERIFY(MediaProbe::probe(m_small, small, nullptr));
        QVERIFY(!Proxy::recommended(small));

        const QString proxy = m_dir.filePath(QStringLiteral("p/big_proxy.mkv"));
        QList<double> seen;
        QString err;
        QVERIFY2(Proxy::make(m_big, proxy, &err, [&](double f) { seen << f; }), qPrintable(err));
        QVERIFY(!QFile::exists(proxy + QStringLiteral(".part")));
        QVERIFY(!seen.isEmpty());
        QCOMPARE(seen.last(), 1.0);

        MediaInfo info;
        QVERIFY2(MediaProbe::probe(proxy, info, &err), qPrintable(err));
        QCOMPARE(info.height, Proxy::kHeight);
        QCOMPARE(info.width, 960);
        QCOMPARE(info.videoCodec, QStringLiteral("mpeg4"));
        QVERIFY(info.hasAudio()); // copied, so the times line up
        QVERIFY2(qAbs(info.durationUs - orig.durationUs) < 100000, qPrintable(QString::number(info.durationUs)));
        QVERIFY(QFileInfo(proxy).size() < QFileInfo(m_big).size());

        // The same moment shows the same picture (the counter moves every frame).
        for (qint64 t : {0LL, 1000000LL, 2480000LL}) {
            const QImage a = MediaProbe::frameAt(m_big, t, 320, 180);
            const QImage b = MediaProbe::frameAt(proxy, t, 320, 180);
            QVERIFY(!a.isNull() && !b.isNull());
            const double d = difference(a, b);
            QVERIFY2(d < 10, qPrintable(QStringLiteral("t=%1 diff=%2").arg(t).arg(d)));
        }
    }

    void proxyCanBeCancelled()
    {
        const QString proxy = m_dir.filePath(QStringLiteral("p/cancelled.mkv"));
        std::atomic_bool cancel{true};
        QString err;
        QVERIFY(!Proxy::make(m_big, proxy, &err, {}, &cancel));
        QVERIFY(!QFile::exists(proxy));
        QVERIFY(!QFile::exists(proxy + QStringLiteral(".part")));
        QVERIFY(!Proxy::make(m_dir.filePath(QStringLiteral("none.mp4")), proxy, &err));
    }

    void bigVideosGetProxiesAutomatically()
    {
        ProjectController ctl;
        QVERIFY2(ctl.canPlay(), qPrintable(ctl.engineError()));
        QSignalSpy finished(&ctl, &ProjectController::proxyFinished);
        QCOMPARE(ctl.importFiles({m_big, m_small}), 2);
        waitIdle(ctl);
        ctl.waitForProxies();
        const QUuid big = ctl.project().items[0].path.endsWith(QStringLiteral("big.mp4"))
                              ? ctl.project().items[0].id : ctl.project().items[1].id;
        const QUuid small = big == ctl.project().items[0].id ? ctl.project().items[1].id : ctl.project().items[0].id;
        QVERIFY(ctl.hasProxy(big));
        QVERIFY(!ctl.hasProxy(small)); // Full HD or less: no proxy unless asked
        QCOMPARE(finished.count(), 1);
        QVERIFY(finished.first().at(1).toBool());
        QVERIFY(QFile::exists(MediaCache::pathFor(MediaCache::Kind::Proxy, m_big)));

        // On request also for small ones; a second request does nothing.
        QCOMPARE(ctl.createProxies({small}), 1);
        QCOMPARE(ctl.createProxies({small}), 0);
        ctl.waitForProxies();
        QVERIFY(ctl.hasProxy(small));

        // Playback with and without proxies shows the same picture.
        ctl.placeMedia({big}, 0, 0);
        TimelinePlayer withProxy, original;
        withProxy.setPreviewLimit(QSize(320, 180));
        original.setPreviewLimit(QSize(320, 180));
        QHash<QUuid, QString> proxies{{big, MediaCache::pathFor(MediaCache::Kind::Proxy, m_big)}};
        withProxy.setProxies(proxies);
        withProxy.setProject(ctl.project());
        original.setProject(ctl.project());
        for (qint64 f : {0, 30, 60}) {
            const double d = difference(withProxy.renderFrame(f), original.renderFrame(f));
            QVERIFY2(d < 12, qPrintable(QStringLiteral("frame %1 diff %2").arg(f).arg(d)));
        }

        // Deleting removes the file and goes back to the original.
        ctl.deleteProxies({big, small});
        QVERIFY(!ctl.hasProxy(big));
        QVERIFY(!QFile::exists(MediaCache::pathFor(MediaCache::Kind::Proxy, m_big)));
    }

    void previewUsesProxyExportUsesOriginal()
    {
        QSettings().setValue(QStringLiteral("proxy/auto"), false);
        const QString media = m_dir.filePath(QStringLiteral("blue.mp4"));
        QVERIFY(ffmpeg({QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                        QStringLiteral("color=c=blue:size=640x360:rate=25:duration=2"),
                        QStringLiteral("-c:v"), QStringLiteral("mpeg4"), media}));
        // A "proxy" that is red: the preview must show red, the export blue.
        const QString fake = MediaCache::pathFor(MediaCache::Kind::Proxy, media);
        QDir().mkpath(QFileInfo(fake).absolutePath());
        QVERIFY(ffmpeg({QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                        QStringLiteral("color=c=red:size=320x180:rate=25:duration=2"),
                        QStringLiteral("-c:v"), QStringLiteral("mpeg4"), fake}));

        ProjectController ctl;
        SequenceSettings seq = ctl.project().sequence;
        seq.width = 640;
        seq.height = 360;
        ctl.setSequence(seq);
        QCOMPARE(ctl.importFiles({media}), 1);
        waitIdle(ctl);
        const QUuid id = ctl.project().items.first().id;
        QVERIFY(ctl.hasProxy(id));
        ctl.placeMedia({id}, 0, 0);

        // Frames arrive in the background; wait for the newest one.
        QSignalSpy frame(&ctl, &ProjectController::programFrameReady);
        auto shown = [&] {
            for (int i = 0; i < 20; ++i) {
                frame.wait(100);
                ctl.waitForBackgroundWork();
                QCoreApplication::processEvents();
            }
            return frame.isEmpty() ? QColor() : average(frame.last().first().value<QImage>());
        };
        ctl.setPlayhead(10);
        QColor c = shown();
        QVERIFY2(c.red() > 180 && c.blue() < 80, qPrintable(c.name()));

        ctl.setUseProxies(false);
        c = shown();
        ctl.setUseProxies(true);
        QSettings().setValue(QStringLiteral("proxy/use"), true);
        QVERIFY2(c.blue() > 180 && c.red() < 80, qPrintable(c.name()));

        ExportSettings s;
        s.path = m_dir.filePath(QStringLiteral("export.mp4"));
        s.encoder = MediaProbe::usableVideoEncoders().last();
        s.videoKbps = 1000;
        QSignalSpy done(&ctl, &ProjectController::exportFinished);
        QString err;
        QVERIFY2(ctl.startExport(s, &err), qPrintable(err));
        QVERIFY(done.wait(60000));
        QVERIFY(done.first().at(0).toBool());
        c = average(MediaProbe::frameAt(s.path, 400000, 160, 90));
        QVERIFY2(c.blue() > 180 && c.red() < 80, qPrintable(c.name()));
        QSettings().remove(QStringLiteral("proxy/auto"));
    }

    void useProxiesMenuAction()
    {
        MainWindow w;
        auto *act = w.findChild<QAction *>(QStringLiteral("actUseProxies"));
        QVERIFY(act && act->isCheckable() && act->isEnabled());
        QVERIFY(act->isChecked());
        act->setChecked(false);
        QVERIFY(!w.controller()->useProxies());
        QVERIFY(!ProjectController::proxiesEnabledSetting());
        act->setChecked(true);
        QVERIFY(w.findChild<QAction *>(QStringLiteral("actCollect")));
        QVERIFY(w.findChild<QAction *>(QStringLiteral("actMediaCache")));
    }

    // --- media cache

    void cacheCanBeCleared()
    {
        ProjectController ctl;
        QCOMPARE(ctl.importFiles({m_big}), 1);
        waitIdle(ctl);
        ctl.waitForProxies();
        const QUuid id = ctl.project().items.first().id;
        QVERIFY(ctl.hasProxy(id));
        QVERIFY(MediaCache::size(MediaCache::Kind::Thumbnail) > 0);
        QVERIFY(MediaCache::size(MediaCache::Kind::Waveform) > 0);
        QVERIFY(MediaCache::size(MediaCache::Kind::Proxy) > 0);

        CacheDialog dlg(&ctl);
        dlg.clearThumbnails();
        dlg.clearWaveforms();
        QCOMPARE(MediaCache::size(MediaCache::Kind::Thumbnail), 0);
        QCOMPARE(MediaCache::size(MediaCache::Kind::Waveform), 0);
        dlg.clearProxies();
        QCOMPARE(MediaCache::size(MediaCache::Kind::Proxy), 0);
        QVERIFY(!ctl.hasProxy(id));
        QVERIFY(!dlg.findChild<QPushButton *>(QStringLiteral("clearProxies"))->isEnabled());
    }

    // --- collect project

    void collectCopiesUsedMedia()
    {
        // Two files with the same name in different folders, one unused file
        // and one missing file.
        QDir().mkpath(m_dir.filePath(QStringLiteral("a")));
        QDir().mkpath(m_dir.filePath(QStringLiteral("b")));
        const QString a = m_dir.filePath(QStringLiteral("a/clip.mp4"));
        const QString b = m_dir.filePath(QStringLiteral("b/clip.mp4"));
        const QString gone = m_dir.filePath(QStringLiteral("gone.mp4"));
        QVERIFY(QFile::copy(m_small, a));
        QVERIFY(QFile::copy(m_small, b));
        QVERIFY(QFile::copy(m_small, gone));

        ProjectController ctl;
        QCOMPARE(ctl.importFiles({a, b, gone, m_big}), 4);
        waitIdle(ctl);
        QList<QUuid> used;
        for (const MediaItem &m : ctl.project().items)
            if (m.path != QFileInfo(m_big).absoluteFilePath())
                used << m.id;
        ctl.placeMedia(used, 0, 0);
        QFile::remove(gone);

        const QString dest = m_dir.filePath(QStringLiteral("collected"));
        QDir().mkpath(dest);
        Collector::Plan plan = Collector::plan(ctl.project(), QStringLiteral("Film"), dest, true);
        QCOMPARE(plan.unusedRemoved, 1); // big.mp4 is not on the timeline
        QCOMPARE(plan.copies.size(), 2);
        QCOMPARE(plan.missing.size(), 1);
        QStringList names;
        for (const auto &c : plan.copies)
            names << QFileInfo(c.to).fileName();
        names.sort();
        QCOMPARE(names, QStringList({QStringLiteral("clip (2).mp4"), QStringLiteral("clip.mp4")}));

        QString err;
        qint64 lastBytes = 0;
        QVERIFY2(Collector::run(plan, &err, [&](qint64 n) { lastBytes = n; }), qPrintable(err));
        QCOMPARE(lastBytes, plan.totalBytes);
        QVERIFY(QFile::exists(plan.projectFile));

        // The collected project opens with its media inside the new folder.
        ProjectController opened;
        QVERIFY2(opened.open(plan.projectFile, &err), qPrintable(err));
        QCOMPARE(opened.project().items.size(), 3);
        QCOMPARE(opened.offlineCount(), 1); // the missing one stays missing
        for (const MediaItem &m : opened.project().items)
            if (opened.isOnline(m.id))
                QVERIFY(m.path.startsWith(QDir::cleanPath(dest + QStringLiteral("/Film/Media/"))));
        QCOMPARE(clipCount(opened.timeline()), clipCount(ctl.timeline()));

        // A second run into the same place is refused, and nothing is touched.
        QVERIFY(!Collector::run(plan, &err));
        QVERIFY(QFile::exists(plan.projectFile));
        // The open project did not change.
        QCOMPARE(ctl.project().items.size(), 4);
    }

    void collectCancelLeavesNothing()
    {
        ProjectController ctl;
        QCOMPARE(ctl.importFiles({m_big}), 1);
        waitIdle(ctl);
        const QString dest = m_dir.filePath(QStringLiteral("cancelled"));
        QDir().mkpath(dest);
        const Collector::Plan plan = Collector::plan(ctl.project(), QStringLiteral("X"), dest, false);
        QCOMPARE(plan.copies.size(), 1);
        std::atomic_bool cancel{true};
        QString err;
        QVERIFY(!Collector::run(plan, &err, {}, &cancel));
        QVERIFY(!QFileInfo::exists(plan.folder));
    }

    void collectDialogRuns()
    {
        ProjectController ctl;
        QCOMPARE(ctl.importFiles({m_small}), 1);
        waitIdle(ctl);
        ctl.placeMedia({ctl.project().items.first().id}, 0, 0);
        const QString dest = m_dir.filePath(QStringLiteral("dialog"));
        QDir().mkpath(dest);
        CollectDialog dlg(&ctl);
        dlg.setDestination(dest);
        auto *start = dlg.findChild<QPushButton *>(QStringLiteral("collectStart"));
        QVERIFY(start->isEnabled());
        QSignalSpy finished(&dlg, &CollectDialog::finished);
        start->click();
        QVERIFY(finished.wait(30000));
        QVERIFY(finished.first().first().toBool());
        QVERIFY(QFile::exists(dlg.collectedProject()));
        // The same destination now exists: starting again is not possible.
        dlg.setDestination(dest);
        QVERIFY(!start->isEnabled());
    }
};

QTEST_MAIN(TestPhase5b)
#include "tst_phase5b.moc"
