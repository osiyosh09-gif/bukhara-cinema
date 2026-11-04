// Phase 11 (part 1) acceptance tests: subtitles, .srt files, drawing into
// the picture (preview and export) and the Subtitles panel.
#include "app/MainWindow.h"
#include "app/ProjectController.h"
#include "app/SubtitlesPanel.h"
#include "core/MediaCache.h"
#include "core/MediaProbe.h"
#include "core/Subtitles.h"
#include "engine/Exporter.h"
#include "engine/TimelinePlayer.h"

#include <QCheckBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QUndoStack>
#include <QtTest>

using namespace bc;
using Kind = Timeline::Kind;

namespace {

bool makeBlackVideo(const QString &path)
{
    QProcess p;
    p.start(QStringLiteral(BC_FFMPEG_EXE),
            {QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-f"),
             QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("color=c=black:s=320x240:r=25:d=3"),
             QStringLiteral("-c:v"), QStringLiteral("mpeg4"), path});
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

// Bright pixels in the lower part of a picture (white subtitle text on black).
int brightBelow(const QImage &img, double fromY = 0.5)
{
    const QImage rgb = img.convertToFormat(QImage::Format_RGB32);
    int n = 0;
    for (int y = int(rgb.height() * fromY); y < rgb.height(); ++y)
        for (int x = 0; x < rgb.width(); ++x)
            if (qGray(rgb.pixel(x, y)) > 180)
                ++n;
    return n;
}

const char *kSample = "\xEF\xBB\xBF"
                      "1\r\n00:00:01,000 --> 00:00:02,500\r\nSalom, <i>dunyo</i>!\r\n\r\n"
                      "2\r\n00:00:03,200 --> 00:00:05,000 X1:10 X2:20\r\n{\\an8}Ikkinchi\r\nqator\r\n\r\n"
                      "3\r\nbroken line\r\n\r\n"
                      "00:00:06,000 --> 00:00:07,000\r\nRaqamsiz\r\n";

} // namespace

class TestPhase11a : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_video;

