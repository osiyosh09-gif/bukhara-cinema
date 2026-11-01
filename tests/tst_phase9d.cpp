// Phase 9 (part 4) acceptance tests: colour wheels (lift / gamma / gain) in
// the model, the pixel work, the engine, the controller and the Colour panel.
#include "app/ColorPanel.h"
#include "app/ColorWheel.h"
#include "app/MainWindow.h"
#include "app/ProjectController.h"
#include "app/TimelinePanel.h"
#include "core/Color.h"
#include "core/MediaCache.h"
#include "engine/TimelinePlayer.h"

#include <QDir>
#include <QDoubleSpinBox>
#include <QSettings>
#include <QSlider>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QUndoStack>
#include <QtTest>

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

QColor apply(QColor in, const ClipColor &c)
{
    uint8_t px[4] = {uint8_t(in.red()), uint8_t(in.green()), uint8_t(in.blue()), 255};
    applyColor(px, 1, c);
    return QColor(px[0], px[1], px[2]);
}

ClipColor with(const QString &key, double v, const QString &key2 = {}, double v2 = 0)
{
    ClipColor c;
    c.setValue(key, v);
    if (!key2.isEmpty())
        c.setValue(key2, v2);
    return c;
}

double luma(const QColor &c)
{
    return 0.2126 * c.red() + 0.7152 * c.green() + 0.0722 * c.blue();
}

} // namespace

