// Phase 13 (part 3) acceptance tests: loudness of the whole mix (measured
// through the engine like the export, master volume set to a target).
#include "app/MainWindow.h"
#include "app/MixLoudnessDialog.h"
#include "app/ProjectController.h"
#include "core/Loudness.h"
#include "core/MediaCache.h"

#include <QAction>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUndoStack>
#include <QtTest>

#include <cmath>

using namespace bc;

namespace {

// Stereo 16-bit WAV: a 1 kHz tone of `amplitude` (0..1) in both channels.
bool writeTone(const QString &path, double seconds, double amplitude)
{
    const int rate = 48000;
    const int n = int(rate * seconds);
    QByteArray pcm(n * 4, 0);
    auto *out = reinterpret_cast<qint16 *>(pcm.data());
    for (int i = 0; i < n; ++i) {
        const qint16 v = qint16(std::lround(32767 * amplitude * std::sin(2 * M_PI * 1000.0 * i / rate)));
        out[2 * i] = v;
        out[2 * i + 1] = v;
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    QDataStream st(&f);
    st.setByteOrder(QDataStream::LittleEndian);
    st.writeRawData("RIFF", 4);
    st << quint32(36 + pcm.size());
    st.writeRawData("WAVEfmt ", 8);
    st << quint32(16) << quint16(1) << quint16(2) << quint32(rate) << quint32(rate * 4) << quint16(4) << quint16(16);
    st.writeRawData("data", 4);
    st << quint32(pcm.size());
    st.writeRawData(pcm.constData(), int(pcm.size()));
    return true;
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

bool measureNow(ProjectController &ctl)
{
    QSignalSpy done(&ctl, &ProjectController::mixLoudnessMeasured);
    QString err;
    if (!ctl.measureMixLoudness(&err)) {
        qWarning() << err;
        return false;
    }
    return done.wait(120000) && done.first().at(0).toBool();
}

} // namespace

class TestPhase13c : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_tone;

    QUuid placeTone(ProjectController &ctl, qint64 start)
    {
        ctl.importFiles({m_tone});
        waitIdle(ctl);
        QUuid media;
        for (const MediaItem &m : ctl.project().items)
            if (m.path == m_tone)
                media = m.id;
        const int a1 = ctl.timeline().indexOf(Timeline::Kind::Audio, 0);
        const QList<QUuid> placed = ctl.placeMedia({media}, a1, start);
        return placed.isEmpty() ? QUuid() : placed.first();
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("BukharaCinemaTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_phase13c"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        QSettings().setValue(QStringLiteral("proxy/auto"), false);
        QDir(MediaCache::root()).removeRecursively();
        QVERIFY(m_dir.isValid());
        m_tone = m_dir.filePath(QStringLiteral("tone.wav"));
        QVERIFY(writeTone(m_tone, 8, 0.1)); // about -20 LUFS, peak -20 dBFS
    }

    void gainToTarget()
    {
        Loudness::Result r;
        QCOMPARE(Loudness::gainToTarget(r, -14, -1), 0.0); // not valid
        r.valid = true;
        r.lufs = -20;
        r.peakDb = -10;
        QCOMPARE(Loudness::gainToTarget(r, -14, -1), 6.0);
        QCOMPARE(Loudness::gainToTarget(r, -23, -1), -3.0);
        QCOMPARE(Loudness::gainToTarget(r, -5, -1), 9.0); // the peak wins over the target
    }

    void measureTheMix()
    {
        ProjectController ctl;
        if (!ctl.canPlay())
            QSKIP("video engine not available");
        Loudness::Profile ref;
        QVERIFY(Loudness::compute(m_tone, ref));
        const Loudness::Result file = Loudness::measure(ref, 0, 8);
        QVERIFY(file.valid);
        QVERIFY2(std::abs(file.lufs + 20) < 1.0, qPrintable(QString::number(file.lufs)));

        QString err;
        QVERIFY(!ctl.measureMixLoudness(&err)); // empty sequence
        QVERIFY(!err.isEmpty());

        const QUuid clip = placeTone(ctl, 0);
        QVERIFY(!clip.isNull());
        ctl.addTitle(0); // a picture changes nothing
        bool current = true;
        ctl.mixLoudness(&current);
        QVERIFY(!current);
        QVERIFY(measureNow(ctl));
        Loudness::Result mix = ctl.mixLoudness(&current);
        QVERIFY(current && mix.valid);
        QVERIFY2(std::abs(mix.lufs - file.lufs) < 0.5, qPrintable(QStringLiteral("%1 %2").arg(mix.lufs).arg(file.lufs)));
        QVERIFY2(std::abs(mix.peakDb + 20) < 0.5, qPrintable(QString::number(mix.peakDb)));
        QVERIFY(std::abs(ctl.mixSeconds() - 8) < 0.1);

        // Clip volume, track fader and master all count: +4 -2 -1 = +1 dB.
        QVERIFY(ctl.setClipAudio(clip, 4, 0, 0));
        QVERIFY(ctl.setTrackGain(ctl.timeline().indexOf(Timeline::Kind::Audio, 0), -2));
        QVERIFY(ctl.setMasterGain(-1));
        ctl.mixLoudness(&current);
        QVERIFY(!current); // the sequence changed
        QVERIFY(measureNow(ctl));
        mix = ctl.mixLoudness(&current);
        QVERIFY2(std::abs(mix.lufs - (file.lufs + 1)) < 0.5, qPrintable(QString::number(mix.lufs)));

        // To -16 LUFS: the master moves by the difference, one undo step.
        const double gain = ctl.mixGain(-16, -1);
        QVERIFY2(std::abs(gain - (-16 - mix.lufs)) < 0.06, qPrintable(QString::number(gain)));
        const int undoBefore = ctl.undoStack()->count();
        QVERIFY(ctl.normalizeMix(-16, -1));
        QCOMPARE(ctl.undoStack()->count(), undoBefore + 1);
        QVERIFY(std::abs(ctl.timeline().masterGainDb - (-1 + gain)) < 0.06);
        mix = ctl.mixLoudness(&current);
        QVERIFY(current);
        QVERIFY(std::abs(mix.lufs + 16) < 0.06);
        QVERIFY(!ctl.normalizeMix(-16, -1)); // nothing more to do
        // Measured again, it really is -16.
        QVERIFY(measureNow(ctl));
        mix = ctl.mixLoudness(&current);
        QVERIFY2(std::abs(mix.lufs + 16) < 0.5, qPrintable(QString::number(mix.lufs)));

        // A loud target is held back by the peak ceiling.
        // (The master fader's +12 dB may stop it first.)
        const double loud = ctl.mixGain(-3, -1);
        QVERIFY2(mix.peakDb + loud <= -1 + 0.06, qPrintable(QString::number(loud)));
        QVERIFY(std::abs(mix.peakDb + loud - (-1)) < 0.06
                || std::abs(ctl.timeline().masterGainDb + loud - TimelineTrack::kMaxGainDb) < 0.06);
        QVERIFY(ctl.normalizeMix(-3, -1));
        QVERIFY(measureNow(ctl));
        mix = ctl.mixLoudness();
        QVERIFY2(mix.peakDb < -0.5, qPrintable(QString::number(mix.peakDb)));

        // Undo brings the old master back.
        const double before = ctl.timeline().masterGainDb;
        ctl.undoStack()->undo();
        QVERIFY(ctl.timeline().masterGainDb < before);

        // Cancel: no result change, no error.
        QSignalSpy done(&ctl, &ProjectController::mixLoudnessMeasured);
        QVERIFY(ctl.measureMixLoudness(&err));
        QVERIFY(ctl.isMeasuringMix());
        QVERIFY(!ctl.measureMixLoudness(&err)); // one at a time
        ctl.cancelMixLoudness();
        QVERIFY(done.wait(60000));
        QVERIFY(done.first().at(1).toString().isEmpty());
        QVERIFY(!ctl.isMeasuringMix());
    }

    void dialog()
    {
        MainWindow w;
        w.resize(1200, 700);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        ProjectController *ctl = w.controller();
        QAction *act = w.findChild<QAction *>(QStringLiteral("actMixLoudness"));
        QVERIFY(act);
        QCOMPARE(act->isEnabled(), ctl->canPlay());
        if (!ctl->canPlay())
            return;
        QVERIFY(!placeTone(*ctl, 0).isNull());

        QSignalSpy done(ctl, &ProjectController::mixLoudnessMeasured);
        MixLoudnessDialog dlg(ctl, &w); // starts measuring at once
        QVERIFY(ctl->isMeasuringMix());
        auto *apply = dlg.findChild<QPushButton *>(QStringLiteral("mixLoudnessApply"));
        auto *result = dlg.findChild<QLabel *>(QStringLiteral("mixLoudnessResult"));
        QVERIFY(apply && result);
        QVERIFY(!apply->isEnabled());
        QVERIFY(done.wait(120000));
        QVERIFY(result->text().contains(QStringLiteral("LUFS")));
        QVERIFY(apply->isEnabled()); // -20 LUFS, target -14 by default
        apply->click();
        QVERIFY2(std::abs(ctl->timeline().masterGainDb - 6) < 0.6, qPrintable(QString::number(ctl->timeline().masterGainDb)));
        QVERIFY(!apply->isEnabled());

        // An edit makes the result old until measured again.
        ctl->addTitle(300);
        QVERIFY(!apply->isEnabled());
        auto *again = dlg.findChild<QPushButton *>(QStringLiteral("mixLoudnessMeasure"));
        QVERIFY(again && again->isEnabled());
    }
};

QTEST_MAIN(TestPhase13c)
#include "tst_phase13c.moc"
