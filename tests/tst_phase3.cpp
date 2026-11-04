// Phase 3 acceptance tests: title clips, clip volume and fades, solo.
#include "app/ClipInspector.h"
#include "app/Language.h"
#include "app/TimelinePanel.h"
#include "app/MainWindow.h"
#include "app/ProjectController.h"
#include "core/Project.h"
#include "core/Timeline.h"
#include "engine/TimelinePlayer.h"
#include "engine/TitleRenderer.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QFile>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUndoStack>
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

TitleData whiteTitle(const QString &text, TitleData::Position pos = TitleData::Position::Center)
{
    TitleData t;
    t.text = text;
    t.size = 200;
    t.position = pos;
    return t;
}

// Share of nearly white pixels in a rectangle.
double whiteShare(const QImage &img, const QRect &r)
{
    int white = 0, all = 0;
    for (int y = r.top(); y <= r.bottom(); ++y)
        for (int x = r.left(); x <= r.right(); ++x) {
            const QColor c = img.pixelColor(x, y);
            white += c.red() > 230 && c.green() > 230 && c.blue() > 230;
            ++all;
        }
    return all ? double(white) / all : 0;
}

} // namespace

class TestPhase3 : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_video;

    // A project with the test video placed at frame 0 (V1 + A1).
    void loadVideo(ProjectController &ctl)
    {
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

    // --- model -----------------------------------------------------------

    void titleClipModel()
    {
        Timeline t = Timeline::makeDefault();
        const QUuid id = t.placeTitle(whiteTitle(QStringLiteral("Салом")), 1, 10, 50);
        QVERIFY(!id.isNull());
        QVERIFY(t.clip(id)->isTitle());
        QCOMPARE(t.kindOf(id), Kind::Video);
        QVERIFY(t.placeTitle(whiteTitle(QStringLiteral("x")), 7, 0, 10).isNull()); // no V8

        // Split keeps the text on both halves.
        const QList<QUuid> right = t.splitAt(30, {id});
        QCOMPARE(right.size(), 1);
        QCOMPARE(t.clip(right.first())->title.text, QStringLiteral("Салом"));

        // JSON round trip keeps titles, volume, fades and solo.
        const int a1 = t.indexOf(Kind::Audio, 0);
        TimelineClip audio;
        audio.id = QUuid::createUuid();
        audio.mediaId = QUuid::createUuid();
        audio.length = 100;
        audio.gainDb = -6;
        audio.fadeIn = 10;
        audio.fadeOut = 20;
        t.insertSorted(a1, audio);
        t.tracks[a1].solo = true;
        t.tracks[t.indexOf(Kind::Video, 1)].clips[0].title.box = true;
        const Timeline back = Timeline::fromJson(t.toJson());
        QVERIFY(back == t);
    }

    void splitResetsInnerFades()
    {
        Timeline t = Timeline::makeDefault();
        const int a1 = t.indexOf(Kind::Audio, 0);
        TimelineClip c;
        c.id = QUuid::createUuid();
        c.mediaId = QUuid::createUuid();
        c.length = 100;
        c.fadeIn = 10;
        c.fadeOut = 10;
        t.insertSorted(a1, c);
        t.splitAt(50);
        QCOMPARE(t.tracks[a1].clips[0].fadeIn, 10);
        QCOMPARE(t.tracks[a1].clips[0].fadeOut, 0);
        QCOMPARE(t.tracks[a1].clips[1].fadeIn, 0);
        QCOMPARE(t.tracks[a1].clips[1].fadeOut, 10);
    }

    void soloRules()
    {
        Timeline t = Timeline::makeDefault();
        const int a1 = t.indexOf(Kind::Audio, 0);
        const int a2 = t.indexOf(Kind::Audio, 1);
        QVERIFY(t.isAudible(a1) && t.isAudible(a2));
        t.tracks[a2].solo = true;
        QVERIFY(!t.isAudible(a1));
        QVERIFY(t.isAudible(a2));
        t.tracks[a2].muted = true; // mute wins over solo
        QVERIFY(!t.isAudible(a2));
        QVERIFY(!t.isAudible(t.indexOf(Kind::Video, 0)));
    }

    // --- controller ------------------------------------------------------

    void addTitleUsesFreeTrackAndUndo()
    {
        ProjectController ctl;
        loadVideo(ctl);
        const QUuid id = ctl.addTitle(0);
        QVERIFY(!id.isNull());
        const Timeline::ClipRef r = ctl.timeline().find(id);
        QCOMPARE(ctl.timeline().trackName(r.track), QStringLiteral("V3")); // highest free track
        QCOMPARE(ctl.timeline().clip(id)->length, 125);                     // 5 s at 25 fps

        // Typing several characters is one undo step.
        TitleData t = ctl.timeline().clip(id)->title;
        t.text = QStringLiteral("A");
        QVERIFY(ctl.setClipTitle(id, t));
        t.text = QStringLiteral("AB");
        QVERIFY(ctl.setClipTitle(id, t));
        QCOMPARE(ctl.timeline().clip(id)->title.text, QStringLiteral("AB"));
        ctl.undoStack()->undo();
        QCOMPARE(ctl.timeline().clip(id)->title.text, ctl.tr("Title"));
        ctl.undoStack()->undo();
        QVERIFY(!ctl.timeline().clip(id));

        // All video tracks busy: a new track is added.
        ProjectController full;
        loadVideo(full);
        full.addTitle(0);
        full.addTitle(0);
        const QUuid third = full.addTitle(0);
        QCOMPARE(full.timeline().count(Kind::Video), 4);
        QCOMPARE(full.timeline().trackName(full.timeline().find(third).track), QStringLiteral("V4"));
    }

    void clipAudioSettingsAreClamped()
    {
        ProjectController ctl;
        loadVideo(ctl);
        const Timeline &tl = ctl.timeline();
        const QUuid audio = tl.tracks[tl.indexOf(Kind::Audio, 0)].clips[0].id;
        const QUuid video = tl.tracks[tl.indexOf(Kind::Video, 0)].clips[0].id;
        QVERIFY(!ctl.setClipAudio(video, -6, 0, 0)); // only audio clips
        QVERIFY(ctl.setClipAudio(audio, -100, 80, 80));
        const TimelineClip *c = ctl.timeline().clip(audio);
        QCOMPARE(c->gainDb, -60.0);
        QCOMPARE(c->fadeIn, 80);
        QCOMPARE(c->fadeOut, 20); // fades never overlap beyond the clip (100 frames)
        ctl.setTrackFlag(tl.indexOf(Kind::Video, 0), QStringLiteral("solo"), true);
        QVERIFY(!ctl.timeline().tracks[tl.indexOf(Kind::Video, 0)].solo); // solo is for audio tracks
    }

    void titlesSurviveSaveAndOpen()
    {
        const QString path = m_dir.filePath(QStringLiteral("titles.bcproj"));
        {
            ProjectController ctl;
            loadVideo(ctl);
            const QUuid id = ctl.addTitle(25);
            TitleData t = whiteTitle(QStringLiteral("Бухоро"), TitleData::Position::Top);
            t.color = QStringLiteral("#ffcc00");
            QVERIFY(ctl.setClipTitle(id, t));
            QVERIFY(ctl.saveAs(path, nullptr));
        }
        ProjectController ctl;
        QVERIFY(ctl.open(path, nullptr));
        int titles = 0;
        for (const auto &track : ctl.timeline().tracks)
            for (const auto &c : track.clips)
                if (c.isTitle()) {
                    ++titles;
                    QCOMPARE(c.title.text, QStringLiteral("Бухоро"));
                    QCOMPARE(c.title.color, QStringLiteral("#ffcc00"));
                    QCOMPARE(c.title.position, TitleData::Position::Top);
                }
        QCOMPARE(titles, 1);
    }

    // --- engine ----------------------------------------------------------

    void titleRendererPlacesText()
    {
        const QImage bottom = renderTitle(whiteTitle(QStringLiteral("ABC"), TitleData::Position::Bottom), QSize(640, 360));
        const QImage top = renderTitle(whiteTitle(QStringLiteral("ABC"), TitleData::Position::Top), QSize(640, 360));
        QCOMPARE(bottom.size(), QSize(640, 360));
        QVERIFY(whiteShare(bottom, QRect(0, 240, 640, 120)) > 0.01);
        QVERIFY(whiteShare(bottom, QRect(0, 0, 640, 120)) == 0);
        QVERIFY(whiteShare(top, QRect(0, 0, 640, 120)) > 0.01);
        QCOMPARE(bottom.pixelColor(2, 2).alpha(), 0); // transparent around the text
        QVERIFY(renderTitle(TitleData(), QSize(64, 36)).pixelColor(32, 18).alpha() == 0);
    }

    void titleIsCompositedOverVideo()
    {
        ProjectController ctl;
        QVERIFY2(ctl.canPlay(), qPrintable(ctl.engineError()));
        loadVideo(ctl);
        const QUuid id = ctl.addTitle(0);
        QVERIFY(ctl.setClipTitle(id, whiteTitle(QStringLiteral("III"))));
        TimelinePlayer player;
        player.setPreviewLimit(QSize(320, 180));

        player.setProject(ctl.project());
        const QImage with = player.renderFrame(40);
        const int v3 = ctl.timeline().find(id).track;
        Project hidden = ctl.project();
        hidden.timeline.tracks[v3].hidden = true;
        player.setProject(hidden);
        const QImage without = player.renderFrame(40);
        QVERIFY(!with.isNull() && !without.isNull());

        const QRect centre(with.width() / 2 - 30, with.height() / 2 - 20, 60, 40);
        QVERIFY(whiteShare(with, centre) > whiteShare(without, centre) + 0.1);
        // Outside the text the video shows through (the title is transparent).
        const QPoint corner(with.width() / 8, with.height() / 8);
        const QColor a = with.pixelColor(corner), b = without.pixelColor(corner);
        QVERIFY2(qAbs(a.red() - b.red()) < 12 && qAbs(a.green() - b.green()) < 12 && qAbs(a.blue() - b.blue()) < 12,
                 qPrintable(QStringLiteral("%1 vs %2").arg(a.name(), b.name())));
        QVERIFY(a.red() + a.green() + a.blue() > 60); // not black
    }

    void transparentPngShowsLowerTrack()
    {
        ProjectController ctl;
        QVERIFY2(ctl.canPlay(), qPrintable(ctl.engineError()));
        const QString red = m_dir.filePath(QStringLiteral("red.png"));
        const QString half = m_dir.filePath(QStringLiteral("half.png"));
        QImage r(320, 180, QImage::Format_RGB32);
        r.fill(QColor(220, 0, 0));
        QVERIFY(r.save(red));
        QImage h(320, 180, QImage::Format_ARGB32);
        h.fill(Qt::transparent);
        for (int y = 0; y < 180; ++y)
            for (int x = 160; x < 320; ++x)
                h.setPixelColor(x, y, QColor(0, 0, 230));
        QVERIFY(h.save(half));
        QCOMPARE(ctl.importFiles({red, half}), 2);
        waitIdle(ctl);
        QUuid redId, halfId;
        for (const MediaItem &m : ctl.project().items)
            (m.name.startsWith(QLatin1String("red")) ? redId : halfId) = m.id;
        ctl.placeMedia({redId}, 0, 0);
        ctl.placeMedia({halfId}, 1, 0);

        TimelinePlayer player;
        player.setPreviewLimit(QSize(320, 180));
        player.setProject(ctl.project());
        const QImage img = player.renderFrame(60); // well after the first frame of the still
        QVERIFY(!img.isNull());
        const QColor left = img.pixelColor(img.width() / 4, img.height() / 2);
        const QColor right = img.pixelColor(img.width() * 3 / 4, img.height() / 2);
        QVERIFY2(left.red() > 180 && left.blue() < 60, qPrintable(left.name()));
        QVERIFY2(right.blue() > 180 && right.red() < 60, qPrintable(right.name()));
    }

    void volumeFadesMuteAndSolo()
    {
        ProjectController ctl;
        QVERIFY2(ctl.canPlay(), qPrintable(ctl.engineError()));
        loadVideo(ctl); // 100 frames, 440 Hz tone
        const Timeline &tl = ctl.timeline();
        const int a1 = tl.indexOf(Kind::Audio, 0);
        const QUuid audio = tl.tracks[a1].clips[0].id;
        TimelinePlayer player;

        player.setProject(ctl.project());
        const double full = player.audioLevel(50);
        QVERIFY2(full > 0.05, qPrintable(QString::number(full)));

        QVERIFY(ctl.setClipAudio(audio, -20, 0, 0));
        player.setProject(ctl.project());
        const double quiet = player.audioLevel(50);
        QVERIFY2(qAbs(quiet / full - 0.1) < 0.03, qPrintable(QString::number(quiet / full)));

        QVERIFY(ctl.setClipAudio(audio, 0, 40, 40));
        player.setProject(ctl.project());
        QVERIFY(player.audioLevel(1) < full * 0.1);    // start of the fade in
        const double mid = player.audioLevel(20) / full; // half way
        QVERIFY2(mid > 0.35 && mid < 0.65, qPrintable(QString::number(mid)));
        QVERIFY(qAbs(player.audioLevel(50) / full - 1) < 0.1);
        QVERIFY(player.audioLevel(98) < full * 0.1);   // end of the fade out

        ctl.setTrackFlag(a1, QStringLiteral("muted"), true);
        player.setProject(ctl.project());
        QVERIFY(player.audioLevel(50) < 0.001);
        ctl.setTrackFlag(a1, QStringLiteral("muted"), false);
        ctl.setTrackFlag(tl.indexOf(Kind::Audio, 1), QStringLiteral("solo"), true); // another track solo
        player.setProject(ctl.project());
        QVERIFY(player.audioLevel(50) < 0.001);
    }
    // --- user interface --------------------------------------------------

    void newTextActionAndInspector()
    {
        MainWindow w;
        w.show();
        ProjectController *ctl = w.controller();
        loadVideo(*ctl);
        ctl->setPlayhead(25);
        auto *newText = w.findChild<QAction *>(QStringLiteral("actNewText"));
        QVERIFY(newText && newText->isEnabled());
        QVERIFY(!newText->property("bc_planned").toBool());
        newText->trigger();

        auto *view = w.findChild<TimelineView *>();
        QCOMPARE(view->selection().size(), 1);
        const QUuid id = view->selection().first();
        QVERIFY(ctl->timeline().clip(id)->isTitle());
        QCOMPARE(ctl->timeline().clip(id)->start, 25);

        auto *insp = w.findChild<ClipInspector *>();
        QCOMPARE(insp->titleClip(), id);
        auto *text = insp->findChild<QPlainTextEdit *>(QStringLiteral("titleText"));
        QVERIFY(text->isVisibleTo(insp));
        text->setPlainText(QStringLiteral("Бухоро кинолари"));
        QCOMPARE(ctl->timeline().clip(id)->title.text, QStringLiteral("Бухоро кинолари"));
        insp->findChild<QSpinBox *>(QStringLiteral("titleSize"))->setValue(120);
        insp->findChild<QComboBox *>(QStringLiteral("titlePosition"))->setCurrentIndex(0);
        insp->findChild<QCheckBox *>(QStringLiteral("titleBox"))->setChecked(true);
        const TitleData &t = ctl->timeline().clip(id)->title;
        QCOMPARE(t.size, 120);
        QCOMPARE(t.position, TitleData::Position::Top);
        QVERIFY(t.box);

        // Undo brings the inspector back to the previous value.
        ctl->undoStack()->undo();
        QVERIFY(!insp->findChild<QCheckBox *>(QStringLiteral("titleBox"))->isChecked());
    }

    void inspectorEditsAudioClip()
    {
        MainWindow w;
        ProjectController *ctl = w.controller();
        loadVideo(*ctl);
        auto *gainAct = w.findChild<QAction *>(QStringLiteral("actAudioGain"));
        QVERIFY(!gainAct->isEnabled()); // nothing selected

        const Timeline &tl = ctl->timeline();
        const QUuid video = tl.tracks[tl.indexOf(Kind::Video, 0)].clips[0].id;
        const QUuid audio = tl.tracks[tl.indexOf(Kind::Audio, 0)].clips[0].id;
        auto *view = w.findChild<TimelineView *>();
        view->setSelection({video, audio}); // a linked pair counts as one clip
        auto *insp = w.findChild<ClipInspector *>();
        QCOMPARE(insp->audioClip(), audio);
        QVERIFY(gainAct->isEnabled());

        insp->findChild<QDoubleSpinBox *>(QStringLiteral("clipGain"))->setValue(-6);
        insp->findChild<QDoubleSpinBox *>(QStringLiteral("clipFadeIn"))->setValue(1.0);
        insp->findChild<QDoubleSpinBox *>(QStringLiteral("clipFadeOut"))->setValue(0.5);
        const TimelineClip *c = ctl->timeline().clip(audio);
        QCOMPARE(c->gainDb, -6.0);
        QCOMPARE(c->fadeIn, 25);
        QCOMPARE(c->fadeOut, 13);

        // Two unrelated clips: nothing is edited.
        const QUuid title = ctl->addTitle(0);
        view->setSelection({title, audio});
        QVERIFY(insp->audioClip().isNull() && insp->titleClip().isNull());
    }
    void muteAndSoloButtonsInTrackHeader()
    {
        MainWindow w;
        w.resize(1200, 800);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        ProjectController *ctl = w.controller();
        auto *view = w.findChild<TimelineView *>();
        const int a1 = ctl->timeline().indexOf(Kind::Audio, 0);
        const QRect r = view->trackRect(a1);
        const int buttonX = view->headerWidth() - 6 - 18 + 9; // rightmost button centre
        // Buttons from the right: lock, S, M.
        QTest::mouseClick(view->viewport(), Qt::LeftButton, {}, QPoint(buttonX - 2 * 22, r.center().y()));
        QVERIFY(ctl->timeline().tracks[a1].muted);
        QTest::mouseClick(view->viewport(), Qt::LeftButton, {}, QPoint(buttonX - 22, r.center().y()));
        QVERIFY(ctl->timeline().tracks[a1].solo);
        QVERIFY(!ctl->timeline().tracks[a1].locked);
    }
};

QTEST_MAIN(TestPhase3)
#include "tst_phase3.moc"