class TestPhase9d : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_grey;

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
        QCoreApplication::setApplicationName(QStringLiteral("tst_phase9d"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        QSettings().setValue(QStringLiteral("proxy/auto"), false);
        QDir(MediaCache::root()).removeRecursively();
        QVERIFY(m_dir.isValid());
        m_grey = m_dir.filePath(QStringLiteral("grey.png"));
        QImage img(160, 90, QImage::Format_RGB32);
        img.fill(QColor(110, 110, 110));
        QVERIFY(img.save(m_grey));
    }

    void model()
    {
        QCOMPARE(ClipColor::wheels(), (QStringList{"lift", "gamma", "gain"}));
        QVERIFY(ClipColor::isWheelKey(QStringLiteral("gamma_x")));
        QVERIFY(!ClipColor::isWheelKey(QStringLiteral("exposure")));
        ClipColor c;
        QVERIFY(c.setValue(QStringLiteral("gain_x"), 250));
        QCOMPARE(c.value(QStringLiteral("gain_x")), 100.0);
        QVERIFY(!c.isNeutral());
        c.setValue(QStringLiteral("lift_l"), -20);
        QCOMPARE(ClipColor::fromJson(c.toJson()), c);
        QCOMPARE(ClipColor::fromEngineString(c.toEngineString()), c);
        QVERIFY(c.setValue(QStringLiteral("gain_x"), 0));
        QVERIFY(c.setValue(QStringLiteral("lift_l"), 0));
        QVERIFY(c.isNeutral());
        // Older projects (format 9) have no wheel keys: neutral wheels.
        QVERIFY(ClipColor::fromJson(QJsonObject{{"exposure", 1}}).value(QStringLiteral("gamma_y")) == 0);
    }

    void balance()
    {
        double rgb[3];
        wheelBalance(0, 0, rgb);
        QCOMPARE(rgb[0], 0.0);
        // Each direction keeps the brightness (no change of luma) and reaches 1 at the edge.
        for (int deg = 0; deg < 360; deg += 15) {
            const double a = deg * M_PI / 180;
            wheelBalance(100 * std::cos(a), 100 * std::sin(a), rgb);
            QVERIFY(std::abs(0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2]) < 1e-9);
            QVERIFY(std::abs(std::max({std::abs(rgb[0]), std::abs(rgb[1]), std::abs(rgb[2])}) - 1) < 1e-9);
        }
        wheelBalance(0, 100, rgb); // up = towards red (Cr), away from cyan
        QVERIFY(rgb[0] > 0.9 && rgb[1] < 0);
        wheelBalance(100, 0, rgb); // right = towards blue (Cb), away from yellow
        QVERIFY(rgb[2] > 0.9 && rgb[1] < 0);
        wheelBalance(0, 50, rgb); // half way = half as much
        QVERIFY(std::abs(rgb[0] - 0.5) < 1e-9);
    }

    void pixels()
    {
        const QColor black(0, 0, 0), white(255, 255, 255), mid(110, 110, 110);
        // Masters: lift raises black and keeps white; gain dims white and keeps black; gamma moves mid grey.
        const ClipColor lift = with(QStringLiteral("lift_l"), 50);
        QVERIFY(apply(black, lift).red() > 40);
        QCOMPARE(apply(white, lift), white);
        const ClipColor gain = with(QStringLiteral("gain_l"), -50);
        QCOMPARE(apply(black, gain), black);
        QVERIFY(apply(white, gain).red() < 200);
        const ClipColor gamma = with(QStringLiteral("gamma_l"), 50);
        QCOMPARE(apply(black, gamma), black);
        QCOMPARE(apply(white, gamma), white);
        QVERIFY(apply(mid, gamma).red() > 135);
        QVERIFY(apply(mid, with(QStringLiteral("gamma_l"), -50)).red() < 90);

        // Lift towards red tints dark tones more than bright ones.
        const ClipColor warmLift = with(QStringLiteral("lift_y"), 100);
        const QColor d = apply(QColor(30, 30, 30), warmLift), b = apply(QColor(220, 220, 220), warmLift);
        QVERIFY2(d.red() - d.blue() > b.red() - b.blue(), qPrintable(d.name() + b.name()));
        QVERIFY(d.red() > d.blue() + 15);
        // Gain towards blue tints bright tones more than dark ones.
        const ClipColor blueGain = with(QStringLiteral("gain_x"), 100);
        const QColor bd = apply(QColor(30, 30, 30), blueGain), bb = apply(QColor(200, 200, 200), blueGain);
        QVERIFY(bb.blue() - bb.red() > bd.blue() - bd.red());
        QVERIFY(bb.blue() > bb.red() + 40);
        // A gamma tint keeps the brightness of mid grey close to the same.
        const QColor gt = apply(mid, with(QStringLiteral("gamma_x"), -60, QStringLiteral("gamma_y"), 60));
        QVERIFY(gt.red() > gt.blue() + 15);
        QVERIFY2(std::abs(luma(gt) - 110) < 12, qPrintable(gt.name()));
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
        p.timeline.tracks[0].clips << c;
        auto render = [&] {
            TimelinePlayer player;
            player.setPreviewLimit(QSize(160, 90));
            player.setProject(p);
            const QImage img = player.renderFrame(10);
            return img.pixelColor(img.width() / 2, img.height() / 2);
        };
        const QColor plain = render();
        p.timeline.tracks[0].clips[0].color.setValue(QStringLiteral("gamma_y"), 80);
        const QColor warm = render();
        QVERIFY2(warm.red() > warm.green() + 20, qPrintable(warm.name()));
        QVERIFY(std::abs(plain.red() - plain.blue()) < 6);
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
        QVERIFY(panel);
        w.findChild<QTabWidget *>(QStringLiteral("rightTabs"))->setCurrentWidget(panel);
        w.findChild<TimelineView *>(QStringLiteral("timelineView"))->setSelection({id});
        QCOMPARE(panel->clip(), id);
        // The wheel points have no slider rows of their own.
        QVERIFY(!panel->findChild<QSlider *>(QStringLiteral("colorSlider_lift_x")));
        auto *wheel = panel->findChild<ColorWheel *>(QStringLiteral("colorWheel_gain"));
        QVERIFY(wheel);
        QTRY_VERIFY(wheel->isVisible() && wheel->width() >= 60);
        auto *reset = panel->findChild<QToolButton *>(QStringLiteral("colorWheelsReset"));
        QVERIFY(!reset->isEnabled());

        // A drag towards the top (red) is one undo step.
        const int steps = ctl->undoStack()->count();
        QTest::mousePress(wheel, Qt::LeftButton, {}, wheel->pointPos(0, 0).toPoint());
        for (int y = 10; y <= 50; y += 10) {
            QMouseEvent move(QEvent::MouseMove, wheel->pointPos(0, y), wheel->mapToGlobal(wheel->pointPos(0, y)),
                             Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(wheel, &move);
        }
        QTest::mouseRelease(wheel, Qt::LeftButton, {}, wheel->pointPos(0, 50).toPoint());
        const double y = ctl->clipColor(id).value(QStringLiteral("gain_y"));
        QVERIFY2(y > 40 && y < 60, qPrintable(QString::number(y)));
        QVERIFY(std::abs(ctl->clipColor(id).value(QStringLiteral("gain_x"))) < 8);
        QCOMPARE(ctl->undoStack()->count(), steps + 1);
        QVERIFY(reset->isEnabled());

        // Keys move the point; the master slider changes the master.
        wheel->setFocus();
        QTest::keyClick(wheel, Qt::Key_Right, Qt::ShiftModifier);
        QCOMPARE(ctl->clipColor(id).value(QStringLiteral("gain_x")), wheel->x());
        panel->findChild<QDoubleSpinBox *>(QStringLiteral("color_lift_l"))->setValue(-30);
        QCOMPARE(ctl->clipColor(id).value(QStringLiteral("lift_l")), -30.0);
        // Undo puts the point of the wheel back too (the keys merged with the drag).
        ctl->undoStack()->undo();
        ctl->undoStack()->undo();
        QCOMPARE(wheel->y(), 0.0);
        QCOMPARE(ctl->clipColor(id).value(QStringLiteral("lift_l")), 0.0);
        ctl->undoStack()->redo();
        QCOMPARE(wheel->y(), y);
        // Double-click: back to the centre.
        QTest::mouseDClick(wheel, Qt::LeftButton, {}, wheel->pointPos(0, 50).toPoint());
        QCOMPARE(ctl->clipColor(id).value(QStringLiteral("gain_y")), 0.0);

        ctl->setClipColorWheel(id, QStringLiteral("lift"), -20, 30);
        ctl->setClipColorValue(id, QStringLiteral("exposure"), 1);
        QVERIFY(reset->isEnabled());
        reset->click(); // the wheels only: exposure stays
        for (const QString &k : {"lift_x", "lift_y", "gain_y"})
            QCOMPARE(ctl->clipColor(id).value(k), 0.0);
        QCOMPARE(ctl->clipColor(id).value(QStringLiteral("exposure")), 1.0);
        QVERIFY(!reset->isEnabled());
        QVERIFY(!ctl->setClipColorWheel(id, QStringLiteral("nothing"), 1, 1));
        ctl->setClipColorWheel(id, QStringLiteral("gamma"), 40, -40);
    }
};

QTEST_MAIN(TestPhase9d)
#include "tst_phase9d.moc"
