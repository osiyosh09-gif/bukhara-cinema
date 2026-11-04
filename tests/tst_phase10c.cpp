// Phase 10 (part 3) acceptance tests: noise reduction and hum removal on
// audio clips: FFT, signal work, latency, engine, panel.
#include "app/EffectsPanel.h"
#include "app/MainWindow.h"
#include "app/ProjectController.h"
#include "app/TimelinePanel.h"
#include "core/AudioFx.h"
#include "core/MediaCache.h"
#include "engine/TimelinePlayer.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QProcess>
#include <QRandomGenerator>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

#include <cmath>
#include <vector>

using namespace bc;
using Kind = Timeline::Kind;

namespace {

constexpr int kRate = 48000;

// A tone with hum or noise: ffmpeg makes the file the engine test plays.
bool makeNoisyVideo(const QString &path)
{
    QProcess p;
    p.start(QStringLiteral(BC_FFMPEG_EXE),
            {QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
             QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
             QStringLiteral("testsrc=size=320x240:rate=25:duration=4"),
             QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
             QStringLiteral("anoisesrc=color=white:amplitude=0.1:sample_rate=48000:duration=4:seed=7"),
             QStringLiteral("-c:v"), QStringLiteral("mpeg4"), QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"),
             QStringLiteral("-shortest"), path});
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

// Level of one frequency (Goertzel) in a stretch of channel 0, as amplitude.
double tone(const std::vector<float> &s, double freq, int from, int to)
{
    const double w = 2 * M_PI * freq / kRate;
    const double coeff = 2 * std::cos(w);
    double s1 = 0, s2 = 0;
    for (int i = from; i < to; ++i) {
        const double s0 = s[size_t(i) * 2] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    return 2 * std::sqrt(std::max(0.0, power)) / (to - from);
}

double rms(const std::vector<float> &s, int from, int to)
{
    double sum = 0;
    for (int i = from; i < to; ++i)
        sum += double(s[size_t(i) * 2]) * s[size_t(i) * 2];
    return std::sqrt(sum / (to - from));
}

double db(double ratio)
{
    return 20 * std::log10(std::max(1e-12, ratio));
}

ClipEffect effect(ClipEffect::Kind kind, const QString &key = {}, double v = 0)
{
    ClipEffect e = ClipEffect::make(kind);
    if (!key.isEmpty())
        e.setValue(key, v);
    return e;
}

} // namespace

class TestPhase10c : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_noisy;

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("BukharaCinemaTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_phase10c"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        QSettings().setValue(QStringLiteral("proxy/auto"), false);
        QDir(MediaCache::root()).removeRecursively();
        QVERIFY(m_dir.isValid());
        m_noisy = m_dir.filePath(QStringLiteral("noise.mkv"));
        QVERIFY2(makeNoisyVideo(m_noisy), "ffmpeg could not create test media");
    }

    void model()
    {
        const QList<ClipEffect::Kind> sound = ClipEffect::kinds(true);
        QVERIFY(sound.contains(ClipEffect::Kind::NoiseReduction));
        QVERIFY(sound.contains(ClipEffect::Kind::HumRemoval));
        QVERIFY(!ClipEffect::kinds().contains(ClipEffect::Kind::NoiseReduction));
        QVERIFY(!ClipEffect::make(ClipEffect::Kind::NoiseReduction).isNeutral());
        QVERIFY(effect(ClipEffect::Kind::NoiseReduction, QStringLiteral("reduction"), 0).isNeutral());
        const ClipEffects list{effect(ClipEffect::Kind::HumRemoval, QStringLiteral("hum_freq"), 60),
                               effect(ClipEffect::Kind::NoiseReduction, QStringLiteral("reduction"), 20)};
        QCOMPARE(effectsFromJson(effectsToJson(list)), list);
    }

    void fftRoundTrip()
    {
        std::vector<std::complex<float>> a(64), b;
        for (size_t i = 0; i < a.size(); ++i)
            a[i] = float(std::sin(i * 0.3) + 0.25 * std::cos(i * 1.7));
        b = a;
        fft(b, false);
        // A pure tone lands in its bin.
        std::vector<std::complex<float>> t(64);
        for (size_t i = 0; i < t.size(); ++i)
            t[i] = float(std::cos(2 * M_PI * 5 * i / 64.0));
        fft(t, false);
        QVERIFY(std::abs(t[5]) > 31 && std::abs(t[4]) < 0.01 && std::abs(t[59]) > 31);
        fft(b, true);
        for (size_t i = 0; i < a.size(); ++i)
            QVERIFY(std::abs(a[i] - b[i]) < 1e-5);
    }

    void humRemoval()
    {
        // 50 Hz and its harmonics plus a 1 kHz "voice".
        std::vector<float> s(size_t(kRate) * 2 * 2);
        for (int i = 0; i < kRate * 2; ++i) {
            double v = 0.3 * std::sin(2 * M_PI * 1000 * i / kRate);
            for (int h = 1; h <= 3; ++h)
                v += 0.1 * std::sin(2 * M_PI * 50 * h * i / kRate);
            s[size_t(i) * 2] = s[size_t(i) * 2 + 1] = float(v);
        }
        const std::vector<float> in = s;
        AudioChain chain;
        chain.configure({ClipEffect::make(ClipEffect::Kind::HumRemoval)}, kRate, 2);
        QCOMPARE(chain.latency(), 0);
        chain.process(s.data(), kRate * 2);
        const int from = kRate, to = kRate * 2; // settled
        for (int h = 1; h <= 3; ++h)
            QVERIFY2(db(tone(s, 50 * h, from, to) / tone(in, 50 * h, from, to)) < -25, qPrintable(QString::number(h)));
        QVERIFY(std::abs(db(tone(s, 1000, from, to) / tone(in, 1000, from, to))) < 0.5);
        // 60 Hz setting leaves 50 Hz alone.
        std::vector<float> s60 = in;
        AudioChain c60;
        c60.configure({effect(ClipEffect::Kind::HumRemoval, QStringLiteral("hum_freq"), 60)}, kRate, 2);
        c60.process(s60.data(), kRate * 2);
        QVERIFY(std::abs(db(tone(s60, 50, from, to) / tone(in, 50, from, to))) < 1.5);
    }

    void noiseReduction()
    {
        // 3 s: noise only for the first second, then a tone over the noise.
        const int n = kRate * 3;
        std::vector<float> s(size_t(n) * 2);
        QRandomGenerator rng(42);
        for (int i = 0; i < n; ++i) {
            const double noise = 0.02 * (rng.generateDouble() * 2 - 1);
            const double voice = i >= kRate ? 0.3 * std::sin(2 * M_PI * 440 * i / kRate) : 0;
            s[size_t(i) * 2] = s[size_t(i) * 2 + 1] = float(noise + voice);
        }
        const std::vector<float> in = s;
        AudioChain chain;
        chain.configure({effect(ClipEffect::Kind::NoiseReduction, QStringLiteral("reduction"), 20)}, kRate, 2);
        const int lat = chain.latency();
        QCOMPARE(lat, Denoiser::kLatency);
        for (int i = 0; i < n; i += 1920) // in blocks, as the engine does
            chain.process(s.data() + size_t(i) * 2, std::min(1920, n - i));
        // Noise alone (after a moment to learn): about 20 dB lower.
        const double quiet = db(rms(s, kRate / 2 + lat, kRate) / rms(in, kRate / 2, kRate - lat));
        QVERIFY2(quiet < -14, qPrintable(QString::number(quiet)));
        // The tone keeps its level (compare with the same delay).
        const int from = kRate * 2, to = kRate * 3 - lat;
        double kept = 0;
        {
            std::vector<float> shifted(s.begin() + size_t(lat) * 2, s.end());
            shifted.resize(s.size(), 0);
            kept = db(tone(shifted, 440, from, to) / tone(in, 440, from, to));
        }
        QVERIFY2(std::abs(kept) < 1.0, qPrintable(QString::number(kept)));

        // Latency: a click comes out exactly `lat` samples later.
        std::vector<float> click(size_t(kRate) * 2, 0.0f);
        click[size_t(1000) * 2] = click[size_t(1000) * 2 + 1] = 1.0f;
        AudioChain c2;
        c2.configure({effect(ClipEffect::Kind::NoiseReduction, QStringLiteral("reduction"), 0.0001)}, kRate, 2);
        c2.configure({effect(ClipEffect::Kind::NoiseReduction, QStringLiteral("sensitivity"), 0)}, kRate, 2);
        c2.process(click.data(), kRate);
        int at = 0;
        for (int i = 1; i < kRate; ++i)
            if (std::abs(click[size_t(i) * 2]) > std::abs(click[size_t(at) * 2]))
                at = i;
        QCOMPARE(at, 1000 + lat);
    }

    void engine()
    {
        ProjectController ctl;
        QVERIFY2(ctl.canPlay(), qPrintable(ctl.engineError()));
        QCOMPARE(ctl.importFiles({m_noisy}), 1);
        waitIdle(ctl);
        QVERIFY(!ctl.placeMedia({ctl.project().items.first().id}, 0, 0).isEmpty());
        const int a1 = ctl.timeline().indexOf(Kind::Audio, 0);
        const QUuid audio = ctl.timeline().tracks[a1].clips[0].id;
        TimelinePlayer player;
        auto level = [&] {
            player.setProject(ctl.project());
            double l = 0;
            for (qint64 f = 0; f <= 60; ++f) // contiguous from the start: it learns the noise
                l = player.audioLevel(f);
            return l;
        };
        const double full = level();
        QVERIFY(full > 0.02);
        QCOMPARE(ctl.addClipEffect(audio, ClipEffect::Kind::NoiseReduction), 0);
        QVERIFY(ctl.setClipEffectValue(audio, 0, QStringLiteral("reduction"), 24));
        const double reduced = db(level() / full);
        QVERIFY2(reduced < -15, qPrintable(QString::number(reduced)));
    }

    void panel()
    {
        MainWindow w;
        w.resize(1366, 768);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        ProjectController *ctl = w.controller();
        QCOMPARE(ctl->importFiles({m_noisy}), 1);
        waitIdle(*ctl);
        ctl->placeMedia({ctl->project().items.first().id}, 0, 0);
        const QUuid audio = ctl->timeline().tracks[ctl->timeline().indexOf(Kind::Audio, 0)].clips[0].id;
        auto *panel = w.findChild<EffectsPanel *>(QStringLiteral("effectsPanel"));
        w.findChild<QTabWidget *>(QStringLiteral("rightTabs"))->setCurrentWidget(panel);
        w.findChild<TimelineView *>(QStringLiteral("timelineView"))->setSelection({audio});
        auto *kind = panel->findChild<QComboBox *>(QStringLiteral("effectAddKind"));
        QCOMPARE(kind->count(), ClipEffect::kinds(true).size());
        kind->setCurrentIndex(kind->findData(int(ClipEffect::Kind::HumRemoval)));
        panel->findChild<QToolButton *>(QStringLiteral("effectAdd"))->click();
        auto *freq = panel->findChild<QDoubleSpinBox *>(QStringLiteral("effectParam_hum_freq"));
        QTRY_VERIFY(freq && freq->isVisible());
        freq->setValue(60);
        QCOMPARE(ctl->clipEffects(audio)[0].value(QStringLiteral("hum_freq")), 60.0);
        QCOMPARE(EffectsPanel::kindName(ClipEffect::Kind::NoiseReduction), EffectsPanel::tr("Noise reduction"));
    }
};

QTEST_MAIN(TestPhase10c)
#include "tst_phase10c.moc"
