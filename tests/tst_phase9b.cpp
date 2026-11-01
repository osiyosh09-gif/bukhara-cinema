// Phase 9 (part 2) acceptance tests: .cube LUTs (reader, interpolation,
// colour pipeline, engine, collect, Colour panel).
#include "app/ColorPanel.h"
#include "app/MainWindow.h"
#include "app/ProjectController.h"
#include "app/TimelinePanel.h"
#include "core/Collector.h"
#include "core/Color.h"
#include "core/Lut.h"
#include "core/MediaCache.h"
#include "engine/TimelinePlayer.h"

#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QLabel>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QUndoStack>
#include <QtTest>

#include <functional>

using namespace bc;

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

// A 3D .cube text of size n for f(r, g, b) -> (r', g', b').
QByteArray cube(int n, const std::function<void(double, double, double, double *)> &f, const QByteArray &extra = {})
{
    QByteArray t = "# test\nTITLE \"Test look\"\n" + extra + "LUT_3D_SIZE " + QByteArray::number(n) + "\n";
    for (int b = 0; b < n; ++b)
        for (int g = 0; g < n; ++g)
            for (int r = 0; r < n; ++r) {
                double o[3];
                f(r / double(n - 1), g / double(n - 1), b / double(n - 1), o);
                t += QByteArray::number(o[0], 'f', 6) + ' ' + QByteArray::number(o[1], 'f', 6) + ' '
                     + QByteArray::number(o[2], 'f', 6) + '\n';
            }
    return t;
}

bool write(const QString &path, const QByteArray &data)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

} // namespace

