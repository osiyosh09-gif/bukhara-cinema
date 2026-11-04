// Developer helper: renders the main window to PNG files (one per language)
// without a display. Usage: bc_screenshot <output-dir>
#include <QThread>
#include "app/Language.h"
#include "app/MainWindow.h"
#include "app/ProjectController.h"
#include "app/Theme.h"

#include <QAction>

#include <QApplication>
#include <QDir>
#include <QStandardPaths>
#include <QUuid>

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    QApplication::setApplicationName(QStringLiteral("bc_screenshot"));
    bc::Theme::apply(app);
    const QString out = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral(".");
    QDir().mkpath(out);
    for (const QString &code : bc::Language::availableCodes()) {
        bc::Language::instance().setLanguage(code, false);
        bc::MainWindow w;
        w.resize(1366, 768);
        w.show();
        // Optional media files to import: bc_screenshot <dir> [files...]
        if (argc > 2) {
            QStringList files;
            for (int i = 2; i < argc; ++i)
                files << QString::fromLocal8Bit(argv[i]);
            bc::ProjectController *ctl = w.controller();
            ctl->importFiles(files);
            ctl->waitForBackgroundWork();
            for (int i = 0; i < 20; ++i)
                app.processEvents();
            // Put everything on the timeline and show a frame in the Program monitor.
            QList<QUuid> ids;
            for (const auto &m : ctl->project().items)
                m.info.hasVideo() ? ids.prepend(m.id) : ids.append(m.id);
            ctl->placeMedia(ids, 0, 0);
            ctl->setPlayhead(40);
            ctl->addMarker(20);
            // The first video in the Source monitor with an In/Out range.
            if (!ids.isEmpty() && ctl->setSourceMedia(ids.first())) {
                ctl->setSourcePosition(10);
                ctl->setSourceIn(10);
                ctl->setSourceOut(std::max<qint64>(10, ctl->sourceDuration() / 2));
            }
            // A title over the picture, selected so the Clip panel shows it.
            if (auto *newText = w.findChild<QAction *>(QStringLiteral("actNewText")))
                newText->trigger();
            // BC_SHOT_SUBTITLES=1: a subtitle under the playhead, Subtitles tab open.
            if (qEnvironmentVariableIsSet("BC_SHOT_SUBTITLES"))
                if (auto *add = w.findChild<QAction *>(QStringLiteral("actAddSubtitle"))) {
                    ctl->setPlayhead(30);
                    add->trigger();
                    const auto &subs = ctl->timeline().subtitles;
                    if (!subs.isEmpty())
                        ctl->setSubtitleText(subs.first().id, QStringLiteral("Бухоро — қадимий шаҳар\nBuxoro — qadimiy shahar"));
                    ctl->setPlayhead(40);
                }
            ctl->waitForBackgroundWork();
            for (int i = 0; i < 40; ++i) {
                app.processEvents();
                QThread::msleep(25);
            }
        }
        app.processEvents();
        w.grab().save(out + QStringLiteral("/main_%1.png").arg(code));
    }
    return 0;
}
