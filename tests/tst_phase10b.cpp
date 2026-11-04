// Phase 10 (part 2) acceptance tests: sound effects on audio clips
// (equalizer, compressor): model, signal work, engine, controller, panel.
#include "app/EffectsPanel.h"
#include "app/MainWindow.h"
#include "app/ProjectController.h"
#include "app/TimelinePanel.h"
#include "core/AudioFx.h"
#include "core/MediaCache.h"
#include "engine/TimelinePlayer.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QListWidget>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QUndoStack>
#include <QtTest>

#include <cmath>
#include <vector>

using namespace bc;
using Kind = Timeline::Kind;

namespace {

constexpr int kRate = 48000;

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

std::vector<float> sine(double freq, double amplitude, int frames = kRate)
{
    std::vector<float> s(size_t(frames) * 2);
    for (int i = 0; i < frames; ++i)
        s[size_t(i) * 2] = s[size_t(i) * 2 + 1] = float(amplitude * std::sin(2 * M_PI * freq * i / kRate));
    return s;
}

// Peak of the last quarter (after the filters have settled), in dB.
double peakDb(const std::vector<float> &s)
{
    float peak = 0;
    for (size_t i = s.size() * 3 / 4; i < s.size(); ++i)
        peak = std::max(peak, std::abs(s[i]));
    return 20 * std::log10(std::max(1e-9f, peak));
}

double run(const ClipEffect &e, double freq, double amplitude = 0.5)
{
    AudioChain chain;
    chain.configure({e}, kRate, 2);
    std::vector<float> s = sine(freq, amplitude);
    chain.process(s.data(), int(s.size() / 2));
    return peakDb(s) - 20 * std::log10(amplitude); // change in dB
}

ClipEffect eq(const QString &key, double v, const QString &key2 = {}, double v2 = 0)
{
    ClipEffect e = ClipEffect::make(ClipEffect::Kind::Equalizer);
    e.setValue(key, v);
    if (!key2.isEmpty())
        e.setValue(key2, v2);
    return e;
}

} // namespace

