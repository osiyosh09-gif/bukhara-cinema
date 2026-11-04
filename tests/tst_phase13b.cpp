// Phase 13 (part 2) acceptance tests: render preview (regions and keys,
// the preview writer, rendering in the background, playback from the files,
// cancel, delete, menu and cache dialog).
#include "app/CacheDialog.h"
#include "app/MainWindow.h"
#include "app/ProjectController.h"
#include "core/MediaCache.h"
#include "core/MediaProbe.h"
#include "core/RenderPreview.h"
#include "engine/TimelinePlayer.h"

#include <QAction>
#include <QDataStream>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QUndoStack>
#include <QtTest>

#include <cmath>

using namespace bc;

namespace {

const QSize kPreview(960, 540);

// Mean absolute difference of two pictures (0..255).
double difference(const QImage &a, const QImage &b)
{
    if (a.isNull() || b.isNull() || a.size() != b.size())
        return 255;
    const QImage x = a.convertToFormat(QImage::Format_RGB32), y = b.convertToFormat(QImage::Format_RGB32);
    double sum = 0;
    qint64 n = 0;
    for (int row = 0; row < x.height(); row += 3) {
        const QRgb *p = reinterpret_cast<const QRgb *>(x.constScanLine(row));
        const QRgb *q = reinterpret_cast<const QRgb *>(y.constScanLine(row));
        for (int col = 0; col < x.width(); col += 3, ++n)
            sum += std::abs(qRed(p[col]) - qRed(q[col])) + std::abs(qGreen(p[col]) - qGreen(q[col]))
                   + std::abs(qBlue(p[col]) - qBlue(q[col]));
    }
    return n > 0 ? sum / (3.0 * n) : 255;
}

bool writeSine(const QString &path, int seconds)
{
    const int rate = 48000;
    QByteArray pcm(rate * seconds * 2, 0);
    auto *out = reinterpret_cast<qint16 *>(pcm.data());
    for (int i = 0; i < rate * seconds; ++i)
        out[i] = qint16(12000 * std::sin(2 * M_PI * 440.0 * i / rate));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    QDataStream st(&f);
    st.setByteOrder(QDataStream::LittleEndian);
    st.writeRawData("RIFF", 4);
    st << quint32(36 + pcm.size());
    st.writeRawData("WAVEfmt ", 8);
    st << quint32(16) << quint16(1) << quint16(1) << quint32(rate) << quint32(rate * 2) << quint16(2) << quint16(16);
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

QList<TimelinePlayer::PreviewSegment> renderedSegments(const Project &p)
{
    QList<TimelinePlayer::PreviewSegment> out;
    for (const RenderPreview::Region &r : RenderPreview::regions(p, p.timeline, kPreview))
        if (QFile::exists(RenderPreview::fileFor(r.key)))
            out << TimelinePlayer::PreviewSegment{r.start, r.length, RenderPreview::fileFor(r.key)};
    return out;
}

} // namespace

class TestPhase13b : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("BukharaCinemaTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_phase13b"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        QDir(RenderPreview::folder()).removeRecursively();
    }

