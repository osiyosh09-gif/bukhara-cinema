#include "app/MediaBinPanel.h"

#include "app/Formatting.h"
#include "app/ProjectController.h"
#include "core/MediaProbe.h"

#include <QDesktopServices>
#include <QDir>
#include <QShortcut>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QStyle>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace bc {

// ---------------------------------------------------------------------------
// BinTree

BinTree::BinTree(QWidget *parent)
    : QTreeWidget(parent)
{
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::MoveAction);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
}

QUuid BinTree::folderAt(const QPoint &pos) const
{
    QTreeWidgetItem *it = itemAt(pos);
    while (it && !it->data(0, MediaBinPanel::IsFolderRole).toBool())
        it = it->parent();
    return it ? it->data(0, MediaBinPanel::IdRole).toUuid() : QUuid();
}

void BinTree::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls() || e->source() == this)
        e->acceptProposedAction();
    else
        e->ignore();
}

void BinTree::dragMoveEvent(QDragMoveEvent *e)
{
    if (e->mimeData()->hasUrls() || e->source() == this)
        e->acceptProposedAction();
    else
        e->ignore();
}

void BinTree::dropEvent(QDropEvent *e)
{
    const QUuid target = folderAt(e->position().toPoint());
    if (e->source() == this) {
        QList<QUuid> items, folders;
        for (QTreeWidgetItem *it : selectedItems()) {
            const QUuid id = it->data(0, MediaBinPanel::IdRole).toUuid();
            (it->data(0, MediaBinPanel::IsFolderRole).toBool() ? folders : items) << id;
        }
        e->setDropAction(Qt::IgnoreAction); // the controller rebuilds the tree
        e->accept();
        emit itemsMoved(items, folders, target);
        return;
    }
    if (e->mimeData()->hasUrls()) {
        QStringList paths;
        for (const QUrl &u : e->mimeData()->urls())
            if (u.isLocalFile())
                paths << u.toLocalFile();
        e->acceptProposedAction();
        emit filesDropped(paths, target);
    }
}

// ---------------------------------------------------------------------------
// MediaBinPanel

