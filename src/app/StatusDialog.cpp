#include "app/StatusDialog.h"

#include "app/FeatureStatus.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace bc {

StatusDialog::StatusDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("statusDialog"));
    setWindowTitle(tr("Feature status"));

    auto *tree = new QTreeWidget(this);
    tree->setObjectName(QStringLiteral("statusTree"));
    tree->setRootIsDecorated(false);
    tree->setAlternatingRowColors(true);
    tree->setHeaderLabels({tr("Status"), tr("Phase"), tr("Feature"), tr("Note")});
    for (const FeatureEntry &e : FeatureStatus::entries()) {
        auto *item = new QTreeWidgetItem(tree);
        item->setText(0, FeatureStatus::stateName(e.state));
        item->setText(1, QString::number(e.phase));
        item->setText(2, e.title);
        item->setText(3, e.note);
        QColor c = e.state == FeatureEntry::State::Ready     ? QColor(0x7c, 0xc4, 0x7c)
                   : e.state == FeatureEntry::State::Partial ? QColor(0xd1, 0xa5, 0x4a)
                                                             : QColor(0x8a, 0x8a, 0x8a);
        item->setForeground(0, c);
    }
    tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(2, QHeaderView::Interactive);
    tree->setColumnWidth(2, 380);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Close"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *lay = new QVBoxLayout(this);
    lay->addWidget(tree);
    lay->addWidget(buttons);
    resize(900, 520);
}

} // namespace bc