    void regionsAndKeys()
    {
        ProjectController ctl;
        const QUuid a = ctl.addTitle(0);   // 0..125
        const QUuid b = ctl.addTitle(200); // 200..325, a gap before it
        QVERIFY(!a.isNull() && !b.isNull());
        QList<RenderPreview::Region> r = RenderPreview::regions(ctl.project(), ctl.timeline(), kPreview);
        QCOMPARE(r.size(), qsizetype(2)); // the gap has no picture
        QCOMPARE(r[0].start, 0);
        QCOMPARE(r[0].length, 125);
        QCOMPARE(r[1].start, 200);
        QVERIFY(!r[0].heavy && !r[1].heavy); // plain titles play without rendering
        QCOMPARE(r[0].key, r[1].key);        // same picture, wherever it lies
        QCOMPARE(RenderPreview::fileFor(r[0].key).startsWith(MediaCache::folder(MediaCache::Kind::Preview)), true);

        // Motion makes a clip heavy and changes its key.
        ClipTransform t;
        t.scale = 50;
        QVERIFY(ctl.setClipTransform(b, t, 200));
        r = RenderPreview::regions(ctl.project(), ctl.timeline(), kPreview);
        QVERIFY(!r[0].heavy && r[1].heavy);
        QVERIFY(r[0].key != r[1].key);
        const QByteArray heavyKey = r[1].key;

        // Moving the clip keeps its key (the file is used again).
        QVERIFY(ctl.moveClips({b}, 50, Timeline::Kind::Video, 0));
        r = RenderPreview::regions(ctl.project(), ctl.timeline(), kPreview);
        QCOMPARE(r[1].start, 250);
        QCOMPARE(r[1].key, heavyKey);

        // A title on V2 over part of the first: three regions, the middle one
        // (two layers) heavy.
        Project p = ctl.project();
        const int v2 = p.timeline.addTrack(Timeline::Kind::Video); // above the titles' track
        TitleData td;
        td.text = QStringLiteral("Top");
        p.timeline.placeTitle(td, v2, 50, 25);
        r = RenderPreview::regions(p, p.timeline, kPreview);
        QCOMPARE(r.size(), qsizetype(4));
        QCOMPARE(r[0].length, 50);
        QVERIFY(!r[0].heavy);
        QCOMPARE(r[1].start, 50);
        QCOMPARE(r[1].length, 25);
        QVERIFY(r[1].heavy);
        QVERIFY(!r[2].heavy);
        // A hidden track does not count.
        p.timeline.tracks[v2].hidden = true;
        QCOMPARE(RenderPreview::regions(p, p.timeline, kPreview).size(), qsizetype(2));

        // A transition joins the two clips around the cut into one region.
        Project q = ctl.project();
        QList<TimelineClip> &clips = q.timeline.tracks[q.timeline.find(a).track].clips;
        QCOMPARE(clips.size(), qsizetype(2));
        clips[1].start = clips[0].end();
        clips[1].head.kind = ClipTransition::Kind::Dissolve;
        clips[1].head.length = 20;
        r = RenderPreview::regions(q, q.timeline, kPreview);
        QCOMPARE(r.size(), qsizetype(1));
        QCOMPARE(r[0].length, 250);
        QVERIFY(r[0].heavy);
        // The size of the preview is part of the key.
        QVERIFY(RenderPreview::regions(q, q.timeline, QSize(640, 360))[0].key != r[0].key);
    }

    void writer()
    {
        const QString file = QDir(RenderPreview::folder()).filePath(QStringLiteral("writer-test.mkv"));
        RenderPreview::Writer w;
        QString err;
        QVERIFY2(w.open(file, QSize(320, 180), 25, 1, &err), qPrintable(err));
        QImage red(320, 180, QImage::Format_RGBA8888);
        red.fill(QColor(220, 30, 40));
        for (int i = 0; i < 25; ++i)
            QVERIFY(w.write(i == 10 ? red.scaled(100, 50) : red, &err)); // other sizes are scaled
        QVERIFY(!QFile::exists(file)); // written to a temporary file first
        QVERIFY2(w.finish(&err), qPrintable(err));
        QVERIFY(QFile::exists(file));
        QVERIFY(!QFile::exists(file + QStringLiteral(".part")));
        MediaInfo info;
        QVERIFY(MediaProbe::probe(file, info, &err));
        QCOMPARE(info.width, 320);
        QVERIFY2(std::abs(info.durationUs / 1e6 - 1.0) < 0.1, qPrintable(QString::number(info.durationUs)));

        // Aborted: nothing stays.
        const QString other = QDir(RenderPreview::folder()).filePath(QStringLiteral("aborted.mkv"));
        {
            RenderPreview::Writer a;
            QVERIFY(a.open(other, QSize(64, 64), 25, 1));
            QVERIFY(a.write(red));
        } // destroyed without finish()
        QVERIFY(!QFile::exists(other));
        QVERIFY(!QFile::exists(other + QStringLiteral(".part")));
        QFile::remove(file);
    }

    void playerUsesPreviewFiles()
    {
        if (!Engine::init())
            QSKIP("video engine not available");
        ProjectController ctl;
        ctl.addTitle(0);
        Project p = ctl.project();
        // A preview file of solid green for 0..125.
        const QList<RenderPreview::Region> r = RenderPreview::regions(p, p.timeline, kPreview);
        QCOMPARE(r.size(), qsizetype(1));
        const QString file = QDir(RenderPreview::folder()).filePath(QStringLiteral("green.mkv"));
        RenderPreview::Writer w;
        QVERIFY(w.open(file, kPreview, 25, 1));
        QImage green(kPreview, QImage::Format_RGBA8888);
        green.fill(QColor(30, 200, 60));
        for (int i = 0; i < 125; ++i)
            QVERIFY(w.write(green));
        QVERIFY(w.finish());

        TimelinePlayer player;
        player.setPreviews({{0, 125, file}});
        player.setProject(p);
        const QImage img = player.renderFrame(60);
        QVERIFY(!img.isNull());
        const QColor c = img.pixelColor(img.width() / 2, 10);
        QVERIFY2(std::abs(c.red() - 30) < 10 && std::abs(c.green() - 200) < 10 && std::abs(c.blue() - 60) < 10,
                 qPrintable(c.name()));
        // Without previews the title is drawn as usual.
        player.setPreviews({});
        player.setProject(p);
        QVERIFY(player.renderFrame(60).pixelColor(img.width() / 2, 10) != c);
        QFile::remove(file);
    }

