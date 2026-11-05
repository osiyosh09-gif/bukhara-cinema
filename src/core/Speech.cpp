#include "core/Speech.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QProcess>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace bc::Speech {

namespace {

// --- the helper program and the library (phase 14b): Vosk runs in
// BukharaSpeech, which loads libvosk itself (see src/speechhelper/main.cpp).

QMutex s_mutex;
QString s_libraryPath;
QString s_helperPath;

QString appDir()
{
    return QCoreApplication::applicationDirPath();
}

QString helperPath()
{
    if (!s_helperPath.isEmpty())
        return s_helperPath;
    const QString env = qEnvironmentVariable("BC_SPEECH_HELPER");
    if (!env.isEmpty())
        return env;
#ifdef Q_OS_WIN
    return QDir(appDir()).filePath(QStringLiteral("BukharaSpeech.exe"));
#else
    return QDir(appDir()).filePath(QStringLiteral("BukharaSpeech"));
#endif
}

QString libraryPath()
{
    if (!s_libraryPath.isEmpty())
        return s_libraryPath;
    const QString env = qEnvironmentVariable("BC_VOSK_LIB");
    if (!env.isEmpty())
        return env;
#ifdef Q_OS_WIN
    return QDir(appDir()).filePath(QStringLiteral("speech/libvosk.dll"));
#else
    return QDir(appDir()).filePath(QStringLiteral("speech/libvosk.so"));
#endif
}

struct FormatCloser { void operator()(AVFormatContext *c) const { avformat_close_input(&c); } };
struct CodecCloser { void operator()(AVCodecContext *c) const { avcodec_free_context(&c); } };
struct FrameCloser { void operator()(AVFrame *f) const { av_frame_free(&f); } };
struct PacketCloser { void operator()(AVPacket *p) const { av_packet_free(&p); } };

float sampleAt(const AVFrame *f, int i, int ch, int channels)
{
    const auto fmt = AVSampleFormat(f->format);
    const bool planar = av_sample_fmt_is_planar(fmt);
    const uint8_t *base = planar ? f->extended_data[ch] : f->extended_data[0];
    const int idx = planar ? i : i * channels + ch;
    switch (av_get_packed_sample_fmt(fmt)) {
    case AV_SAMPLE_FMT_U8: return (reinterpret_cast<const uint8_t *>(base)[idx] - 128) / 128.f;
    case AV_SAMPLE_FMT_S16: return reinterpret_cast<const int16_t *>(base)[idx] / 32768.f;
    case AV_SAMPLE_FMT_S32: return float(reinterpret_cast<const int32_t *>(base)[idx] / 2147483648.0);
    case AV_SAMPLE_FMT_FLT: return reinterpret_cast<const float *>(base)[idx];
    case AV_SAMPLE_FMT_DBL: return float(reinterpret_cast<const double *>(base)[idx]);
    default: return 0;
    }
}

bool isApostrophe(QChar c)
{
    return c == QLatin1Char('\'') || c == QLatin1Char('`') || c == QChar(0x02BB) || c == QChar(0x02BC)
           || c == QChar(0x2018) || c == QChar(0x2019);
}

bool isVowel(QChar c)
{
    return QStringLiteral("aeiouAEIOU").contains(c);
}

} // namespace

// --- the library

void setLibraryPath(const QString &path)
{
    QMutexLocker lock(&s_mutex);
    s_libraryPath = path;
}

void setHelperProgram(const QString &path)
{
    QMutexLocker lock(&s_mutex);
    s_helperPath = path;
}

bool libraryAvailable(QString *error)
{
    QMutexLocker lock(&s_mutex);
    if (!QFileInfo(helperPath()).isExecutable()) {
        if (error)
            *error = QStringLiteral("%1 is not there").arg(QDir::toNativeSeparators(helperPath()));
        return false;
    }
    if (!QFileInfo::exists(libraryPath())) {
        if (error)
            *error = QStringLiteral("%1 is not there").arg(QDir::toNativeSeparators(libraryPath()));
        return false;
    }
    return true;
}

