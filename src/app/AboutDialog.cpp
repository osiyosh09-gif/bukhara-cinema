#include "app/AboutDialog.h"

#include "app/AppInfo.h"
#include "core/MediaProbe.h"
#include "engine/TimelinePlayer.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QtGlobal>

namespace bc {

QString AboutDialog::licensesHtml()
{
    QString html;
    html += QStringLiteral("<h3>%1</h3>").arg(tr("Third-party components"));
    html += QStringLiteral("<p>%1</p>").arg(
        tr("BUKHARA CINEMA uses the following open-source components. They are linked dynamically "
           "(as separate DLL files), so they can be replaced with compatible versions."));
    html += QStringLiteral("<table cellpadding='4'>"
                           "<tr><td><b>Qt %1</b></td><td>GNU LGPL v3</td><td>%2</td></tr>"
                           "<tr><td><b>%5</b></td><td>%6</td><td>%7</td></tr>"
                           "<tr><td><b>%8</b></td><td>GNU LGPL v2.1</td><td>%9</td></tr>"
                           "<tr><td><b>SDL 2</b></td><td>zlib</td><td>%10</td></tr>"
                           "<tr><td><b>MinGW-w64 runtime</b></td><td>%3</td><td>%4</td></tr>"
                           "</table>")
                .arg(QString::fromLatin1(qVersion()),
                     tr("User interface"),
                     tr("GCC runtime library exception, MIT/ZPL"),
                     tr("C++ runtime libraries"),
                     MediaProbe::ffmpegVersionString().toHtmlEscaped(),
                     MediaProbe::ffmpegLicense().toHtmlEscaped(),
                     tr("Reading media files, thumbnails"),
                     Engine::versionString().toHtmlEscaped(),
                     tr("Video engine: timeline playback"),
                     tr("Sound output"));
    html += QStringLiteral("<p>%1</p>").arg(
        tr("Full license texts are installed in the \"licenses\" folder next to the program. "
           "Source code: Qt https://download.qt.io/, FFmpeg https://ffmpeg.org/download.html, "
           "MLT https://github.com/mltframework/mlt, SDL https://libsdl.org/"));
    html += QStringLiteral("<p>%1</p>").arg(
        tr("Only the LGPL parts of MLT are shipped; its GPL modules are left out."));
    return html;
}

AboutDialog::AboutDialog(Page page, QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("aboutDialog"));
    setWindowTitle(tr("About BUKHARA CINEMA"));
    auto *tabs = new QTabWidget(this);

    auto *about = new QLabel(this);
    about->setAlignment(Qt::AlignCenter);
    about->setTextFormat(Qt::RichText);
    about->setText(QStringLiteral(
                       "<p style='font-size:22px; font-weight:700; color:#d1a54a;'>%1</p>"
                       "<p>%2</p><p>%3</p><p style='color:#8a8a8a;'>%4</p>")
                       .arg(appName(),
                            tr("Version %1").arg(appVersion()),
                            tr("Independent video editor for Windows."),
                            tr("Phase 3: editing, playback with sound, titles and clip volume. MP4 export "
                               "arrives in the next phase; see Help > Feature status.")));
    tabs->addTab(about, tr("About"));

    auto *lic = new QTextBrowser(this);
    lic->setOpenExternalLinks(true);
    lic->setHtml(licensesHtml());
    tabs->addTab(lic, tr("Licenses"));
    tabs->setCurrentIndex(page == Page::Licenses ? 1 : 0);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Close"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *lay = new QVBoxLayout(this);
    lay->addWidget(tabs);
    lay->addWidget(buttons);
    resize(560, 380);
}

} // namespace bc
