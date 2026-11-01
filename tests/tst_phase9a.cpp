// Phase 9 (part 1) acceptance tests: basic colour correction (model, file,
// pixel work, engine, controller, Colour panel).
#include "app/ColorPanel.h"
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

QColor centre(const QImage &img)
{
    return img.pixelColor(img.width() / 2, img.height() / 2);
}

QColor apply(QColor in, const QString &key, double v, const QString &key2 = {}, double v2 = 0)
{
    ClipColor c;
    c.setValue(key, v);
    if (!key2.isEmpty())
        c.setValue(key2, v2);
    uint8_t px[4] = {uint8_t(in.red()), uint8_t(in.green()), uint8_t(in.blue()), 255};
    applyColor(px, 1, c);
    return QColor(px[0], px[1], px[2]);
}

} // namespace

class TestPhase9a : public QObject
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
        QCoreApplication::setApplicationName(QStringLiteral("tst_phase9a"));
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
        ClipColor c;
        QVERIFY(c.isNeutral());
        QCOMPARE(c.value(QStringLiteral("saturation")), 100.0);
        QVERIFY(c.setValue(QStringLiteral("exposure"), 9));
        QCOMPARE(c.value(QStringLiteral("exposure")), 4.0);
        QVERIFY(!c.setValue(QStringLiteral("exposure"), 4));
        QVERIFY(!c.setValue(QStringLiteral("nothing"), 1));
        QVERIFY(c.setValue(QStringLiteral("exposure"), 0)); // back to neutral
        QVERIFY(c.isNeutral());
        QVERIFY(c.toEngineString().isEmpty());
        c.setValue(QStringLiteral("temperature"), 30);
        c.setValue(QStringLiteral("saturation"), 150);
        QCOMPARE(ClipColor::fromEngineString(c.toEngineString()), c);

        Timeline t = Timeline::makeDefault();
        TimelineClip clip;
        clip.id = QUuid::createUuid();
        clip.mediaId = QUuid::createUuid();
        clip.length = 100;
        clip.color = c;
        t.tracks[0].clips << clip;
        const Timeline back = Timeline::fromJson(t.toJson());
        QCOMPARE(back.clip(clip.id)->color, c);
        const QList<QUuid> made = t.splitAt(50, {clip.id});
        QCOMPARE(t.clip(made.first())->color, c);
    }

    void pixels()
    {
        const QColor grey(128, 128, 128);
        QCOMPARE(apply(grey, QStringLiteral("contrast"), 0), grey);
        // Exposure: +1 stop doubles.
        QVERIFY(std::abs(apply(QColor(60, 60, 60), QStringLiteral("exposure"), 1).red() - 120) <= 1);
        QVERIFY(apply(grey, QStringLiteral("exposure"), -1).red() < 70);
        // Contrast: dark gets darker, light gets lighter, mid grey stays.
        QVERIFY(apply(QColor(60, 60, 60), QStringLiteral("contrast"), 100).red() < 40);
        QVERIFY(apply(QColor(200, 200, 200), QStringLiteral("contrast"), 100).red() > 240);
        QVERIFY(std::abs(apply(grey, QStringLiteral("contrast"), 100).red() - 128) <= 1);
        // Temperature: warmer = more red than blue; tint +: less green.
        const QColor warm = apply(grey, QStringLiteral("temperature"), 100);
        QVERIFY(warm.red() > 140 && warm.blue() < 116);
        const QColor cool = apply(grey, QStringLiteral("temperature"), -100);
        QVERIFY(cool.blue() > cool.red());
        QVERIFY(apply(grey, QStringLiteral("tint"), 100).green() < 110);
        // Shadows lift dark tones more than light ones; highlights pull light tones.
        const int dark = apply(QColor(70, 70, 70), QStringLiteral("shadows"), 100).red() - 70;
        const int light = apply(QColor(200, 200, 200), QStringLiteral("shadows"), 100).red() - 200;
        QVERIFY2(dark > 30 && light < dark, qPrintable(QStringLiteral("%1 %2").arg(dark).arg(light)));
        QVERIFY(apply(QColor(180, 180, 180), QStringLiteral("highlights"), -100).red() < 150);
        QCOMPARE(apply(QColor(0, 0, 0), QStringLiteral("shadows"), 100).red(), 0); // black stays black
        // Saturation 0 = grey; greys never get colour.
        const QColor bw = apply(QColor(200, 50, 50), QStringLiteral("saturation"), 0);
        QVERIFY(bw.red() == bw.green() && bw.green() == bw.blue());
        QCOMPARE(apply(grey, QStringLiteral("saturation"), 200), grey);
        // Vibrance boosts a dull colour more than a strong one (relative to its own chroma).
        const QColor dull = apply(QColor(140, 120, 120), QStringLiteral("vibrance"), 100);
        const QColor strong = apply(QColor(240, 20, 20), QStringLiteral("vibrance"), 100);
        QVERIFY((dull.red() - dull.green()) / 20.0 > (strong.red() - strong.green()) / 220.0);
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
            return centre(player.renderFrame(10));
        };
        const QColor plain = render();
        QVERIFY(std::abs(plain.red() - plain.blue()) < 6);
        p.timeline.tracks[0].clips[0].color.setValue(QStringLiteral("temperature"), 100);
        const QColor warm = render();
        QVERIFY2(warm.red() > warm.blue() + 30, qPrintable(warm.name()));
        // Colour comes before the effects: saturation 0 after a warm balance is grey again.
        p.timeline.tracks[0].clips[0].effects << ClipEffect::make(ClipEffect::Kind::Saturation);
        p.timeline.tracks[0].clips[0].effects[0].setValue(QStringLiteral("saturation"), 0);
        const QColor grey = render();
        QVERIFY2(std::abs(grey.red() - grey.blue()) < 6, qPrintable(grey.name()));
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
        QVERIFY(w.findChild<QWidget *>(QStringLiteral("colorEmpty"))->isVisibleTo(panel));
        w.findChild<TimelineView *>(QStringLiteral("timelineView"))->setSelection({id});
        QCOMPARE(panel->clip(), id);
        auto *reset = panel->findChild<QToolButton *>(QStringLiteral("colorReset"));
        QVERIFY(!reset->isEnabled());

        const int steps = ctl->undoStack()->count();
        auto *slider = panel->findChild<QSlider *>(QStringLiteral("colorSlider_exposure"));
        auto *spin = panel->findChild<QDoubleSpinBox *>(QStringLiteral("color_exposure"));
        for (int v = 1; v <= 5; ++v)
            slider->setValue(v); // a drag: 0.1 .. 0.5 stops
        QCOMPARE(ctl->clipColor(id).value(QStringLiteral("exposure")), 0.5);
        QCOMPARE(spin->value(), 0.5);
        QCOMPARE(ctl->undoStack()->count(), steps + 1);
        panel->findChild<QDoubleSpinBox *>(QStringLiteral("color_saturation"))->setValue(40);
        QCOMPARE(ctl->clipColor(id).value(QStringLiteral("saturation")), 40.0);
        QVERIFY(reset->isEnabled());
        reset->click();
        QVERIFY(ctl->clipColor(id).isNeutral());
        QCOMPARE(slider->value(), 0);
        ctl->undoStack()->undo();
        QCOMPARE(ctl->clipColor(id).value(QStringLiteral("saturation")), 40.0);
        QCOMPARE(ctl->clipColor(id).value(QStringLiteral("exposure")), 0.5);
    }
};

QTEST_MAIN(TestPhase9a)
#include "tst_phase9a.moc"
