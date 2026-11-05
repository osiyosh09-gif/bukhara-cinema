#pragma once

#include <functional>

#include <QTreeWidget>
#include <QUuid>
#include <QWidget>

class QAction;
class QLineEdit;
class QToolButton;
class QLabel;

namespace bc {

class ProjectController;

// Drag payload from the bin to the timeline: media ids, one per line.
inline constexpr char kMediaIdsMime[] = "application/x-bukhara-media-ids";

// Tree that accepts files from Windows Explorer and moves bin items
// between folders by drag and drop.
class BinTree : public QTreeWidget
{
    Q_OBJECT
public:
    explicit BinTree(QWidget *parent = nullptr);

signals:
    void filesDropped(const QStringList &paths, const QUuid &folder);
    void itemsMoved(const QList<QUuid> &items, const QList<QUuid> &folders, const QUuid &target);

protected:
    QMimeData *mimeData(const QList<QTreeWidgetItem *> &items) const override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dropEvent(QDropEvent *e) override;

private:
    QUuid folderAt(const QPoint &pos) const;
};

// Left panel: media bin with folders, metadata columns and thumbnails.
class MediaBinPanel : public QWidget
{
    Q_OBJECT
public:
    enum Role { IdRole = Qt::UserRole + 1, IsFolderRole };
    enum Column { ColName, ColDuration, ColResolution, ColFps, ColVideo, ColAudio, ColSize, ColStatus, ColCount };

    explicit MediaBinPanel(ProjectController *controller, QWidget *parent = nullptr);

    BinTree *tree() const { return m_tree; }
    // Folder that new imports go to: the selected folder (or the folder of
    // the selected item), otherwise the root.
    QUuid currentFolder() const;
    // Sequence > Create multicamera sequence (phase 12a): the selected video
    // and audio files (two or more) become the angles.
    // When fewer than two files are selected in the bin, the media of the
    // clips selected on the timeline are used (set by the main window).
    QAction *multicamAction() const { return m_multicam; }
    QList<QUuid> multicamMedia() const;
    void openMulticamDialog();
    void setTimelineMedia(std::function<QList<QUuid>()> provider);
    void updateMulticamAction();

public slots:
    void importDialog();
    void newFolder();
    void relinkSelected();
    void findMissingInFolder();

protected:
    void changeEvent(QEvent *event) override;

private:
    void rebuild();
    void updateItemRow(const QUuid &id);
    void fillItemRow(QTreeWidgetItem *row, const QUuid &id);
    void applyFilter();
    void retranslate();
    void showContextMenu(const QPoint &pos);
    void removeSelected();
    void renameSelected();
    void showInExplorer();
    void onItemChanged(QTreeWidgetItem *row, int column);
    QList<QUuid> selectedItems() const;
    QList<QUuid> selectedFolders() const;

    ProjectController *m_ctl;
    BinTree *m_tree;
    QLineEdit *m_filter;
    QToolButton *m_importBtn;
    QToolButton *m_folderBtn;
    QLabel *m_summary;
    QAction *m_multicam;
    std::function<QList<QUuid>()> m_timelineMedia;
    QHash<QUuid, QTreeWidgetItem *> m_rows;
    bool m_rebuilding = false;
};

} // namespace bc
