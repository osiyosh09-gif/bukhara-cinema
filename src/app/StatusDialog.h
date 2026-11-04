#pragma once

#include <QDialog>

namespace bc {

// Help > Feature status: Ready / Partial / Planned, per phase.
class StatusDialog : public QDialog
{
    Q_OBJECT
public:
    explicit StatusDialog(QWidget *parent = nullptr);
};

} // namespace bc
