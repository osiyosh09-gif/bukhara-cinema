// Phase 1 acceptance tests: project file, media probing, media bin, relinking.
#include "app/Language.h"
#include "app/MainWindow.h"
#include "app/MediaBinPanel.h"
#include "app/ProjectController.h"
#include "core/MediaProbe.h"
#include "core/Project.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using namespace bc;

namespace {

// Generates small test media with the ffmpeg command line tool using only
// LGPL encoders (mpeg4 video, aac audio).
bool makeVideo(const QString &path, int seconds = 2)
{
    QProcess p;
    p.start(QStringLiteral(BC_FFMPEG_EXE),
            {QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
             QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
             QStringLiteral("testsrc=size=320x240:rate=25:duration=%1").arg(seconds),
             QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
             QStringLiteral("sine=frequency=440:sample_rate=48000:duration=%1").arg(seconds),
             QStringLiteral("-c:v"), QStringLiteral("mpeg4"), QStringLiteral("-c:a"), QStringLiteral("aac"),
             QStringLiteral("-shortest"), path});
    return p.waitForFinished(60000) && p.exitCode() == 0 && QFile::exists(path);
}

bool makeAudio(const QString &path)
{
    QProcess p;
    p.start(QStringLiteral(BC_FFMPEG_EXE),
            {QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
             QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
             QStringLiteral("sine=frequency=220:sample_rate=44100:duration=3"),
             QStringLiteral("-ac"), QStringLiteral("1"), path});
    return p.waitForFinished(60000) && p.exitCode() == 0 && QFile::exists(path);
}

bool makeImage(const QString &path)
{
    QImage img(400, 300, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    return img.save(path);
}

void waitForInfo(ProjectController &ctl)
{
    ctl.waitForBackgroundWork();
    QTRY_VERIFY_WITH_TIMEOUT(
        [&] {
            for (const MediaItem &m : ctl.project().items)
                if (ctl.isProbing(m.id))
                    return false;
            return true;
        }(),
        20000);
    ctl.waitForBackgroundWork();
}

} // namespace

class TestPhase1 : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_video, m_audio, m_image;

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QApplication::setOrganizationName(QStringLiteral("BUKHARA CINEMA Tests"));
        QApplication::setApplicationName(QStringLiteral("tst_phase1"));
        QSettings().clear();
        QVERIFY(m_dir.isValid());
        QDir().mkpath(m_dir.filePath(QStringLiteral("media/sub")));
        m_video = m_dir.filePath(QStringLiteral("media/clip.mp4"));
        m_audio = m_dir.filePath(QStringLiteral("media/sub/voice.wav"));
        m_image = m_dir.filePath(QStringLiteral("media/logo.png"));
        QVERIFY2(makeVideo(m_video), "ffmpeg could not create the test video");
        QVERIFY2(makeAudio(m_audio), "ffmpeg could not create the test audio");
        QVERIFY(makeImage(m_image));
        Language::instance().setLanguage(QStringLiteral("uz_Cyrl"), false);
    }

    void probeVideo()
    {
        MediaInfo i;
        QString err;
        QVERIFY2(MediaProbe::probe(m_video, i, &err), qPrintable(err));
        QCOMPARE(i.kind, MediaInfo::Kind::Video);
        QCOMPARE(i.width, 320);
        QCOMPARE(i.height, 240);
        QCOMPARE(i.fpsNum, 25);
        QCOMPARE(i.fpsDen, 1);
        QVERIFY2(qAbs(i.durationUs - 2000000) < 150000, qPrintable(QString::number(i.durationUs)));
        QCOMPARE(i.videoCodec, QStringLiteral("mpeg4"));
        QCOMPARE(i.audioCodec, QStringLiteral("aac"));
        QCOMPARE(i.sampleRate, 48000);
        QVERIFY(i.fileSize > 0);

        const QImage thumb = MediaProbe::thumbnail(m_video, 160, 90);
        QVERIFY(!thumb.isNull());
        QCOMPARE(thumb.height(), 90);
        QCOMPARE(thumb.width(), 120); // 4:3 fitted into 160x90
    }

    void probeAudioAndImage()
    {
        MediaInfo a;
        QVERIFY(MediaProbe::probe(m_audio, a, nullptr));
        QCOMPARE(a.kind, MediaInfo::Kind::Audio);
        QCOMPARE(a.channels, 1);
        QCOMPARE(a.sampleRate, 44100);
        QVERIFY(qAbs(a.durationUs - 3000000) < 100000);
        QVERIFY(MediaProbe::thumbnail(m_audio, 160, 90).isNull());

        MediaInfo img;
        QVERIFY(MediaProbe::probe(m_image, img, nullptr));
        QCOMPARE(img.kind, MediaInfo::Kind::Image);
        QCOMPARE(img.width, 400);
        QCOMPARE(img.height, 300);
        QVERIFY(!MediaProbe::thumbnail(m_image, 160, 90).isNull());
    }

    void probeRejectsBadFiles()
    {
        const QString bad = m_dir.filePath(QStringLiteral("broken.mp4"));
        QFile f(bad);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("this is not a video");
        f.close();
        MediaInfo i;
        QString err;
        QVERIFY(!MediaProbe::probe(bad, i, &err));
        QVERIFY(!err.isEmpty());
        QVERIFY(!MediaProbe::probe(m_dir.filePath(QStringLiteral("missing.mp4")), i, &err));
    }

    void ffmpegIsLgpl()
    {
        qInfo() << MediaProbe::ffmpegVersionString() << MediaProbe::ffmpegLicense();
#ifdef BC_REQUIRE_LGPL_FFMPEG
        // The shipped Windows build must never contain GPL-only FFmpeg parts.
        QVERIFY2(MediaProbe::ffmpegLicense().contains(QStringLiteral("LGPL")),
                 qPrintable(MediaProbe::ffmpegLicense()));
#endif
    }

    void projectJsonRoundTrip()
    {
        Project p;
        p.sequence.width = 1080;
        p.sequence.height = 1920;
        p.sequence.fpsNum = 30000;
        p.sequence.fpsDen = 1001;
        p.sequence.channels = 1;
        const QUuid folder = p.addFolder(QStringLiteral("Интервью")).id;
        const QUuid sub = p.addFolder(QStringLiteral("B-roll"), folder).id;
        MediaItem &m = p.addItem(m_video, sub);
        m.info.kind = MediaInfo::Kind::Video;
        m.info.durationUs = 2000000;
        m.probed = true;
        const QUuid itemId = m.id;

        const QString file = m_dir.filePath(QStringLiteral("roundtrip.bcproj"));
        QString err;
        QVERIFY2(p.save(file, &err), qPrintable(err));

        Project q;
        QVERIFY2(Project::load(file, q, &err), qPrintable(err));
        QCOMPARE(q.sequence, p.sequence);
        QCOMPARE(q.folders.size(), 2);
        QCOMPARE(q.folder(sub)->parentId, folder);
        QCOMPARE(q.folder(folder)->name, QStringLiteral("Интервью"));
        QCOMPARE(q.items.size(), 1);
        QCOMPARE(q.item(itemId)->folderId, sub);
        QCOMPARE(q.item(itemId)->info.durationUs, qint64(2000000));
        QVERIFY(q.item(itemId)->probed);

        // Removing a folder removes its sub-folders and their media.
        q.removeFolder(folder);
        QVERIFY(q.folders.isEmpty());
        QVERIFY(q.items.isEmpty());
    }

    void rejectsForeignFiles()
    {
        Project p;
        QString err;
        QVERIFY(!Project::fromJson("{\"hello\":1}", {}, p, &err));
        QVERIFY(!Project::fromJson("not json", {}, p, &err));
        QVERIFY(!Project::fromJson("{\"format\":\"BUKHARA CINEMA project\",\"version\":99}", {}, p, &err));
    }

    void movedProjectFolderStillFindsMedia()
    {
        QTemporaryDir a, b;
        QDir().mkpath(a.filePath(QStringLiteral("footage")));
        QVERIFY(QFile::copy(m_image, a.filePath(QStringLiteral("footage/pic.png"))));
        Project p;
        p.addItem(a.filePath(QStringLiteral("footage/pic.png")));
        QVERIFY(p.save(a.filePath(QStringLiteral("film.bcproj")), nullptr));

        // "Move" the whole project folder elsewhere.
        QDir().mkpath(b.filePath(QStringLiteral("moved/footage")));
        QVERIFY(QFile::copy(a.filePath(QStringLiteral("film.bcproj")), b.filePath(QStringLiteral("moved/film.bcproj"))));
        QVERIFY(QFile::copy(a.filePath(QStringLiteral("footage/pic.png")), b.filePath(QStringLiteral("moved/footage/pic.png"))));
        QVERIFY(QFile::remove(a.filePath(QStringLiteral("footage/pic.png"))));

        Project q;
        QVERIFY(Project::load(b.filePath(QStringLiteral("moved/film.bcproj")), q, nullptr));
        QCOMPARE(q.items.first().path, QDir::cleanPath(b.filePath(QStringLiteral("moved/footage/pic.png"))));
    }

    void controllerImportSaveOpen()
    {
        ProjectController ctl;
        QSignalSpy modified(&ctl, &ProjectController::modifiedChanged);
        // A folder is scanned recursively; unsupported files are ignored.
        QFile junk(m_dir.filePath(QStringLiteral("media/notes.txt")));
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.close();
        QCOMPARE(ctl.importFiles({m_dir.filePath(QStringLiteral("media"))}), 3);
        QVERIFY(ctl.isModified());
        QCOMPARE(modified.count(), 1);
        // Duplicates are skipped.
        QCOMPARE(ctl.importFiles({m_video}), 0);

        waitForInfo(ctl);
        for (const MediaItem &m : ctl.project().items) {
            QVERIFY2(m.probed, qPrintable(m.path));
            QVERIFY(ctl.isOnline(m.id));
            if (m.info.kind != MediaInfo::Kind::Audio)
                QTRY_VERIFY_WITH_TIMEOUT(!ctl.thumbnail(m.id).isNull(), 10000);
        }

        const QString file = m_dir.filePath(QStringLiteral("film.bcproj"));
        QVERIFY(ctl.saveAs(m_dir.filePath(QStringLiteral("film")), nullptr)); // extension added
        QCOMPARE(ctl.filePath(), QFileInfo(file).absoluteFilePath());
        QVERIFY(!ctl.isModified());
        QVERIFY(ProjectController::recentProjects().contains(ctl.filePath()));

        ProjectController ctl2;
        QString err;
        QVERIFY2(ctl2.open(file, &err), qPrintable(err));
        QCOMPARE(ctl2.project().items.size(), 3);
        QCOMPARE(ctl2.offlineCount(), 0);
        QVERIFY(!ctl2.isModified());
        QCOMPARE(ctl2.displayName(), QStringLiteral("film"));
    }

    void missingFilesAndRelink()
    {
        QTemporaryDir work;
        QDir().mkpath(work.filePath(QStringLiteral("old")));
        QVERIFY(QFile::copy(m_video, work.filePath(QStringLiteral("old/a.mp4"))));
        QVERIFY(QFile::copy(m_image, work.filePath(QStringLiteral("old/b.png"))));

        ProjectController ctl;
        QCOMPARE(ctl.importFiles({work.filePath(QStringLiteral("old"))}), 2);
        waitForInfo(ctl);
        const QString proj = work.filePath(QStringLiteral("p.bcproj"));
        QVERIFY(ctl.saveAs(proj, nullptr));

        // The media folder is renamed outside the program.
        QVERIFY(QDir(work.path()).rename(QStringLiteral("old"), QStringLiteral("new")));

        ProjectController ctl2;
        QVERIFY(ctl2.open(proj, nullptr));
        QCOMPARE(ctl2.offlineCount(), 2);

        QUuid videoId;
        for (const MediaItem &m : ctl2.project().items)
            if (m.path.endsWith(QStringLiteral("a.mp4")))
                videoId = m.id;
        QVERIFY(!videoId.isNull());

        // A file of a different kind is reported as a mismatch.
        QVERIFY(!ctl2.relinkMismatch(videoId, work.filePath(QStringLiteral("new/b.png"))).isEmpty());
        QVERIFY(ctl2.relinkMismatch(videoId, work.filePath(QStringLiteral("new/a.mp4"))).isEmpty());

        int others = -1;
        QString err;
        QVERIFY2(ctl2.relink(videoId, work.filePath(QStringLiteral("new/a.mp4")), &others, &err), qPrintable(err));
        QCOMPARE(others, 1);              // b.png found automatically in the same folder
        QCOMPARE(ctl2.offlineCount(), 0);
        QVERIFY(ctl2.isModified());
        waitForInfo(ctl2);
    }

    void relinkFromFolder()
    {
        QTemporaryDir work;
        QVERIFY(QFile::copy(m_image, work.filePath(QStringLiteral("x.png"))));
        ProjectController ctl;
        ctl.importFiles({work.filePath(QStringLiteral("x.png"))});
        waitForInfo(ctl);
        QVERIFY(ctl.saveAs(work.filePath(QStringLiteral("p.bcproj")), nullptr));
        QDir().mkpath(work.filePath(QStringLiteral("archive/2024")));
        QVERIFY(QFile::rename(work.filePath(QStringLiteral("x.png")), work.filePath(QStringLiteral("archive/2024/x.png"))));

        ProjectController ctl2;
        QVERIFY(ctl2.open(work.filePath(QStringLiteral("p.bcproj")), nullptr));
        QCOMPARE(ctl2.offlineCount(), 1);
        QCOMPARE(ctl2.relinkFromFolder(work.filePath(QStringLiteral("archive"))), 1); // searched recursively
        QCOMPARE(ctl2.offlineCount(), 0);
        waitForInfo(ctl2);
    }

    void foldersMoveAndRename()
    {
        ProjectController ctl;
        const QUuid a = ctl.addFolder(QStringLiteral("A"));
        const QUuid b = ctl.addFolder(QStringLiteral("B"), a);
        ctl.moveFolder(a, b); // into its own child: refused
        QCOMPARE(ctl.project().folder(a)->parentId, QUuid());
        ctl.renameFolder(b, QStringLiteral("  Б-ролл "));
        QCOMPARE(ctl.project().folder(b)->name, QStringLiteral("Б-ролл"));
        ctl.importFiles({m_image});
        const QUuid item = ctl.project().items.first().id;
        ctl.moveItems({item}, b);
        QCOMPARE(ctl.project().item(item)->folderId, b);
        waitForInfo(ctl);
    }

    void sequenceSettingsChangeMarksModified()
    {
        ProjectController ctl;
        SequenceSettings s = ctl.project().sequence;
        s.fpsNum = 30;
        ctl.setSequence(s);
        QVERIFY(ctl.isModified());
        QCOMPARE(ctl.project().sequence.fpsNum, 30);
    }

    void mainWindowActionsWork()
    {
        MainWindow w;
        for (const char *name : {"actNewProject", "actOpenProject", "actSave", "actSaveAs", "actImport",
                                 "actSequenceSettings"}) {
            auto *a = w.findChild<QAction *>(QString::fromLatin1(name));
            QVERIFY2(a, name);
            QVERIFY2(a->isEnabled(), name);
            QVERIFY2(!a->property("bc_planned").toBool(), name);
        }
        // Export is off while the timeline is empty.
        auto *exportAct = w.findChild<QAction *>(QStringLiteral("actExport"));
        QVERIFY(exportAct && !exportAct->isEnabled());

        // The bin shows imported files.
        w.controller()->importFiles({m_video, m_image});
        auto *bin = w.findChild<MediaBinPanel *>();
        QVERIFY(bin);
        QCOMPARE(bin->tree()->topLevelItemCount(), 2);
        waitForInfo(*w.controller());
        QVERIFY(w.windowTitle().contains(QLatin1Char('*')));
        w.controller()->newProject(); // discard without asking (no dialog in tests)
        QCOMPARE(bin->tree()->topLevelItemCount(), 0);
    }
};

QTEST_MAIN(TestPhase1)
#include "tst_phase1.moc"