MediaBinPanel::MediaBinPanel(ProjectController *controller, QWidget *parent)
    : QWidget(parent)
    , m_ctl(controller)
    , m_tree(new BinTree(this))
    , m_filter(new QLineEdit(this))
    , m_importBtn(new QToolButton(this))
    , m_folderBtn(new QToolButton(this))
    , m_summary(new QLabel(this))
{
    setObjectName(QStringLiteral("mediaBin"));
    m_tree->setObjectName(QStringLiteral("binTree"));
    m_tree->setColumnCount(ColCount);
    m_tree->setIconSize(QSize(64, 36));
    m_tree->setUniformRowHeights(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->setSortingEnabled(true);
    m_tree->sortByColumn(ColName, Qt::AscendingOrder);
    m_tree->setEditTriggers(QAbstractItemView::EditKeyPressed);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(ColName, QHeaderView::Interactive);
    m_tree->setColumnWidth(ColName, 220);

    m_importBtn->setObjectName(QStringLiteral("binImport"));
    m_importBtn->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    m_importBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_folderBtn->setObjectName(QStringLiteral("binNewFolder"));
    m_folderBtn->setIcon(style()->standardIcon(QStyle::SP_FileDialogNewFolder));
    m_folderBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_filter->setObjectName(QStringLiteral("binFilter"));
    m_filter->setClearButtonEnabled(true);
    m_summary->setStyleSheet(QStringLiteral("color:#8a8a8a; padding:2px 4px;"));

    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(0, 0, 0, 0);
    bar->addWidget(m_importBtn);
    bar->addWidget(m_folderBtn);
    bar->addWidget(m_filter, 1);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(4);
    lay->addLayout(bar);
    lay->addWidget(m_tree, 1);
    lay->addWidget(m_summary);

    connect(m_importBtn, &QToolButton::clicked, this, &MediaBinPanel::importDialog);
    connect(m_folderBtn, &QToolButton::clicked, this, &MediaBinPanel::newFolder);
    connect(m_filter, &QLineEdit::textChanged, this, &MediaBinPanel::applyFilter);
    connect(m_tree, &QWidget::customContextMenuRequested, this, &MediaBinPanel::showContextMenu);
    connect(m_tree, &QTreeWidget::itemChanged, this, &MediaBinPanel::onItemChanged);
    connect(m_tree, &BinTree::filesDropped, this, [this](const QStringList &paths, const QUuid &folder) {
        m_ctl->importFiles(paths, folder);
    });
    connect(m_tree, &BinTree::itemsMoved, this,
            [this](const QList<QUuid> &items, const QList<QUuid> &folders, const QUuid &target) {
                m_ctl->moveItems(items, target);
                for (const QUuid &f : folders)
                    m_ctl->moveFolder(f, target);
            });

    auto *del = new QShortcut(QKeySequence::Delete, m_tree);
    del->setContext(Qt::WidgetShortcut);
    connect(del, &QShortcut::activated, this, &MediaBinPanel::removeSelected);

    connect(m_ctl, &ProjectController::projectReset, this, &MediaBinPanel::rebuild);
    connect(m_ctl, &ProjectController::binChanged, this, &MediaBinPanel::rebuild);
    connect(m_ctl, &ProjectController::itemUpdated, this, &MediaBinPanel::updateItemRow);

    retranslate();
    rebuild();
}

QUuid MediaBinPanel::currentFolder() const
{
    QTreeWidgetItem *it = m_tree->currentItem();
    if (!it || !it->isSelected())
        return {};
    if (it->data(0, IsFolderRole).toBool())
        return it->data(0, IdRole).toUuid();
    if (const MediaItem *m = m_ctl->project().item(it->data(0, IdRole).toUuid()))
        return m->folderId;
    return {};
}

void MediaBinPanel::rebuild()
{
    m_rebuilding = true;
    // Remember expanded folders and selection across rebuilds.
    QSet<QUuid> expanded, selected;
    for (auto it = m_rows.cbegin(); it != m_rows.cend(); ++it) {
        if (it.value()->isExpanded())
            expanded.insert(it.key());
        if (it.value()->isSelected())
            selected.insert(it.key());
    }
    const bool first = m_rows.isEmpty();

    m_tree->setSortingEnabled(false);
    m_tree->clear();
    m_rows.clear();
    const Project &p = m_ctl->project();

    // Folders: create all, then attach to parents.
    for (const BinFolder &f : p.folders) {
        auto *row = new QTreeWidgetItem;
        row->setData(0, IdRole, f.id);
        row->setData(0, IsFolderRole, true);
        row->setText(ColName, f.name);
        row->setIcon(ColName, style()->standardIcon(QStyle::SP_DirIcon));
        row->setFlags(row->flags() | Qt::ItemIsEditable | Qt::ItemIsDropEnabled);
        m_rows.insert(f.id, row);
    }
    for (const BinFolder &f : p.folders) {
        QTreeWidgetItem *row = m_rows.value(f.id);
        if (QTreeWidgetItem *parent = m_rows.value(f.parentId))
            parent->addChild(row);
        else
            m_tree->addTopLevelItem(row);
    }
    for (const MediaItem &m : p.items) {
        auto *row = new QTreeWidgetItem;
        row->setData(0, IdRole, m.id);
        row->setData(0, IsFolderRole, false);
        row->setFlags((row->flags() | Qt::ItemIsEditable) & ~Qt::ItemIsDropEnabled);
        m_rows.insert(m.id, row);
        if (QTreeWidgetItem *parent = m_rows.value(m.folderId))
            parent->addChild(row);
        else
            m_tree->addTopLevelItem(row);
        fillItemRow(row, m.id);
    }
    for (auto it = m_rows.cbegin(); it != m_rows.cend(); ++it) {
        it.value()->setExpanded(first || expanded.contains(it.key()));
        it.value()->setSelected(selected.contains(it.key()));
    }
    m_tree->setSortingEnabled(true);
    m_rebuilding = false;
    applyFilter();
    retranslate(); // summary line
}

void MediaBinPanel::updateItemRow(const QUuid &id)
{
    if (QTreeWidgetItem *row = m_rows.value(id)) {
        m_rebuilding = true;
        fillItemRow(row, id);
        m_rebuilding = false;
    }
    retranslate();
}

void MediaBinPanel::fillItemRow(QTreeWidgetItem *row, const QUuid &id)
{
    const MediaItem *m = m_ctl->project().item(id);
    if (!m)
        return;
    const MediaInfo &i = m->info;
    const bool online = m_ctl->isOnline(id);

    row->setText(ColName, m->name);
    row->setToolTip(ColName, QDir::toNativeSeparators(m->path));
    row->setText(ColDuration, i.kind == MediaInfo::Kind::Image || !m->probed ? QString()
                                                                             : Fmt::timecode(i.durationUs, i.fps()));
    row->setText(ColResolution, Fmt::resolution(i.width, i.height));
    row->setText(ColFps, Fmt::frameRate(i.fpsNum, i.fpsDen));
    row->setText(ColVideo, i.videoCodec);
    row->setText(ColAudio, i.audioCodec.isEmpty() ? QString()
                                                  : i.audioCodec + QStringLiteral(", ") + Fmt::audio(i.sampleRate, i.channels));
    row->setText(ColSize, i.fileSize > 0 ? Fmt::fileSize(i.fileSize) : QString());

    QString status;
    if (!online)
        status = tr("File not found");
    else if (m_ctl->isProbing(id))
        status = tr("Reading...");
    else if (!m->probed)
        status = tr("Cannot read file");
    else
        status = Fmt::kindName(i.kind);
    row->setText(ColStatus, status);

    const QColor fg = !online || (!m->probed && !m_ctl->isProbing(id)) ? QColor(0xe0, 0x60, 0x60) : QColor();
    for (int c = 0; c < ColCount; ++c)
        row->setForeground(c, fg.isValid() ? QBrush(fg) : QBrush());

    const QImage thumb = m_ctl->thumbnail(id);
    if (!thumb.isNull()) {
        QPixmap pm(64, 36);
        pm.fill(Qt::black);
        QPainter painter(&pm);
        const QImage scaled = thumb.scaled(64, 36, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        painter.drawImage((64 - scaled.width()) / 2, (36 - scaled.height()) / 2, scaled);
        row->setIcon(ColName, QIcon(pm));
    } else if (!online) {
        row->setIcon(ColName, style()->standardIcon(QStyle::SP_MessageBoxWarning));
    } else if (i.kind == MediaInfo::Kind::Audio) {
        row->setIcon(ColName, style()->standardIcon(QStyle::SP_MediaVolume));
    } else {
        row->setIcon(ColName, style()->standardIcon(QStyle::SP_FileIcon));
    }
}

void MediaBinPanel::applyFilter()
{
    const QString f = m_filter->text().trimmed();
    for (auto it = m_rows.cbegin(); it != m_rows.cend(); ++it) {
        QTreeWidgetItem *row = it.value();
        if (row->data(0, IsFolderRole).toBool())
            continue;
        row->setHidden(!f.isEmpty() && !row->text(ColName).contains(f, Qt::CaseInsensitive));
    }
}

void MediaBinPanel::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
        m_rebuilding = true;
        for (auto it = m_rows.cbegin(); it != m_rows.cend(); ++it)
            if (!it.value()->data(0, IsFolderRole).toBool())
                fillItemRow(it.value(), it.key());
        m_rebuilding = false;
    }
    QWidget::changeEvent(event);
}

