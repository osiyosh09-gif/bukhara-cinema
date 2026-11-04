// Phase 10 (part 4) acceptance tests: loudness (BS.1770 / EBU R128) and clip
// loudness normalisation.
#include "app/LoudnessDialog.h"
#include "app/ProjectController.h"
#include "core/Loudness.h"
#include "core/MediaCache.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUndoStack>
#include <QtTest>

#include <cmath>
#include <vector>

using namespace bc;
using Kind = Timeline::Kind;

namespace {

// 4 s of 997 Hz mono: 2 s at amplitude 1/8 (-18 dBFS), then 2 s at 1/2 (-6 dBFS).
bool makeTone(const QString &path)
{
    QProcess p;
    p.start(QStringLiteral(BC_FFMPEG_EXE),
            {QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
             QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
             QStringLiteral("aevalsrc=if(lt(t\\,2)\\,0.125\\,0.5)*sin(2*PI*997*t):s=48000:d=4"),
             QStringLiteral("-c:a"), QStringLiteral("pcm_f32le"), path});
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

std::vector<float> sine(int rate, int channels, double seconds, const std::vector<double> &amp)
{
    const int n = int(rate * seconds);
    std::vector<float> s(size_t(n) * size_t(channels));
    for (int i = 0; i < n; ++i)
        for (int c = 0; c < channels; ++c)
            s[size_t(i) * size_t(channels) + size_t(c)] = float(amp[size_t(c)] * std::sin(2 * M_PI * 997 * i / rate));
    return s;
}

Loudness::Profile profileOf(int rate, int channels, const std::vector<float> &s)
{
    Loudness::Meter m(channels, rate);
    m.add(s.data(), int(s.size() / size_t(channels)));
    return m.profile();
}

// -18.06 dBFS sine in one channel: -21.07 LUFS (0 dBFS sine in one channel = -3.01).
constexpr double kQuiet = -21.07;

} // namespace

class TestPhase10d : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_tone;

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("BukharaCinemaTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_phase10d"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        QSettings().setValue(QStringLiteral("proxy/auto"), false);
        QDir(MediaCache::root()).removeRecursively();
        QVERIFY(m_dir.isValid());
        m_tone = m_dir.filePath(QStringLiteral("tone.wav"));
        QVERIFY2(makeTone(m_tone), "ffmpeg could not create test media");
    }

    void reference()
    {
        // BS.1770 reference: a 0 dBFS 997 Hz sine in one channel reads -3.01 LUFS,
        // at 48 and 44.1 kHz.
        for (int rate : {48000, 44100}) {
            const auto p = profileOf(rate, 2, sine(rate, 2, 5, {1.0, 0.0}));
            QCOMPARE(int(p.power.size()), 50);
            const Loudness::Result r = Loudness::measure(p, 0, 5);
            QVERIFY(r.valid);
            QVERIFY2(std::abs(r.lufs + 3.01) < 0.1, qPrintable(QString::number(r.lufs)));
            QVERIFY(std::abs(r.peakDb) < 0.01);
        }
        // Both channels: twice the power, +3 dB.
        const auto p = profileOf(48000, 2, sine(48000, 2, 5, {0.5, 0.5}));
        QVERIFY(std::abs(Loudness::measure(p, 0, 5).lufs - (-6.02)) < 0.1);
        // 5.1: the LFE does not count.
        const auto lfe = profileOf(48000, 6, sine(48000, 6, 3, {0, 0, 0, 1.0, 0, 0}));
        QVERIFY(!Loudness::measure(lfe, 0, 3).valid);
    }