    void renderInBackground()
    {
        ProjectController ctl;
        if (!ctl.canPlay())
            QSKIP("video engine not available");
        const QUuid a = ctl.addTitle(0);
        ctl.addTitle(125);
        const QUuid c = ctl.addTitle(250);
        ClipTransform t;
        t.scale = 60;
        t.rotation = 10;
        QVERIFY(ctl.setClipTransform(a, t, 0));
        QVERIFY(ctl.setClipTransform(c, t, 250));
        QList<ProjectController::PreviewPart> parts = ctl.previewParts();
        QCOMPARE(parts.size(), qsizetype(3));
        QVERIFY(parts[0].heavy && !parts[1].heavy && parts[2].heavy);
        QVERIFY(!parts[0].rendered);

        // Only the second clip (not heavy): nothing to do.
        QString err;
        QVERIFY(!ctl.renderPreview(130, 200, &err));
        QVERIFY(!err.isEmpty());

        QSignalSpy done(&ctl, &ProjectController::previewFinished);
        QSignalSpy changed(&ctl, &ProjectController::previewsChanged);
        QSignalSpy progress(&ctl, &ProjectController::previewProgress);
        QVERIFY2(ctl.renderPreview(0, 100, &err), qPrintable(err)); // touches the first part only
        QVERIFY(ctl.isRenderingPreview());
        QVERIFY(!ctl.renderPreview(0, 400, &err)); // one at a time
        QVERIFY(done.wait(120000));
        QCOMPARE(done.first().at(0).toBool(), true);
        QVERIFY(!ctl.isRenderingPreview());
        QVERIFY(changed.count() >= 1);
        QCOMPARE(progress.last().at(0).toLongLong(), 125);
        parts = ctl.previewParts();
        QVERIFY(parts[0].rendered && !parts[1].rendered && !parts[2].rendered);

        // The Program monitor now plays the file there; it looks like the
        // computed picture.
        Project p = ctl.project();
        TimelinePlayer computed, fromFile;
        computed.setProject(p);
        fromFile.setPreviews(renderedSegments(p));
        QCOMPARE(fromFile.previews().size(), qsizetype(1));
        fromFile.setProject(p);
        for (qint64 f : {0, 40, 124}) {
            const double d = difference(computed.renderFrame(f), fromFile.renderFrame(f));
            QVERIFY2(d < 4.0, qPrintable(QStringLiteral("frame %1: %2").arg(f).arg(d)));
        }

        // A colourful picture with sound under it: the rendered part looks
        // like the computed one and the sound still plays.
        const QString png = m_dir.filePath(QStringLiteral("colours.png"));
        QImage pic(320, 180, QImage::Format_RGB32);
        for (int y = 0; y < pic.height(); ++y)
            for (int x = 0; x < pic.width(); ++x)
                pic.setPixelColor(x, y, QColor::fromHsv((x * 359) / pic.width(), 120 + y / 2, 230));
        QVERIFY(pic.save(png));
        const QString wav = m_dir.filePath(QStringLiteral("tone.wav"));
        QVERIFY(writeSine(wav, 10));
        ctl.importFiles({png, wav});
        waitIdle(ctl);
        QUuid picId, wavId;
        for (const MediaItem &m : ctl.project().items) {
            if (m.path == png)
                picId = m.id;
            else if (m.path == wav)
                wavId = m.id;
        }
        QVERIFY(!picId.isNull() && !wavId.isNull());
        const int v1 = ctl.timeline().indexOf(Timeline::Kind::Video, 0);
        const int a1 = ctl.timeline().indexOf(Timeline::Kind::Audio, 0);
        const QList<QUuid> placed = ctl.placeMedia({picId}, v1, 400);
        QVERIFY(!placed.isEmpty());
        QVERIFY(!ctl.placeMedia({wavId}, a1, 400).isEmpty());
        t.rotation = -8;
        QVERIFY(ctl.setClipTransform(placed.first(), t, 400));
        done.clear();
        QVERIFY2(ctl.renderPreview(400, 401, &err), qPrintable(err));
        QVERIFY(done.wait(120000));
        QVERIFY(done.first().at(0).toBool());
        p = ctl.project();
        computed.setProject(p);
        fromFile.setPreviews(renderedSegments(p));
        // The first part keeps its file: a clip on another track elsewhere
        // does not change it.
        QCOMPARE(fromFile.previews().size(), qsizetype(2));
        QCOMPARE(fromFile.previews().last().start, 400);
        fromFile.setProject(p);
        for (qint64 f : {400, 430}) {
            const double d = difference(computed.renderFrame(f), fromFile.renderFrame(f));
            QVERIFY2(d < 2.5, qPrintable(QStringLiteral("frame %1: %2").arg(f).arg(d)));
        }
        const double soundComputed = computed.audioLevel(420), soundPreview = fromFile.audioLevel(420);
        QVERIFY2(soundComputed > 0.05 && std::abs(soundPreview - soundComputed) < 0.01,
                 qPrintable(QStringLiteral("%1 %2").arg(soundComputed).arg(soundPreview)));

        // An edit of the clip makes its part red again; undo brings the file back.
        t.rotation = 20;
        QVERIFY(ctl.setClipTransform(a, t, 0));
        QVERIFY(!ctl.previewParts()[0].rendered);
        ctl.undoStack()->undo();
        QVERIFY(ctl.previewParts()[0].rendered);

        // Cancel: no file, no error.
        done.clear();
        QVERIFY(ctl.renderPreview(0, 400, &err)); // the third part
        ctl.cancelPreviewRender();
        QVERIFY(done.wait(60000));
        QCOMPARE(done.first().at(0).toBool(), false);
        QVERIFY(done.first().at(1).toString().isEmpty());
        QVERIFY(!ctl.previewParts()[2].rendered);
        const QStringList left = QDir(RenderPreview::folder()).entryList({QStringLiteral("*.part")}, QDir::Files);
        QVERIFY2(left.isEmpty(), qPrintable(left.join(u',')));

        // Delete all preview files. This test's own player lets go of them
        // first: Windows cannot delete open files (the controller closes its
        // Program monitor's files itself).
        fromFile.setPreviews({});
        fromFile.setProject(p);
        QVERIFY(ctl.deletePreviews() >= 1);
        QVERIFY(!ctl.previewParts()[0].rendered);
        QVERIFY(QDir(RenderPreview::folder()).entryList(QDir::Files).isEmpty());
        QVERIFY(!ctl.previewParts()[3].rendered);
    }

