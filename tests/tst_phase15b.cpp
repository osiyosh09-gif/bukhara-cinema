// Phase 15 (part 2) acceptance tests: import from other editors. Our own
// exported files read back the same edit; hand-written files in the style
// of Premiere Pro (XML), DaVinci Resolve (EDL) and current OpenTimelineIO
// (Clip.2) read correctly; the controller puts the edit into the project
// as a nested clip or straight onto the timeline, in one undo step.
//
// With BC_INTERCHANGE_IN set, files written by the OpenTimelineIO reference
// library (tests/make_interchange_refs.py, Linux CI) are imported too.
#include "app/InterchangeImportDialog.h"
#include "app/Language.h"
#include "app/MainWindow.h"
#include "app/ProjectController.h"
#include "core/Interchange.h"

#include <QDir>
#include <QFile>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QUndoStack>
#include <QtTest>

#include <cmath>

using namespace bc;
using Interchange::Format;
using Interchange::ImportClip;
using Interchange::ImportLoss;
using Interchange::ReadResult;
using Kind = TimelineTrack::Kind;

namespace {

SequenceSettings rate(int num, int den)
{
    SequenceSettings s;
    s.fpsNum = num;
    s.fpsDen = den;
    return s;
}

MediaItem media(const QString &path, double seconds)
{
    MediaItem m;
    m.id = QUuid::createUuid();
    m.path = path;
    m.name = QFileInfo(path).fileName();
    m.probed = true;
    m.info.kind = MediaInfo::Kind::Video;
    m.info.durationUs = qint64(seconds * 1e6);
    m.info.width = 1920;
    m.info.height = 1080;
    m.info.fpsNum = 25;
    m.info.fpsDen = 1;
    m.info.channels = 2;
    m.info.sampleRate = 48000;
    return m;
}

// a.mov [0,100) from 25 (sound -6 dB) | b.mov [100,200) from 50 with a
// 20-frame dissolve | b.mov [300,350) from 0 at 200 % ; marker at 120.
Project sample(const QString &dir)
{
    Project p;
    p.name = QStringLiteral("Round trip");
    const MediaItem a = media(QDir(dir).filePath(QStringLiteral("a.mov")), 10);
    const MediaItem b = media(QDir(dir).filePath(QStringLiteral("b.mov")), 8);
    p.items << a << b;
    Timeline &t = p.timeline;
    for (const QUuid &id : t.place(a, 0, 0, 0, 25, 100))
        if (t.kindOf(id) == Kind::Audio) {
            const Timeline::ClipRef r = t.find(id);
            t.tracks[r.track].clips[r.index].gainDb = -6;
        }
    for (const QUuid &id : t.place(b, 0, 0, 100, 50, 100))
        t.setTransition(id, true, {ClipTransition::Kind::Dissolve, 20});
    for (const QUuid &id : t.place(b, 0, 0, 300, 0, 50)) {
        const Timeline::ClipRef r = t.find(id);
        t.tracks[r.track].clips[r.index].speed = 2.0;
    }
    t.addMarker(120, QStringLiteral("Scene 2"));
    return p;
}

QList<ImportClip> on(const ReadResult &r, Kind kind, int track)
{
    QList<ImportClip> out;
    for (const ImportClip &c : r.clips)
        if (c.kind == kind && c.track == track)
            out << c;
    return out;
}

QString line(const ImportClip &c)
{
    return QStringLiteral("%1+%2 from %3").arg(c.start).arg(c.length).arg(c.in);
}

QString file(const ImportClip &c)
{
    return c.path.isEmpty() ? c.name : QFileInfo(c.path).fileName();
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

const char *kPremiereXml = R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE xmeml>
<xmeml version="4">
  <sequence id="sequence-7">
    <name>Premiere seq</name>
    <rate><timebase>30</timebase><ntsc>TRUE</ntsc></rate>
    <media>
      <video>
        <format><samplecharacteristics><width>1280</width><height>720</height></samplecharacteristics></format>
        <track>
          <clipitem id="v1">
            <name>a b.mp4</name>
            <enabled>TRUE</enabled>
            <rate><timebase>30</timebase><ntsc>TRUE</ntsc></rate>
            <start>0</start><end>90</end><in>30</in><out>120</out>
            <file id="file-a"><name>a b.mp4</name><pathurl>file://localhost/C%3a/Media/a%20b.mp4</pathurl></file>
            <filter><effect><name>Basic Motion</name><effectid>basic</effectid></effect></filter>
            <link><linkclipref>v1</linkclipref></link>
            <link><linkclipref>a1</linkclipref></link>
            <link><linkclipref>a2</linkclipref></link>
          </clipitem>
          <transitionitem>
            <start>80</start><end>100</end><alignment>center</alignment>
            <effect><name>Cross Dissolve</name><effectid>Cross Dissolve</effectid></effect>
          </transitionitem>
          <clipitem id="v2">
            <name>b.mov</name>
            <start>90</start><end>150</end><in>10</in><out>70</out>
            <file id="file-b"><name>b.mov</name><pathurl>file://localhost/C%3a/Media/b.mov</pathurl></file>
            <filter><effect><name>Time Remap</name><effectid>timeremap</effectid>
              <parameter><parameterid>speed</parameterid><value>200</value></parameter></effect></filter>
          </clipitem>
          <generatoritem id="g1"><name>Color Matte</name><start>150</start><end>180</end></generatoritem>
        </track>
        <track>
          <clipitem id="v3">
            <name>a b.mp4</name>
            <start>200</start><end>230</end><in>0</in><out>30</out>
            <file id="file-a"/>
          </clipitem>
        </track>
      </video>
      <audio>
        <track>
          <clipitem id="a1">
            <name>a b.mp4</name>
            <start>0</start><end>90</end><in>30</in><out>120</out>
            <file id="file-a"/>
            <sourcetrack><mediatype>audio</mediatype><trackindex>1</trackindex></sourcetrack>
            <filter><effect><name>Audio Levels</name><effectid>audiolevels</effectid>
              <parameter><parameterid>level</parameterid><value>0.5</value></parameter></effect></filter>
            <link><linkclipref>v1</linkclipref></link>
            <link><linkclipref>a1</linkclipref></link>
            <link><linkclipref>a2</linkclipref></link>
          </clipitem>
        </track>
        <track>
          <clipitem id="a2">
            <name>a b.mp4</name>
            <start>0</start><end>90</end><in>30</in><out>120</out>
            <file id="file-a"/>
            <sourcetrack><mediatype>audio</mediatype><trackindex>2</trackindex></sourcetrack>
            <filter><effect><name>Audio Levels</name><effectid>audiolevels</effectid>
              <parameter><parameterid>level</parameterid><value>0.5</value></parameter></effect></filter>
          </clipitem>
        </track>
      </audio>
    </media>
    <marker><name>Chapter</name><in>45</in><out>-1</out></marker>
  </sequence>
</xmeml>
)";

const char *kResolveEdl =
    "TITLE: Resolve cut\r\n"
    "FCM: NON-DROP FRAME\r\n"
    "\r\n"
    "001  AX       AA/V  C        00:00:01:00 00:00:05:00 01:00:00:00 01:00:04:00\r\n"
    "* FROM CLIP NAME: a.mov\r\n"
    "\r\n"
    "002  AX       V     C        00:00:06:00 00:00:06:00 01:00:04:00 01:00:04:00\r\n"
    "002  AX       V     D    020 00:00:02:00 00:00:06:00 01:00:04:00 01:00:08:00\r\n"
    "* FROM CLIP NAME: a.mov\r\n"
    "* TO CLIP NAME: b.mov\r\n"
    "\r\n"
    "003  AX       V     C        00:00:00:00 00:00:02:00 01:00:12:00 01:00:14:00\r\n"
    "M2   AX       050.0                      00:00:00:00\r\n"
    "* FROM CLIP NAME: b.mov\r\n";

QByteArray rt(double v, double r)
{
    return QStringLiteral(R"({"OTIO_SCHEMA":"RationalTime.1","rate":%1,"value":%2})").arg(r).arg(v).toUtf8();
}

QByteArray range(double s, double d, double r)
{
    return R"({"OTIO_SCHEMA":"TimeRange.1","start_time":)" + rt(s, r) + R"(,"duration":)" + rt(d, r) + "}";
}

// Clip.2 with media_references, a time warp, a generator; 50 fps.
QByteArray modernOtio()
{
    return R"({"OTIO_SCHEMA":"Timeline.1","name":"Modern","tracks":{"OTIO_SCHEMA":"Stack.1","children":[)"
           R"({"OTIO_SCHEMA":"Track.1","name":"V1","kind":"Video","children":[)"
           R"({"OTIO_SCHEMA":"Gap.1","source_range":)" + range(0, 100, 50) + "},"
           R"({"OTIO_SCHEMA":"Clip.2","name":"x.mov","source_range":)" + range(100, 200, 50) +
           R"(,"active_media_reference_key":"DEFAULT_MEDIA","media_references":{"DEFAULT_MEDIA":)"
           R"({"OTIO_SCHEMA":"ExternalReference.1","target_url":"file:///m/x.mov"}},)"
           R"("effects":[{"OTIO_SCHEMA":"LinearTimeWarp.1","time_scalar":2.0}]},)"
           R"({"OTIO_SCHEMA":"Clip.2","name":"Title","source_range":)" + range(0, 100, 50) +
           R"(,"media_references":{"DEFAULT_MEDIA":{"OTIO_SCHEMA":"GeneratorReference.1","generator_kind":"SolidColor"}}},)"
           R"({"OTIO_SCHEMA":"Clip.2","name":"y.mov","source_range":)" + range(0, 50, 50) +
           R"(,"media_references":{"DEFAULT_MEDIA":{"OTIO_SCHEMA":"ExternalReference.1","target_url":"y.mov"}}})"
           R"(]}]}})";
}

} // namespace

