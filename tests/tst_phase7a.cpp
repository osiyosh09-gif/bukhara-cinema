// Phase 7 (part 1) acceptance tests: clip motion (position, scale, rotation,
// opacity) with keyframes: model, file, controller and undo, engine, panel.
#include "app/ClipInspector.h"
#include "app/MainWindow.h"
#include "app/ProjectController.h"
#include "app/TimelinePanel.h"
#include "core/MediaCache.h"
#include "core/Motion.h"
#include "engine/TimelinePlayer.h"

#include <QCheckBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QGroupBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QToolButton>
#include <QUndoStack>
#include <QtTest>

using namespace bc;
using Kind = Timeline::Kind;

namespace {

bool makeVideo(const QString &path)
{
    QProcess p;
    p.start(QStringLiteral(BC_FFMPEG_EXE),
            {QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-f"),
             QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("testsrc=size=320x240:rate=25:duration=4"),
             QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
             QStringLiteral("sine=frequency=440:sample_rate=48000:duration=4"), QStringLiteral("-c:v"),
             QStringLiteral("mpeg4"), QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("-shortest"), path});
    return p.waitForFinished(120000) && p.exitCode() == 0 && QFile::exists(path);
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

const TimelineClip &at(const Timeline &t, Kind kind, int n, int index)
{
    return t.tracks[t.indexOf(kind, n)].clips[index];
}

int brightness(const QImage &img, double fx, double fy)
{
    const QColor c = img.pixelColor(int(fx * (img.width() - 1)), int(fy * (img.height() - 1)));
    return (c.red() + c.green() + c.blue()) / 3;
}

double meanBrightness(const QImage &img)
{
    const QImage a = img.convertToFormat(QImage::Format_RGB32);
    double s = 0;
    int n = 0;
    for (int y = 0; y < a.height(); y += 4)
        for (int x = 0; x < a.width(); x += 4) {
            const QColor c = a.pixelColor(x, y);
            s += (c.red() + c.green() + c.blue()) / 3.0;
            ++n;
        }
    return n ? s / n : 0;
}

QImage render(const Project &p, qint64 frame)
{
    TimelinePlayer player;
    player.setPreviewLimit(QSize(384, 216));
    player.setProject(p);
    return player.renderFrame(frame);
}

} // namespace

class TestPhase7a : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_video;