void MediaBinPanel::retranslate()
{
    m_tree->setHeaderLabels({tr("Name"), tr("Duration"), tr("Resolution"), tr("FPS"), tr("Video"), tr("Audio"),
                             tr("Size"), tr("Status")});
    m_importBtn->setText(tr("Import"));
    m_importBtn->setToolTip(tr("Import media files (Ctrl+I). You can also drag files here from Explorer."));
    m_folderBtn->setText(tr("Folder"));
    m_folderBtn->setToolTip(tr("New folder"));
    m_filter->setPlaceholderText(tr("Search by name"));

    const int total = int(m_ctl->project().items.size());
    const int offline = m_ctl->offlineCount();
    QString s = tr("Files: %1").arg(total);
    if (offline > 0)
        s += QStringLiteral(" · ") + tr("Not found: %1").arg(offline);
    m_summary->setText(s);
}

QList<QUuid> MediaBinPanel::selectedItems() const
{
    QList<QUuid> out;
    for (QTreeWidgetItem *it : m_tree->selectedItems())
        if (!it->data(0, IsFolderRole).toBool())
            out << it->data(0, IdRole).toUuid();
    return out;
}

QList<QUuid> MediaBinPanel::selectedFolders() const
{
    QList<QUuid> out;
    for (QTreeWidgetItem *it : m_tree->selectedItems())
        if (it->data(0, IsFolderRole).toBool())
            out << it->data(0, IdRole).toUuid();
    return out;
}

void MediaBinPanel::importDialog()
{
    QStringList patterns;
    for (const QString &e : MediaProbe::videoExtensions() + MediaProbe::audioExtensions()
                                + MediaProbe::imageExtensions())
        patterns << QStringLiteral("*.") + e;
    QSettings s;
    const QString dir = s.value(QStringLiteral("import/lastDir")).toString();
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Import media"), dir,
        tr("Media files (%1)").arg(patterns.join(QLatin1Char(' '))) + QStringLiteral(";;")
            + tr("All files (*)"));
    if (files.isEmpty())
        return;
    s.setValue(QStringLiteral("import/lastDir"), QFileInfo(files.first()).absolutePath());
    m_ctl->importFiles(files, currentFolder());
}

void MediaBinPanel::newFolder()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New folder"), tr("Folder name:"), QLineEdit::Normal,
                                               tr("New folder"), &ok);
    if (ok && !name.trimmed().isEmpty())
        m_ctl->addFolder(name.trimmed(), currentFolder());
}

