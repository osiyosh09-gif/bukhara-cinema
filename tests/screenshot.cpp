// Developer helper: renders the main window to PNG files (one per language)
// without a display. Usage: bc_screenshot <output-dir>
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
            // A title over the picture, selected so the Clip panel shows it.
            if (auto *newText = w.findChild<QAction *>(QStringLiteral("actNewText")))
                newText->trigger();
            ctl->waitForBackgroundWork();
            for (int i = 0; i < 20; ++i)
                app.processEvents();
        }
        app.processEvents();
        w.grab().save(out + QStringLiteral("/main_%1.png").arg(code));
    }
    return 0;
}
