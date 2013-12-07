#include "tabplaylist.h"
#include <model/libraryitem.h>
#include "settings.h"

#include <QAudio>
#include <QApplication>
#include <QFileSystemModel>
#include <QHeaderView>

#include "tabbar.h"
#include "treeview.h"

#include <QEventLoop>

/** Default constructor. */
TabPlaylist::TabPlaylist(QWidget *parent) :
	QTabWidget(parent), _tabIndex(-1)
{
	TabBar *tabBar = new TabBar(this);
	this->setTabBar(tabBar);
	///FIXME
	//this->setStyleSheet(Settings::getInstance()->styleSheet(this));
	this->setDocumentMode(true);
	//_watcher = new QFileSystemWatcher(this);
	messageBox = new TracksNotFoundMessageBox(this);

	// Keep playlists on drive before exit
	connect(qApp, &QCoreApplication::aboutToQuit, this, &TabPlaylist::savePlaylists);

	// Add a new playlist
	connect(this, &QTabWidget::currentChanged, this, &TabPlaylist::checkAddPlaylistButton);

	// Removing a playlist
	connect(this, &QTabWidget::tabCloseRequested, this, &TabPlaylist::removeTabFromCloseButton);

	connect(qApp, &QCoreApplication::aboutToQuit, [=]() {
		Settings *settings = Settings::getInstance();
		if (playlists().size() == 1 && playlist(0)->mediaPlaylist()->isEmpty()) {
			settings->remove("columnStateForPlaylist");
		} else {
			for (int i = 0; i < playlists().size(); i++) {
				settings->saveColumnStateForPlaylist(i, playlist(i)->horizontalHeader()->saveState());
			}
		}
	});

	/// TODO
	//connect(_watcher, &QFileSystemWatcher::fileChanged, [=](const QString &file) {
	//	qDebug() << "file has changed:" << file;
	//});
}

void TabPlaylist::setMediaPlayer(QWeakPointer<MediaPlayer> mediaPlayer)
{
	_mediaPlayer = mediaPlayer;
}

/** Retranslate tabs' name and all playlists in this widget. */
void TabPlaylist::changeEvent(QEvent *event)
{
	if (event->type() == QEvent::LanguageChange) {
		// No translation for the (+) tab button
		for (int i = 0; i < count() - 1; i++) {
			QString playlistName = tr("Playlist ");
			playlistName.append(QString::number(i + 1));
			if (tabText(i) == playlistName) {
				this->setTabText(i, playlistName);
			}
			playlist(i)->horizontalHeader();
		}
	}
}

/** Restore playlists at startup. */
void TabPlaylist::restorePlaylists()
{
	Settings *settings = Settings::getInstance();
	if (settings->playbackKeepPlaylists()) {
		QList<QVariant> playlists = settings->value("playlists").toList();
		if (!playlists.isEmpty()) {
			QList<QMediaContent> tracksNotFound;

			// For all playlists (p : { QList<QVariant[Str=track]> ; QVariant[Str=playlist name] ; QVariant[QMediaPlaylist::PlaybackMode] }
			for (int i = 0, j = 0; i < playlists.size(); i++) {
				QList<QVariant> vTracks = playlists.at(i).toList();
				Playlist *p = this->addPlaylist();
				this->setTabText(count(), playlists.at(++i).toString());
				p->mediaPlaylist()->setPlaybackMode((QMediaPlaylist::PlaybackMode) playlists.at(++i).toInt());

				// For all tracks in current playlist
				QStringList medias;
				foreach (QVariant vTrack, vTracks) {
					QUrl url = vTrack.toUrl();
					if (url.isLocalFile()) {
						medias.append(url.toLocalFile());
					} else {
						tracksNotFound.append(QMediaContent(url));
					}
				}
				this->insertItemsToPlaylist(0, medias);
				if (i == playlists.size() - 1) {
					emit updatePlaybackModeButton();
				}

				// Restore columnState for each playlist too
				p->horizontalHeader()->restoreState(settings->restoreColumnStateForPlaylist(j));
				j++;
			}
			// Error handling
			if (!tracksNotFound.isEmpty()) {
				messageBox->displayError(TracksNotFoundMessageBox::RESTORE_AT_STARTUP, tracksNotFound);
			}
		} else {
			this->addPlaylist();
		}
	} else {
		this->addPlaylist();
	}
}

