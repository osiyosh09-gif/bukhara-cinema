// Phase 5 (part 1) acceptance tests: autosave, crash recovery, previous
// versions and audio waveforms.
#include "app/Autosave.h"
#include "app/MainWindow.h"
#include "app/ProjectController.h"
#include "app/RecoveryDialog.h"
#include "app/SettingsDialog.h"
#include "core/Backups.h"
#include "core/Waveform.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QListWidget>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

using namespace bc;

namespace {

int clipCount(const Timeline &t)
{
    int n = 0;
    for (const TimelineTrack &tr : t.tracks)
        n += int(tr.clips.size());
    return n;
}

bool makeVideo(const QString &path, int seconds = 4)
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

// 1 s of silence, then 1 s of a loud tone (WAV, exact samples).
bool makeSilenceThenTone(const QString &path)
{
    QProcess p;
    p.start(QStringLiteral(BC_FFMPEG_EXE),
            {QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
             QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
             QStringLiteral("aevalsrc='if(lt(t,1),0,0.9*sin(2*PI*440*t))':s=48000:d=2"),
             QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"), path});
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

// Clicks `button` of the first visible top-level dialog with `dialogName`.
void clickLater(const QString &dialogName, const QString &button)
{
    QTimer::singleShot(200, [dialogName, button] {
        for (QWidget *w : QApplication::topLevelWidgets())
            if (w->objectName() == dialogName && w->isVisible())
                if (auto *b = w->findChild<QPushButton *>(button))
                    b->click();
    });
}

} // namespace

class TestPhase5 : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_video;
    QString m_autosaveDir;

    void clearAutosaveDir()
    {
        QDir(m_autosaveDir).removeRecursively();
        QDir().mkpath(m_autosaveDir);
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        QVERIFY(m_dir.isValid());
        m_video = m_dir.filePath(QStringLiteral("tone.mp4"));
        QVERIFY(makeVideo(m_video));
        m_autosaveDir = m_dir.filePath(QStringLiteral("autosave"));
        Autosave::setDirectory(m_autosaveDir);
        // The thumbnail and waveform cache lives in the test cache folder.
        QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).removeRecursively();
    }

    // --- waveforms

    void waveformFollowsTheSound()
    {
        const QString wav = m_dir.filePath(QStringLiteral("silence_tone.wav"));
        QVERIFY(makeSilenceThenTone(wav));
        QByteArray peaks;
        QString err;
        QVERIFY2(Waveform::compute(wav, peaks, &err), qPrintable(err));
        QVERIFY(qAbs(peaks.size() - 2 * Waveform::kPeaksPerSecond) <= 2);
        QCOMPARE(Waveform::peakIn(peaks, 0.1, 0.9), 0);           // silence
        const int loud = Waveform::peakIn(peaks, 1.1, 1.9);
        QVERIFY2(loud >= 220 && loud <= 235, qPrintable(QString::number(loud))); // 0.9 of full scale

        // Round trip through the cache file.
        const QString cache = m_dir.filePath(QStringLiteral("w.bcwf"));
        QVERIFY(Waveform::save(cache, peaks));
        QByteArray back;
        QVERIFY(Waveform::load(cache, back));
        QCOMPARE(back, peaks);
        QVERIFY(!Waveform::compute(m_dir.filePath(QStringLiteral("missing.wav")), peaks));
    }

    void controllerComputesWaveformsInBackground()
    {
        ProjectController ctl;
        QCOMPARE(ctl.importFiles({m_video}), 1);
        waitIdle(ctl);
        const QUuid id = ctl.project().items.first().id;
        QByteArray peaks = ctl.waveform(id);
        QVERIFY(qAbs(peaks.size() - 4 * Waveform::kPeaksPerSecond) <= 10);
        QVERIFY(Waveform::peakIn(peaks, 1, 3) > 10); // ffmpeg's sine is 1/8 of full scale

        // Clips on the timeline draw it (smoke check: painting must not crash).
        ctl.placeMedia({id}, 0, 0);
        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
    }

    // --- previous versions

    void savingKeepsPreviousVersions()
    {
        QSettings().setValue(QStringLiteral("backup/keep"), 3);
        const QString path = m_dir.filePath(QStringLiteral("versions/film.bcproj"));
        QDir().mkpath(QFileInfo(path).absolutePath());
        ProjectController ctl;
        QString err;
        QVERIFY2(ctl.saveAs(path, &err), qPrintable(err));
        QVERIFY(Backups::list(path).isEmpty()); // nothing existed before

        QVERIFY2(ctl.save(&err), qPrintable(err));
        QVERIFY(Backups::list(path).isEmpty()); // unchanged save: no new version

        for (int i = 1; i <= 5; ++i) {
            ctl.addFolder(QStringLiteral("Folder %1").arg(i));
            QVERIFY2(ctl.save(&err), qPrintable(err));
            QTest::qWait(i == 1 ? 1100 : 0); // different time stamps for the first ones
        }
        const QList<Backups::Version> versions = Backups::list(path);
        QCOMPARE(versions.size(), 3); // keep = 3
        QVERIFY(versions.first().path.startsWith(Backups::folderFor(path)));
        QVERIFY(versions[0].time >= versions[1].time);

        // The newest version has 4 folders (state before the last save).
        QVERIFY2(ctl.openCopy(versions.first().path, path, &err), qPrintable(err));
        QCOMPARE(ctl.project().folders.size(), 4);
        QCOMPARE(ctl.filePath(), QFileInfo(path).absoluteFilePath());
        QVERIFY(ctl.isModified());

        VersionsDialog dlg(path);
        QCOMPARE(dlg.findChild<QListWidget *>(QStringLiteral("versionsList"))->count(), 3);
        QSettings().remove(QStringLiteral("backup/keep"));
    }

    void versionsCanBeSwitchedOff()
    {
        QSettings().setValue(QStringLiteral("backup/keep"), 0);
        const QString path = m_dir.filePath(QStringLiteral("nobackup/a.bcproj"));
        QDir().mkpath(QFileInfo(path).absolutePath());
        ProjectController ctl;
        QString err;
        QVERIFY(ctl.saveAs(path, &err));
        ctl.addFolder(QStringLiteral("x"));
        QVERIFY(ctl.save(&err));
        QVERIFY(!QFileInfo::exists(Backups::folderFor(path)));
        QSettings().remove(QStringLiteral("backup/keep"));
    }

    // --- autosave and recovery

    void autosaveWritesOnlyChanges()
    {
        clearAutosaveDir();
        ProjectController ctl;
        Autosave as(&ctl);
        QCOMPARE(as.intervalMinutes(), 2); // default
        QVERIFY(as.saveNow());
        QVERIFY(!as.lastSaved().isValid()); // nothing changed yet

        QCOMPARE(ctl.importFiles({m_video}), 1);
        waitIdle(ctl);
        QSignalSpy saved(&as, &Autosave::saved);
        QVERIFY(as.saveNow());
        QCOMPARE(saved.count(), 1);
        QVERIFY(as.saveNow()); // no change since: no new write
        QCOMPARE(saved.count(), 1);
        QCOMPARE(QDir(m_autosaveDir).entryList({QStringLiteral("*.bcproj")}, QDir::Files).size(), 1);

        // This program is running: its copy is not offered for recovery.
        QVERIFY(Autosave::orphaned().isEmpty());

        // Saving the project makes the copy unnecessary.
        QString err;
        QDir().mkpath(m_dir.filePath(QStringLiteral("auto")));
        QVERIFY2(ctl.saveAs(m_dir.filePath(QStringLiteral("auto/p.bcproj")), &err), qPrintable(err));
        QVERIFY(QDir(m_autosaveDir).entryList({QStringLiteral("*.bcproj")}, QDir::Files).isEmpty());
    }

    void timerAutosaves()
    {
        clearAutosaveDir();
        ProjectController ctl;
        Autosave as(&ctl);
        as.setIntervalMinutes(0);
        QCOMPARE(Autosave::intervalSetting(), 0);
        as.setIntervalMinutes(1);
        QCOMPARE(Autosave::intervalSetting(), 1);
        as.setIntervalMinutes(2);
        ctl.addFolder(QStringLiteral("changed"));
        // The timer itself is minutes long; its slot is the same saveNow().
        QSignalSpy saved(&as, &Autosave::saved);
        QMetaObject::invokeMethod(&as, &Autosave::saveNow);
        QCOMPARE(saved.count(), 1);
    }

    void crashLeavesARecoverableCopy()
    {
        clearAutosaveDir();
        const QString projectPath = m_dir.filePath(QStringLiteral("crash/movie.bcproj"));
        QDir().mkpath(QFileInfo(projectPath).absolutePath());
        QString err;
        {
            ProjectController ctl;
            QVERIFY(ctl.importFiles({m_video}) == 1);
            waitIdle(ctl);
            QVERIFY(ctl.saveAs(projectPath, &err));
            ctl.placeMedia({ctl.project().items.first().id}, 0, 0);
            ctl.addFolder(QStringLiteral("after save"));
            Autosave as(&ctl);
            QVERIFY(as.saveNow());
            // Simulate a crash: the copy stays, the program's lock is gone.
            for (const QString &ext : {QStringLiteral("bcproj"), QStringLiteral("json")}) {
                const QStringList f = QDir(m_autosaveDir).entryList({QStringLiteral("*.") + ext}, QDir::Files);
                QCOMPARE(f.size(), 1);
                QVERIFY(QFile::copy(m_autosaveDir + QLatin1Char('/') + f.first(),
                                    m_autosaveDir + QStringLiteral("/crashed.") + ext));
            }
        }
        const QList<Autosave::Entry> entries = Autosave::orphaned();
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().originalPath, QFileInfo(projectPath).absoluteFilePath());
        QCOMPARE(entries.first().name, QStringLiteral("movie"));
        QVERIFY(entries.first().savedAt.isValid());

        // Start-up: the dialog offers the copy; Recover opens it.
        MainWindow w;
        w.show();
        clickLater(QStringLiteral("recoveryDialog"), QStringLiteral("recoverButton"));
        QVERIFY(w.checkRecovery());
        ProjectController *ctl = w.controller();
        QCOMPARE(ctl->filePath(), QFileInfo(projectPath).absoluteFilePath());
        QVERIFY(ctl->isModified());
        QCOMPARE(ctl->project().folders.size(), 1);
        QCOMPARE(clipCount(ctl->timeline()), 2); // video + audio
        QVERIFY(ctl->isOnline(ctl->project().items.first().id));
        QVERIFY(Autosave::orphaned().isEmpty()); // handled

        // Saving writes the recovered work to the original file.
        QVERIFY(ctl->save(&err));
        ProjectController reopened;
        QVERIFY(reopened.open(projectPath, &err));
        QCOMPARE(clipCount(reopened.timeline()), 2);
    }

