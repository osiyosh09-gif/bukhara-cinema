#include "app/AppInfo.h"
#include "app/Language.h"
#include "app/Logging.h"
#include "app/MainWindow.h"
#include "app/Theme.h"

#include <QApplication>
#include <QIcon>
#include <QSysInfo>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(bc::orgName());
    QApplication::setApplicationName(bc::appName());
    QApplication::setApplicationVersion(bc::appVersion());
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/bukhara-cinema.png")));

    bc::Logging::init();
    qInfo("%s %s starting on %s (Qt %s)",
          qPrintable(bc::appName()), qPrintable(bc::appVersion()),
          qPrintable(QSysInfo::prettyProductName()), qVersion());

    bc::Theme::apply(app);
    bc::Language::instance().loadSaved();

    bc::MainWindow window;
    window.show();

    // A .bcproj passed on the command line (double-click in Explorer).
    for (const QString &arg : QCoreApplication::arguments().mid(1)) {
        if (arg.endsWith(QStringLiteral(".bcproj"), Qt::CaseInsensitive)) {
            window.openProjectFile(arg);
            break;
        }
    }

    // Used by the Windows build pipeline: start, show the window, exit.
    // It proves that every required DLL was packaged.
    if (QCoreApplication::arguments().contains(QStringLiteral("--smoke-test")))
        QTimer::singleShot(1500, &app, [] { QCoreApplication::exit(0); });

    const int rc = app.exec();
    qInfo("exit code %d", rc);
    return rc;
}