class TestPhase15b : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("BukharaCinemaTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_phase15b"));
        QSettings().clear();
        QSettings().setValue(QStringLiteral("proxy/auto"), false);
        QVERIFY(m_dir.isValid());
    }

    void timecodes()
    {
        QCOMPARE(Interchange::parseTimecode(QStringLiteral("01:00:00:00"), 25, false), qint64(90000));
        QCOMPARE(Interchange::parseTimecode(QStringLiteral("00:00:01:05"), 25, false), qint64(30));
        // Drop frame (29.97): 00:01:00;02 is frame 1800, 00:10:00;00 is 17982.
        QCOMPARE(Interchange::parseTimecode(QStringLiteral("00:01:00;02"), 30, true), qint64(1800));
        QCOMPARE(Interchange::parseTimecode(QStringLiteral("00:10:00;00"), 30, true), qint64(17982));
        QCOMPARE(Interchange::parseTimecode(QStringLiteral("garbage"), 25, false), qint64(-1));
        Format f;
        QVERIFY(Interchange::formatFromPath(QStringLiteral("/x/cut.EDL"), &f) && f == Format::Edl);
        QVERIFY(!Interchange::formatFromPath(QStringLiteral("/x/cut.mp4"), &f));
    }

    void roundTrip_data()
    {
        QTest::addColumn<int>("format");
        QTest::newRow("xml") << int(Format::FcpXml);
        QTest::newRow("otio") << int(Format::Otio);
        QTest::newRow("edl") << int(Format::Edl);
    }

    // What we write, we read back as the same edit (speed is not carried).
    void roundTrip()
    {
        QFETCH(int, format);
        const Format f = Format(format);
        const Project p = sample(m_dir.path());
        const ReadResult r = Interchange::read(Interchange::write(p, f, false).data, f, p.sequence, m_dir.path());
        QCOMPARE(int(r.error), int(ReadResult::Error::None));
        const QList<ImportClip> v1 = on(r, Kind::Video, 0);
        QCOMPARE(int(v1.size()), 3);
        QCOMPARE(line(v1[0]), QStringLiteral("0+100 from 25"));
        QCOMPARE(line(v1[1]), QStringLiteral("100+100 from 50"));
        QCOMPARE(line(v1[2]), QStringLiteral("300+50 from 0"));
        QCOMPARE(file(v1[0]), QStringLiteral("a.mov"));
        QCOMPARE(file(v1[2]), QStringLiteral("b.mov"));
        if (f == Format::Edl) {
            QVERIFY(on(r, Kind::Audio, 0).isEmpty()); // our EDL holds V1 only
            QVERIFY(v1[1].head.isNone());              // and cuts only
            return;
        }
        QCOMPARE(v1[0].path, QDir(m_dir.path()).filePath(QStringLiteral("a.mov")));
        QCOMPARE(int(v1[1].head.kind), int(ClipTransition::Kind::Dissolve));
        QCOMPARE(v1[1].head.length, qint64(20));
        const QList<ImportClip> a1 = on(r, Kind::Audio, 0);
        QCOMPARE(int(a1.size()), 3);
        QCOMPARE(line(a1[1]), QStringLiteral("100+100 from 50"));
        QVERIFY(std::abs(a1[0].gainDb + 6.0) < 0.01);
        QVERIFY(v1[0].link >= 0);
        QCOMPARE(v1[0].link, a1[0].link);
        QCOMPARE(int(r.markers.size()), 1);
        QCOMPARE(r.markers[0].frame, qint64(120));
        QCOMPARE(r.markers[0].name, QStringLiteral("Scene 2"));
        QCOMPARE(r.fpsNum, 25);
    }

    void premiereStyleXml()
    {
        const ReadResult r = Interchange::read(kPremiereXml, Format::FcpXml, rate(30000, 1001), m_dir.path());
        QCOMPARE(int(r.error), int(ReadResult::Error::None));
        QCOMPARE(r.name, QStringLiteral("Premiere seq"));
        QCOMPARE(r.fpsNum, 30000);
        QCOMPARE(r.fpsDen, 1001);
        QCOMPARE(r.width, 1280);
        const QList<ImportClip> v1 = on(r, Kind::Video, 0);
        QCOMPARE(int(v1.size()), 2);
        QCOMPARE(line(v1[0]), QStringLiteral("0+90 from 30"));
        QCOMPARE(v1[0].path, QStringLiteral("C:/Media/a b.mp4"));
        QCOMPARE(line(v1[1]), QStringLiteral("90+60 from 10"));
        QCOMPARE(v1[1].head.length, qint64(20)); // the centred dissolve 80..100
        const QList<ImportClip> v2 = on(r, Kind::Video, 1);
        QCOMPARE(int(v2.size()), 1);
        QCOMPARE(v2[0].path, QStringLiteral("C:/Media/a b.mp4")); // the file given only by id
        // The second mono channel of the stereo file is not a second clip.
        QCOMPARE(r.audioTracks, 1);
        const QList<ImportClip> a1 = on(r, Kind::Audio, 0);
        QCOMPARE(int(a1.size()), 1);
        QVERIFY(std::abs(a1[0].gainDb - 20 * std::log10(0.5)) < 0.01);
        QVERIFY(a1[0].link >= 0);
        QCOMPARE(a1[0].link, v1[0].link);
        QCOMPARE(v2[0].link, -1);
        QCOMPARE(r.losses.value(ImportLoss::Generators), 1);
        QCOMPARE(r.losses.value(ImportLoss::Speed), 1);
        QCOMPARE(r.losses.value(ImportLoss::Effects), 1); // Basic Motion
        QCOMPARE(int(r.markers.size()), 1);
        QCOMPARE(r.markers[0].frame, qint64(45));

        // Into a 25 fps sequence the times are converted.
        const ReadResult c = Interchange::read(kPremiereXml, Format::FcpXml, rate(25, 1), m_dir.path());
        QCOMPARE(line(on(c, Kind::Video, 0)[0]), QStringLiteral("0+75 from 25"));
        QCOMPARE(on(c, Kind::Video, 1)[0].start, qint64(167)); // 200 / 29.97 * 25
    }

    void resolveStyleEdl()
    {
        const ReadResult r = Interchange::read(kResolveEdl, Format::Edl, rate(25, 1), m_dir.path());
        QCOMPARE(int(r.error), int(ReadResult::Error::None));
        QCOMPARE(r.name, QStringLiteral("Resolve cut"));
        const QList<ImportClip> v1 = on(r, Kind::Video, 0);
        QCOMPARE(int(v1.size()), 3);
        // Record times start at 01:00:00:00 -> 0. The 20-frame dissolve
        // 100..120 becomes a cut at 110 with the dissolve centred on it.
        QCOMPARE(line(v1[0]), QStringLiteral("0+110 from 25"));
        QCOMPARE(line(v1[1]), QStringLiteral("110+90 from 60"));
        QCOMPARE(v1[1].head.length, qint64(20));
        QCOMPARE(file(v1[1]), QStringLiteral("b.mov"));
        QCOMPARE(line(v1[2]), QStringLiteral("300+50 from 0"));
        QVERIFY(v1[0].path.isEmpty()); // only a name: found by the controller
        const QList<ImportClip> a1 = on(r, Kind::Audio, 0);
        QCOMPARE(int(a1.size()), 1);
        QCOMPARE(line(a1[0]), QStringLiteral("0+100 from 25"));
        QCOMPARE(a1[0].link, v1[0].link);
        QCOMPARE(r.losses.value(ImportLoss::Speed), 1); // the M2 line
        QCOMPARE(r.fpsNum, 0);                          // an EDL has no rate
    }

    void modernOtioClip2()
    {
        const ReadResult r = Interchange::read(modernOtio(), Format::Otio, rate(25, 1), QStringLiteral("/base"));
        QCOMPARE(int(r.error), int(ReadResult::Error::None));
        QCOMPARE(r.fpsNum, 50);
        const QList<ImportClip> v1 = on(r, Kind::Video, 0);
        QCOMPARE(int(v1.size()), 2);
        QCOMPARE(line(v1[0]), QStringLiteral("50+100 from 50"));
        QCOMPARE(v1[0].path, QStringLiteral("/m/x.mov"));
        QCOMPARE(line(v1[1]), QStringLiteral("200+25 from 0")); // after the 50-frame generator
        QCOMPARE(v1[1].path, QStringLiteral("/base/y.mov"));    // relative to the file
        QCOMPARE(r.losses.value(ImportLoss::Speed), 1);
        QCOMPARE(r.losses.value(ImportLoss::Generators), 1);
    }

    void notATimeline()
    {
        QCOMPARE(int(Interchange::read("<html></html>", Format::FcpXml, rate(25, 1), {}).error),
                 int(ReadResult::Error::NotRecognized));
        QCOMPARE(int(Interchange::read("not json", Format::Otio, rate(25, 1), {}).error),
                 int(ReadResult::Error::NotRecognized));
        QCOMPARE(int(Interchange::read("hello\nworld\n", Format::Edl, rate(25, 1), {}).error),
                 int(ReadResult::Error::NotRecognized));
        QCOMPARE(int(Interchange::read("TITLE: x\nFCM: NON-DROP FRAME\n", Format::Edl, rate(25, 1), {}).error),
                 int(ReadResult::Error::Empty));
    }

    void controllerImport()
    {
        // a.mov exists (empty) next to the EDL, b.mov does not.
        const QString dir = m_dir.filePath(QStringLiteral("import"));
        QVERIFY(QDir().mkpath(dir));
        QVERIFY(writeFile(QDir(dir).filePath(QStringLiteral("a.mov")), "x"));
        const QString edl = QDir(dir).filePath(QStringLiteral("cut.edl"));
        QVERIFY(writeFile(edl, kResolveEdl));

        ProjectController ctl;
        const int itemsBefore = int(ctl.project().items.size());
        ctl.setPlayhead(1000);
        QString error;
        Interchange::ReadResult r;
        QVERIFY2(ctl.readInterchange(edl, &r, &error), qPrintable(error));
        int found = 0, missing = 0;
        ctl.interchangeMediaCount(r, dir, &found, &missing);
        QCOMPARE(found, 1);
        QCOMPARE(missing, 1);

        // As a nested clip at the playhead.
        QCOMPARE(ctl.importInterchange(edl, true, &error), 4);
        ctl.waitForBackgroundWork();
        QCOMPARE(int(ctl.project().items.size()), itemsBefore + 2);
        QCOMPARE(int(ctl.project().nested.size()), 1);
        const NestedSequence &nest = ctl.project().nested.first();
        QCOMPARE(nest.name, QStringLiteral("Resolve cut"));
        QCOMPARE(nest.timeline.duration(), qint64(350));
        const Timeline &main = ctl.project().timeline;
        const TimelineTrack &v1 = main.tracks[main.indexOf(Kind::Video, 0)];
        QCOMPARE(int(v1.clips.size()), 1);
        QCOMPARE(v1.clips[0].nestId, nest.id);
        QCOMPARE(v1.clips[0].start, qint64(1000));
        QCOMPARE(v1.clips[0].length, qint64(350));
        QVERIFY(main.isLinked(v1.clips[0].id)); // with its sound on A1
        int offline = 0;
        for (const MediaItem &m : ctl.project().items)
            if (!ctl.isOnline(m.id))
                ++offline;
        QCOMPARE(offline, 1); // b.mov, to relink later
        // One undo step takes it back.
        ctl.undoStack()->undo();
        QVERIFY(ctl.project().timeline.tracks[main.indexOf(Kind::Video, 0)].clips.isEmpty());
        QVERIFY(ctl.project().nested.isEmpty());

        // Straight onto the timeline; the bin items are reused, not doubled.
        ctl.setPlayhead(0);
        QCOMPARE(ctl.importInterchange(edl, false, &error), 4);
        QCOMPARE(int(ctl.project().items.size()), itemsBefore + 2);
        QVERIFY(ctl.project().nested.isEmpty());
        const Timeline &t = ctl.project().timeline;
        const TimelineTrack &tv = t.tracks[t.indexOf(Kind::Video, 0)];
        QCOMPARE(int(tv.clips.size()), 3);
        QCOMPARE(tv.clips[1].start, qint64(110));
        QCOMPARE(tv.clips[1].head.length, qint64(20));
        QCOMPARE(int(t.tracks[t.indexOf(Kind::Audio, 0)].clips.size()), 1);

        // Not a timeline: a clear message, nothing changes.
        const QString bad = QDir(dir).filePath(QStringLiteral("bad.xml"));
        QVERIFY(writeFile(bad, "<html/>"));
        QCOMPARE(ctl.importInterchange(bad, true, &error), 0);
        QVERIFY(!error.isEmpty());
    }

    void dialogAndMenu()
    {
        const QString dir = m_dir.filePath(QStringLiteral("dialog"));
        QVERIFY(QDir().mkpath(dir));
        const QString edl = QDir(dir).filePath(QStringLiteral("cut.edl"));
        QVERIFY(writeFile(edl, kResolveEdl));
        MainWindow w;
        QAction *act = w.findChild<QAction *>(QStringLiteral("actImportInterchange"));
        QVERIFY(act && act->isEnabled());

        InterchangeImportDialog dlg(w.controller(), edl, &w);
        QVERIFY(dlg.isReadable());
        QVERIFY(dlg.reportLines().contains(InterchangeImportDialog::lossText(ImportLoss::Speed, 1)));
        QVERIFY(dlg.findChild<QPushButton *>(QStringLiteral("interchangeImport"))->isEnabled());
        dlg.setNested(false);
        QCOMPARE(dlg.importNow(), 4);
        QCOMPARE(dlg.importedClips(), 4);

        const QString bad = QDir(dir).filePath(QStringLiteral("bad.otio"));
        QVERIFY(writeFile(bad, "{\"x\":1}"));
        InterchangeImportDialog badDlg(w.controller(), bad, &w);
        QVERIFY(!badDlg.isReadable());
        QVERIFY(!badDlg.findChild<QPushButton *>(QStringLiteral("interchangeImport"))->isEnabled());
    }

    void translated()
    {
        MainWindow w;
        w.show();
        QVERIFY(Language::instance().setLanguage(QStringLiteral("uz_Cyrl")));
        QCoreApplication::processEvents();
        QCOMPARE(w.findChild<QAction *>(QStringLiteral("actImportInterchange"))->text(),
                 QStringLiteral("Бошқа дастурлардан импорт..."));
        QVERIFY(Language::instance().setLanguage(QStringLiteral("en")));
    }

    // Files written by the OpenTimelineIO reference library (Linux CI).
    void readReferenceFiles()
    {
        const QString dir = qEnvironmentVariable("BC_INTERCHANGE_IN");
        if (dir.isEmpty())
            QSKIP("BC_INTERCHANGE_IN not set");
        const SequenceSettings seq = rate(25, 1);
        QStringList problems;
        const QStringList expected = {QStringLiteral("0+100 from 25 a.mov"), QStringLiteral("100+100 from 50 b.mov"),
                                      QStringLiteral("300+50 from 0 c.mov")};
        for (const QString &name : {QStringLiteral("ref.otio"), QStringLiteral("ref.xml"), QStringLiteral("ref.edl")}) {
            QFile f(QDir(dir).filePath(name));
            QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(name));
            Format fmt;
            QVERIFY(Interchange::formatFromPath(name, &fmt));
            const ReadResult r = Interchange::read(f.readAll(), fmt, seq, dir);
            if (r.error != ReadResult::Error::None) {
                problems << name + QStringLiteral(": not read");
                continue;
            }
            QList<Kind> kinds{Kind::Video};
            if (fmt != Format::Edl)
                kinds << Kind::Audio;
            for (Kind kind : kinds) {
                QStringList got;
                const QList<ImportClip> cs = on(r, kind, 0);
                for (const ImportClip &c : cs)
                    got << line(c) + QLatin1Char(' ') + file(c);
                if (got != expected)
                    problems << QStringLiteral("%1 %2: %3").arg(name, kind == Kind::Video ? QStringLiteral("V1") : QStringLiteral("A1"),
                                                                got.join(QStringLiteral(" | ")));
                if (cs.size() >= 2 && cs[1].head.length != 20)
                    problems << QStringLiteral("%1: dissolve %2 frames").arg(name).arg(cs[1].head.length);
            }
            qInfo().noquote() << name << "read:" << r.clips.size() << "clips, losses" << r.losses.size();
        }
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
    }
};

QTEST_MAIN(TestPhase15b)
#include "tst_phase15b.moc"