    void setUp(ProjectController &ctl)
    {
        SequenceSettings seq = ctl.project().sequence;
        seq.width = 320;
        seq.height = 240;
        ctl.setSequence(seq);
        QCOMPARE(ctl.importFiles({m_video}), 1);
        waitIdle(ctl);
        QVERIFY(!ctl.placeMedia({ctl.project().items.first().id}, 0, 0).isEmpty());
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("BukharaCinemaTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_phase11a"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        QSettings().setValue(QStringLiteral("proxy/auto"), false);
        QDir(MediaCache::root()).removeRecursively();
        QVERIFY(m_dir.isValid());
        m_video = m_dir.filePath(QStringLiteral("black.mp4"));
        QVERIFY2(makeBlackVideo(m_video), "ffmpeg could not create test media");
    }

    void srtFiles()
    {
        int skipped = 0;
        const QList<Srt::Cue> cues = Srt::parse(Srt::decode(QByteArray(kSample)), &skipped);
        QCOMPARE(cues.size(), 3);
        QCOMPARE(skipped, 1);
        QCOMPARE(cues[0].startMs, 1000);
        QCOMPARE(cues[0].endMs, 2500);
        QCOMPARE(cues[0].text, QStringLiteral("Salom, dunyo!"));
        QCOMPARE(cues[1].endMs, 5000);
        QCOMPARE(cues[1].text, QStringLiteral("Ikkinchi\nqator"));
        QCOMPARE(cues[2].text, QStringLiteral("Raqamsiz"));

        // Writing and reading back gives the same cues.
        const QByteArray out = Srt::write(cues);
        QVERIFY(out.startsWith("1\r\n00:00:01,000 --> 00:00:02,500\r\n"));
        QCOMPARE(Srt::parse(Srt::decode(out)), cues);

        QCOMPARE(Srt::formatTime(3723456), QStringLiteral("01:02:03,456"));
        bool ok = false;
        QCOMPARE(Srt::parseTime(QStringLiteral("00:00:01.5"), &ok), 1500);
        QVERIFY(ok);
        Srt::parseTime(QStringLiteral("1:2"), &ok);
        QVERIFY(!ok);

        // Encodings: Windows-1251 ("Салом") and UTF-16 with BOM.
        QCOMPARE(Srt::decode(QByteArray("\xD1\xE0\xEB\xEE\xEC")), QStringLiteral("Салом"));
        QByteArray u16("\xFF\xFE");
        u16.append(reinterpret_cast<const char *>(u"Ўзбек"), 10);
        QCOMPARE(Srt::decode(u16), QStringLiteral("Ўзбек"));
        QCOMPARE(Srt::decode(QStringLiteral("Ўзбек").toUtf8()), QStringLiteral("Ўзбек"));

        // Frames <-> milliseconds.
        const QList<Subtitle> subs = Srt::toSubtitles(cues, 25);
        QCOMPARE(subs[0].start, 25);
        QCOMPARE(subs[0].length, 38); // 1.0 s .. 2.52 s rounds to frame 63
        QCOMPARE(Srt::fromSubtitles(subs, 25)[0].startMs, 1000);
    }

    void model()
    {
        Timeline t = Timeline::makeDefault();
        t.subtitles = Srt::toSubtitles(Srt::parse(QString::fromLatin1(kSample)), 25);
        t.subtitleStyle.size = 70;
        t.subtitleStyle.box = false;
        QVERIFY(t.subtitleAt(30));
        QCOMPARE(t.subtitleAt(30)->text, QStringLiteral("Salom, dunyo!"));
        QVERIFY(!t.subtitleAt(70));
        // Insert edits on all tracks move later subtitles too.
        t.insertSpace(50, 25);
        QCOMPARE(t.subtitles[0].start, 25);
        QCOMPARE(t.subtitles[1].start, 80 + 25);
        // Frame rate change keeps the times.
        Timeline r = t;
        r.rescale(25, 50);
        QCOMPARE(r.subtitles[0].start, 50);
        QCOMPARE(r.subtitles[0].length, 76);

        Project p;
        p.timeline = t;
        const QString file = m_dir.filePath(QStringLiteral("subs.bcproj"));
        QString error;
        QVERIFY2(p.save(file, &error), qPrintable(error));
        Project back;
        QVERIFY2(Project::load(file, back, &error), qPrintable(error));
        QCOMPARE(back.timeline.subtitles, t.subtitles);
        QCOMPARE(back.timeline.subtitleStyle, t.subtitleStyle);
        QFile f(file);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("version")).toInt() >= 12);
    }

    void controller()
    {
        ProjectController ctl;
        const QUuid later = ctl.addSubtitle(60, QStringLiteral("B"));
        const QUuid first = ctl.addSubtitle(50);
        QCOMPARE(ctl.timeline().subtitles.size(), qsizetype(2));
        QCOMPARE(ctl.timeline().subtitles[0].id, first);
        QCOMPARE(ctl.timeline().subtitle(first)->length, 10); // stops where the next one starts
        QCOMPARE(ctl.timeline().subtitle(later)->length, 75); // 3 s at 25 fps

        // Typing is one undo step.
        const int steps = ctl.undoStack()->count();
        QVERIFY(ctl.setSubtitleText(first, QStringLiteral("S")));
        QVERIFY(ctl.setSubtitleText(first, QStringLiteral("Sa")));
        QVERIFY(ctl.setSubtitleText(first, QStringLiteral("Salom")));
        QCOMPARE(ctl.undoStack()->count(), steps + 1);
        QVERIFY(ctl.setSubtitleTiming(first, 200, 30));
        QCOMPARE(ctl.timeline().subtitles.last().id, first); // sorted again
        SubtitleStyle st;
        st.size = 999;
        QVERIFY(ctl.setSubtitleStyle(st));
        QCOMPARE(ctl.timeline().subtitleStyle.size, SubtitleStyle::kMaxSize);
        QCOMPARE(ctl.removeSubtitles({later}), 1);
        ctl.undoStack()->undo();
        QVERIFY(ctl.timeline().subtitle(later));

        // .srt import (add / replace, offset) and export.
        const QString in = m_dir.filePath(QStringLiteral("in.srt"));
        {
            QFile f(in);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(kSample);
        }
        QString error;
        int skipped = 0;
        QCOMPARE(ctl.importSrt(in, 0, false, &error, &skipped), 3);
        QCOMPARE(skipped, 1);
        QCOMPARE(ctl.timeline().subtitles.size(), qsizetype(5));
        QCOMPARE(ctl.undoStack()->undoText(), tr("Import subtitles"));
        QCOMPARE(ctl.importSrt(in, 100, true, &error), 3);
        QCOMPARE(ctl.timeline().subtitles.size(), qsizetype(3));
        QCOMPARE(ctl.timeline().subtitles[0].start, 125);
        const QString out = m_dir.filePath(QStringLiteral("out.srt"));
        QVERIFY2(ctl.exportSrt(out, &error), qPrintable(error));
        QFile f(out);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QList<Srt::Cue> back = Srt::parse(Srt::decode(f.readAll()));
        QCOMPARE(back.size(), 3);
        QCOMPARE(back[0].startMs, 5000);
        QCOMPARE(back[1].text, QStringLiteral("Ikkinchi\nqator"));
        QCOMPARE(ctl.importSrt(m_dir.filePath(QStringLiteral("none.srt")), 0, false, &error), 0);
        QVERIFY(!error.isEmpty());
    }

    void preview()
    {
        ProjectController ctl;
        QVERIFY2(ctl.canPlay(), qPrintable(ctl.engineError()));
        setUp(ctl);
        ctl.addSubtitle(10, QStringLiteral("SALOM DUNYO"));
        TimelinePlayer player;
        player.setProject(ctl.project());
        QVERIFY(brightBelow(player.renderFrame(20)) > 100);
        QCOMPARE(brightBelow(player.renderFrame(5)), 0);  // before it
        QCOMPARE(brightBelow(player.renderFrame(0), 0.0), 0);
        // Higher up with a bigger bottom margin.
        SubtitleStyle st = ctl.timeline().subtitleStyle;
        st.margin = 40;
        ctl.setSubtitleStyle(st);
        player.setProject(ctl.project());
        QVERIFY(brightBelow(player.renderFrame(20), 0.7) < 5);
        QVERIFY(brightBelow(player.renderFrame(20), 0.3) > 100);
        // Hidden: nothing drawn.
        st.show = false;
        ctl.setSubtitleStyle(st);
        player.setProject(ctl.project());
        QCOMPARE(brightBelow(player.renderFrame(20), 0.0), 0);
    }

    void exportBurnAndSrt()
    {
        ProjectController ctl;
        QVERIFY2(ctl.canPlay(), qPrintable(ctl.engineError()));
        setUp(ctl);
        ctl.addSubtitle(0, QStringLiteral("SALOM DUNYO"));
        for (bool burn : {true, false}) {
            ExportSettings s;
            s.path = m_dir.filePath(burn ? QStringLiteral("burn.mp4") : QStringLiteral("clean.mp4"));
            s.encoder = MediaProbe::usableVideoEncoders().first();
            s.videoKbps = 1500;
            s.burnSubtitles = burn;
            s.writeSrt = !burn;
            QSignalSpy done(&ctl, &ProjectController::exportFinished);
            QString error;
            QVERIFY2(ctl.startExport(s, &error), qPrintable(error));
            QVERIFY(done.wait(120000));
            QVERIFY2(done.first().at(0).toBool(), qPrintable(done.first().at(1).toString()));
            const QImage frame = MediaProbe::frameAt(s.path, 1000000, 320, 240);
            QVERIFY(!frame.isNull());
            if (burn)
                QVERIFY(brightBelow(frame) > 50);
            else
                QVERIFY(brightBelow(frame) < 5);
            QCOMPARE(QFile::exists(m_dir.filePath(burn ? QStringLiteral("burn.srt") : QStringLiteral("clean.srt"))), !burn);
        }
    }

    void panel()
    {
        MainWindow w;
        ProjectController *ctl = w.controller();
        QVERIFY(ctl);
        auto *panel = w.findChild<SubtitlesPanel *>(QStringLiteral("subtitlesPanel"));
        QVERIFY(panel);
        auto *list = panel->findChild<QTreeWidget *>(QStringLiteral("subtitleList"));
        auto *text = panel->findChild<QPlainTextEdit *>(QStringLiteral("subtitleText"));
        auto *show = panel->findChild<QCheckBox *>(QStringLiteral("subtitleShow"));
        QVERIFY(list && text && show);
        QCOMPARE(list->topLevelItemCount(), 0);

        auto *add = w.findChild<QAction *>(QStringLiteral("actAddSubtitle"));
        QVERIFY(add && add->isEnabled());
        add->trigger();
        QCOMPARE(list->topLevelItemCount(), 1);
        QCOMPARE(w.findChild<QTabWidget *>(QStringLiteral("rightTabs"))->currentWidget(), panel);
        QVERIFY(!panel->current().isNull());
        text->setPlainText(QStringLiteral("Xush kelibsiz"));
        QCOMPARE(ctl->timeline().subtitle(panel->current())->text, QStringLiteral("Xush kelibsiz"));
        QCOMPARE(list->topLevelItem(0)->text(1), QStringLiteral("Xush kelibsiz"));
        show->setChecked(false);
        QVERIFY(!ctl->timeline().subtitleStyle.show);

        const QString in = m_dir.filePath(QStringLiteral("panel.srt"));
        {
            QFile f(in);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(kSample);
        }
        QCOMPARE(panel->importFile(in), 3);
        QCOMPARE(list->topLevelItemCount(), 4);
        QVERIFY(panel->exportFile(m_dir.filePath(QStringLiteral("panel-out.srt"))));
        // Picking a row moves the playhead to it.
        list->setCurrentItem(list->topLevelItem(3));
        QCOMPARE(ctl->playhead(), ctl->timeline().subtitles[3].start);
    }
};

QTEST_MAIN(TestPhase11a)
#include "tst_phase11a.moc"
