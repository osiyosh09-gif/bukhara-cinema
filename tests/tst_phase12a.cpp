// Phase 12 (part 1) acceptance tests: lining recordings up by sound,
// multicamera sequences, clip angles (model, file, engine, dialog, inspector).
#include "app/ClipInspector.h"
#include "app/MainWindow.h"
#include "app/MediaBinPanel.h"
#include "app/MulticamDialog.h"
#include "app/ProjectController.h"
#include "app/TimelinePanel.h"
#include "core/AudioSync.h"
#include "core/MediaCache.h"
#include "engine/TimelinePlayer.h"

#include <QAction>
#include <QComboBox>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QGroupBox>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUndoStack>
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

using namespace bc;
using Kind = Timeline::Kind;

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

// Speech-like envelope: bursts of random length and level with pauses.
std::vector<float> bursts(int seconds, unsigned seed)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> u(0.f, 1.f);
    std::vector<float> env;
    while (int(env.size()) < seconds * AudioSync::kRate) {
        const int len = 5 + int(u(rng) * 40);
        const float level = u(rng) < 0.35f ? 0.002f : 0.05f + u(rng) * 0.4f;
        for (int i = 0; i < len; ++i)
            env.push_back(level * (0.8f + 0.4f * u(rng)));
    }
    env.resize(size_t(seconds) * AudioSync::kRate);
    return env;
}

// 20 s mono 48 kHz WAV: noise whose level follows bursts(), sounding in the
// first second (so every part of it has sound soon).
bool writeBurstsWav(const QString &path, int seconds)
{
    const int rate = 48000;
    std::vector<float> env = bursts(seconds, 42);
    for (int i = 0; i < AudioSync::kRate; ++i)
        env[size_t(i)] = 0.3f;
    std::mt19937 rng(5);
    std::uniform_real_distribution<float> u(-1.f, 1.f);
    QByteArray pcm;
    const int n = rate * seconds;
    pcm.resize(n * 2);
    auto *out = reinterpret_cast<qint16 *>(pcm.data());
    for (int i = 0; i < n; ++i) {
        const float level = env[size_t(std::min<qint64>(qint64(env.size()) - 1, qint64(i) * AudioSync::kRate / rate))];
        out[i] = qint16(std::clamp(level * 2.f * u(rng), -1.f, 1.f) * 32767);
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    QDataStream s(&f);
    s.setByteOrder(QDataStream::LittleEndian);
    s.writeRawData("RIFF", 4);
    s << quint32(36 + pcm.size());
    s.writeRawData("WAVEfmt ", 8);
    s << quint32(16) << quint16(1) << quint16(1) << quint32(rate) << quint32(rate * 2) << quint16(2) << quint16(16);
    s.writeRawData("data", 4);
    s << quint32(pcm.size());
    s.writeRawData(pcm.constData(), int(pcm.size()));
    return true;
}

const TimelineClip *firstClip(const Timeline &tl, Kind kind)
{
    const int t = tl.indexOf(kind, 0);
    return t >= 0 && !tl.tracks[t].clips.isEmpty() ? &tl.tracks[t].clips.first() : nullptr;
}

} // namespace