/** Add a new playlist tab. */
Playlist* TabPlaylist::addPlaylist()
{
	QString newPlaylistName = tr("Playlist ").append(QString::number(count()));

	// Then append a new empty playlist to the others
	Playlist *p = new Playlist(this, _mediaPlayer);
	int i = insertTab(count(), p, newPlaylistName);

	// If there's a custom stylesheet on the playlist, copy it from the previous one
	if (i > 1) {
		Playlist *previous = this->playlist(i - 1);
		p->setStyleSheet(previous->styleSheet());
		/// FIXME: stylesheet should be for Class, not instances
		p->horizontalHeader()->setStyleSheet(previous->horizontalHeader()->styleSheet());
	}

	// Select the new empty playlist
	setCurrentIndex(i);
	emit created();
	return p;
}


/** Add external folders (from a drag and drop) to the current playlist. */
void TabPlaylist::addExtFolders(const QList<QDir> &folders)
{
	/*bool isEmpty = */ this->currentPlayList()->mediaPlaylist()->isEmpty();
	foreach (QDir folder, folders) {
		QDirIterator it(folder, QDirIterator::Subdirectories);
		QList<QMediaContent> medias;
		while (it.hasNext()) {
			medias.append(QMediaContent(QUrl::fromLocalFile(it.next())));
		}
		this->currentPlayList()->insertMedias(currentPlayList()->model()->rowCount(), medias);
	}
	// Automatically plays the first track
	/*if (isEmpty) {
		this->skip();
	}*/
}

/** Append a single track chosen by one from the library or the filesystem into the active playlist. */
void TabPlaylist::appendItemToPlaylist(const QString &track)
{
	QList<QString> tracks;
	tracks.append(track);
	this->insertItemsToPlaylist(-1, tracks);
}

/** Insert multiple tracks chosen by one from the library or the filesystem into a playlist. */
void TabPlaylist::insertItemsToPlaylist(int rowIndex, const QStringList &tracks)
{
	currentPlayList()->insertMedias(rowIndex, tracks);
}

/** Remove a playlist when clicking on a close button in the corner. */
void TabPlaylist::removeTabFromCloseButton(int index)
{
	// Don't delete the first tab, if it's the last one remaining
	if (index > 0 || (index == 0 && count() > 2)) {
		// If the playlist we want to delete has no more right tab, then pick the left tab
		if (index + 1 > count() - 2) {
			setCurrentIndex(index - 1);
		}
		Playlist *p = playlist(index);
		this->removeTab(index);
		delete p;
		emit destroyed(index);
	} else {
		// Clear the content of first tab
		currentPlayList()->mediaPlaylist()->clear();
		currentPlayList()->model()->removeRows(0, currentPlayList()->model()->rowCount());
	}
}

void TabPlaylist::updateRowHeight()
{
	Settings *settings = Settings::getInstance();
	for (int i = 0; i < count() - 1; i++) {
		Playlist *p = playlist(i);
		p->verticalHeader()->setDefaultSectionSize(QFontMetrics(settings->font(Settings::PLAYLIST)).height());
		p->highlightCurrentTrack();
	}
}

/** When the user is clicking on the (+) button to add a new playlist. */
void TabPlaylist::checkAddPlaylistButton(int i)
{
	// The (+) button is the last tab
	if (i == count() - 1) {
		addPlaylist();
	} else {
		//currentPlayList()->countSelectedItems();
		emit updatePlaybackModeButton();
	}
}

/** Save playlists before exit. */
void TabPlaylist::savePlaylists()
{
	Settings *settings = Settings::getInstance();
	if (settings->playbackKeepPlaylists()) {
		QList<QVariant> vPlaylists;
		// Iterate on all playlists, except empty ones and the last one (the (+) button)
		for (int i = 0; i < count() - 1; i++) {
			Playlist *p = playlist(i);

			if (!p->mediaPlaylist()->isEmpty()) {
				QList<QVariant> vTracks;
				for (int j = 0; j < p->mediaPlaylist()->mediaCount(); j++) {
					vTracks.append(p->mediaPlaylist()->media(j).canonicalUrl());
				}
				vPlaylists.append(QVariant(vTracks));
				vPlaylists.append(QVariant(tabBar()->tabText(i)));
				vPlaylists.append(p->mediaPlaylist()->playbackMode());
			}
		}
		// Tracks are stored in QList< QList<QVariant> >
		settings->setValue("playlists", vPlaylists);
		settings->sync();
	}
}