// Phase 14b acceptance tests: subtitles from speech (Vosk). Recognition runs
// when CI provides the library (BC_VOSK_LIB), a model (BC_VOSK_MODEL) and a
// spoken sentence (BC_SPEECH_WAV, made by a speech synthesizer); the rest
// (lines, transliteration, cache, controller mapping) always runs.
#include "app/MainWindow.h"
#include "app/ProjectController.h"
#include "app/SpeechDialog.h"
#include "app/TimelinePanel.h"
#include "core/MediaCache.h"
#include "core/Speech.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUndoStack>
#include <QtTest>

using namespace bc;
using Speech::Word;
using Kind = Timeline::Kind;

namespace {

QList<Word> sentence(const QStringList &texts, double start, double each = 0.4, double gap = 0.05)
{
    QList<Word> out;
    double t = start;
    for (const QString &s : texts) {
        out << Word{t, t + each, s, 1.0};
        t += each + gap;
    }
    return out;
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

QString env(const char *name)
{
    return qEnvironmentVariable(name);
}

bool recognitionReady()
{
    return !env("BC_VOSK_MODEL").isEmpty() && !env("BC_SPEECH_WAV").isEmpty() && Speech::libraryAvailable()
           && QFileInfo::exists(env("BC_SPEECH_WAV"));
}

} // namespace

class TestPhase14b : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("BukharaCinemaTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_phase14b"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        QSettings().setValue(QStringLiteral("proxy/auto"), false);
        QDir(MediaCache::root()).removeRecursively();
        QVERIFY(m_dir.isValid());
    }

    void cyrillic()
    {
        const QList<QPair<QString, QString>> cases = {
            {QStringLiteral("o'zbekiston"), QStringLiteral("ўзбекистон")},
            {QStringLiteral("oʻzbek tili"), QStringLiteral("ўзбек тили")},
            {QStringLiteral("shahar"), QStringLiteral("шаҳар")},
            {QStringLiteral("choy ichamiz"), QStringLiteral("чой ичамиз")},
            {QStringLiteral("yaxshi"), QStringLiteral("яхши")},
            {QStringLiteral("ertaga"), QStringLiteral("эртага")},
            {QStringLiteral("g'alaba"), QStringLiteral("ғалаба")},
            {QStringLiteral("Toshkent"), QStringLiteral("Тошкент")},
            {QStringLiteral("maʼno"), QStringLiteral("маъно")},
            {QStringLiteral("yoʻl"), QStringLiteral("йўл")},
            {QStringLiteral("kecha"), QStringLiteral("кеча")},
            {QStringLiteral("yer yuzi"), QStringLiteral("ер юзи")},
            {QStringLiteral("qishloq xo'jaligi"), QStringLiteral("қишлоқ хўжалиги")},
            {QStringLiteral("2024 yil"), QStringLiteral("2024 йил")},
        };
        for (const auto &c : cases)
            QCOMPARE(Speech::uzLatinToCyrillic(c.first), c.second);
    }

    void lines()
    {
        // Two sentences with a pause between them: two lines, each starting with a capital.
        QList<Word> words = sentence({QStringLiteral("hello"), QStringLiteral("world")}, 1.0);
        words << sentence({QStringLiteral("one"), QStringLiteral("two"), QStringLiteral("three")}, 3.0);
        QList<Speech::Cue> c = Speech::cues(words);
        QCOMPARE(c.size(), 2);
        QCOMPARE(c[0].text, QStringLiteral("Hello world"));
        QCOMPARE(c[1].text, QStringLiteral("One two three"));
        QVERIFY(c[0].start == 1.0 && c[0].end <= 3.0 && c[0].end >= 1.85);
        // A long speech without pauses: lines of at most 5 s and 2 x 42 letters.
        QStringList many;
        for (int i = 0; i < 60; ++i)
            many << QStringLiteral("word%1").arg(i);
        c = Speech::cues(sentence(many, 0));
        QVERIFY(c.size() > 3);
        for (const Speech::Cue &cue : c) {
            QVERIFY2(cue.end - cue.start <= 5.3, qPrintable(QString::number(cue.end - cue.start)));
            const QStringList rows = cue.text.split(QLatin1Char('\n'));
            QVERIFY(rows.size() <= 2);
            for (const QString &r : rows)
                QVERIFY2(r.size() <= 42, qPrintable(r));
        }
        // No line overlaps the next.
        for (int i = 0; i + 1 < c.size(); ++i)
            QVERIFY(c[i].end <= c[i + 1].start + 1e-9);
    }

