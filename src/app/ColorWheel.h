#pragma once

#include <QWidget>

namespace bc {

// One colour wheel of the Colour panel (phase 9d): a disc showing the hues,
// with a point that is dragged towards the colour to add. x and y are in
// -100 .. 100 (the edge of the disc); double-click puts the point back in
// the centre. Arrow keys move it by 1 (Shift: 10).
class ColorWheel : public QWidget
{
    Q_OBJECT
public:
    explicit ColorWheel(QWidget *parent = nullptr);

    double x() const { return m_x; }
    double y() const { return m_y; }
    void setPoint(double x, double y); // no signal

    // Where a point lies in widget coordinates (for tests).
    QPointF pointPos(double x, double y) const;

    QSize sizeHint() const override { return {110, 110}; }
    QSize minimumSizeHint() const override { return {64, 64}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return w; }

signals:
    void moved(double x, double y); // by the user

protected:
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    double radius() const;
    QPointF centre() const;
    void moveTo(const QPointF &pos);
    void setByUser(double x, double y);

    double m_x = 0;
    double m_y = 0;
    QImage m_disc; // cached hue disc for the current size
};

} // namespace bc
