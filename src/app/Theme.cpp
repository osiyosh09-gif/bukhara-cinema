#include "app/Theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

namespace bc::Theme {

void apply(QApplication &app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    const QColor window(0x26, 0x26, 0x26);
    const QColor base(0x1c, 0x1c, 0x1c);
    const QColor alt(0x2e, 0x2e, 0x2e);
    const QColor text(0xd6, 0xd6, 0xd6);
    const QColor disabled(0x6e, 0x6e, 0x6e);
    const QColor accent(0xd1, 0xa5, 0x4a); // Bukhara gold

    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, alt);
    p.setColor(QPalette::ToolTipBase, alt);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, disabled);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, alt);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::black);
    p.setColor(QPalette::Link, accent);
    p.setColor(QPalette::Mid, QColor(0x3a, 0x3a, 0x3a));
    p.setColor(QPalette::Dark, QColor(0x18, 0x18, 0x18));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, disabled);
    app.setPalette(p);

    app.setStyleSheet(QStringLiteral(R"(
        QMainWindow::separator { background: #1a1a1a; width: 4px; height: 4px; }
        QMainWindow::separator:hover { background: #d1a54a; }
        QDockWidget { color: #cfcfcf; }
        QDockWidget::title { background: #303030; padding: 5px 8px; border-bottom: 1px solid #1a1a1a; }
        QTabWidget::pane { border: 1px solid #1a1a1a; top: -1px; }
        QTabBar::tab { background: #2b2b2b; color: #a8a8a8; padding: 5px 12px; border: 1px solid #1a1a1a; border-bottom: none; }
        QTabBar::tab:selected { background: #333333; color: #ffffff; border-top: 2px solid #d1a54a; }
        QMenuBar { background: #202020; }
        QMenuBar::item:selected { background: #3a3a3a; }
        QMenu { background: #2a2a2a; border: 1px solid #151515; }
        QMenu::item:selected { background: #d1a54a; color: #000000; }
        QMenu::item:disabled { color: #6e6e6e; }
        QToolBar { background: #202020; border-bottom: 1px solid #151515; spacing: 4px; padding: 2px; }
        QToolButton { padding: 4px 10px; border-radius: 3px; }
        QToolButton:hover { background: #3a3a3a; }
        QToolButton:disabled { color: #6e6e6e; }
        QStatusBar { background: #202020; color: #9a9a9a; }
        QPushButton { background: #353535; border: 1px solid #151515; padding: 5px 14px; border-radius: 3px; }
        QPushButton:hover { background: #404040; }
        QPushButton:disabled { color: #6e6e6e; }
        QComboBox { background: #2e2e2e; border: 1px solid #151515; padding: 4px 8px; }
        QSplitter::handle { background: #1a1a1a; }
        QSplitter::handle:hover { background: #d1a54a; }
    )"));
}

} // namespace bc::Theme
