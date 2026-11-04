#pragma once

#include <QDialog>

namespace bc {

class AboutDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Page { About, Licenses };
    explicit AboutDialog(Page page, QWidget *parent = nullptr);

    static QString licensesHtml();
};

} // namespace bc
