#pragma once

#include <QList>
#include <QString>

#include <atomic>
#include <functional>

namespace bc::Speech {

// Speech to text (phase 14b) with Vosk (Apache 2.0), on this computer, in the
// helper program BukharaSpeech; the models (folders) are downloaded once or
// chosen by the user. Nothing is sent anywhere.

struct Word
{
    double start = 0; // seconds in the media file
    double end = 0;
    QString text;
    double conf = 1;
    bool operator==(const Word &o) const { return start == o.start && end == o.end && text == o.text; }
};

// --- the recognizer: the helper program BukharaSpeech (next to the program,
// or BC_SPEECH_HELPER) loads libvosk (speech/libvosk.dll next to the program,
// or BC_VOSK_LIB) in a process of its own.
void setLibraryPath(const QString &path);   // empty: the default
void setHelperProgram(const QString &path); // empty: the default
bool libraryAvailable(QString *error = nullptr); // both files are there

// --- models
struct ModelInfo
{
    QString id;       // folder name, e.g. "vosk-model-small-uz-0.22"
    QString language; // "uz", "ru", "en"
    QString url;      // where the zip is
    int sizeMb = 0;   // download size
};
QList<ModelInfo> knownModels();
QString modelsFolder();                 // AppLocalData/speech-models
bool isModelFolder(const QString &dir); // has what Vosk needs
// Model folders in modelsFolder() and the ones the user added (QSettings "speech/models").
QStringList installedModels();
void addModelFolder(const QString &dir);
// The language of a model folder from its name ("uz", "ru", "en"; empty when unknown).
QString languageOf(const QString &modelDir);
// Unpacks a downloaded zip into modelsFolder() (with the system's tar on
// Windows, unzip elsewhere); returns the model folder.
QString installZip(const QString &zipPath, QString *error);

// --- recognition
// Recognizes the speech of a media file between `from` and `to` seconds
// (to < 0: to the end). `progress` gets 0..1. False with *error on failure
// or when cancelled.
bool transcribe(const QString &mediaPath, const QString &modelDir, double from, double to, QList<Word> *words,
                QString *error, const std::function<void(double)> &progress = {},
                const std::atomic_bool *cancel = nullptr);
// Words as Vosk writes them in a result: {"result":[{"word":..,"start":..,"end":..,"conf":..}],"text":..}.
QList<Word> parseResult(const QByteArray &json);

// --- from words to subtitle lines
struct CueSettings
{
    int maxChars = 42;      // per line
    int maxLines = 2;
    double maxSeconds = 5;  // a line stays on screen at most this long
    double minSeconds = 1;  // and at least this long (if the next one lets it)
    double pause = 0.7;     // a pause this long starts a new line
};
struct Cue
{
    double start = 0;
    double end = 0;
    QString text; // lines separated by \n
};
QList<Cue> cues(const QList<Word> &words, const CueSettings &settings = {});

// Uzbek in Latin letters to Cyrillic (o'zbek -> ўзбек), by the official rules.
QString uzLatinToCyrillic(const QString &text);

// Cached words of a media file and model (MediaCache::Kind::Transcript):
// the parts of the file already heard and what was said in them.
struct Transcript
{
    QList<QPair<double, double>> covered; // seconds
    QList<Word> words;
    bool covers(double from, double to) const;
    void add(double from, double to, const QList<Word> &heard); // replaces words in that part
};
bool saveTranscript(const QString &file, const Transcript &t);
bool loadTranscript(const QString &file, Transcript *t);

} // namespace bc::Speech
