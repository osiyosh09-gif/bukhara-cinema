// Phase 9 (part 3) acceptance tests: video scopes (statistics, drawing,
// Scopes window following the Program monitor).
#include "app/MainWindow.h"
#include "app/ProjectController.h"
#include "app/ScopesPanel.h"
#include "core/MediaCache.h"
#include "core/Scopes.h"

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using namespace bc;
using SK = Scopes::Kind;

namespace {

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

QImage halves(QColor left, QColor right, int w = 200, int h = 100)
{
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(left);
    for (int y = 0; y < h; ++y)
        for (int x = w / 2; x < w; ++x)
            img.setPixelColor(x, y, right);
    return img;
}

// Brightest value (max of r, g, b) in a region.
int brightest(const QImage &img, const QRect &r)
{
    int best = 0;
    for (int y = r.top(); y <= r.bottom(); ++y)
        for (int x = r.left(); x <= r.right(); ++x) {
            const QColor c = img.pixelColor(x, y);
            best = std::max({best, c.red(), c.green(), c.blue()});
        }
    return best;
}

} // namespace

class TestPhase9c : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("BukharaCinemaTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_phase9c"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        QSettings().setValue(QStringLiteral("proxy/auto"), false);
        QDir(MediaCache::root()).removeRecursively();
        QVERIFY(m_dir.isValid());
    }

    void statistics()
    {
        QCOMPARE(Scopes::luma(255, 255, 255), 255);
        QCOMPARE(Scopes::luma(0, 255, 0), 182);
        double cb, cr;
        Scopes::chroma(128, 128, 128, &cb, &cr);
        QVERIFY(std::abs(cb) < 1e-9 && std::abs(cr) < 1e-9);
        Scopes::chroma(255, 0, 0, &cb, &cr);
        QVERIFY(cr > 120 && cb < 0);

        const Scopes::Stats s = Scopes::statistics(halves(Qt::black, Qt::white, 10, 4));
        QCOMPARE(s.pixels, 40);
        QCOMPARE(s.luma[0], 20);
        QCOMPARE(s.luma[255], 20);
        QCOMPARE(s.red[255], 20);
        QCOMPARE(Scopes::statistics(QImage()).pixels, 0);
    }

    void waveformAndParade()
    {
        // Black on the left, white on the right.
        const QImage wf = Scopes::render(halves(Qt::black, Qt::white), SK::Waveform, QSize(200, 101));
        QVERIFY(brightest(wf, QRect(10, 98, 80, 3)) > 150);  // left half at 0 %
        QVERIFY(brightest(wf, QRect(110, 0, 80, 3)) > 150);  // right half at 100 %
        QVERIFY(brightest(wf, QRect(110, 98, 80, 3)) < 100); // nothing black on the right
        QVERIFY(brightest(wf, QRect(10, 40, 80, 8)) < 100);  // nothing in the middle (only the grid)

        // Pure red: red channel at the top, green and blue at the bottom.
        const QImage pa = Scopes::render(halves(Qt::red, Qt::red), SK::Parade, QSize(300, 101));
        const QColor redTop = pa.pixelColor(50, 0);
        QVERIFY2(redTop.red() > 200 && redTop.red() > redTop.green() + 80, qPrintable(redTop.name()));
        QVERIFY(pa.pixelColor(150, 100).green() > 150);
        QVERIFY(pa.pixelColor(150, 0).green() < 100);
        QVERIFY(pa.pixelColor(250, 100).blue() > 150);
    }

    void vectorscope()
    {
        const QSize size(201, 201);
        const QImage grey = Scopes::render(halves(Qt::gray, Qt::gray), SK::Vectorscope, size);
        QVERIFY(brightest(grey, QRect(98, 98, 5, 5)) > 150); // all in the centre
        const QImage red = Scopes::render(halves(Qt::red, Qt::red), SK::Vectorscope, size);
        double cb, cr;
        Scopes::chroma(255, 0, 0, &cb, &cr);
        const double radius = 100.5 - 4;
        const QPoint at(int(std::lround(100.5 + cb / 128 * radius)), int(std::lround(100.5 - cr / 128 * radius)));
        QVERIFY2(brightest(red, QRect(at - QPoint(2, 2), QSize(5, 5))) > 150, qPrintable(QStringLiteral("%1,%2").arg(at.x()).arg(at.y())));
        QVERIFY(at.y() < 60 && at.x() > 70); // red sits up and a bit right of centre-left
        QVERIFY(brightest(red, QRect(98, 98, 5, 5)) < 150);
    }

    void histogram()
    {
        const QImage h = Scopes::render(halves(QColor(64, 64, 64), QColor(192, 192, 192)), SK::Histogram, QSize(256, 100));
        QVERIFY(brightest(h, QRect(64, 50, 1, 50)) > 150);
        QVERIFY(brightest(h, QRect(192, 50, 1, 50)) > 150);
        QVERIFY(brightest(h, QRect(128, 90, 1, 10)) < 100);
        QCOMPARE(Scopes::render(QImage(), SK::Histogram, QSize(64, 64)).pixelColor(10, 10), QColor(18, 18, 18));
    }

    void window()
    {
        MainWindow w;
        w.resize(1366, 768);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        auto *act = w.findChild<QAction *>(QStringLiteral("actScopes"));
        auto *dock = w.findChild<QDockWidget *>(QStringLiteral("scopesDock"));
        auto *panel = w.findChild<ScopesPanel *>(QStringLiteral("scopesPanel"));
        QVERIFY(act && dock && panel);
        QVERIFY(!dock->isVisible() && !act->isChecked());
        // While hidden nothing is drawn.
        panel->setFrame(halves(Qt::red, Qt::blue));
        QTest::qWait(150);
        QVERIFY(panel->scopeImage().isNull());

        act->trigger();
        QVERIFY(dock->isVisible());
        QVERIFY(!panel->scopeImage().isNull());
        QCOMPARE(panel->kind(), SK::Waveform);

        // It follows the Program monitor.
        ProjectController *ctl = w.controller();
        const QString path = m_dir.filePath(QStringLiteral("white.png"));
        QImage white(160, 90, QImage::Format_RGB32);
        white.fill(Qt::white);
        QVERIFY(white.save(path));
        ctl->importFiles({path});
        waitIdle(*ctl);
        if (!ctl->canPlay())
            QSKIP("video engine not available");
        ctl->placeMedia({ctl->project().items.first().id}, 0, 0);
        ctl->setPlayhead(5);
        QTRY_VERIFY(brightest(panel->scopeImage(), QRect(40, 0, 300, 4)) > 150); // white = 100 %

        auto *kind = panel->findChild<QComboBox *>(QStringLiteral("scopeKind"));
        kind->setCurrentIndex(kind->findData(int(SK::Vectorscope)));
        QCOMPARE(panel->scopeImage().size(), QSize(256, 256));
        QCOMPARE(QSettings().value(QStringLiteral("scopes/kind")).toInt(), int(SK::Vectorscope));
    }
};

QTEST_MAIN(TestPhase9c)
#include "tst_phase9c.moc"