    void menuAndCacheDialog()
    {
        MainWindow w;
        w.resize(1200, 700);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        ProjectController *ctl = w.controller();
        QAction *render = w.findChild<QAction *>(QStringLiteral("actRenderPreview"));
        QAction *del = w.findChild<QAction *>(QStringLiteral("actDeletePreviews"));
        QVERIFY(render && del);
        QCOMPARE(render->isEnabled(), ctl->canPlay());
        QCOMPARE(render->shortcut(), QKeySequence(Qt::Key_Return));
        QVERIFY(!render->text().contains(QStringLiteral("13"))); // no longer a planned item

        CacheDialog dlg(ctl, &w);
        auto *clear = dlg.findChild<QPushButton *>(QStringLiteral("clearPreviews"));
        QVERIFY(clear);
        if (!ctl->canPlay())
            return;
        const QUuid a = ctl->addTitle(0);
        ClipTransform t;
        t.opacity = 50;
        QVERIFY(ctl->setClipTransform(a, t, 0));
        QSignalSpy done(ctl, &ProjectController::previewFinished);
        render->trigger(); // nothing selected: the whole sequence
        QVERIFY(ctl->isRenderingPreview());
        QVERIFY(w.findChild<QLabel *>(QStringLiteral("previewStatus"))->isVisible());
        QVERIFY(done.wait(120000));
        QVERIFY(done.first().at(0).toBool());
        QVERIFY(!w.findChild<QLabel *>(QStringLiteral("previewStatus"))->isVisible());
        QVERIFY(ctl->previewParts()[0].rendered);
        dlg.refresh();
        QVERIFY(clear->isEnabled());
        clear->click();
        QVERIFY(!ctl->previewParts()[0].rendered);
        QVERIFY(!clear->isEnabled());
    }
};

QTEST_MAIN(TestPhase13b)
#include "tst_phase13b.moc"