void MediaBinPanel::relinkSelected()
{
    const QList<QUuid> ids = selectedItems();
    if (ids.isEmpty())
        return;
    const QUuid id = ids.first();
    const MediaItem *m = m_ctl->project().item(id);
    if (!m)
        return;
    const QString start = QFileInfo(m->path).absolutePath();
    const QString file = QFileDialog::getOpenFileName(this, tr("Relink \"%1\"").arg(m->name), start,
                                                      tr("Expected file: %1").arg(QFileInfo(m->path).fileName())
                                                          + QStringLiteral(" (*)"));
    if (file.isEmpty())
        return;
    const QString mismatch = m_ctl->relinkMismatch(id, file);
    if (!mismatch.isEmpty()) {
        QMessageBox box(QMessageBox::Warning, tr("Relink"),
                        tr("The selected file looks different from the original (%1). Use it anyway?").arg(mismatch),
                        QMessageBox::NoButton, this);
        QPushButton *yes = box.addButton(tr("Use this file"), QMessageBox::AcceptRole);
        box.addButton(tr("Cancel"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() != yes)
            return;
    }
    int others = 0;
    QString err;
    if (!m_ctl->relink(id, file, &others, &err)) {
        QMessageBox::warning(this, tr("Relink"), tr("Cannot read the file: %1").arg(err));
        return;
    }
    if (others > 0)
        QMessageBox::information(this, tr("Relink"),
                                 tr("Other missing files found in the same folder and relinked: %1.").arg(others));
}

void MediaBinPanel::findMissingInFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Folder to search for missing files"));
    if (dir.isEmpty())
        return;
    const int n = m_ctl->relinkFromFolder(dir);
    QMessageBox::information(this, tr("Relink"), tr("Files relinked: %1.").arg(n));
}

void MediaBinPanel::removeSelected()
{
    const QList<QUuid> items = selectedItems();
    const QList<QUuid> folders = selectedFolders();
    if (items.isEmpty() && folders.isEmpty())
        return;
    QMessageBox box(QMessageBox::Question, tr("Remove from project"),
                    tr("Remove the selected items from the project? Files on disk are not deleted."),
                    QMessageBox::NoButton, this);
    QPushButton *yes = box.addButton(tr("Remove"), QMessageBox::AcceptRole);
    box.addButton(tr("Cancel"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() != yes)
        return;
    m_ctl->removeItems(items);
    for (const QUuid &f : folders)
        m_ctl->removeFolder(f);
}

void MediaBinPanel::renameSelected()
{
    if (QTreeWidgetItem *it = m_tree->currentItem())
        m_tree->editItem(it, ColName);
}

void MediaBinPanel::onItemChanged(QTreeWidgetItem *row, int column)
{
    if (m_rebuilding || column != ColName)
        return;
    const QUuid id = row->data(0, IdRole).toUuid();
    const QString name = row->text(ColName);
    // Defer: the controller rebuilds the tree, which must not happen
    // inside the itemChanged signal of the same tree.
    QMetaObject::invokeMethod(this, [this, id, name, isFolder = row->data(0, IsFolderRole).toBool()] {
        if (isFolder)
            m_ctl->renameFolder(id, name);
        else
            m_ctl->renameItem(id, name);
    }, Qt::QueuedConnection);
}

void MediaBinPanel::showInExplorer()
{
    const QList<QUuid> ids = selectedItems();
    if (ids.isEmpty())
        return;
    if (const MediaItem *m = m_ctl->project().item(ids.first()))
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m->path).absolutePath()));
}

void MediaBinPanel::showContextMenu(const QPoint &pos)
{
    QMenu menu(this);
    const QList<QUuid> items = selectedItems();
    const bool anySelected = !items.isEmpty() || !selectedFolders().isEmpty();

    menu.addAction(tr("Import..."), this, &MediaBinPanel::importDialog);
    menu.addAction(tr("New folder"), this, &MediaBinPanel::newFolder);
    menu.addSeparator();
    QAction *rename = menu.addAction(tr("Rename"), this, &MediaBinPanel::renameSelected);
    rename->setEnabled(m_tree->currentItem() != nullptr);
    QAction *relink = menu.addAction(tr("Relink..."), this, &MediaBinPanel::relinkSelected);
    relink->setEnabled(items.size() == 1);
    QAction *find = menu.addAction(tr("Find missing files in folder..."), this, &MediaBinPanel::findMissingInFolder);
    find->setEnabled(m_ctl->offlineCount() > 0);
    QAction *explorer = menu.addAction(tr("Show in Explorer"), this, &MediaBinPanel::showInExplorer);
    explorer->setEnabled(items.size() == 1 && m_ctl->isOnline(items.first()));
    menu.addSeparator();
    QAction *remove = menu.addAction(tr("Remove from project"), this, &MediaBinPanel::removeSelected);
    remove->setEnabled(anySelected);
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

} // namespace bc
