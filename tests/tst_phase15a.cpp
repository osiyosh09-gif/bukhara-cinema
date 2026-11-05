// Phase 15 (part 1) acceptance tests: export of the main sequence for other
// editors as Final Cut Pro 7 XML, OpenTimelineIO and CMX 3600 EDL; the list
// of what each format does not carry; the dialog and the File menu item.
//
// With BC_INTERCHANGE_OUT set, the three files of the sample sequence are
// also written there; tests/check_interchange.py then reads them with the
// OpenTimelineIO reference library (Linux CI).
#include "app/InterchangeDialog.h"
#include "app/Language.h"
#include "app/MainWindow.h"
#include "app/ProjectController.h"
#include "core/Interchange.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QtTest>

using namespace bc;
using Interchange::Format;
using Interchange::Loss;

namespace {

MediaItem media(const QString &path, MediaInfo::Kind kind, double seconds)
{
    MediaItem m;
    m.id = QUuid::createUuid();
    m.path = path;
    m.name = QFileInfo(path).fileName();
    m.probed = true;
    m.info.kind = kind;
    m.info.durationUs = qint64(seconds * 1e6);
    m.info.width = 1920;
    m.info.height = 1080;
    if (kind == MediaInfo::Kind::Video) {
        m.info.fpsNum = 25;
        m.info.fpsDen = 1;
        m.info.channels = 2;
        m.info.sampleRate = 48000;
        m.info.videoCodec = QStringLiteral("h264");
        m.info.audioCodec = QStringLiteral("aac");
    }
    return m;
}

TimelineClip *clip(Timeline &t, const QUuid &id)
{
    const Timeline::ClipRef r = t.find(id);
    return r.isValid() ? &t.tracks[r.track].clips[r.index] : nullptr;
}

// The sample sequence (25 fps):
//   V1/A1: a.mp4 [0,100) from 25 (sound -6 dB) | b.mov [100,200) from 50 with a
//          20-frame dissolve and crossfade | b.mov [300,350) from 0 at 200 % |
//          nested clip [400,450) showing nest frames 10..60 (a.mp4 from 0)
//   V2:    title [0,50) | photo c.png [200,250) scaled to 50 %
//   marker at 120 "Scene 2", one subtitle.
Project sample(const QString &dir)
{
    Project p;
    p.name = QStringLiteral("Sample");
    MediaItem a = media(QDir(dir).filePath(QStringLiteral("a b.mp4")), MediaInfo::Kind::Video, 10);
    MediaItem b = media(QDir(dir).filePath(QStringLiteral("b.mov")), MediaInfo::Kind::Video, 8);
    MediaItem c = media(QDir(dir).filePath(QStringLiteral("c.png")), MediaInfo::Kind::Image, 0);
    p.items << a << b << c;
    Timeline &t = p.timeline;

    const QList<QUuid> A = t.place(a, 0, 0, 0, 25, 100);
    const QList<QUuid> B = t.place(b, 0, 0, 100, 50, 100);
    const QList<QUuid> D = t.place(b, 0, 0, 300, 0, 50);
    Q_ASSERT(A.size() == 2 && B.size() == 2 && D.size() == 2);
    for (const QUuid &id : A)
        if (t.kindOf(id) == Timeline::Kind::Audio)
            clip(t, id)->gainDb = -6;
    for (const QUuid &id : B)
        t.setTransition(id, true, {ClipTransition::Kind::Dissolve, 20});
    for (const QUuid &id : D)
        clip(t, id)->speed = 2.0;

    t.placeTitle(TitleData{QStringLiteral("Hello"), 80, QStringLiteral("#ffffff"), TitleData::Position::Bottom, false},
                 1, 0, 50);
    const QList<QUuid> C = t.place(c, 1, -1, 200, 0, 50);
    Q_ASSERT(C.size() == 1);
    ClipTransform half;
    half.scale = 50;
    clip(t, C.first())->motion.base = half;

    NestedSequence nest;
    nest.id = QUuid::createUuid();
    nest.name = QStringLiteral("Nest");
    nest.timeline = Timeline::makeDefault();
    nest.timeline.place(a, 0, 0, 0, 0, 60);
    p.nested << nest;
    const QUuid link = QUuid::createUuid();
    for (Timeline::Kind kind : {Timeline::Kind::Video, Timeline::Kind::Audio}) {
        TimelineClip n;
        n.id = QUuid::createUuid();
        n.nestId = nest.id;
        n.linkId = link;
        n.start = 400;
        n.in = 10;
        n.length = 50;
        t.insertSorted(t.indexOf(kind, 0), n);
    }

    t.addMarker(120, QStringLiteral("Scene 2"));
    Subtitle s;
    s.id = QUuid::createUuid();
    s.start = 10;
    s.length = 40;
    s.text = QStringLiteral("Hi");
    t.subtitles << s;
    return p;
}

struct XmlItem
{
    QString element; // clipitem / transitionitem
    QString track;   // "video1", "audio1", ...
    QMap<QString, QString> values;
};

// Direct children of clipitems and transitionitems, per track.
QList<XmlItem> readXml(const QByteArray &data, QString *error)
{
    QList<XmlItem> items;
    QXmlStreamReader x(data);
    QString media;
    int track[2] = {0, 0};
    QStringList path;
    XmlItem *cur = nullptr;
    while (!x.atEnd()) {
        x.readNext();
        if (x.isStartElement()) {
            const QString n = x.name().toString();
            path << n;
            if (path.size() >= 2 && path[path.size() - 2] == QLatin1String("media") && (n == QLatin1String("video") || n == QLatin1String("audio")))
                media = n;
            if (n == QLatin1String("track") && path.size() >= 2 && path[path.size() - 2] == media)
                ++track[media == QLatin1String("video") ? 0 : 1];
            if ((n == QLatin1String("clipitem") || n == QLatin1String("transitionitem")) && path.size() >= 2
                && path[path.size() - 2] == QLatin1String("track")) {
                items.append(XmlItem{n, media + QString::number(track[media == QLatin1String("video") ? 0 : 1]), {}});
                cur = &items.last();
            } else if (cur && path.size() >= 2
                       && (path[path.size() - 2] == QLatin1String("clipitem") || path[path.size() - 2] == QLatin1String("transitionitem"))) {
                const QString text = x.readElementText(QXmlStreamReader::SkipChildElements);
                cur->values.insert(n, text);
                path.removeLast();
            }
        } else if (x.isEndElement()) {
            const QString n = x.name().toString();
            if (n == QLatin1String("clipitem") || n == QLatin1String("transitionitem"))
                cur = nullptr;
            if (!path.isEmpty())
                path.removeLast();
        }
    }
    if (x.hasError() && error)
        *error = x.errorString();
    return items;
}

QList<XmlItem> onTrack(const QList<XmlItem> &all, const QString &track, const QString &element)
{
    QList<XmlItem> out;
    for (const XmlItem &i : all)
        if (i.track == track && i.element == element)
            out << i;
    return out;
}

QString clipLine(const XmlItem &i)
{
    return QStringLiteral("%1 %2 %3 %4").arg(i.values.value(QStringLiteral("start")), i.values.value(QStringLiteral("end")),
                                             i.values.value(QStringLiteral("in")), i.values.value(QStringLiteral("out")));
}

} // namespace