class TestPhase10b : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_video;

    void loadVideo(ProjectController &ctl)
    {
        QCOMPARE(ctl.importFiles({m_video}), 1);
        waitIdle(ctl);
        QVERIFY(!ctl.placeMedia({ctl.project().items.first().id}, 0, 0).isEmpty());
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("BukharaCinemaTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_phase10b"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        QSettings().setValue(QStringLiteral("proxy/auto"), false);
        QDir(MediaCache::root()).removeRecursively();
        QVERIFY(m_dir.isValid());
        m_video = m_dir.filePath(QStringLiteral("tone.mp4"));
        QVERIFY2(makeVideo(m_video), "ffmpeg could not create test media");
    }

    void model()
    {
        const QList<ClipEffect::Kind> sound = ClipEffect::kinds(true);
        QCOMPARE(sound, (QList<ClipEffect::Kind>{ClipEffect::Kind::Equalizer, ClipEffect::Kind::Compressor}));
        for (ClipEffect::Kind k : ClipEffect::kinds())
            QVERIFY(!ClipEffect::isAudio(k));
        QVERIFY(ClipEffect::make(ClipEffect::Kind::Equalizer).isNeutral());
        QVERIFY(!ClipEffect::make(ClipEffect::Kind::Compressor).isNeutral()); // ratio 4
        ClipEffect c = ClipEffect::make(ClipEffect::Kind::Compressor);
        QVERIFY(c.setValue(QStringLiteral("ratio"), 1));
        QVERIFY(c.isNeutral());
        QVERIFY(c.setValue(QStringLiteral("makeup"), 6));
        QVERIFY(!c.isNeutral());

        // Saved with the audio clip; splitting keeps them.
        Timeline t = Timeline::makeDefault();
        TimelineClip clip;
        clip.id = QUuid::createUuid();
        clip.mediaId = QUuid::createUuid();
        clip.length = 100;
        clip.effects << eq(QStringLiteral("low_cut"), 80, QStringLiteral("mid_gain"), -4) << c;
        t.tracks[t.indexOf(Kind::Audio, 0)].clips << clip;
        const Timeline back = Timeline::fromJson(t.toJson());
        QCOMPARE(back.clip(clip.id)->effects, clip.effects);
        QCOMPARE(effectsFromEngineString(effectsToEngineString(clip.effects)), clip.effects);
        const QList<QUuid> made = t.splitAt(50, {clip.id});
        QCOMPARE(t.clip(made.first())->effects, clip.effects);
    }

    void equalizer()
    {
        // Neutral: nothing changes.
        QVERIFY(std::abs(run(ClipEffect::make(ClipEffect::Kind::Equalizer), 1000)) < 0.01);
        // Low cut at 300 Hz: 60 Hz almost gone, 3 kHz kept.
        QVERIFY(run(eq(QStringLiteral("low_cut"), 300), 60) < -20);
        QVERIFY(std::abs(run(eq(QStringLiteral("low_cut"), 300), 3000)) < 0.5);
        // Bass +12 dB below the shelf, the treble untouched.
        QVERIFY(std::abs(run(eq(QStringLiteral("low_gain"), 12), 50) - 12) < 1.5);
        QVERIFY(std::abs(run(eq(QStringLiteral("low_gain"), 12), 5000)) < 0.5);
        // Middle bell: -10 dB at its centre, little at 4 octaves away.
        const ClipEffect mid = eq(QStringLiteral("mid_gain"), -10, QStringLiteral("mid_freq"), 1000);
        QVERIFY(std::abs(run(mid, 1000) + 10) < 0.5);
        QVERIFY(std::abs(run(mid, 62.5)) < 1);
        // Treble -12 dB above the shelf.
        QVERIFY(std::abs(run(eq(QStringLiteral("high_gain"), -12), 15000) + 12) < 1.5);
        QVERIFY(std::abs(run(eq(QStringLiteral("high_gain"), -12), 100)) < 0.5);
    }

    void compressor()
    {
        ClipEffect c = ClipEffect::make(ClipEffect::Kind::Compressor);
        c.setValue(QStringLiteral("threshold"), -20);
        c.setValue(QStringLiteral("ratio"), 4);
        c.setValue(QStringLiteral("attack"), 1);
        // 0 dBFS in: 20 dB over, a quarter of it stays: about -15 dBFS out.
        const double change = run(c, 440, 1.0);
        QVERIFY2(std::abs(change + 15) < 1.0, qPrintable(QString::number(change)));
        // Quiet sound (-40 dBFS) is untouched; make-up gain lifts everything.
        QVERIFY(std::abs(run(c, 440, 0.01)) < 0.1);
        c.setValue(QStringLiteral("makeup"), 6);
        QVERIFY(std::abs(run(c, 440, 0.01) - 6) < 0.1);

        // Blocks give the same result as one long call (state is kept).
        AudioChain a, b;
        a.configure({c}, kRate, 2);
        b.configure({c}, kRate, 2);
        std::vector<float> s1 = sine(440, 1.0, 9600), s2 = s1;
        a.process(s1.data(), 9600);
        for (int i = 0; i < 9600; i += 1920)
            b.process(s2.data() + size_t(i) * 2, 1920);
        for (size_t i = 0; i < s1.size(); ++i)
            QVERIFY(std::abs(s1[i] - s2[i]) < 1e-5);
        QVERIFY(a.gainReductionDb() > 10);
        a.reset();
        QCOMPARE(a.gainReductionDb(), 0.0);
    }

    void engine()
    {
        ProjectController ctl;
        QVERIFY2(ctl.canPlay(), qPrintable(ctl.engineError()));
        loadVideo(ctl);
        const int a1 = ctl.timeline().indexOf(Kind::Audio, 0);
        const QUuid audio = ctl.timeline().tracks[a1].clips[0].id;
        TimelinePlayer player;
        auto level = [&] {
            player.setProject(ctl.project());
            double l = 0;
            for (qint64 f = 30; f <= 50; ++f) // contiguous: filters settle
                l = player.audioLevel(f);
            return l;
        };
        const double full = level();
        QVERIFY(full > 0.05);

        // A bell at the tone's frequency, -15 dB.
        const int e = ctl.addClipEffect(audio, ClipEffect::Kind::Equalizer);
        QCOMPARE(e, 0);
        QVERIFY(ctl.setClipEffectValue(audio, e, QStringLiteral("mid_freq"), 440));
        QVERIFY(ctl.setClipEffectValue(audio, e, QStringLiteral("mid_gain"), -15));
        const double cut = level() / full;
        QVERIFY2(cut > 0.12 && cut < 0.25, qPrintable(QString::number(cut)));
        // Switched off: back to full.
        QVERIFY(ctl.setClipEffectEnabled(audio, e, false));
        QVERIFY(std::abs(level() / full - 1) < 0.05);

        // Compressor far below the tone's level: much quieter; make-up brings some back.
        const int c = ctl.addClipEffect(audio, ClipEffect::Kind::Compressor);
        QVERIFY(ctl.setClipEffectValue(audio, c, QStringLiteral("threshold"), -40));
        QVERIFY(ctl.setClipEffectValue(audio, c, QStringLiteral("ratio"), 20));
        const double squashed = level() / full;
        QVERIFY2(squashed < 0.2, qPrintable(QString::number(squashed)));
        QVERIFY(ctl.setClipEffectValue(audio, c, QStringLiteral("makeup"), 12));
        QVERIFY(level() / full > squashed * 3);
    }

    void controller()
    {
        ProjectController ctl;
        if (!ctl.canPlay())
            QSKIP("video engine not available");
        loadVideo(ctl);
        const Timeline &tl = ctl.timeline();
        const QUuid video = tl.tracks[tl.indexOf(Kind::Video, 0)].clips[0].id;
        const QUuid audio = tl.tracks[tl.indexOf(Kind::Audio, 0)].clips[0].id;
        // Sound effects only on sound clips, picture effects only on pictures.
        QCOMPARE(ctl.addClipEffect(video, ClipEffect::Kind::Equalizer), -1);
        QCOMPARE(ctl.addClipEffect(audio, ClipEffect::Kind::Blur), -1);
        QCOMPARE(ctl.addClipEffect(audio, ClipEffect::Kind::Compressor), 0);
        QCOMPARE(ctl.clipEffects(audio).size(), 1);
        QVERIFY(ctl.clipEffects(video).isEmpty());
        // A locked track refuses.
        ctl.setTrackFlag(tl.indexOf(Kind::Audio, 0), QStringLiteral("locked"), true);
        QCOMPARE(ctl.addClipEffect(audio, ClipEffect::Kind::Equalizer), -1);
        QVERIFY(!ctl.removeClipEffect(audio, 0));
    }

    void panel()
    {
        MainWindow w;
        w.resize(1366, 768);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        ProjectController *ctl = w.controller();
        if (!ctl->canPlay())
            QSKIP("video engine not available");
        loadVideo(*ctl);
        const Timeline &tl = ctl->timeline();
        const QUuid video = tl.tracks[tl.indexOf(Kind::Video, 0)].clips[0].id;
        const QUuid audio = tl.tracks[tl.indexOf(Kind::Audio, 0)].clips[0].id;
        auto *panel = w.findChild<EffectsPanel *>(QStringLiteral("effectsPanel"));
        w.findChild<QTabWidget *>(QStringLiteral("rightTabs"))->setCurrentWidget(panel);
        auto *view = w.findChild<TimelineView *>(QStringLiteral("timelineView"));
        auto *kind = panel->findChild<QComboBox *>(QStringLiteral("effectAddKind"));
        auto *soundBtn = panel->findChild<QToolButton *>(QStringLiteral("effectTargetAudio"));
        auto *pictureBtn = panel->findChild<QToolButton *>(QStringLiteral("effectTargetVideo"));

        // The linked pair: picture first, the switch shows.
        view->setSelection({video, audio});
        QCOMPARE(panel->clip(), video);
        QVERIFY(soundBtn->isVisibleTo(panel));
        QCOMPARE(kind->count(), ClipEffect::kinds().size());
        soundBtn->click();
        QCOMPARE(panel->clip(), audio);
        QCOMPARE(kind->count(), 2);
        QCOMPARE(kind->itemText(0), EffectsPanel::kindName(ClipEffect::Kind::Equalizer));

        // Add an equalizer and change the bass from the form.
        kind->setCurrentIndex(0);
        panel->findChild<QToolButton *>(QStringLiteral("effectAdd"))->click();
        QCOMPARE(ctl->clipEffects(audio).size(), 1);
        auto *bass = panel->findChild<QDoubleSpinBox *>(QStringLiteral("effectParam_low_gain"));
        QTRY_VERIFY(bass && bass->isVisible());
        bass->setValue(6);
        QCOMPARE(ctl->clipEffects(audio)[0].value(QStringLiteral("low_gain")), 6.0);
        QVERIFY(ctl->clipEffects(video).isEmpty());

        // Only the sound clip selected: no switch, sound effects shown.
        view->setSelection({audio});
        QCOMPARE(panel->clip(), audio);
        QVERIFY(!soundBtn->isVisibleTo(panel));
        QCOMPARE(panel->findChild<QListWidget *>(QStringLiteral("effectList"))->count(), 1);
        // Back to the pair: the choice of the sound part is kept until changed.
        view->setSelection({video, audio});
        QCOMPARE(panel->clip(), audio);
        pictureBtn->click();
        QCOMPARE(panel->clip(), video);
        QCOMPARE(panel->findChild<QListWidget *>(QStringLiteral("effectList"))->count(), 0);
    }
};

QTEST_MAIN(TestPhase10b)
#include "tst_phase10b.moc"