    void recoveryCopiesCanBeDeleted()
    {
        clearAutosaveDir();
        QFile data(m_autosaveDir + QStringLiteral("/old.bcproj"));
        QVERIFY(data.open(QIODevice::WriteOnly));
        data.write(ProjectController().project().toJson(data.fileName()));
        data.close();
        QFile meta(m_autosaveDir + QStringLiteral("/old.json"));
        QVERIFY(meta.open(QIODevice::WriteOnly));
        meta.write(R"({"original":"","name":"Untitled","savedAt":"2026-11-01T10:00:00.000"})");
        meta.close();
        const QList<Autosave::Entry> entries = Autosave::orphaned();
        QCOMPARE(entries.size(), 1);
        QVERIFY(entries.first().originalPath.isEmpty());

        RecoveryDialog dlg(entries);
        QCOMPARE(dlg.findChild<QListWidget *>(QStringLiteral("recoveryList"))->count(), 1);
        Autosave::discard(entries.first());
        QVERIFY(Autosave::orphaned().isEmpty());
        QVERIFY(QDir(m_autosaveDir).entryList(QDir::Files).isEmpty());
    }

    void settingsDialogStoresAutosaveAndVersions()
    {
        SettingsDialog dlg;
        auto *combo = dlg.findChild<QComboBox *>(QStringLiteral("autosaveCombo"));
        auto *spin = dlg.findChild<QSpinBox *>(QStringLiteral("backupSpin"));
        QVERIFY(combo && spin);
        combo->setCurrentIndex(combo->findData(5));
        spin->setValue(7);
        dlg.apply();
        QCOMPARE(Autosave::intervalSetting(), 5);
        QCOMPARE(QSettings().value(QStringLiteral("backup/keep")).toInt(), 7);
        combo->setCurrentIndex(combo->findData(0));
        dlg.apply();
        QCOMPARE(Autosave::intervalSetting(), 0);
        QSettings().remove(QStringLiteral("autosave/minutes"));
        QSettings().remove(QStringLiteral("backup/keep"));
    }

    void previousVersionsMenu()
    {
        MainWindow w;
        auto *act = w.findChild<QAction *>(QStringLiteral("actVersions"));
        QVERIFY(act);
        QVERIFY(!act->isEnabled()); // untitled project has no versions
        QString err;
        QDir().mkpath(m_dir.filePath(QStringLiteral("menu")));
        QVERIFY2(w.controller()->saveAs(m_dir.filePath(QStringLiteral("menu/x.bcproj")), &err), qPrintable(err));
        QVERIFY(act->isEnabled());
    }
};

QTEST_MAIN(TestPhase5)
#include "tst_phase5.moc"
