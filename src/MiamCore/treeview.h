#ifndef TREEVIEW_H
#define TREEVIEW_H

#include <QMessageBox>
#include <QTreeView>
#include <model/selectedtracksmodel.h>
#include <model/trackdao.h>

/**
 * \brief		The TreeView class is the base class for displaying trees in the player.
 * \author      Matthieu Bachelier
 * \copyright   GNU General Public License v3
 */
class MIAMCORE_LIBRARY TreeView : public QTreeView, public SelectedTracksModel
{
	Q_OBJECT
public:
	explicit TreeView(QWidget *parent = 0);

	/** Scan nodes and its subitems before dispatching tracks to a specific widget (playlist or tageditor). */
	virtual void findAll(const QModelIndex &index, QStringList &tracks) const = 0;

	virtual QStringList selectedTracks() override;

protected:
	QModelIndexList _cacheSelectedIndexes;

	/** Explore items to count leaves (tracks). */
	virtual int countAll(const QModelIndexList &indexes) const = 0;

	virtual void startDrag(Qt::DropActions supportedActions) override;

private:
	/** Alerts the user if there's too many tracks to add. */
	QMessageBox::StandardButton beforeSending(const QString &target, QStringList &tracks);

public slots:
	/** Sends folders or tracks to the end of a playlist. */
	inline void appendToPlaylist() { this->insertToPlaylist(-1); }

	/** Sends folders or tracks to a specific position in a playlist. */
	void insertToPlaylist(int rowIndex);

	/** Sends folders or tracks to the tag editor. */
	void openTagEditor();

signals:
	/** Adds tracks to the current playlist at a specific position. */
	void aboutToInsertToPlaylist(int rowIndex, const QStringList &tracks);

	/** Adds tracks to the tag editor. */
	void sendToTagEditor(const QModelIndexList indexes, const QStringList &tracks);
};

#endif // TREEVIEW_H