class TestPhase12a : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_camA; // testsrc picture, the sound from 0 s
    QString m_camB; // red picture, the same sound from 4.2 s, quieter

    QList<QUuid> importBoth(ProjectController &ctl)
    {
        ctl.importFiles({m_camA, m_camB});
        waitIdle(ctl);
        QList<QUuid> ids(2);
        for (const MediaItem &m : ctl.project().items)
            ids[m.path.endsWith(QStringLiteral("camB.mkv")) ? 1 : 0] = m.id;
        return ids;
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("BukharaCinemaTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_phase12a"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        QSettings().setValue(QStringLiteral("proxy/auto"), false);
        QDir(MediaCache::root()).removeRecursively();
        QVERIFY(m_dir.isValid());
        const QString master = m_dir.filePath(QStringLiteral("event.wav"));
        // Noise in bursts that start and stop at irregular times (like speech).
        QVERIFY(writeBurstsWav(master, 20));
        m_camA = m_dir.filePath(QStringLiteral("camA.mkv"));
        m_camB = m_dir.filePath(QStringLiteral("camB.mkv"));
        QVERIFY(ffmpeg({QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                        QStringLiteral("testsrc=size=320x240:rate=25:duration=15"), QStringLiteral("-i"), master,
                        QStringLiteral("-t"), QStringLiteral("15"), QStringLiteral("-c:v"), QStringLiteral("mpeg4"),
                        QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"), m_camA}));
        QVERIFY(ffmpeg({QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                        QStringLiteral("color=c=red:size=320x240:rate=25:duration=12"), QStringLiteral("-ss"),
                        QStringLiteral("4.2"), QStringLiteral("-i"), master, QStringLiteral("-t"), QStringLiteral("12"),
                        QStringLiteral("-af"), QStringLiteral("volume=0.5"), QStringLiteral("-c:v"),
                        QStringLiteral("mpeg4"), QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"), m_camB}));
    }

    void matchEnvelopes()
    {
        const std::vector<float> ref = bursts(60, 7);
        // A part that starts 12.34 s in, at another level, with some noise.
        std::vector<float> later(ref.begin() + 1234, ref.begin() + 1234 + 30 * AudioSync::kRate);
        std::mt19937 rng(3);
        std::uniform_real_distribution<float> u(0.9f, 1.1f);
        for (float &v : later)
            v = v * 0.3f * u(rng) + 0.001f;
        AudioSync::Match m = AudioSync::match(ref, later);
        QVERIFY(m.valid);
        QVERIFY2(std::abs(m.offset - 12.34) < 0.02, qPrintable(QString::number(m.offset)));
        QVERIFY(m.confidence > 0.2);
        // The other way round: the reference starts later.
        m = AudioSync::match(later, ref);
        QVERIFY(m.valid);
        QVERIFY2(std::abs(m.offset + 12.34) < 0.02, qPrintable(QString::number(m.offset)));
        // A recording that started earlier and runs past the end.
        std::vector<float> wide = bursts(10, 99);
        wide.insert(wide.end(), ref.begin(), ref.begin() + 20 * AudioSync::kRate);
        m = AudioSync::match(ref, wide);
        QVERIFY(m.valid);
        QVERIFY2(std::abs(m.offset + 10.0) < 0.02, qPrintable(QString::number(m.offset)));
        // Lag limit: the true place is outside it, so no clear match.
        QVERIFY(!AudioSync::match(ref, later, 5.0).valid || std::abs(AudioSync::match(ref, later, 5.0).offset - 12.34) > 1);
        // A different event does not match.
        QVERIFY(!AudioSync::match(ref, bursts(30, 1234)).valid);
        // Under a second: nothing to match.
        QVERIFY(!AudioSync::match(ref, std::vector<float>(50, 0.1f)).valid);

        // align: offsets from the first recording with sound; silent ones are left out.
        const QList<AudioSync::Match> all = AudioSync::align({{}, ref, later});
        QCOMPARE(all.size(), qsizetype(3));
        QVERIFY(!all[0].valid);
        QVERIFY(all[1].valid);
        QCOMPARE(all[1].offset, 0.0);
        QVERIFY(all[2].valid);
        QVERIFY(std::abs(all[2].offset - 12.34) < 0.02);
    }

    void envelopeOfFiles()
    {
        std::vector<float> a, b;
        QVERIFY(AudioSync::envelope(m_camA, a));
        QVERIFY(AudioSync::envelope(m_camB, b));
        QVERIFY(std::abs(int(a.size()) - 15 * AudioSync::kRate) <= 2);
        QVERIFY(std::abs(int(b.size()) - 12 * AudioSync::kRate) <= 2);
        const AudioSync::Match m = AudioSync::match(a, b);
        QVERIFY(m.valid);
        QVERIFY2(std::abs(m.offset - 4.2) < 0.03, qPrintable(QString::number(m.offset)));
        std::vector<float> none;
        QString err;
        QVERIFY(!AudioSync::envelope(m_dir.filePath(QStringLiteral("missing.wav")), none, &err));
        QVERIFY(!err.isEmpty());
    }

    void multicamModel()
    {
        ProjectController ctl;
        const QList<QUuid> ids = importBoth(ctl);
        QVERIFY(!ids[0].isNull() && !ids[1].isNull());
        ctl.setPlayhead(50);
        const int steps = ctl.undoStack()->count();
        // B starts 105 frames (4.2 s) later; starts are made relative.
        const QUuid nest = ctl.createMulticam(ids, {1000, 1105}, 1);
        QVERIFY(!nest.isNull());
        QCOMPARE(ctl.undoStack()->count(), steps + 1);
        QVERIFY(ctl.isMulticam(nest));
        const NestedSequence *n = ctl.project().nest(nest);
        QVERIFY(n && n->multicam);
        QCOMPARE(n->timeline.count(Kind::Video), 2);
        QCOMPARE(n->timeline.count(Kind::Audio), 2);
        QCOMPARE(n->timeline.tracks[n->timeline.indexOf(Kind::Video, 0)].clips.first().start, 0);
        QCOMPARE(n->timeline.tracks[n->timeline.indexOf(Kind::Video, 1)].clips.first().start, 105);
        QCOMPARE(n->timeline.tracks[n->timeline.indexOf(Kind::Audio, 1)].clips.first().start, 105);
        QCOMPARE(n->timeline.duration(), 105 + 300);
        QCOMPARE(ctl.multicamAngles(nest, Kind::Video), (QStringList{QStringLiteral("camA.mkv"), QStringLiteral("camB.mkv")}));

        // One linked clip at the playhead: picture of angle 1, sound of angle 2 (asked for).
        const TimelineClip *v = firstClip(ctl.timeline(), Kind::Video);
        const TimelineClip *a = firstClip(ctl.timeline(), Kind::Audio);
        QVERIFY(v && a);
        QCOMPARE(v->nestId, nest);
        QCOMPARE(v->start, 50);
        QCOMPARE(v->length, 405);
        QCOMPARE(v->angle, 0);
        QCOMPARE(a->angle, 1);
        QVERIFY(!v->linkId.isNull() && v->linkId == a->linkId);
        const QUuid vid = v->id, aid = a->id;

        // Switching: split, then the second part shows angle 2.
        QVERIFY(ctl.splitClips(200, {vid}));
        QUuid second;
        for (const TimelineClip &c : ctl.timeline().tracks[ctl.timeline().indexOf(Kind::Video, 0)].clips)
            if (c.start == 200)
                second = c.id;
        QVERIFY(ctl.setClipAngle(second, 1));
        QCOMPARE(ctl.timeline().clip(second)->angle, 1);
        QCOMPARE(ctl.timeline().clip(vid)->angle, 0);
        QVERIFY(!ctl.setClipAngle(second, 2));  // no third angle
        QVERIFY(!ctl.setClipAngle(second, 1));  // no change
        QVERIFY(ctl.setClipAngle(aid, 0));
        ctl.undoStack()->undo();
        QCOMPARE(ctl.timeline().clip(aid)->angle, 1);

        // A normal nested clip has no angles.
        const QUuid plain = ctl.placeMedia({ids[0]}, 1, 600).value(0);
        QVERIFY(!plain.isNull());
        QVERIFY(!ctl.setClipAngle(plain, 0));

        // Saved and opened again.
        const QString file = m_dir.filePath(QStringLiteral("mc.bcproj"));
        QVERIFY(ctl.saveAs(file, nullptr));
        ProjectController other;
        QVERIFY(other.open(file, nullptr));
        QVERIFY(other.isMulticam(nest));
        QCOMPARE(other.timeline().clip(second)->angle, 1);
        QCOMPARE(other.project().nest(nest)->timeline, n->timeline);

        // Images cannot be angles.
        const QString png = m_dir.filePath(QStringLiteral("still.png"));
        QImage img(64, 48, QImage::Format_RGB32);
        img.fill(Qt::blue);
        QVERIFY(img.save(png));
        ctl.importFiles({png});
        waitIdle(ctl);
        QUuid still;
        for (const MediaItem &m : ctl.project().items)
            if (m.path == png)
                still = m.id;
        QVERIFY(ctl.createMulticam({ids[0], still}, {0, 0}).isNull());
    }

    void engineShowsTheAngle()
    {
        ProjectController ctl;
        if (!ctl.canPlay())
            QSKIP("video engine not available");
        const QList<QUuid> ids = importBoth(ctl);
        ctl.setPlayhead(0);
        const QUuid nest = ctl.createMulticam(ids, {0, 105}, 0);
        QVERIFY(!nest.isNull());
        const QUuid vid = firstClip(ctl.timeline(), Kind::Video)->id;
        const QUuid aid = firstClip(ctl.timeline(), Kind::Audio)->id;

        auto render = [&](qint64 frame) {
            TimelinePlayer p;
            p.setPreviewLimit(QSize(320, 240));
            p.setProject(ctl.project());
            return p.renderFrame(frame).convertToFormat(QImage::Format_RGB32);
        };
        auto level = [&](qint64 frame) { // loudest frame of the next second
            TimelinePlayer p;
            p.setProject(ctl.project());
            double most = 0;
            for (qint64 f = frame; f < frame + 25; f += 2)
                most = std::max(most, p.audioLevel(f));
            return most;
        };
        const QImage a = render(150);
        QVERIFY(!a.isNull());
        const QColor ca = a.pixelColor(a.width() / 2, a.height() / 2);
        QVERIFY2(!(ca.red() > 200 && ca.green() < 60), "angle 1 is the test pattern, not red");
        QVERIFY(level(50) > 0.01);

        QVERIFY(ctl.setClipAngle(vid, 1));
        const QImage b = render(150);
        const QColor cb = b.pixelColor(b.width() / 2, b.height() / 2);
        QVERIFY2(cb.red() > 200 && cb.green() < 60 && cb.blue() < 60, qPrintable(cb.name()));
        // Before angle 2 starts there is nothing to show.
        const QImage early = render(50);
        const QColor ce = early.pixelColor(early.width() / 2, early.height() / 2);
        QVERIFY2(ce.red() < 30 && ce.green() < 30 && ce.blue() < 30, qPrintable(ce.name()));

        // Sound of angle 2: silent before it starts, sounding after.
        QVERIFY(ctl.setClipAngle(aid, 1));
        QVERIFY(level(50) < 0.001); // frames 50..75: angle 2 starts at 105
        QVERIFY(level(150) > 0.005);
    }

    void dialogAndInspector()
    {
        MainWindow w;
        w.resize(1366, 768);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        ProjectController *ctl = w.controller();
        const QList<QUuid> ids = importBoth(*ctl);

        // The action needs two recordings selected in the bin.
        auto *bin = w.findChild<MediaBinPanel *>(QStringLiteral("mediaBin"));
        QVERIFY(bin);
        QAction *act = w.findChild<QAction *>(QStringLiteral("actCreateMulticam"));
        QVERIFY(act);
        bin->tree()->clearSelection();
        QVERIFY(!act->isEnabled());
        bin->tree()->selectAll();
        QVERIFY(act->isEnabled());
        QCOMPARE(bin->multicamMedia().size(), qsizetype(2));

        ctl->setPlayhead(25);
        MulticamDialog dlg(ctl, ids, &w);
        dlg.setSyncBySound(true);
        QSignalSpy done(&dlg, &MulticamDialog::finished);
        dlg.start();
        QVERIFY(dlg.running());
        QVERIFY(done.wait(30000));
        QCOMPARE(done.first().first().toBool(), true);
        QVERIFY(!dlg.result().isNull());
        QVERIFY2(std::abs(dlg.offsets().value(1) - 4.2) < 0.03, qPrintable(QString::number(dlg.offsets().value(1))));
        const NestedSequence *n = ctl->project().nest(dlg.result());
        QVERIFY(n);
        QCOMPARE(n->timeline.tracks[n->timeline.indexOf(Kind::Video, 1)].clips.first().start, 105);

        // By file start: both at 0, nothing measured.
        MulticamDialog dlg2(ctl, ids, &w);
        dlg2.setSyncBySound(false);
        QSignalSpy done2(&dlg2, &MulticamDialog::finished);
        dlg2.start();
        QCOMPARE(done2.size(), 1);
        const NestedSequence *n2 = ctl->project().nest(dlg2.result());
        QVERIFY(n2);
        QCOMPARE(n2->timeline.tracks[n2->timeline.indexOf(Kind::Video, 1)].clips.first().start, 0);
        QVERIFY(n2->name != n->name);

        // Inspector: picture and sound angle lists for the selected clip.
        auto *view = w.findChild<TimelineView *>(QStringLiteral("timelineView"));
        QVERIFY(view);
        const TimelineClip *v = firstClip(ctl->timeline(), Kind::Video);
        QVERIFY(v && ctl->isMulticam(v->nestId));
        view->setSelection({v->id});
        auto *box = w.findChild<QGroupBox *>(QStringLiteral("clipAngleBox"));
        auto *pic = w.findChild<QComboBox *>(QStringLiteral("clipVideoAngle"));
        auto *snd = w.findChild<QComboBox *>(QStringLiteral("clipAudioAngle"));
        QVERIFY(box && pic && snd);
        QVERIFY(!box->isHidden());
        QCOMPARE(pic->count(), 2);
        QCOMPARE(pic->currentIndex(), 0);
        QVERIFY(pic->itemText(1).contains(QStringLiteral("camB.mkv")));
        emit pic->activated(1);
        QCOMPARE(ctl->timeline().clip(v->id)->angle, 1);
        QCOMPARE(pic->currentIndex(), 1);
        // A plain media clip: no angle box.
        const QUuid plain = ctl->placeMedia({ids[0]}, 1, 2000).value(0);
        view->setSelection({plain});
        QVERIFY(box->isHidden());
    }
};

QTEST_MAIN(TestPhase12a)
#include "tst_phase12a.moc"