// --- models

QList<ModelInfo> knownModels()
{
    const QString base = QStringLiteral("https://alphacephei.com/vosk/models/");
    return {
        {QStringLiteral("vosk-model-small-uz-0.22"), QStringLiteral("uz"), base + QStringLiteral("vosk-model-small-uz-0.22.zip"), 49},
        {QStringLiteral("vosk-model-small-ru-0.22"), QStringLiteral("ru"), base + QStringLiteral("vosk-model-small-ru-0.22.zip"), 45},
        {QStringLiteral("vosk-model-small-en-us-0.15"), QStringLiteral("en"), base + QStringLiteral("vosk-model-small-en-us-0.15.zip"), 40},
    };
}

QString modelsFolder()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("speech-models"));
}

bool isModelFolder(const QString &dir)
{
    const QDir d(dir);
    return d.exists() && (QFileInfo::exists(d.filePath(QStringLiteral("am/final.mdl")))
                          || QFileInfo::exists(d.filePath(QStringLiteral("final.mdl"))));
}

QStringList installedModels()
{
    QStringList out;
    const QDir root(modelsFolder());
    for (const QFileInfo &fi : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
        if (isModelFolder(fi.absoluteFilePath()))
            out << fi.absoluteFilePath();
    for (const QString &dir : QSettings().value(QStringLiteral("speech/models")).toStringList())
        if (isModelFolder(dir) && !out.contains(QFileInfo(dir).absoluteFilePath()))
            out << QFileInfo(dir).absoluteFilePath();
    const QString env = qEnvironmentVariable("BC_VOSK_MODEL");
    if (!env.isEmpty() && isModelFolder(env) && !out.contains(QFileInfo(env).absoluteFilePath()))
        out << QFileInfo(env).absoluteFilePath();
    return out;
}

void addModelFolder(const QString &dir)
{
    QSettings s;
    QStringList list = s.value(QStringLiteral("speech/models")).toStringList();
    const QString abs = QFileInfo(dir).absoluteFilePath();
    if (!list.contains(abs))
        list << abs;
    s.setValue(QStringLiteral("speech/models"), list);
}

QString languageOf(const QString &modelDir)
{
    const QString name = QFileInfo(modelDir).fileName().toLower();
    for (const QString &lang : {QStringLiteral("uz"), QStringLiteral("ru"), QStringLiteral("en")})
        if (name.contains(QLatin1Char('-') + lang + QLatin1Char('-')) || name.endsWith(QLatin1Char('-') + lang))
            return lang;
    return {};
}

QString installZip(const QString &zipPath, QString *error)
{
    QDir().mkpath(modelsFolder());
    const QStringList before = QDir(modelsFolder()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    QProcess p;
#ifdef Q_OS_WIN
    // tar.exe of Windows 10 and later reads zip files.
    const QString tar = QDir(qEnvironmentVariable("SystemRoot", QStringLiteral("C:\\Windows")))
                            .filePath(QStringLiteral("System32/tar.exe"));
    p.start(QFileInfo::exists(tar) ? tar : QStringLiteral("tar"),
            {QStringLiteral("-xf"), QDir::toNativeSeparators(zipPath), QStringLiteral("-C"),
             QDir::toNativeSeparators(modelsFolder())});
#else
    p.start(QStringLiteral("unzip"), {QStringLiteral("-q"), QStringLiteral("-o"), zipPath, QStringLiteral("-d"), modelsFolder()});
#endif
    if (!p.waitForFinished(10 * 60 * 1000) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        if (error)
            *error = QStringLiteral("the zip file could not be unpacked: %1")
                         .arg(QString::fromLocal8Bit(p.readAllStandardError()).trimmed());
        return {};
    }
    // The zip holds one folder, named like the model.
    for (const QString &name : QDir(modelsFolder()).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString dir = QDir(modelsFolder()).filePath(name);
        if (isModelFolder(dir) && (!before.contains(name) || zipPath.contains(name)))
            return dir;
    }
    if (error)
        *error = QStringLiteral("the zip file holds no Vosk model");
    return {};
}

// --- recognition

QList<Word> parseResult(const QByteArray &json)
{
    QList<Word> out;
    const QJsonArray result = QJsonDocument::fromJson(json).object().value(QStringLiteral("result")).toArray();
    for (const QJsonValue &v : result) {
        const QJsonObject o = v.toObject();
        Word w;
        w.text = o.value(QStringLiteral("word")).toString();
        w.start = o.value(QStringLiteral("start")).toDouble();
        w.end = o.value(QStringLiteral("end")).toDouble();
        w.conf = o.value(QStringLiteral("conf")).toDouble(1);
        if (!w.text.isEmpty())
            out << w;
    }
    return out;
}

bool transcribe(const QString &mediaPath, const QString &modelDir, double from, double to, QList<Word> *words,
                QString *error, const std::function<void(double)> &progress, const std::atomic_bool *cancel)
{
    auto fail = [error](const QString &e) {
        if (error)
            *error = e;
        return false;
    };
    words->clear();
    QString helper, library;
    {
        QString why;
        if (!libraryAvailable(&why))
            return fail(QStringLiteral("speech recognition is not installed: %1").arg(why));
        QMutexLocker lock(&s_mutex);
        helper = helperPath();
        library = libraryPath();
    }

    AVFormatContext *raw = nullptr;
    if (avformat_open_input(&raw, QFileInfo(mediaPath).absoluteFilePath().toUtf8().constData(), nullptr, nullptr) < 0)
        return fail(QStringLiteral("cannot open %1").arg(mediaPath));
    std::unique_ptr<AVFormatContext, FormatCloser> fmt(raw);
    if (avformat_find_stream_info(raw, nullptr) < 0)
        return fail(QStringLiteral("cannot read stream info"));
    const AVCodec *dec = nullptr;
    const int ai = av_find_best_stream(raw, AVMEDIA_TYPE_AUDIO, -1, -1, &dec, 0);
    if (ai < 0 || !dec)
        return fail(QStringLiteral("no sound in %1").arg(QFileInfo(mediaPath).fileName()));
    AVStream *st = raw->streams[ai];
    std::unique_ptr<AVCodecContext, CodecCloser> cc(avcodec_alloc_context3(dec));
    if (!cc || avcodec_parameters_to_context(cc.get(), st->codecpar) < 0 || avcodec_open2(cc.get(), dec, nullptr) < 0)
        return fail(QStringLiteral("cannot open the sound decoder"));
    for (unsigned i = 0; i < raw->nb_streams; ++i)
        if (int(i) != ai)
            raw->streams[i]->discard = AVDISCARD_ALL;
    const int rate = cc->sample_rate > 0 ? cc->sample_rate : 16000;
    const double fileStart = raw->start_time != AV_NOPTS_VALUE ? raw->start_time / double(AV_TIME_BASE) : 0.0;
    const double duration = raw->duration > 0 ? raw->duration / double(AV_TIME_BASE) : 0.0;
    const double end = to >= 0 ? to : (duration > 0 ? duration : 1e12);
    if (from > 0) {
        const int64_t ts = int64_t((from + fileStart) * AV_TIME_BASE);
        av_seek_frame(raw, -1, ts, AVSEEK_FLAG_BACKWARD);
    }

    // The recognizer: the helper program, fed through its standard input.
    QProcess proc;
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.start(helper, {QDir::toNativeSeparators(library), QDir::toNativeSeparators(QFileInfo(modelDir).absoluteFilePath()),
                        QString::number(rate)});
    if (!proc.waitForStarted(30000))
        return fail(QStringLiteral("%1 did not start").arg(QDir::toNativeSeparators(helper)));
    double origin = -1; // file time of the first sample given
    QString helperError;
    bool done = false;
    auto readLines = [&] {
        while (proc.canReadLine()) {
            const QByteArray line = proc.readLine().trimmed();
            if (line.startsWith('{')) {
                for (Word w : parseResult(line)) {
                    w.start += std::max(0.0, origin);
                    w.end += std::max(0.0, origin);
                    *words << w;
                }
            } else if (line.startsWith("error ")) {
                helperError = QString::fromUtf8(line.mid(6));
            } else if (line == "done") {
                done = true;
            }
        }
    };
    // Loading a model takes a few seconds.
    for (bool ready = false; !ready;) {
        if (!proc.canReadLine() && !proc.waitForReadyRead(120000))
            return fail(helperError.isEmpty() ? QStringLiteral("the recognizer did not start") : helperError);
        while (proc.canReadLine()) {
            const QByteArray line = proc.readLine().trimmed();
            if (line == "ready")
                ready = true;
            else if (line.startsWith("error "))
                return fail(QString::fromUtf8(line.mid(6)));
        }
    }

    std::unique_ptr<AVFrame, FrameCloser> frame(av_frame_alloc());
    std::unique_ptr<AVPacket, PacketCloser> pkt(av_packet_alloc());
    double fed = 0;
    double clock = -1;
    bool finished = false;
    std::vector<short> mono;
    auto consume = [&] {
        const int channels = frame->ch_layout.nb_channels;
        if (channels <= 0 || frame->sample_rate != rate)
            return;
        if (frame->pts != AV_NOPTS_VALUE)
            clock = frame->pts * av_q2d(st->time_base) - fileStart;
        else if (clock < 0)
            clock = 0;
        mono.clear();
        double t = clock;
        for (int i = 0; i < frame->nb_samples; ++i, t += 1.0 / rate) {
            if (t < from)
                continue;
            if (t >= end) {
                finished = true;
                break;
            }
            if (origin < 0)
                origin = t;
            float m = 0;
            for (int c = 0; c < channels; ++c)
                m += sampleAt(frame.get(), i, c, channels);
            m /= float(channels);
            mono.push_back(short(std::clamp(m, -1.0f, 1.0f) * 32767.0f));
        }
        clock += double(frame->nb_samples) / rate;
        if (mono.empty())
            return;
        proc.write(reinterpret_cast<const char *>(mono.data()), qint64(mono.size() * sizeof(short)));
        if (proc.bytesToWrite() > (1 << 20))
            proc.waitForBytesWritten(60000);
        readLines();
        fed += double(mono.size()) / rate;
        if (progress && end > from && end < 1e11)
            progress(std::clamp(fed / (end - from), 0.0, 0.99));
    };

    while (!finished && av_read_frame(raw, pkt.get()) >= 0) {
        if (cancel && cancel->load()) {
            proc.kill();
            proc.waitForFinished(5000);
            return fail(QStringLiteral("cancelled"));
        }
        if (pkt->stream_index == ai && avcodec_send_packet(cc.get(), pkt.get()) >= 0)
            while (!finished && avcodec_receive_frame(cc.get(), frame.get()) >= 0)
                consume();
        av_packet_unref(pkt.get());
        if (proc.state() != QProcess::Running)
            break;
    }
    if (!finished) {
        avcodec_send_packet(cc.get(), nullptr);
        while (!finished && avcodec_receive_frame(cc.get(), frame.get()) >= 0)
            consume();
    }
    proc.waitForBytesWritten(60000);
    proc.closeWriteChannel();
    while (!done) {
        readLines();
        if (done)
            break;
        if (proc.state() != QProcess::Running && !proc.canReadLine()) {
            proc.waitForReadyRead(100);
            readLines();
            break;
        }
        if (cancel && cancel->load()) {
            proc.kill();
            proc.waitForFinished(5000);
            return fail(QStringLiteral("cancelled"));
        }
        proc.waitForReadyRead(1000);
    }
    proc.waitForFinished(10000);
    if (!done)
        return fail(helperError.isEmpty() ? QStringLiteral("the recognizer stopped") : helperError);
    if (progress)
        progress(1.0);
    return true;
}

// --- cues

QList<Cue> cues(const QList<Word> &words, const CueSettings &s)
{
    QList<Cue> out;
    QStringList current;
    double start = 0, last = 0;
    auto lengthOf = [](const QStringList &ws) {
        int n = 0;
        for (const QString &w : ws)
            n += int(w.size()) + 1;
        return std::max(0, n - 1);
    };
    auto flush = [&] {
        if (current.isEmpty())
            return;
        // Lines of at most maxChars, as even as the words allow.
        const int total = lengthOf(current);
        const int lines = std::clamp((total + s.maxChars - 1) / std::max(1, s.maxChars), 1, std::max(1, s.maxLines));
        const int target = (total + lines - 1) / lines;
        QStringList out_lines;
        QStringList line;
        for (const QString &w : current) {
            if (!line.isEmpty() && lengthOf(line) + 1 + int(w.size()) > target && out_lines.size() + 1 < lines) {
                out_lines << line.join(QLatin1Char(' '));
                line.clear();
            }
            line << w;
        }
        out_lines << line.join(QLatin1Char(' '));
        QString text = out_lines.join(QLatin1Char('\n'));
        if (!text.isEmpty())
            text[0] = text[0].toUpper();
        out << Cue{start, last, text};
        current.clear();
    };
    for (const Word &w : words) {
        const bool pause = !current.isEmpty() && w.start - last >= s.pause;
        const bool tooLong = !current.isEmpty() && w.end - start > s.maxSeconds;
        const bool tooFull = !current.isEmpty()
                             && lengthOf(current) + 1 + int(w.text.size()) > s.maxChars * std::max(1, s.maxLines);
        if (pause || tooLong || tooFull)
            flush();
        if (current.isEmpty())
            start = w.start;
        current << w.text;
        last = w.end;
    }
    flush();
    // Short lines stay a little longer, up to the next one.
    for (int i = 0; i < out.size(); ++i) {
        const double next = i + 1 < out.size() ? out[i + 1].start : 1e12;
        const double want = std::max(out[i].end + 0.2, out[i].start + s.minSeconds);
        out[i].end = std::max(out[i].end, std::min(want, next));
    }
    return out;
}

QString uzLatinToCyrillic(const QString &text)
{
    static const QHash<QChar, QString> single = {
        {QLatin1Char('a'), QStringLiteral("а")}, {QLatin1Char('b'), QStringLiteral("б")},
        {QLatin1Char('d'), QStringLiteral("д")}, {QLatin1Char('e'), QStringLiteral("е")},
        {QLatin1Char('f'), QStringLiteral("ф")}, {QLatin1Char('g'), QStringLiteral("г")},
        {QLatin1Char('h'), QStringLiteral("ҳ")}, {QLatin1Char('i'), QStringLiteral("и")},
        {QLatin1Char('j'), QStringLiteral("ж")}, {QLatin1Char('k'), QStringLiteral("к")},
        {QLatin1Char('l'), QStringLiteral("л")}, {QLatin1Char('m'), QStringLiteral("м")},
        {QLatin1Char('n'), QStringLiteral("н")}, {QLatin1Char('o'), QStringLiteral("о")},
        {QLatin1Char('p'), QStringLiteral("п")}, {QLatin1Char('q'), QStringLiteral("қ")},
        {QLatin1Char('r'), QStringLiteral("р")}, {QLatin1Char('s'), QStringLiteral("с")},
        {QLatin1Char('t'), QStringLiteral("т")}, {QLatin1Char('u'), QStringLiteral("у")},
        {QLatin1Char('v'), QStringLiteral("в")}, {QLatin1Char('x'), QStringLiteral("х")},
        {QLatin1Char('y'), QStringLiteral("й")}, {QLatin1Char('z'), QStringLiteral("з")},
        {QLatin1Char('c'), QStringLiteral("с")}, {QLatin1Char('w'), QStringLiteral("в")},
    };
    QString out;
    const int n = int(text.size());
    for (int i = 0; i < n; ++i) {
        const QChar c = text[i];
        const QChar lc = c.toLower();
        const bool upper = c.isUpper();
        const QChar next = i + 1 < n ? text[i + 1].toLower() : QChar();
        const bool wordStart = i == 0 || !text[i - 1].isLetter();
        const bool afterVowel = i > 0 && isVowel(text[i - 1]);
        auto put = [&](const QString &cyr) {
            QString s = cyr;
            if (upper && !s.isEmpty())
                s[0] = s[0].toUpper();
            out += s;
        };
        if ((lc == QLatin1Char('o') || lc == QLatin1Char('g')) && i + 1 < n && isApostrophe(text[i + 1])) {
            put(lc == QLatin1Char('o') ? QStringLiteral("ў") : QStringLiteral("ғ"));
            ++i;
            continue;
        }
        if (lc == QLatin1Char('s') && next == QLatin1Char('h')) {
            put(QStringLiteral("ш"));
            ++i;
            continue;
        }
        if (lc == QLatin1Char('c') && next == QLatin1Char('h')) {
            put(QStringLiteral("ч"));
            ++i;
            continue;
        }
        if (lc == QLatin1Char('y') && (wordStart || afterVowel)) {
            const QString pairs = QStringLiteral("aoue");
            const int k = pairs.indexOf(next);
            if (k >= 0 && !(next == QLatin1Char('o') && i + 2 < n && isApostrophe(text[i + 2]))) {
                put(QStringList{QStringLiteral("я"), QStringLiteral("ё"), QStringLiteral("ю"), QStringLiteral("е")}[k]);
                ++i;
                continue;
            }
        }
        if (lc == QLatin1Char('e') && (wordStart || afterVowel)) {
            put(QStringLiteral("э"));
            continue;
        }
        if (isApostrophe(c)) {
            out += QStringLiteral("ъ");
            continue;
        }
        if (single.contains(lc)) {
            put(single.value(lc));
            continue;
        }
        out += c;
    }
    return out;
}

// --- transcript cache

bool Transcript::covers(double from, double to) const
{
    for (const auto &r : covered)
        if (r.first <= from + 0.05 && r.second >= to - 0.05)
            return true;
    return false;
}

void Transcript::add(double from, double to, const QList<Word> &heard)
{
    QList<Word> kept;
    for (const Word &w : words)
        if (w.end <= from || w.start >= to)
            kept << w;
    kept << heard;
    std::sort(kept.begin(), kept.end(), [](const Word &a, const Word &b) { return a.start < b.start; });
    words = kept;
    covered << qMakePair(from, to);
    std::sort(covered.begin(), covered.end());
    QList<QPair<double, double>> merged;
    for (const auto &r : covered) {
        if (!merged.isEmpty() && r.first <= merged.last().second + 0.05)
            merged.last().second = std::max(merged.last().second, r.second);
        else
            merged << r;
    }
    covered = merged;
}

bool saveTranscript(const QString &file, const Transcript &t)
{
    QJsonArray covered, words;
    for (const auto &r : t.covered)
        covered.append(QJsonArray{r.first, r.second});
    for (const Word &w : t.words)
        words.append(QJsonArray{w.start, w.end, w.text, w.conf});
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), 1},
                                      {QStringLiteral("covered"), covered},
                                      {QStringLiteral("words"), words}})
                .toJson(QJsonDocument::Compact));
    return f.commit();
}

bool loadTranscript(const QString &file, Transcript *t)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    if (o.value(QStringLiteral("version")).toInt() != 1)
        return false;
    t->covered.clear();
    t->words.clear();
    for (const QJsonValue &v : o.value(QStringLiteral("covered")).toArray()) {
        const QJsonArray a = v.toArray();
        t->covered << qMakePair(a.at(0).toDouble(), a.at(1).toDouble());
    }
    for (const QJsonValue &v : o.value(QStringLiteral("words")).toArray()) {
        const QJsonArray a = v.toArray();
        Word w;
        w.start = a.at(0).toDouble();
        w.end = a.at(1).toDouble();
        w.text = a.at(2).toString();
        w.conf = a.at(3).toDouble(1);
        t->words << w;
    }
    return true;
}

} // namespace bc::Speech