class TestPhase9b : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_grey, m_redLut, m_swapLut;

    QUuid import(ProjectController &ctl, const QString &path)
    {
        ctl.importFiles({path});
        waitIdle(ctl);
        for (const MediaItem &m : ctl.project().items)
            if (m.path == path)
                return m.id;
        return {};
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("BukharaCinemaTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_phase9b"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        QSettings().setValue(QStringLiteral("proxy/auto"), false);
        QDir(MediaCache::root()).removeRecursively();
        QVERIFY(m_dir.isValid());
        m_grey = m_dir.filePath(QStringLiteral("grey.png"));
        QImage img(160, 90, QImage::Format_RGB32);
        img.fill(QColor(120, 120, 120));
        QVERIFY(img.save(m_grey));
        // Everything becomes pure red.
        m_redLut = m_dir.filePath(QStringLiteral("red look.cube"));
        QVERIFY(write(m_redLut, cube(2, [](double, double, double, double *o) { o[0] = 1; o[1] = 0; o[2] = 0; })));
        // Channels swapped: (r, g, b) -> (g, b, r).
        m_swapLut = m_dir.filePath(QStringLiteral("swap.cube"));
        QVERIFY(write(m_swapLut, cube(17, [](double r, double g, double b, double *o) { o[0] = g; o[1] = b; o[2] = r; })));
    }

    void reader()
    {
        QString error;
        auto lut = Lut::load(m_swapLut, &error);
        QVERIFY2(lut, qPrintable(error));
        QVERIFY(lut->is3D());
        QCOMPARE(lut->size(), 17);
        QCOMPARE(lut->title(), QStringLiteral("Test look"));
        // Linear functions come out exactly, also between grid points.
        float r = 0.13f, g = 0.71f, b = 0.42f;
        lut->apply(r, g, b);
        QVERIFY(std::abs(r - 0.71f) < 1e-4 && std::abs(g - 0.42f) < 1e-4 && std::abs(b - 0.13f) < 1e-4);
        // Out of range input is clamped.
        r = 1.5f, g = -1.f, b = 0.5f;
        lut->apply(r, g, b);
        QVERIFY(std::abs(r) < 1e-4 && std::abs(g - 0.5f) < 1e-4 && std::abs(b - 1) < 1e-4);

        // A smooth non-linear look is close between grid points.
        auto gamma = Lut::parse(cube(33, [](double r, double g, double b, double *o) {
            o[0] = std::pow(r, 2.2); o[1] = std::pow(g, 2.2); o[2] = std::pow(b, 2.2); }));
        QVERIFY(gamma);
        r = g = b = 0.5f;
        gamma->apply(r, g, b);
        QVERIFY(std::abs(r - std::pow(0.5, 2.2)) < 0.005);

        // 1D LUT and a domain.
        auto one = Lut::parse("LUT_1D_SIZE 3\nDOMAIN_MIN 0 0 0\nDOMAIN_MAX 2 2 2\n0 0 0\n0.5 0.5 0.5\n1 0 1\n");
        QVERIFY(one && !one->is3D());
        r = 1.0f, g = 1.0f, b = 2.0f; // middle of the domain, middle, top
        one->apply(r, g, b);
        QVERIFY(std::abs(r - 0.5f) < 1e-5 && std::abs(g - 0.5f) < 1e-5 && std::abs(b - 1) < 1e-5);

        // Broken files are refused with a reason.
        QVERIFY(!Lut::parse("0 0 0\n", &error));
        QVERIFY(!Lut::parse("LUT_3D_SIZE 2\n0 0 0\n1 1 1\n", &error));
        QVERIFY(error.contains(QLatin1String("expected 8")));
        QVERIFY(!Lut::parse("LUT_3D_SIZE 1\n", &error));
        QVERIFY(!Lut::parse("LUT_1D_SIZE 2\n0 0 0\n1 x 1\n", &error));
        QVERIFY(!Lut::load(m_dir.filePath(QStringLiteral("missing.cube")), &error));
    }

    void pipeline()
    {
        ClipColor c;
        c.lut = m_swapLut;
        QVERIFY(!c.isNeutral());
        uint8_t px[4] = {200, 100, 50, 255};
        applyColor(px, 1, c);
        QCOMPARE(int(px[0]), 100);
        QCOMPARE(int(px[1]), 50);
        QCOMPARE(int(px[2]), 200);
        // Strength mixes with the original.
        c.setValue(QStringLiteral("lut_amount"), 50);
        uint8_t half[4] = {200, 100, 50, 255};
        applyColor(half, 1, c);
        QCOMPARE(int(half[0]), 150);
        QCOMPARE(int(half[2]), 125);
        // 0% = nothing, and that counts as neutral.
        c.setValue(QStringLiteral("lut_amount"), 0);
        QVERIFY(c.isNeutral());
        // The LUT comes after the basic correction: exposure -1 then swap.
        ClipColor both;
        both.lut = m_swapLut;
        both.setValue(QStringLiteral("exposure"), -1);
        uint8_t px2[4] = {200, 100, 50, 255};
        applyColor(px2, 1, both);
        QCOMPARE(int(px2[2]), 100);
        // A missing file leaves the picture alone.
        ClipColor gone;
        gone.lut = m_dir.filePath(QStringLiteral("gone.cube"));
        uint8_t px3[4] = {200, 100, 50, 255};
        applyColor(px3, 1, gone);
        QCOMPARE(int(px3[0]), 200);
        // Saved and read back.
        both.setValue(QStringLiteral("lut_amount"), 70);
        QJsonObject o = both.toJson();
        QCOMPARE(o.value(QStringLiteral("lut")).toString(), m_swapLut);
        QCOMPARE(ClipColor::fromJson(o), both);
        QCOMPARE(ClipColor::fromEngineString(both.toEngineString()), both);
        QVERIFY(both != ClipColor::fromJson(QJsonObject{{QStringLiteral("exposure"), -1}}));
    }

    void engine()
    {
        ProjectController ctl;
        if (!ctl.canPlay())
            QSKIP("video engine not available");
        const QUuid m = import(ctl, m_grey);
        Project p = ctl.project();
        p.timeline = Timeline::makeDefault();
        TimelineClip c;
        c.id = QUuid::createUuid();
        c.mediaId = m;
        c.length = 50;
        c.color.lut = m_redLut;
        p.timeline.tracks[0].clips << c;
        TimelinePlayer player;
        player.setPreviewLimit(QSize(160, 90));
        player.setProject(p);
        const QImage img = player.renderFrame(10);
        const QColor px = img.pixelColor(img.width() / 2, img.height() / 2);
        QVERIFY2(px.red() > 220 && px.green() < 30 && px.blue() < 30, qPrintable(px.name()));
    }

    void collect()
    {
        Project p;
        MediaItem item;
        item.id = QUuid::createUuid();
        item.path = m_grey;
        item.name = QStringLiteral("grey.png");
        p.items << item;
        TimelineClip c;
        c.id = QUuid::createUuid();
        c.mediaId = item.id;
        c.length = 25;
        c.color.lut = m_redLut;
        p.timeline.tracks[0].clips << c;
        TimelineClip d = c;
        d.id = QUuid::createUuid();
        d.start = 30;
        p.timeline.tracks[0].clips << d; // same LUT twice: one copy
        TimelineClip e = c;
        e.id = QUuid::createUuid();
        e.start = 60;
        e.color.lut = m_dir.filePath(QStringLiteral("lost.cube"));
        p.timeline.tracks[0].clips << e;

        QTemporaryDir out;
        const Collector::Plan plan = Collector::plan(p, QStringLiteral("Packed"), out.path(), false);
        QCOMPARE(plan.copies.size(), 2); // picture + one LUT
        const QString lut = plan.project.timeline.tracks[0].clips[0].color.lut;
        QVERIFY(lut.startsWith(plan.folder + QStringLiteral("/Media/")));
        QCOMPARE(plan.project.timeline.tracks[0].clips[1].color.lut, lut);
        QCOMPARE(plan.missing.size(), 1);
        QVERIFY(Collector::run(plan));
        QVERIFY(Lut::load(lut));
    }

    void panel()
    {
        MainWindow w;
        w.resize(1366, 768);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        ProjectController *ctl = w.controller();
        const QUuid m = import(*ctl, m_grey);
        ctl->placeMedia({m}, 0, 0);
        const QUuid id = ctl->timeline().tracks[0].clips[0].id;
        auto *panel = w.findChild<ColorPanel *>(QStringLiteral("colorPanel"));
        w.findChild<QTabWidget *>(QStringLiteral("rightTabs"))->setCurrentWidget(panel);
        w.findChild<TimelineView *>(QStringLiteral("timelineView"))->setSelection({id});
        auto *name = panel->findChild<QLabel *>(QStringLiteral("colorLutName"));
        auto *remove = panel->findChild<QToolButton *>(QStringLiteral("colorLutRemove"));
        auto *amount = panel->findChild<QDoubleSpinBox *>(QStringLiteral("color_lut_amount"));
        QVERIFY(name && remove && amount && panel->findChild<QToolButton *>(QStringLiteral("colorLutChoose")));
        QVERIFY(!remove->isEnabled() && !amount->isEnabled());

        QVERIFY(ctl->setClipLut(id, m_redLut));
        QCOMPARE(name->text(), QStringLiteral("red look.cube"));
        QVERIFY(remove->isEnabled() && amount->isEnabled());
        amount->setValue(40);
        QCOMPARE(ctl->clipColor(id).value(QStringLiteral("lut_amount")), 40.0);

        QVERIFY(ctl->setClipLut(id, m_dir.filePath(QStringLiteral("nowhere.cube"))));
        QVERIFY(name->text().startsWith(QStringLiteral("nowhere.cube (")));
        remove->click();
        QVERIFY(ctl->clipColor(id).lut.isEmpty());
        ctl->undoStack()->undo();
        QVERIFY(ctl->clipColor(id).lut.endsWith(QStringLiteral("nowhere.cube")));
        ctl->undoStack()->undo();
        QCOMPARE(ctl->clipColor(id).lut, m_redLut);
        // Reset colour also removes the LUT.
        panel->findChild<QToolButton *>(QStringLiteral("colorReset"))->click();
        QCOMPARE(ctl->clipColor(id), ClipColor());
    }
};

QTEST_MAIN(TestPhase9b)
#include "tst_phase9b.moc"