    void parseAndCache()
    {
        const QByteArray json = R"({"result":[{"conf":0.9,"end":1.2,"start":0.8,"word":"salom"},
                                              {"conf":1.0,"end":1.8,"start":1.3,"word":"dunyo"}],"text":"salom dunyo"})";
        const QList<Word> w = Speech::parseResult(json);
        QCOMPARE(w.size(), 2);
        QCOMPARE(w[1].text, QStringLiteral("dunyo"));
        QCOMPARE(w[0].start, 0.8);

        Speech::Transcript t;
        QVERIFY(!t.covers(0, 5));
        t.add(0, 5, w);
        QVERIFY(t.covers(0, 5) && t.covers(1, 4) && !t.covers(4, 6));
        t.add(5, 10, sentence({QStringLiteral("xayr")}, 6));
        QVERIFY(t.covers(0, 10));
        QCOMPARE(t.covered.size(), 1);
        t.add(0, 2, {}); // heard again: nothing there now
        QCOMPARE(t.words.size(), 1);
        const QString file = m_dir.filePath(QStringLiteral("t.bctr"));
        QVERIFY(Speech::saveTranscript(file, t));
        Speech::Transcript back;
        QVERIFY(Speech::loadTranscript(file, &back));
        QCOMPARE(back.words.size(), 1);
        QCOMPARE(back.words[0].text, QStringLiteral("xayr"));
        QVERIFY(back.covered == t.covered);
    }

    void models()
    {
        QCOMPARE(Speech::knownModels().size(), 3);
        QCOMPARE(Speech::knownModels().first().language, QStringLiteral("uz"));
        QCOMPARE(Speech::languageOf(QStringLiteral("/x/vosk-model-small-uz-0.22")), QStringLiteral("uz"));
        QCOMPARE(Speech::languageOf(QStringLiteral("vosk-model-small-en-us-0.15")), QStringLiteral("en"));
        const QString fake = m_dir.filePath(QStringLiteral("vosk-model-fake-ru"));
        QVERIFY(QDir().mkpath(fake + QStringLiteral("/am")));
        QVERIFY(!Speech::isModelFolder(fake));
        QFile f(fake + QStringLiteral("/am/final.mdl"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        QVERIFY(Speech::isModelFolder(fake));
        Speech::addModelFolder(fake);
        QVERIFY(Speech::installedModels().contains(QFileInfo(fake).absoluteFilePath()));
    }

    void recognize()
    {
        if (env("BC_VOSK_LIB").isEmpty())
            QSKIP("no speech library here (CI sets BC_VOSK_LIB)");
        QString why;
        QVERIFY2(Speech::libraryAvailable(&why), qPrintable(why));
        if (!recognitionReady())
            QSKIP("no model or no spoken sentence here");
        QList<Word> words;
        QString error;
        double last = -1;
        QVERIFY2(Speech::transcribe(env("BC_SPEECH_WAV"), env("BC_VOSK_MODEL"), 0, -1, &words, &error,
                                    [&](double f) { last = f; }),
                 qPrintable(error));
        QCOMPARE(last, 1.0);
        QStringList heard;
        for (const Word &w : words)
            heard << w.text;
        qInfo("heard: %s", qPrintable(heard.join(QLatin1Char(' '))));
        int found = 0;
        for (const QString &want : {QStringLiteral("one"), QStringLiteral("two"), QStringLiteral("three"),
                                    QStringLiteral("four"), QStringLiteral("five"), QStringLiteral("hello"),
                                    QStringLiteral("world"), QStringLiteral("test")})
            found += heard.contains(want);
        QVERIFY2(found >= 3, qPrintable(heard.join(QLatin1Char(' '))));
        for (int i = 1; i < words.size(); ++i)
            QVERIFY(words[i].start >= words[i - 1].start);
        // A part of the file: only the words in it, with file times.
        const double cut = words[words.size() / 2].start;
        QList<Word> part;
        QVERIFY(Speech::transcribe(env("BC_SPEECH_WAV"), env("BC_VOSK_MODEL"), cut, -1, &part, &error));
        QVERIFY(!part.isEmpty() && part.size() < words.size());
        QVERIFY2(part.first().start >= cut - 0.3, qPrintable(QString::number(part.first().start)));
    }

    void controller()
    {
        if (!recognitionReady())
            QSKIP("no speech library, model or spoken sentence here");
        ProjectController ctl;
        QCOMPARE(ctl.importFiles({env("BC_SPEECH_WAV")}), 1);
        waitIdle(ctl);
        // At 2 s on the sequence, from 0 of the file.
        QVERIFY(!ctl.placeMedia({ctl.project().items.first().id}, 0, 50).isEmpty());
        const QList<QUuid> clips = ctl.speechClips({}); // nothing selected: the first audio track
        QCOMPARE(clips.size(), 1);
        const QString model = env("BC_VOSK_MODEL");
        QVERIFY(!ctl.speechKnown(clips, model));
        QSignalSpy ready(&ctl, &ProjectController::speechReady);
        ctl.transcribeClips(clips, model);
        QVERIFY(ready.wait(120000));
        QVERIFY2(ready.first().at(0).toBool(), qPrintable(ready.first().at(1).toString()));
        QVERIFY(ctl.speechKnown(clips, model));

        const QList<Subtitle> lines = ctl.speechSubtitles(clips, model, Speech::CueSettings(), false);
        QVERIFY(!lines.isEmpty());
        const TimelineClip *c = ctl.timeline().clip(clips[0]);
        for (const Subtitle &s : lines)
            QVERIFY(s.start >= c->start && s.end() <= c->end());
        const int before = ctl.undoStack()->count();
        QCOMPARE(ctl.addSpeechSubtitles(clips, model, Speech::CueSettings(), false, true), int(lines.size()));
        QCOMPARE(ctl.undoStack()->count(), before + 1);
        QCOMPARE(ctl.timeline().subtitles.size(), lines.size());
        // Again with "replace": the same lines, not twice as many.
        QCOMPARE(ctl.addSpeechSubtitles(clips, model, Speech::CueSettings(), false, true), int(lines.size()));
        QCOMPARE(ctl.timeline().subtitles.size(), lines.size());
        ctl.undoStack()->undo();
        ctl.undoStack()->undo();
        QVERIFY(ctl.timeline().subtitles.isEmpty());

        // Heard once: a new controller reads the words from the cache at once.
        ProjectController again;
        again.importFiles({env("BC_SPEECH_WAV")});
        waitIdle(again);
        again.placeMedia({again.project().items.first().id}, 0, 0);
        QSignalSpy ready2(&again, &ProjectController::speechReady);
        again.transcribeClips(again.speechClips({}), model);
        QVERIFY(ready2.wait(10000));
        QVERIFY(again.speechKnown(again.speechClips({}), model));
    }

    void dialog()
    {
        MainWindow w;
        w.resize(1366, 768);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QAction *act = w.findChild<QAction *>(QStringLiteral("actAutoCaptions"));
        QVERIFY(act);
        QVERIFY(act->isEnabled()); // no longer a planned item
        ProjectController *ctl = w.controller();
        {
            SpeechDialog empty(ctl, {}, &w);
            auto *model = empty.findChild<QComboBox *>(QStringLiteral("speechModel"));
            QVERIFY(model->count() >= 3); // the models to download at least
            QVERIFY(!empty.findChild<QPushButton *>(QStringLiteral("speechStart"))->isEnabled()); // no clips
        }
        if (!recognitionReady())
            QSKIP("no speech library, model or spoken sentence here");
        ctl->importFiles({env("BC_SPEECH_WAV")});
        waitIdle(*ctl);
        ctl->placeMedia({ctl->project().items.first().id}, 0, 0);
        SpeechDialog dlg(ctl, {}, &w);
        auto *model = dlg.findChild<QComboBox *>(QStringLiteral("speechModel"));
        model->setCurrentIndex(model->findData(QFileInfo(env("BC_VOSK_MODEL")).absoluteFilePath()));
        QVERIFY(model->currentIndex() >= 0);
        QVERIFY(!dlg.findChild<QCheckBox *>(QStringLiteral("speechCyrillic"))->isVisibleTo(&dlg)); // English model
        auto *start = dlg.findChild<QPushButton *>(QStringLiteral("speechStart"));
        QVERIFY(start->isEnabled());
        start->click();
        QTRY_VERIFY_WITH_TIMEOUT(dlg.added() >= 0, 120000);
        QVERIFY(dlg.added() > 0);
        QCOMPARE(int(ctl->timeline().subtitles.size()), dlg.added());
    }
};

QTEST_MAIN(TestPhase14b)
#include "tst_phase14b.moc"