    QUuid oneClip(ProjectController &ctl, int track = 0)
    {
        if (ctl.project().items.isEmpty()) {
            ctl.importFiles({m_video});
            waitIdle(ctl);
        }
        const QUuid media = ctl.project().items.first().id;
        ctl.placeMedia({media}, track, 0);
        return at(ctl.timeline(), Kind::Video, track, 0).id;
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("BukharaCinemaTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_phase7a"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        QSettings().setValue(QStringLiteral("proxy/auto"), false);
        QDir(MediaCache::root()).removeRecursively();
        QVERIFY(m_dir.isValid());
        m_video = m_dir.filePath(QStringLiteral("clip.mp4"));
        QVERIFY(makeVideo(m_video));
    }

    void motionModel()
    {
        ClipMotion m;
        QVERIFY(m.isIdentity() && !m.animated());
        ClipTransform t;
        t.scale = 50;
        m.set(10, t);
        QCOMPARE(m.at(0).scale, 50.0); // no keys: one value everywhere
        QVERIFY(m.toJson().contains(QStringLiteral("value")));

        m.setAnimated(true, 10);
        QCOMPARE(m.keys.size(), 1);
        ClipTransform t2 = t;
        t2.scale = 150;
        t2.opacity = 0;
        m.set(30, t2);
        QCOMPARE(m.keys.size(), 2);
        QCOMPARE(m.at(0).scale, 50.0);   // held before the first key
        QCOMPARE(m.at(20).scale, 100.0); // linear in between
        QCOMPARE(m.at(20).opacity, 50.0);
        QCOMPARE(m.at(99).scale, 150.0); // held after the last
        QCOMPARE(m.nextKey(10), qint64(30));
        QCOMPARE(m.prevKey(30), qint64(10));
        QCOMPARE(m.nextKey(30), qint64(-1));

        // File and engine text keep everything.
        QCOMPARE(ClipMotion::fromJson(m.toJson()), m);
        const ClipMotion e = ClipMotion::fromEngineString(m.toEngineString(10));
        QCOMPARE(e.at(10).scale, 100.0); // frames shifted by the offset
        QCOMPARE(ClipMotion::fromEngineString(ClipMotion().toEngineString(0)).at(5), ClipTransform());

        // Values are limited.
        ClipTransform wild;
        wild.opacity = 300;
        wild.scale = -5;
        m.set(30, wild);
        QCOMPARE(m.at(30).opacity, 100.0);
        QCOMPARE(m.at(30).scale, 0.0);

        // Turning keys off keeps the value at that frame.
        m.setAnimated(false, 20);
        QVERIFY(!m.animated());
        QCOMPARE(m.base.scale, 25.0);

        // Frame rate change moves keys.
        ClipMotion r;
        r.setAnimated(true, 0);
        r.set(50, t2);
        r.rescale(2.0);
        QCOMPARE(r.keys.last().frame, qint64(100));
    }

    void splitKeepsKeysInPlace()
    {
        ProjectController ctl;
        const QUuid id = oneClip(ctl);
        QVERIFY(ctl.setClipAnimated(id, true, 0));
        ClipTransform t;
        t.x = 0.5;
        QVERIFY(ctl.setClipTransform(id, t, 80));
        const ClipTransform mid = ctl.clipTransformAt(id, 40);
        QCOMPARE(mid.x, 0.25);
        QVERIFY(ctl.splitClips(40, {id}));
        const TimelineClip &right = at(ctl.timeline(), Kind::Video, 0, 1);
        QCOMPARE(ctl.clipTransformAt(right.id, 40).x, 0.25);
        QCOMPARE(ctl.clipTransformAt(right.id, 80).x, 0.5);
    }

    void controllerAndUndo()
    {
        ProjectController ctl;
        const QUuid id = oneClip(ctl);
        const int steps = ctl.undoStack()->count();
        ClipTransform t;
        t.scale = 80;
        QVERIFY(ctl.setClipTransform(id, t, 0));
        t.scale = 70;
        QVERIFY(ctl.setClipTransform(id, t, 0));
        QCOMPARE(ctl.undoStack()->count(), steps + 1); // quick changes form one step
        QCOMPARE(ctl.clipTransformAt(id, 50).scale, 70.0);

        // The keyframe button: first press starts animating.
        QVERIFY(!ctl.hasClipKey(id, 0));
        QVERIFY(ctl.toggleClipKey(id, 0));
        QVERIFY(ctl.hasClipKey(id, 0));
        t.scale = 100;
        QVERIFY(ctl.setClipTransform(id, t, 99));
        QVERIFY(ctl.hasClipKey(id, 99));
        QCOMPARE(at(ctl.timeline(), Kind::Video, 0, 0).motion.keys.size(), 2);
        QCOMPARE(ctl.clipKeyNear(id, 0, true), qint64(99));
        QCOMPARE(ctl.clipKeyNear(id, 99, false), qint64(0));
        QCOMPARE(ctl.clipKeyNear(id, 99, true), qint64(-1));
        QVERIFY(ctl.toggleClipKey(id, 99)); // removes it
        QVERIFY(!ctl.hasClipKey(id, 99));
        QVERIFY(!ctl.toggleClipKey(id, 150)); // outside the clip

        // Audio clips and locked tracks have no motion.
        const TimelineClip &audio = at(ctl.timeline(), Kind::Audio, 0, 0);
        QVERIFY(!ctl.setClipTransform(audio.id, t, 0));
        ctl.setTrackFlag(ctl.timeline().indexOf(Kind::Video, 0), QStringLiteral("locked"), true);
        QVERIFY(!ctl.resetClipMotion(id));
        ctl.setTrackFlag(ctl.timeline().indexOf(Kind::Video, 0), QStringLiteral("locked"), false);
        QVERIFY(ctl.resetClipMotion(id));
        QVERIFY(at(ctl.timeline(), Kind::Video, 0, 0).motion == ClipMotion());
        ctl.undoStack()->undo();
        QVERIFY(at(ctl.timeline(), Kind::Video, 0, 0).motion.animated());

        // Saved and opened again.
        const QString file = m_dir.filePath(QStringLiteral("motion.bcproj"));
        QString err;
        QVERIFY2(ctl.saveAs(file, &err), qPrintable(err));
        QFile f(file);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("version")).toInt() >= 6, true);
        ProjectController other;
        QVERIFY2(other.open(file, &err), qPrintable(err));
        QVERIFY(at(other.timeline(), Kind::Video, 0, 0).motion == at(ctl.timeline(), Kind::Video, 0, 0).motion);
    }

    void engineDrawsTheMotion()
    {
        ProjectController ctl;
        if (!ctl.canPlay())
            QSKIP("video engine not available");
        const QUuid id = oneClip(ctl);
        const QImage plain = render(ctl.project(), 10);
        QVERIFY(!plain.isNull());
        QVERIFY(meanBrightness(plain) > 40);

        // Opacity 0: only the black background.
        ClipTransform t;
        t.opacity = 0;
        ctl.setClipTransform(id, t, 0);
        QVERIFY(meanBrightness(render(ctl.project(), 10)) < 3);

        // Moved right by half the frame: the left half is black.
        t = ClipTransform();
        t.x = 0.5;
        ctl.setClipTransform(id, t, 0);
        const QImage moved = render(ctl.project(), 10);
        QVERIFY(brightness(moved, 0.3, 0.5) < 3);
        QVERIFY(brightness(moved, 0.75, 0.5) > 20);

        // Half size: the edges are black, the centre has the picture.
        t = ClipTransform();
        t.scale = 50;
        ctl.setClipTransform(id, t, 0);
        const QImage small = render(ctl.project(), 10);
        QVERIFY(brightness(small, 0.5, 0.1) < 3);
        QVERIFY(meanBrightness(small) < meanBrightness(plain) * 0.5);
        QVERIFY(meanBrightness(small) > 5);

        // Keyframes: fades in from opacity 0 at frame 0 to 100 at frame 50.
        ctl.resetClipMotion(id);
        ctl.setClipAnimated(id, true, 0);
        t = ClipTransform();
        t.opacity = 0;
        ctl.setClipTransform(id, t, 0);
        t.opacity = 100;
        ctl.setClipTransform(id, t, 50);
        TimelinePlayer player;
        player.setPreviewLimit(QSize(384, 216));
        player.setProject(ctl.project());
        const double b0 = meanBrightness(player.renderFrame(0));
        const double b25 = meanBrightness(player.renderFrame(25));
        const double b60 = meanBrightness(player.renderFrame(60));
        QVERIFY2(b0 < 3, qPrintable(QString::number(b0)));
        QVERIFY2(b25 > b0 + 5 && b25 < b60 - 5, qPrintable(QStringLiteral("%1 %2 %3").arg(b0).arg(b25).arg(b60)));
    }

    void lowerTrackShowsAroundAScaledClip()
    {
        ProjectController ctl;
        if (!ctl.canPlay())
            QSKIP("video engine not available");
        oneClip(ctl, 0);
        const QUuid upper = oneClip(ctl, 1);
        // The 4:3 video is letterboxed in the 16:9 frame; scaled down, its bars
        // must not cover V1.
        ClipTransform t;
        t.scale = 50;
        QVERIFY(ctl.setClipTransform(upper, t, 0));
        ctl.setTrackFlag(ctl.timeline().indexOf(Kind::Video, 0), QStringLiteral("hidden"), true);
        const QImage alone = render(ctl.project(), 10);
        ctl.setTrackFlag(ctl.timeline().indexOf(Kind::Video, 0), QStringLiteral("hidden"), false);
        const QImage both = render(ctl.project(), 10);
        // Inside the scaled frame but outside the scaled picture: V1 shows.
        QVERIFY(brightness(alone, 0.28, 0.5) < 3);
        QVERIFY2(brightness(both, 0.28, 0.5) > 20, qPrintable(QString::number(brightness(both, 0.28, 0.5))));
    }

    void inspectorEditsTheMotion()
    {
        MainWindow w;
        w.resize(1366, 768);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        ProjectController *ctl = w.controller();
        const QUuid id = oneClip(*ctl);
        auto *view = w.findChild<TimelineView *>(QStringLiteral("timelineView"));
        auto *insp = w.findChild<ClipInspector *>(QStringLiteral("clipInspector"));
        QVERIFY(view && insp);
        view->setSelection({id});
        QCOMPARE(insp->videoClip(), id);
        auto *box = insp->findChild<QDoubleSpinBox *>(QStringLiteral("motionScale"))->parentWidget();
        QVERIFY(box->isVisibleTo(insp));

        insp->findChild<QDoubleSpinBox *>(QStringLiteral("motionScale"))->setValue(60);
        QCOMPARE(at(ctl->timeline(), Kind::Video, 0, 0).motion.base.scale, 60.0);
        insp->findChild<QDoubleSpinBox *>(QStringLiteral("motionX"))->setValue(192); // pixels of 1920
        QCOMPARE(at(ctl->timeline(), Kind::Video, 0, 0).motion.base.x, 0.1);

        // Animate, then a value at another frame makes a second key.
        ctl->setPlayhead(0);
        insp->findChild<QCheckBox *>(QStringLiteral("motionAnimate"))->setChecked(true);
        QVERIFY(at(ctl->timeline(), Kind::Video, 0, 0).motion.animated());
        ctl->setPlayhead(50);
        insp->findChild<QDoubleSpinBox *>(QStringLiteral("motionOpacity"))->setValue(20);
        QCOMPARE(at(ctl->timeline(), Kind::Video, 0, 0).motion.keys.size(), 2);
        QVERIFY(insp->findChild<QToolButton *>(QStringLiteral("motionKey"))->isChecked());
        ctl->setPlayhead(25);
        QCOMPARE(insp->findChild<QDoubleSpinBox *>(QStringLiteral("motionOpacity"))->value(), 60.0);
        QVERIFY(!insp->findChild<QToolButton *>(QStringLiteral("motionKey"))->isChecked());
        insp->findChild<QToolButton *>(QStringLiteral("motionNextKey"))->click();
        QCOMPARE(ctl->playhead(), qint64(50));
        insp->findChild<QToolButton *>(QStringLiteral("motionReset"))->click();
        QVERIFY(at(ctl->timeline(), Kind::Video, 0, 0).motion == ClipMotion());
    }
};

QTEST_MAIN(TestPhase7a)
#include "tst_phase7a.moc"
