#pragma once

#include <QWidget>
#include <functional>

class QLabel;

namespace bc {

// A panel area whose real content arrives in a later phase.
// It says so plainly instead of pretending to work.
class PlaceholderPanel : public QWidget
{
    Q_OBJECT
public:
    using TextFn = std::function<QString()>;

    PlaceholderPanel(TextFn title, int phase, QWidget *parent = nullptr);

    QString titleText() const;

protected:
    void changeEvent(QEvent *event) override;

private:
    void retranslate();

    TextFn m_title;
    int m_phase;
    QLabel *m_titleLabel;
    QLabel *m_noteLabel;
};

// Static picture of an empty multitrack timeline (rulers and track headers).
// It does not edit anything yet: editing arrives in phase 2.
class TimelinePlaceholder : public QWidget
{
    Q_OBJECT
public:
    explicit TimelinePlaceholder(QWidget *parent = nullptr);
    QSize minimumSizeHint() const override { return {300, 140}; }

protected:
    void paintEvent(QPaintEvent *event) override;
};

} // namespace bc