class TestPhase15a : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("BukharaCinemaTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_phase15a"));
        QSettings().clear();
        QSettings().setValue(QStringLiteral("proxy/auto"), false);
        QVERIFY(m_dir.isValid());
        // a.mp4 exists (empty), b.mov and c.png do not.
        QFile f(m_dir.filePath(QStringLiteral("a b.mp4")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
    }

    void timecodes()
    {
        QCOMPARE(Interchange::timecode(0, 25), QStringLiteral("00:00:00:00"));
        QCOMPARE(Interchange::timecode(90000, 25), QStringLiteral("01:00:00:00"));
        QCOMPARE(Interchange::timecode(25 * 61 + 7, 25), QStringLiteral("00:01:01:07"));
        QCOMPARE(Interchange::timecodeBase(25, 1), 25);
        QCOMPARE(Interchange::timecodeBase(30000, 1001), 30);
        QCOMPARE(Interchange::timecodeBase(24000, 1001), 24);
        QCOMPARE(Interchange::fileSuffix(Format::Otio), QStringLiteral("otio"));
    }

    void fcpXml()
    {
        const Project p = sample(m_dir.path());
        const Interchange::Result r = Interchange::write(p, Format::FcpXml);
        QVERIFY(r.data.startsWith("<?xml"));
        QVERIFY(r.data.contains("<!DOCTYPE xmeml>"));
        QString error;
        const QList<XmlItem> items = readXml(r.data, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        // V1: start end in out. The dissolve is centred on the cut at 100
        // (10 frames each side), so the clips around it reach into it (-1).
        const QList<XmlItem> v1 = onTrack(items, QStringLiteral("video1"), QStringLiteral("clipitem"));
        QCOMPARE(v1.size(), qsizetype(4));
        QCOMPARE(clipLine(v1[0]), QStringLiteral("0 -1 25 135"));
        QCOMPARE(clipLine(v1[1]), QStringLiteral("-1 200 40 150"));
        QCOMPARE(clipLine(v1[2]), QStringLiteral("300 350 0 50"));  // speed not carried
        QCOMPARE(clipLine(v1[3]), QStringLiteral("400 450 10 60")); // nested clip: the a.mp4 clip inside
        const QList<XmlItem> tv1 = onTrack(items, QStringLiteral("video1"), QStringLiteral("transitionitem"));
        QCOMPARE(tv1.size(), qsizetype(1));
        QCOMPARE(tv1[0].values.value(QStringLiteral("start")), QStringLiteral("90"));
        QCOMPARE(tv1[0].values.value(QStringLiteral("end")), QStringLiteral("110"));
        QCOMPARE(tv1[0].values.value(QStringLiteral("alignment")), QStringLiteral("center"));

        // V2: the photo only (the title is not carried).
        const QList<XmlItem> v2 = onTrack(items, QStringLiteral("video2"), QStringLiteral("clipitem"));
        QCOMPARE(v2.size(), qsizetype(1));
        QCOMPARE(clipLine(v2[0]), QStringLiteral("200 250 0 50"));
        QVERIFY(onTrack(items, QStringLiteral("video3"), QStringLiteral("clipitem")).isEmpty());

        // A1: the same edit with a crossfade; the first clip's volume as Audio Levels.
        const QList<XmlItem> a1 = onTrack(items, QStringLiteral("audio1"), QStringLiteral("clipitem"));
        QCOMPARE(a1.size(), qsizetype(4));
        QCOMPARE(clipLine(a1[1]), QStringLiteral("-1 200 40 150"));
        QCOMPARE(onTrack(items, QStringLiteral("audio1"), QStringLiteral("transitionitem")).size(), qsizetype(1));
        QVERIFY(r.data.contains("<value>0.501187</value>")); // -6 dB
        QVERIFY(r.data.contains("Cross Fade (+3dB)"));

        QVERIFY(r.data.contains("<timebase>25</timebase>"));
        QVERIFY(r.data.contains("<ntsc>FALSE</ntsc>"));
        QVERIFY(r.data.contains("a%20b.mp4"));
        QVERIFY(r.data.contains("<name>Scene 2</name>"));
        QVERIFY(r.data.contains("<in>120</in>"));
        QVERIFY(r.data.contains("<linkclipref>"));

        QCOMPARE(r.clips, 9);
        QCOMPARE(r.flattened, 1);
        QCOMPARE(r.missingMedia, 5); // b.mov: 2 pictures + 2 sounds, c.png: 1 (a b.mp4 exists)
        QCOMPARE(r.losses.value(Loss::Titles), 1);
        QCOMPARE(r.losses.value(Loss::Motion), 1);
        QCOMPARE(r.losses.value(Loss::Speed), 2); // picture and sound of the 200 % clip
        QCOMPARE(r.losses.value(Loss::Subtitles), 1);
        QVERIFY(!r.losses.contains(Loss::ClipVolume)); // XML carries it
        QVERIFY(!r.losses.contains(Loss::Transitions));
        QVERIFY(!r.losses.contains(Loss::Markers));
    }

    void ntscRate()
    {
        Project p = sample(m_dir.path());
        p.sequence.fpsNum = 30000;
        p.sequence.fpsDen = 1001;
        const Interchange::Result x = Interchange::write(p, Format::FcpXml, false);
        QVERIFY(x.data.contains("<timebase>30</timebase>"));
        QVERIFY(x.data.contains("<ntsc>TRUE</ntsc>"));
        const Interchange::Result o = Interchange::write(p, Format::Otio, false);
        QVERIFY(o.data.contains("\"rate\":29.97002997002997"));
    }

    void otio()
    {
        const Project p = sample(m_dir.path());
        const Interchange::Result r = Interchange::write(p, Format::Otio, false);
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(r.data, &err);
        QVERIFY2(err.error == QJsonParseError::NoError, qPrintable(err.errorString()));
        const QJsonObject tl = doc.object();
        QCOMPARE(tl.value(QStringLiteral("OTIO_SCHEMA")).toString(), QStringLiteral("Timeline.1"));
        const QJsonObject stack = tl.value(QStringLiteral("tracks")).toObject();
        QCOMPARE(stack.value(QStringLiteral("OTIO_SCHEMA")).toString(), QStringLiteral("Stack.1"));
        const QJsonArray tracks = stack.value(QStringLiteral("children")).toArray();
        QCOMPARE(int(tracks.size()), 3); // V1, V2, A1
        QStringList kinds;
        for (const QJsonValue &t : tracks)
            kinds << t.toObject().value(QStringLiteral("name")).toString() + QLatin1Char(':')
                         + t.toObject().value(QStringLiteral("kind")).toString();
        QCOMPARE(kinds, QStringList({QStringLiteral("V1:Video"), QStringLiteral("V2:Video"), QStringLiteral("A1:Audio")}));

        QStringList schemas;
        const QJsonArray v1 = tracks[0].toObject().value(QStringLiteral("children")).toArray();
        for (const QJsonValue &c : v1)
            schemas << c.toObject().value(QStringLiteral("OTIO_SCHEMA")).toString();
        QCOMPARE(schemas, QStringList({QStringLiteral("Clip.1"), QStringLiteral("Transition.1"), QStringLiteral("Clip.1"),
                                       QStringLiteral("Gap.1"), QStringLiteral("Clip.1"), QStringLiteral("Gap.1"),
                                       QStringLiteral("Clip.1")}));
        const QJsonObject trans = v1[1].toObject();
        QCOMPARE(trans.value(QStringLiteral("in_offset")).toObject().value(QStringLiteral("value")).toDouble(), 10.0);
        QCOMPARE(trans.value(QStringLiteral("out_offset")).toObject().value(QStringLiteral("value")).toDouble(), 10.0);
        const QJsonObject range = v1[2].toObject().value(QStringLiteral("source_range")).toObject();
        QCOMPARE(range.value(QStringLiteral("start_time")).toObject().value(QStringLiteral("value")).toDouble(), 50.0);
        QCOMPARE(range.value(QStringLiteral("duration")).toObject().value(QStringLiteral("value")).toDouble(), 100.0);
        QCOMPARE(v1[3].toObject().value(QStringLiteral("source_range")).toObject().value(QStringLiteral("duration"))
                     .toObject().value(QStringLiteral("value")).toDouble(), 100.0); // gap 200..300

        // Numbers are written as floats, like the reference implementation.
        QVERIFY(r.data.contains("\"rate\":25.0"));
        QVERIFY(!r.data.contains("\"value\":10,"));
        QCOMPARE(int(stack.value(QStringLiteral("markers")).toArray().size()), 1);

        QCOMPARE(r.losses.value(Loss::ClipVolume), 1); // OTIO has no standard clip volume
        QCOMPARE(r.missingMedia, 0); // not checked here
    }

    void edl()
    {
        const Project p = sample(m_dir.path());
        const Interchange::Result r = Interchange::write(p, Format::Edl, false);
        const QStringList lines = QString::fromUtf8(r.data).split(QStringLiteral("\r\n"));
        QCOMPARE(lines.value(0), QStringLiteral("TITLE: Sample"));
        QCOMPARE(lines.value(1), QStringLiteral("FCM: NON-DROP FRAME"));
        QStringList events;
        for (const QString &l : lines)
            if (!l.isEmpty() && l[0].isDigit())
                events << l.simplified();
        QCOMPARE(int(events.size()), 4);
        QCOMPARE(events[0], QStringLiteral("001 AX V C 00:00:01:00 00:00:05:00 00:00:00:00 00:00:04:00"));
        QCOMPARE(events[1], QStringLiteral("002 AX V C 00:00:02:00 00:00:06:00 00:00:04:00 00:00:08:00"));
        QCOMPARE(events[3], QStringLiteral("004 AX V C 00:00:00:10 00:00:02:10 00:00:16:00 00:00:18:00"));
        QVERIFY(r.data.contains("* FROM CLIP NAME: b.mov"));
        QCOMPARE(r.clips, 4);
        QCOMPARE(r.losses.value(Loss::Transitions), 1);
        QCOMPARE(r.losses.value(Loss::Markers), 1);
        QCOMPARE(r.losses.value(Loss::OtherTracks), 5); // the photo on V2 and four sound clips
        QVERIFY(!r.losses.contains(Loss::ClipVolume)); // sound is left out as a whole
    }

    void multicamShowsOneAngle()
    {
        Project p;
        MediaItem a = media(QStringLiteral("/m/cam1.mp4"), MediaInfo::Kind::Video, 10);
        MediaItem b = media(QStringLiteral("/m/cam2.mp4"), MediaInfo::Kind::Video, 10);
        p.items << a << b;
        NestedSequence mc;
        mc.id = QUuid::createUuid();
        mc.multicam = true;
        mc.timeline = Timeline::makeDefault();
        mc.timeline.place(a, 0, 0, 0, 0, 200);
        mc.timeline.place(b, 1, 1, 0, 30, 200);
        p.nested << mc;
        TimelineClip n;
        n.id = QUuid::createUuid();
        n.nestId = mc.id;
        n.start = 0;
        n.in = 0;
        n.length = 100;
        n.angle = 1; // camera 2
        p.timeline.insertSorted(p.timeline.indexOf(Timeline::Kind::Video, 0), n);
        const Interchange::Result r = Interchange::write(p, Format::Edl, false);
        QVERIFY(r.data.contains("cam2.mp4"));
        QVERIFY(!r.data.contains("cam1.mp4"));
        QVERIFY(r.data.contains("00:00:01:05 00:00:05:05 00:00:00:00 00:00:04:00")); // cam2 from frame 30
        QCOMPARE(r.flattened, 1);
    }

    void dialogAndMenu()
    {
        const QString projectPath = m_dir.filePath(QStringLiteral("sample.bcproj"));
        QString error;
        QVERIFY2(sample(m_dir.path()).save(projectPath, &error), qPrintable(error));

        MainWindow w;
        // Through the controller: MainWindow::openProjectFile would ask about
        // the missing media in a modal box (b.mov and c.png do not exist).
        QVERIFY2(w.controller()->open(projectPath, &error), qPrintable(error));
        w.controller()->waitForBackgroundWork();
        QAction *act = w.findChild<QAction *>(QStringLiteral("actExportInterchange"));
        QVERIFY(act);
        QVERIFY(act->isEnabled());

        InterchangeDialog dlg(w.controller(), &w);
        dlg.setFormat(Format::Edl);
        QVERIFY(dlg.reportLines().contains(InterchangeDialog::lossText(Loss::OtherTracks, 5)));
        QVERIFY(dlg.reportLines().contains(InterchangeDialog::lossText(Loss::Titles, 1)));
        dlg.setFormat(Format::FcpXml);
        QVERIFY(!dlg.reportLines().contains(InterchangeDialog::lossText(Loss::ClipVolume, 1)));
        dlg.setFormat(Format::Otio);
        QVERIFY(dlg.reportLines().contains(InterchangeDialog::lossText(Loss::ClipVolume, 1)));
        QVERIFY(dlg.findChild<QPushButton *>(QStringLiteral("interchangeExport"))->isEnabled());
        QVERIFY(!dlg.findChild<QLabel *>(QStringLiteral("interchangeReport"))->text().isEmpty());

        dlg.setFormat(Format::FcpXml);
        const QString out = m_dir.filePath(QStringLiteral("for-premiere.xml"));
        QVERIFY2(dlg.exportTo(out, &error), qPrintable(error));
        QCOMPARE(dlg.savedPath(), out);
        QFile f(out);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(f.readAll().contains("<xmeml version=\"5\">"));
        // A folder that does not exist: a clear error, no crash.
        QVERIFY(!dlg.exportTo(m_dir.filePath(QStringLiteral("no/such/dir/x.xml")), &error));
        QVERIFY(!error.isEmpty());

        // An empty main sequence: nothing to export.
        w.controller()->newProject();
        QVERIFY(!act->isEnabled());
    }

    void translated_data()
    {
        QTest::addColumn<QString>("code");
        QTest::addColumn<QString>("text");
        QTest::newRow("uz_Cyrl") << "uz_Cyrl" << QStringLiteral("Бошқа дастурлар учун экспорт...");
        QTest::newRow("ru") << "ru" << QStringLiteral("Экспорт для других монтажных программ...");
        QTest::newRow("en") << "en" << QStringLiteral("Export for other editors...");
    }

    void translated()
    {
        QFETCH(QString, code);
        QFETCH(QString, text);
        MainWindow w;
        w.show();
        QVERIFY(Language::instance().setLanguage(code));
        QCoreApplication::processEvents();
        QCOMPARE(w.findChild<QAction *>(QStringLiteral("actExportInterchange"))->text(), text);
        QVERIFY(Language::instance().setLanguage(QStringLiteral("en")));
    }

    // Files for tests/check_interchange.py (Linux CI).
    void writeSamplesForReferenceCheck()
    {
        const QString dir = qEnvironmentVariable("BC_INTERCHANGE_OUT");
        if (dir.isEmpty())
            QSKIP("BC_INTERCHANGE_OUT not set");
        QVERIFY(QDir().mkpath(dir));
        const Project p = sample(QStringLiteral("/media/sample"));
        for (Format f : {Format::FcpXml, Format::Otio, Format::Edl}) {
            QFile file(QDir(dir).filePath(QStringLiteral("sample.") + Interchange::fileSuffix(f)));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(Interchange::write(p, f, false).data);
        }
    }
};

QTEST_MAIN(TestPhase15a)
#include "tst_phase15a.moc"