    void gating()
    {
        // 5 s tone, 5 s silence: the silence is gated out.
        auto s = sine(48000, 1, 10, {0.25});
        std::fill(s.begin() + 48000 * 5, s.end(), 0.0f);
        const auto p = profileOf(48000, 1, s);
        const Loudness::Result r = Loudness::measure(p, 0, 10);
        const double tone = Loudness::measure(p, 0, 5).lufs;
        QVERIFY(std::abs(r.lufs - tone) < 0.2);
        // A part 20 dB quieter is below the relative gate; one 6 dB quieter counts.
        auto q = sine(48000, 1, 10, {0.5});
        for (size_t i = 48000 * 5; i < q.size(); ++i)
            q[i] *= 0.1f;
        const auto pq = profileOf(48000, 1, q);
        QVERIFY(std::abs(Loudness::measure(pq, 0, 10).lufs - Loudness::measure(pq, 0, 5).lufs) < 0.2);
        auto h = sine(48000, 1, 10, {0.5});
        for (size_t i = 48000 * 5; i < h.size(); ++i)
            h[i] *= 0.5f;
        const auto ph = profileOf(48000, 1, h);
        QVERIFY(Loudness::measure(ph, 0, 10).lufs < Loudness::measure(ph, 0, 5).lufs - 2);
        // Silence and a too short range are not valid.
        QVERIFY(!Loudness::measure(profileOf(48000, 1, std::vector<float>(48000 * 2, 0.0f)), 0, 2).valid);
        QVERIFY(!Loudness::measure(p, 1.0, 1.3).valid);
    }

    void fileAndCache()
    {
        Loudness::Profile p;
        QString err;
        QVERIFY2(Loudness::compute(m_tone, p, &err), qPrintable(err));
        QCOMPARE(int(p.power.size()), 40);
        QVERIFY(std::abs(Loudness::measure(p, 0, 2).lufs - kQuiet) < 0.15);
        QVERIFY(std::abs(Loudness::measure(p, 2, 4).lufs - (kQuiet + 12.04)) < 0.15);
        QVERIFY(std::abs(Loudness::peakDb(p, 0, 2) + 18.06) < 0.05);
        const QString file = m_dir.filePath(QStringLiteral("c/x.bcld"));
        QVERIFY(Loudness::save(file, p));
        Loudness::Profile back;
        QVERIFY(Loudness::load(file, back));
        QCOMPARE(back.power, p.power);
        QCOMPARE(back.peak, p.peak);
        QVERIFY(!Loudness::compute(m_dir.filePath(QStringLiteral("missing.wav")), back));
    }

    void normalize()
    {
        ProjectController ctl;
        QCOMPARE(ctl.importFiles({m_tone}), 1);
        waitIdle(ctl);
        QVERIFY(!ctl.placeMedia({ctl.project().items.first().id}, 0, 0).isEmpty());
        const int a1 = ctl.timeline().indexOf(Kind::Audio, 0);
        QVERIFY(a1 >= 0);
        const QUuid clip = ctl.timeline().tracks[a1].clips[0].id;
        QCOMPARE(ctl.loudnessClips({clip}), QList<QUuid>{clip});
        QVERIFY(!ctl.loudnessKnown(clip));
        QCOMPARE(ctl.normalizeLoudness({clip}, -14, -1, false), 0); // not measured yet

        QSignalSpy done(&ctl, &ProjectController::loudnessAnalyzed);
        ctl.analyzeLoudness({clip});
        QVERIFY(done.wait(20000));
        QVERIFY(done.first().at(1).toBool());
        QVERIFY(ctl.loudnessKnown(clip));

        // Split at 2 s: the first part is the quiet one.
        const qint64 fps = qint64(ctl.project().sequence.fps());
        QVERIFY(ctl.splitClips(2 * fps, {clip}));
        QCOMPARE(ctl.timeline().tracks[a1].clips.size(), qsizetype(2));
        const QUuid quiet = ctl.timeline().tracks[a1].clips[0].id;
        const QUuid loud = ctl.timeline().tracks[a1].clips[1].id;
        QVERIFY(std::abs(ctl.clipLoudness(quiet).lufs - kQuiet) < 0.2);
        QVERIFY(std::abs(ctl.clipLoudness(loud).lufs - (kQuiet + 12.04)) < 0.2);

        // Each clip separately: -14 LUFS, but no peak above -1 dBFS.
        QHash<QUuid, double> g = ctl.loudnessGains({quiet, loud}, -14, -1, false);
        QCOMPARE(g.size(), 2);
        QVERIFY2(std::abs(g[quiet] - 7.1) < 0.15, qPrintable(QString::number(g[quiet])));
        QVERIFY2(std::abs(g[loud] - (-14 - (kQuiet + 12.04))) < 0.15, qPrintable(QString::number(g[loud])));
        // A low ceiling holds the quiet clip back: peak -18.06 + gain <= -12.
        g = ctl.loudnessGains({quiet}, -14, -12, false);
        QVERIFY(std::abs(g[quiet] - 6.1) < 0.15);
        // Together: one change for both (from the combined measurement).
        g = ctl.loudnessGains({quiet, loud}, -14, 0, true);
        QCOMPARE(g[quiet], g[loud]);

        QCOMPARE(ctl.normalizeLoudness({quiet, loud}, -14, -1, false), 2);
        QVERIFY(std::abs(ctl.timeline().clip(quiet)->gainDb - 7.1) < 0.15);
        QVERIFY(ctl.timeline().clip(loud)->gainDb < -4);
        QCOMPARE(ctl.undoStack()->undoText(), tr("Normalize loudness"));
        ctl.undoStack()->undo();
        QCOMPARE(ctl.timeline().clip(quiet)->gainDb, 0.0);
        QCOMPARE(ctl.timeline().clip(loud)->gainDb, 0.0);

        // Speed 2x with the same length: the clip uses twice as much source
        // (the quiet and the loud part).
        const qint64 len = ctl.timeline().clip(quiet)->length;
        QVERIFY(ctl.setClipSpeed(quiet, 2.0, len, true));
        QCOMPARE(ctl.timeline().clip(quiet)->length, len);
        const double both = ctl.clipLoudness(quiet).lufs;
        QVERIFY2(both > kQuiet + 3, qPrintable(QString::number(both)));
    }

