// BukharaSpeech (phase 14b): runs Vosk speech recognition for BUKHARA CINEMA
// in a process of its own. libvosk for Windows is built with another C++
// runtime than the program; loaded here (a static program that loads nothing
// else), it gets its own runtime DLLs from its folder. A crash in it cannot
// take the editor down either.
//
//   BukharaSpeech <libvosk path> <model folder> <sample rate>
// stdin: mono 16-bit little-endian samples. stdout: "ready", then one line of
// JSON per recognized piece ({"result":[{"word","start","end","conf"}...]}),
// then "done". Errors: "error <text>" and exit code 1.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

struct VoskModel;
struct VoskRecognizer;

using ModelNew = VoskModel *(*)(const char *);
using RecognizerNew = VoskRecognizer *(*)(VoskModel *, float);
using SetWords = void (*)(VoskRecognizer *, int);
using Accept = int (*)(VoskRecognizer *, const short *, int);
using Result = const char *(*)(VoskRecognizer *);
using SetLogLevel = void (*)(int);

void *symbol(void *lib, const char *name)
{
#ifdef _WIN32
    return reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(lib), name));
#else
    return dlsym(lib, name);
#endif
}

int fail(const std::string &what)
{
    std::printf("error %s\n", what.c_str());
    std::fflush(stdout);
    return 1;
}

// One line: Vosk writes its JSON over several lines.
void emit(const char *json)
{
    std::string s = json ? json : "";
    for (char &c : s)
        if (c == '\n' || c == '\r')
            c = ' ';
    std::printf("%s\n", s.c_str());
    std::fflush(stdout);
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 4)
        return fail("usage: BukharaSpeech <libvosk> <model folder> <sample rate>");
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _setmode(_fileno(stdin), _O_BINARY);
    // Its own C++ runtime DLLs are next to it: search the library's folder first.
    const int n = MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, nullptr, 0);
    std::wstring path(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, path.data(), n);
    for (wchar_t &c : path) // the altered search needs a path with backslashes
        if (c == L'/')
            c = L'\\';
    void *lib = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!lib)
        return fail("cannot load " + std::string(argv[1]) + " (error " + std::to_string(GetLastError()) + ")");
#else
    void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib)
        return fail(std::string("cannot load ") + argv[1] + ": " + dlerror());
#endif
    auto modelNew = reinterpret_cast<ModelNew>(symbol(lib, "vosk_model_new"));
    auto recognizerNew = reinterpret_cast<RecognizerNew>(symbol(lib, "vosk_recognizer_new"));
    auto setWords = reinterpret_cast<SetWords>(symbol(lib, "vosk_recognizer_set_words"));
    auto accept = reinterpret_cast<Accept>(symbol(lib, "vosk_recognizer_accept_waveform_s"));
    auto result = reinterpret_cast<Result>(symbol(lib, "vosk_recognizer_result"));
    auto finalResult = reinterpret_cast<Result>(symbol(lib, "vosk_recognizer_final_result"));
    auto setLogLevel = reinterpret_cast<SetLogLevel>(symbol(lib, "vosk_set_log_level"));
    if (!modelNew || !recognizerNew || !setWords || !accept || !result || !finalResult)
        return fail("the library lacks functions this program needs");
    if (setLogLevel)
        setLogLevel(-1);
    VoskModel *model = modelNew(argv[2]);
    if (!model)
        return fail(std::string("the model in ") + argv[2] + " could not be loaded");
    const float rate = float(std::atof(argv[3]));
    VoskRecognizer *rec = recognizerNew(model, rate > 0 ? rate : 16000.0f);
    if (!rec)
        return fail("the recognizer could not start");
    setWords(rec, 1);
    std::printf("ready\n");
    std::fflush(stdout);

    std::vector<short> buf(8192);
    size_t got;
    while ((got = std::fread(buf.data(), sizeof(short), buf.size(), stdin)) > 0)
        if (accept(rec, buf.data(), int(got)))
            emit(result(rec));
    emit(finalResult(rec));
    std::printf("done\n");
    std::fflush(stdout);
    return 0; // the library and model go with the process
}
