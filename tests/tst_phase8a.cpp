// Phase 8 (part 1) acceptance tests: transitions (model, file, engine for
// pictures and sound, controller, Ctrl+D, Clip panel).
#include "app/ClipInspector.h"
#include "app/MainWindow.h"
#include "app/ProjectController.h"
#include "app/TimelinePanel.h"
#include "core/MediaCache.h"
#include "engine/TimelinePlayer.h"

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUndoStack>
#include <QtTest>

using namespace bc;
using Kind = Timeline::Kind;
using TK = ClipTransition::Kind;

namespace {

bool ffmpeg(const QStringList &args)
{
    QProcess p;
    p.start(QStringLiteral(BC_FFMPEG_EXE), QStringList{QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error")} + args);
    return p.waitForFinished(120000) && p.exitCode() == 0;
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

QColor centre(const QImage &img, double fx = 0.5)
{
    return img.pixelColor(int(fx * (img.width() - 1)), img.height() / 2);
}

TimelineClip clipOf(const QUuid &media, qint64 start, qint64 in, qint64 length)
{
    TimelineClip c;
    c.id = QUuid::createUuid();
    c.mediaId = media;
    c.start = start;
    c.in = in;
    c.length = length;
    return c;
}

QImage render(const Project &p, qint64 frame)
{
    TimelinePlayer player;
    player.setPreviewLimit(QSize(160, 90));
    player.setProject(p);
    return player.renderFrame(frame);
}

double grey(const QImage &img)
{
    const QImage a = img.convertToFormat(QImage::Format_RGB32);
    double sum = 0;
    int n = 0;
    for (int y = 0; y < a.height(); y += 3)
        for (int x = 0; x < a.width(); x += 3) {
            sum += qGray(a.pixel(x, y));
            ++n;
        }
    return n ? sum / n : 0;
}

} // namespace

class TestPhase8a : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_red, m_blue, m_ramp, m_tone, m_silence;

    QUuid import(ProjectController &ctl, const QString &path)
    {
        ctl.importFiles({path});
        waitIdle(ctl);
        for (const MediaItem &m : ctl.project().items)
            if (m.path == path)
                return m.id;
        return {};
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("BukharaCinemaTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_phase8a"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        QSettings().setValue(QStringLiteral("proxy/auto"), false);
        QDir(MediaCache::root()).removeRecursively();
        QVERIFY(m_dir.isValid());
        m_red = m_dir.filePath(QStringLiteral("red.png"));
        m_blue = m_dir.filePath(QStringLiteral("blue.png"));
        QImage img(160, 90, QImage::Format_RGB32);
        img.fill(QColor(255, 0, 0));
        QVERIFY(img.save(m_red));
        img.fill(QColor(0, 0, 255));
        QVERIFY(img.save(m_blue));
        m_ramp = m_dir.filePath(QStringLiteral("ramp.mp4"));
        QVERIFY(ffmpeg({QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                        QStringLiteral("nullsrc=s=160x120:r=25:d=4,geq=lum='16+N*2':cb=128:cr=128"),
                        QStringLiteral("-c:v"), QStringLiteral("mpeg4"), QStringLiteral("-q:v"), QStringLiteral("1"),
                        QStringLiteral("-g"), QStringLiteral("1"), m_ramp}));
        m_tone = m_dir.filePath(QStringLiteral("tone.wav"));
        m_silence = m_dir.filePath(QStringLiteral("silence.wav"));
        QVERIFY(ffmpeg({QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                        QStringLiteral("sine=frequency=440:sample_rate=48000:duration=4"), m_tone}));
        QVERIFY(ffmpeg({QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                        QStringLiteral("anullsrc=r=48000:cl=stereo"), QStringLiteral("-t"), QStringLiteral("4"),
                        m_silence}));
    }

    void model()
    {
        Timeline t = Timeline::makeDefault();
        const QUuid m = QUuid::createUuid();
        TimelineClip a = clipOf(m, 0, 0, 50), b = clipOf(m, 50, 0, 30), c = clipOf(m, 100, 0, 40);
        t.tracks[0].clips << a << b << c;
        QVERIFY(t.touchingAfter(a.id) && !t.touchingAfter(b.id));
        // The end of A is the start of B (a cut).
        QVERIFY(t.setTransition(a.id, false, {TK::Dissolve, 20}));
        QCOMPARE(t.clip(b.id)->head.kind, TK::Dissolve);
        QVERIFY(t.clip(a.id)->tail.isNone());
        QCOMPARE(t.transitionAt(a.id, false).length, qint64(20));
        // Limited to the shorter clip.
        QVERIFY(t.setTransition(b.id, true, {TK::DipBlack, 100}));
        QCOMPARE(t.clip(b.id)->head.length, qint64(30));
        // A free end keeps its own.
        QVERIFY(t.setTransition(c.id, false, {TK::WipeLeft, 10}));
        QCOMPARE(t.clip(c.id)->tail.kind, TK::WipeLeft);
        // Splitting does not copy a transition to the new cut.
        const QList<QUuid> made = t.splitAt(120, {c.id});
        QVERIFY(t.clip(c.id)->tail.isNone());
        QCOMPARE(t.clip(made.first())->tail.kind, TK::WipeLeft);
        QVERIFY(t.clip(made.first())->head.isNone());
        // Saved and read.
        const Timeline back = Timeline::fromJson(t.toJson());
        QCOMPARE(back.clip(b.id)->head, t.clip(b.id)->head);
        QCOMPARE(back.clip(made.first())->tail, t.clip(made.first())->tail);
        // Sound has only the crossfade.
        TimelineClip s = clipOf(m, 0, 0, 50);
        t.tracks[t.indexOf(Kind::Audio, 0)].clips << s;
        QVERIFY(t.setTransition(s.id, true, {TK::WipeRight, 10}));
        QCOMPARE(t.clip(s.id)->head.kind, TK::Dissolve);
        // Removing.
        QVERIFY(t.setTransition(s.id, true, {}));
        QVERIFY(t.clip(s.id)->head.isNone());
    }

    void enginePictures()
    {
        ProjectController ctl;
        if (!ctl.canPlay())
            QSKIP("video engine not available");
        const QUuid red = import(ctl, m_red), blue = import(ctl, m_blue);
        Project p = ctl.project();
        p.timeline = Timeline::makeDefault();
        TimelineClip a = clipOf(red, 0, 0, 50), b = clipOf(blue, 50, 0, 50);
        p.timeline.tracks[0].clips << a << b;
        // Plain cut.
        QVERIFY(centre(render(p, 49)).red() > 200);
        QVERIFY(centre(render(p, 50)).blue() > 200);

        p.timeline.setTransition(b.id, true, {TK::Dissolve, 20});
        QImage mid = render(p, 50);
        QVERIFY2(centre(mid).red() > 80 && centre(mid).blue() > 80, qPrintable(centre(mid).name()));
        QVERIFY(centre(render(p, 35)).red() > 200);
        QVERIFY(centre(render(p, 65)).blue() > 200);
        QCOMPARE(p.timeline.duration(), qint64(100));

        p.timeline.setTransition(b.id, true, {TK::DipBlack, 20});
        const QColor dip = centre(render(p, 50));
        QVERIFY2(dip.red() < 40 && dip.blue() < 40, qPrintable(dip.name()));

        p.timeline.setTransition(b.id, true, {TK::WipeLeft, 20});
        const QImage wipe = render(p, 50);
        QVERIFY(centre(wipe, 0.1).blue() > 200);
        QVERIFY(centre(wipe, 0.9).red() > 200);

        // Free ends: fade from black at the start, to white at the end.
        p.timeline.setTransition(b.id, true, {});
        p.timeline.setTransition(a.id, true, {TK::Dissolve, 10});
        p.timeline.setTransition(b.id, false, {TK::DipWhite, 10});
        QVERIFY(centre(render(p, 0)).red() < 40);
        QVERIFY(centre(render(p, 20)).red() > 200);
        const QColor white = centre(render(p, 99));
        QVERIFY2(white.red() > 200 && white.green() > 200, qPrintable(white.name()));
    }

    void engineVideoHandles()
    {
        ProjectController ctl;
        if (!ctl.canPlay())
            QSKIP("video engine not available");
        const QUuid ramp = import(ctl, m_ramp);
        Project p = ctl.project();
        p.timeline = Timeline::makeDefault();
        // A: source 0..40 at 0; B: source 60..100 at 40.
        TimelineClip a = clipOf(ramp, 0, 0, 40), b = clipOf(ramp, 40, 60, 40);
        p.timeline.tracks[0].clips << a << b;
        QList<double> level;
        {
            TimelinePlayer plain;
            plain.setPreviewLimit(QSize(160, 90));
            Project q = p;
            q.timeline.tracks[0].clips = {clipOf(ramp, 0, 0, 100)};
            plain.setProject(q);
            for (int k = 0; k < 100; ++k)
                level << centre(plain.renderFrame(k)).red();
        }
        auto which = [&](const QImage &img) {
            const double g = centre(img).red();
            int best = 0;
            for (int k = 1; k < level.size(); ++k)
                if (qAbs(level[k] - g) < qAbs(level[best] - g))
                    best = k;
            return best;
        };
        p.timeline.setTransition(b.id, true, {TK::Dissolve, 10});
        TimelinePlayer player;
        player.setPreviewLimit(QSize(160, 90));
        player.setProject(p);
        // Outside the transition every frame is where it was.
        QCOMPARE(which(player.renderFrame(20)), 20);
        QCOMPARE(which(player.renderFrame(70)), 90);
        // In the middle: half of source 40 (A) and source 60 (B) = about 50.
        QVERIFY(qAbs(which(player.renderFrame(40)) - 50) <= 2);
    }

    void engineSoundCrossfade()
    {
        ProjectController ctl;
        if (!ctl.canPlay())
            QSKIP("video engine not available");
        const QUuid tone = import(ctl, m_tone), silence = import(ctl, m_silence);
        Project p = ctl.project();
        p.timeline = Timeline::makeDefault();
        const int a1 = p.timeline.indexOf(Kind::Audio, 0);
        // The silent clip starts 20 frames into its file, so the crossfade can be centred.
        TimelineClip a = clipOf(tone, 0, 0, 50), b = clipOf(silence, 50, 20, 50);
        p.timeline.tracks[a1].clips << a << b;
        p.timeline.setTransition(b.id, true, {TK::Dissolve, 20});
        TimelinePlayer player;
        player.setProject(p);
        const double early = player.audioLevel(30);
        const double before = player.audioLevel(43);
        const double after = player.audioLevel(57);
        const double late = player.audioLevel(70);
        QVERIFY2(early > 0.03 && late < 0.002, qPrintable(QStringLiteral("%1 %2").arg(early).arg(late)));
        // The tone fades out across the cut.
        QVERIFY2(before < early * 0.95 && after < before && after > early * 0.05,
                 qPrintable(QStringLiteral("%1 %2 %3 %4").arg(early).arg(before).arg(after).arg(late)));
    }

    void controllerAndPanel()
    {
        MainWindow w;
        w.resize(1366, 768);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        ProjectController *ctl = w.controller();
        const QUuid red = import(*ctl, m_red);
        ctl->placeMedia({red, red}, 0, 0);
        const Timeline &tl = ctl->timeline();
        const QUuid first = tl.tracks[0].clips[0].id;
        const QUuid second = tl.tracks[0].clips[1].id;
        const qint64 cut = tl.tracks[0].clips[1].start;
        auto *view = w.findChild<TimelineView *>(QStringLiteral("timelineView"));
        auto *act = w.findChild<QAction *>(QStringLiteral("actAddTransition"));
        QVERIFY(act && !act->isEnabled());
        view->setSelection({first});
        QVERIFY(act->isEnabled());
        ctl->setPlayhead(cut - 3);
        const int steps = ctl->undoStack()->count();
        act->trigger();
        QCOMPARE(ctl->undoStack()->count(), steps + 1);
        QCOMPARE(ctl->timeline().clip(second)->head.kind, TK::Dissolve);
        QCOMPARE(ctl->timeline().clip(second)->head.length, qint64(25));
        QCOMPARE(ctl->addDefaultTransitions({first}, cut), 0); // already there

        auto *insp = w.findChild<ClipInspector *>(QStringLiteral("clipInspector"));
        auto *tailKind = insp->findChild<QComboBox *>(QStringLiteral("transitionTailKind"));
        auto *tailLen = insp->findChild<QDoubleSpinBox *>(QStringLiteral("transitionTailLength"));
        QVERIFY(tailKind->isVisibleTo(insp));
        QCOMPARE(tailKind->currentData().toInt(), int(TK::Dissolve));
        tailKind->setCurrentIndex(tailKind->findData(int(TK::DipBlack)));
        QCOMPARE(ctl->timeline().clip(second)->head.kind, TK::DipBlack);
        tailLen->setValue(0.5);
        QCOMPARE(ctl->timeline().clip(second)->head.length, qint64(13));
        auto *headKind = insp->findChild<QComboBox *>(QStringLiteral("transitionHeadKind"));
        headKind->setCurrentIndex(headKind->findData(int(TK::Dissolve)));
        QCOMPARE(ctl->timeline().clip(first)->head.kind, TK::Dissolve);
        ctl->undoStack()->undo();
        QVERIFY(ctl->timeline().clip(first)->head.isNone());
    }
};

QTEST_MAIN(TestPhase8a)
#include "tst_phase8a.moc"