    void cacheReused()
    {
        // The profile was saved in the cache: a new controller loads it at once.
        QVERIFY(QFile::exists(MediaCache::pathFor(MediaCache::Kind::Loudness, m_tone)));
        QCOMPARE(MediaCache::folder(MediaCache::Kind::Loudness), MediaCache::folder(MediaCache::Kind::Waveform));
    }

    void dialog()
    {
        ProjectController ctl;
        QCOMPARE(ctl.importFiles({m_tone}), 1);
        waitIdle(ctl);
        const QList<QUuid> made = ctl.placeMedia({ctl.project().items.first().id}, 0, 0);
        QVERIFY(!made.isEmpty());
        LoudnessDialog dlg(&ctl, made);
        auto *apply = dlg.findChild<QPushButton *>(QStringLiteral("loudnessApply"));
        auto *preset = dlg.findChild<QComboBox *>(QStringLiteral("loudnessPreset"));
        auto *target = dlg.findChild<QDoubleSpinBox *>(QStringLiteral("loudnessTarget"));
        QVERIFY(apply && preset && target);
        QTRY_VERIFY_WITH_TIMEOUT(dlg.measured(), 20000);
        QVERIFY(apply->isEnabled());
        preset->setCurrentIndex(2); // -23 LUFS (TV)
        QCOMPARE(target->value(), -23.0);
        QVERIFY(!target->isEnabled());
        QCOMPARE(dlg.apply(), 1);
        const int a1 = ctl.timeline().indexOf(Kind::Audio, 0);
        const TimelineClip &c = ctl.timeline().tracks[a1].clips[0];
        const double whole = ctl.clipLoudness(c.id).lufs;
        QVERIFY(std::abs(c.gainDb - std::round((-23 - whole) * 10) / 10) < 0.01);
        QCOMPARE(QSettings().value(QStringLiteral("loudness/target")).toDouble(), -23.0);

        // Nothing selected: the dialog says so and Apply stays off.
        LoudnessDialog empty(&ctl, {});
        QVERIFY(!empty.findChild<QPushButton *>(QStringLiteral("loudnessApply"))->isEnabled());
    }
};

QTEST_MAIN(TestPhase10d)
#include "tst_phase10d.moc"
