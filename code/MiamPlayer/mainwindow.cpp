#include <QtDebug>

#include <QAction>
#include <QDirIterator>
#include <QFileSystemModel>
#include <QStandardPaths>

#include "mainwindow.h"
#include "dialogs/customizethemedialog.h"
#include "interfaces/mediaplayerplugininterface.h"
#include "playlists/playlist.h"

#include <QtMultimedia/QAudioDeviceInfo>
#include <QGraphicsScene>
#include <QGraphicsProxyWidget>
#include <QLibrary>
#include <QMessageBox>
#include <QPluginLoader>

MainWindow::MainWindow(QWidget *parent) :
	QMainWindow(parent), _librarySqlModel(NULL)
{
	setupUi(this);
	Settings *settings = Settings::getInstance();

	this->setAcceptDrops(true);

	this->setWindowIcon(QIcon(":/icons/mmmmp.ico"));
	///FIXME
//	this->setStyleSheet(settings->styleSheet(this));
//	leftTabs->setStyleSheet(settings->styleSheet(leftTabs));
//	widgetSearchBar->setStyleSheet(settings->styleSheet(0));
//	splitter->setStyleSheet(settings->styleSheet(splitter));
//	volumeSlider->setStyleSheet(settings->styleSheet(volumeSlider));
//	seekSlider->setStyleSheet(settings->styleSheet(seekSlider));

	// Special behaviour for media buttons
	mediaButtons << skipBackwardButton << seekBackwardButton << playButton << stopButton;
	mediaButtons << seekForwardButton << skipForwardButton << playbackModeButton;
	/*foreach (MediaButton *b, mediaButtons) {
		b->setStyleSheet(settings->styleSheet(b));
	}*/

	// Init the audio module
	audioOutput = new QAudioOutput(QAudioDeviceInfo::defaultOutputDevice());
	_mediaPlayer = QSharedPointer<MediaPlayer>(new MediaPlayer(this));
	_mediaPlayer->setVolume(settings->volume());
	tabPlaylists->setMediaPlayer(_mediaPlayer);

	//_libraryModel = new LibraryModel(this);

	/// XXX
	_uniqueLibrary = new UniqueLibrary(this);
	//QSharedPointer<LibraryModel> m(_libraryModel);
	//_uniqueLibrary->setLibraryModel(m);

	stackedWidget->addWidget(_uniqueLibrary);
	_uniqueLibrary->hide();
	volumeSlider->setValue(settings->volume());

	// Init shortcuts
	QMap<QString, QVariant> shortcutMap = settings->shortcuts();
	QMapIterator<QString, QVariant> it(shortcutMap);
	while (it.hasNext()) {
		it.next();
		this->bindShortcut(it.key(), it.value().toInt());
	}

	// Instantiate dialogs
	customizeThemeDialog = new CustomizeThemeDialog(this);
	customizeThemeDialog->loadTheme();
	customizeOptionsDialog = new CustomizeOptionsDialog(this);
	playlistManager = new PlaylistManager(tabPlaylists);
	dragDropDialog = new DragDropDialog(this);
	playbackModeWidgetFactory = new PlaybackModeWidgetFactory(this, playbackModeButton, tabPlaylists);

	// Tag Editor
	tagEditor->hide();
}

void MainWindow::init()
{
	//library->setModel(_libraryModel);
	QSharedPointer<LibrarySqlModel> sharedModel(_librarySqlModel);
	library->init(sharedModel);

	// Load playlists at startup if any, otherwise just add an empty one
	this->setupActions();
	this->drawLibrary();

	Settings *settings = Settings::getInstance();
	this->restoreGeometry(settings->value("mainWindowGeometry").toByteArray());
	splitter->restoreState(settings->value("splitterState").toByteArray());
	leftTabs->setCurrentIndex(settings->value("leftTabsIndex").toInt());

	// Init the address bar
	addressBar->init(QStandardPaths::standardLocations(QStandardPaths::MusicLocation).first());
}

/** Plugins OMFG §§§ */
void MainWindow::loadPlugins()
{
	QDir appDirPath = QDir(qApp->applicationDirPath());
	if (!appDirPath.cd("plugins")) {
		return;
	}
	QDirIterator it(appDirPath);
	while (it.hasNext()) {
		if (QLibrary::isLibrary(it.next())) {
			QPluginLoader pluginLoader(appDirPath.absoluteFilePath(it.fileName()), this);
			QObject *plugin = pluginLoader.instance();
			if (plugin) {
				qDebug() << "what plugin is it?";
				BasicPluginInterface *basic = dynamic_cast<BasicPluginInterface *>(plugin);
				if (basic) {
					// Attach a new config page it the plugin provides one
					if (basic->configPage()) {
						customizeOptionsDialog->tabPlugins->addTab(basic->configPage(), basic->name());
					}
					// Add name, state and version info on a summary page
					int row = customizeOptionsDialog->pluginSummaryTableWidget->rowCount();
					customizeOptionsDialog->pluginSummaryTableWidget->insertRow(row);
					QTableWidgetItem *checkbox = new QTableWidgetItem();
					checkbox->setCheckState(Qt::Checked);
					customizeOptionsDialog->pluginSummaryTableWidget->setItem(row, 0, new QTableWidgetItem(basic->name()));
					customizeOptionsDialog->pluginSummaryTableWidget->setItem(row, 1, checkbox);
					customizeOptionsDialog->pluginSummaryTableWidget->setItem(row, 2, new QTableWidgetItem(basic->version()));
				}

				/// XXX Make a dispatcher for other types of plugins?
				if (MediaPlayerPluginInterface *mediaPlayerPlugin = qobject_cast<MediaPlayerPluginInterface *>(plugin)) {
					qDebug() << "MediaPlayerPluginInterface";
					mediaPlayerPlugin->setMediaPlayer(_mediaPlayer);
					qDebug() << "MediaPlayerPluginInterface";
				}
			} else {
				qDebug() << "plugin was NOT loaded !" << it.fileName();
				QString message = "A plugin was found but was the player was unable to load it :(";
				QMessageBox *m = new QMessageBox(QMessageBox::Warning, "Warning", message, QMessageBox::Close, this);
				m->show();
			}
		}
	}
	if (customizeOptionsDialog->pluginSummaryTableWidget->rowCount() == 0) {
		customizeOptionsDialog->listWidget->setRowHidden(5, true);
	}
}

/** Update fonts for menu and context menus. */
void MainWindow::updateFonts(const QFont &font)
{
	menuBar()->setFont(font);
	foreach (QAction *action, findChildren<QAction*>()) {
		action->setFont(font);
	}
}



/** Set up all actions and behaviour. */
void MainWindow::setupActions()
{
	// Load music
	connect(customizeOptionsDialog, &CustomizeOptionsDialog::musicLocationsHaveChanged, [=] () {
		this->drawLibrary(true);
	});

	// Adds a group where view mode are mutually exclusive
	QActionGroup *viewModeGroup = new QActionGroup(this);
	actionPlaylistMode->setActionGroup(viewModeGroup);
	actionPlaylistMode->setData(QVariant(0));
	actionUniqueLibraryMode->setActionGroup(viewModeGroup);
	actionUniqueLibraryMode->setData(QVariant(1));

	/// TODO: A sample plugin to append a new view (let's say in QML with QQuickControls?)
	connect(viewModeGroup, &QActionGroup::triggered, [=](QAction* action) {
		stackedWidget->setCurrentIndex(action->data().toInt());
	});

	QActionGroup *actionPlaybackGroup = new QActionGroup(this);
	foreach(QAction *actionPlayBack, findChildren<QAction*>(QRegExp("actionPlayback*", Qt::CaseSensitive, QRegExp::Wildcard))) {
		actionPlaybackGroup->addAction(actionPlayBack);
	}

	/// TODO
	/// Update QMenu when one switches from a playlist to another

	// Link user interface
	// Actions from the menu
    connect(actionExit, &QAction::triggered, &QApplication::quit);
	connect(actionAddPlaylist, &QAction::triggered, tabPlaylists, &TabPlaylist::addPlaylist);
	connect(actionDeleteCurrentPlaylist, &QAction::triggered, tabPlaylists, &TabPlaylist::removeCurrentPlaylist);
	connect(actionShowCustomize, &QAction::triggered, [=] {
		customizeThemeDialog->show();
		customizeThemeDialog->activateWindow();
	});
	connect(actionShowOptions, &QAction::triggered, [=] {
		customizeOptionsDialog->show();
		customizeOptionsDialog->activateWindow();
	});
	connect(actionAboutM4P, &QAction::triggered, this, &MainWindow::aboutM4P);
    connect(actionAboutQt, &QAction::triggered, &QApplication::aboutQt);
	//connect(actionScanLibrary, &QAction::triggered, this, &MainWindow::drawLibrary);
	//connect(actionScanLibrary, &QAction::triggered, _libraryModel->musicSearchEngine(), &MusicSearchEngine::doSearch);
	//connect(actionScanLibrary, &QAction::triggered, _librarySqlModel, &LibrarySqlModel::rebuild);
	_librarySqlModel = new LibrarySqlModel(this);
	connect(actionScanLibrary, &QAction::triggered, _librarySqlModel, &LibrarySqlModel::rebuild);

	// Quick Start
	connect(quickStart->commandLinkButtonLibrary, &QAbstractButton::clicked, [=] () {
		customizeOptionsDialog->listWidget->setCurrentRow(0);
		customizeOptionsDialog->open();
	});

	// Select only folders that are checked by one
	connect(quickStart->quickStartApplyButton, &QDialogButtonBox::clicked, [=] (QAbstractButton *) {
		QStringList newLocations;
		for (int i = 1; i < quickStart->quickStartTableWidget->rowCount(); i++) {
			if (quickStart->quickStartTableWidget->item(i, 0)->checkState() == Qt::Checked) {
				QString musicLocation = quickStart->quickStartTableWidget->item(i, 1)->data(Qt::UserRole).toString();
				customizeOptionsDialog->addMusicLocation(musicLocation);
				newLocations.append(musicLocation);
			}
		}
		Settings::getInstance()->setMusicLocations(newLocations);
		this->drawLibrary(true);
		quickStart->hide();
	});

	foreach (TreeView *tab, this->findChildren<TreeView*>()) {
		connect(tab, &TreeView::setTagEditorVisible, this, &MainWindow::toggleTagEditor);
		connect(tab, &TreeView::aboutToInsertToPlaylist, tabPlaylists, &TabPlaylist::insertItemsToPlaylist);
		connect(tab, &TreeView::sendToTagEditor, tagEditor, &TagEditor::addItemsToEditor);
	}

	// Send one folder to the music locations
	connect(filesystem, &FileSystemTreeView::aboutToAddMusicLocation, customizeOptionsDialog, &CustomizeOptionsDialog::addMusicLocation);
	/// FIXME
	//connect(filesystem, &QAbstractItemView::doubleClicked, tabPlaylists, &TabPlaylist::appendItemToPlaylist);
	//connect(filesystem, &QAbstractItemView::doubleClicked, [=](const QModelIndex &index){});

	// Send music to the tag editor
	connect(tagEditor, &TagEditor::closeTagEditor, this, &MainWindow::toggleTagEditor);

	// Rebuild the treeview when tracks have changed using the tag editor
	//connect(tagEditor, &TagEditor::rebuildTreeView, library, &LibraryTreeView::rebuild);

	// Media buttons
	connect(_mediaPlayer.data(), &QMediaPlayer::stateChanged, [=] (QMediaPlayer::State state) {
		playButton->disconnect();
		if (state == QMediaPlayer::PlayingState) {
			playButton->setIcon(QIcon(":/player/" + Settings::getInstance()->theme() + "/pause"));
			connect(playButton, &QAbstractButton::clicked, _mediaPlayer.data(), &MediaPlayer::pause);
			seekSlider->setEnabled(true);
		} else {
			playButton->setIcon(QIcon(":/player/" + Settings::getInstance()->theme() + "/play"));
			connect(playButton, &QAbstractButton::clicked, _mediaPlayer.data(), &MediaPlayer::play);
			seekSlider->setDisabled(state == QMediaPlayer::StoppedState);
		}
	});

	connect(skipBackwardButton, &QAbstractButton::clicked, _mediaPlayer.data(), &MediaPlayer::skipBackward);
	connect(seekBackwardButton, &QAbstractButton::clicked, _mediaPlayer.data(), &MediaPlayer::seekBackward);
	connect(playButton, &QAbstractButton::clicked, _mediaPlayer.data(), &QMediaPlayer::play);
	connect(stopButton, &QAbstractButton::clicked, _mediaPlayer.data(), &QMediaPlayer::stop);
	connect(seekForwardButton, &QAbstractButton::clicked, _mediaPlayer.data(), &MediaPlayer::seekForward);
	connect(skipForwardButton, &QAbstractButton::clicked, _mediaPlayer.data(), &MediaPlayer::skipForward);
	connect(playbackModeButton, &MediaButton::mediaButtonChanged, playbackModeWidgetFactory, &PlaybackModeWidgetFactory::update);

	// Sliders
	connect(_mediaPlayer.data(), &QMediaPlayer::positionChanged, [=] (qint64 pos) {
		if (_mediaPlayer.data()->duration() > 0) {
			seekSlider->setValue(1000 * pos / _mediaPlayer.data()->duration());
			timeLabel->setTime(pos, _mediaPlayer.data()->duration());
		}
	});

	connect(seekSlider, &QSlider::sliderPressed, [=] () {
		_mediaPlayer.data()->blockSignals(true);
		_mediaPlayer.data()->setMuted(true);
	});

	connect(seekSlider, &QSlider::sliderMoved, [=] (int pos) {
		_mediaPlayer.data()->setPosition(pos * _mediaPlayer.data()->duration() / 1000);
	});

	connect(seekSlider, &QSlider::sliderReleased, [=] () {
		_mediaPlayer.data()->setMuted(false);
		_mediaPlayer.data()->blockSignals(false);
	});

	// Time label
	//connect()

	// Volume
	connect(volumeSlider, &QSlider::valueChanged, _mediaPlayer.data(), &QMediaPlayer::setVolume);

	// Filter the library when user is typing some text to find artist, album or tracks
	connect(searchBar, &QLineEdit::textEdited, library, &LibraryTreeView::filterLibrary);

	// Playback
	/// XXX
	/// Warning: tabPlaylists->restorePlaylists() needs to be exactly at this position!
	connect(tabPlaylists, &TabPlaylist::updatePlaybackModeButton, playbackModeWidgetFactory, &PlaybackModeWidgetFactory::update);
	tabPlaylists->restorePlaylists();
	connect(actionRemoveSelectedTracks, &QAction::triggered, tabPlaylists->currentPlayList(), &Playlist::removeSelectedTracks);
	connect(actionMoveTrackUp, &QAction::triggered, tabPlaylists->currentPlayList(), &Playlist::moveTracksUp);
	connect(actionMoveTrackDown, &QAction::triggered, tabPlaylists->currentPlayList(), &Playlist::moveTracksDown);
	connect(actionShowPlaylistManager, &QAction::triggered, playlistManager, &QDialog::open);

	connect(filesystem, &FileSystemTreeView::folderChanged, addressBar, &AddressBar::init);
	connect(addressBar, &AddressBar::pathChanged, filesystem, &FileSystemTreeView::reloadWithNewPath);

	// Drag & Drop actions
	connect(dragDropDialog, SIGNAL(rememberDragDrop(QToolButton*)), customizeOptionsDialog, SLOT(setExternalDragDropPreference(QToolButton*)));
    /// FIXME Qt5
	//connect(dragDropDialog, SIGNAL(aboutToAddExtFoldersToLibrary(QList<QDir>)), library->searchEngine(), SLOT(setLocations(QList<QDir>)));
	//connect(dragDropDialog, SIGNAL(reDrawLibrary()), this, SLOT(drawLibrary()));
	connect(dragDropDialog, SIGNAL(aboutToAddExtFoldersToPlaylist(QList<QDir>)), tabPlaylists, SLOT(addExtFolders(QList<QDir>)));

	connect(playbackModeButton, &QPushButton::clicked, playbackModeWidgetFactory, &PlaybackModeWidgetFactory::togglePlaybackModes);

	connect(menuPlayback, &QMenu::aboutToShow, [=](){
		QMediaPlaylist::PlaybackMode mode = tabPlaylists->currentPlayList()->mediaPlaylist()->playbackMode();
		const QMetaObject &mo = QMediaPlaylist::staticMetaObject;
		QMetaEnum metaEnum = mo.enumerator(mo.indexOfEnumerator("PlaybackMode"));
		QAction *action = findChild<QAction*>(QString("actionPlayback").append(metaEnum.valueToKey(mode)));
		action->setChecked(true);
	});
	connect(menuPlaylist, &QMenu::aboutToShow, [=]() {
		bool b = tabPlaylists->currentPlayList()->selectionModel()->hasSelection();
		actionRemoveSelectedTracks->setEnabled(b);
		actionMoveTrackUp->setEnabled(b);
		actionMoveTrackDown->setEnabled(b);
		if (b) {
			int selectedRows = tabPlaylists->currentPlayList()->selectionModel()->selectedRows().count();
			actionRemoveSelectedTracks->setText(tr("&Remove selected tracks", "Number of tracks to remove", selectedRows));
			actionMoveTrackUp->setText(tr("Move selected tracks &up", "Move upward", selectedRows));
			actionMoveTrackDown->setText(tr("Move selected tracks &down", "Move downward", selectedRows));
		}
	});
}

/** Redefined to be able to retransltate User Interface at runtime. */
void MainWindow::changeEvent(QEvent *event)
{
	if (event->type() == QEvent::LanguageChange) {
		retranslateUi(this);
		quickStart->retranslateUi(quickStart);
		customizeThemeDialog->retranslateUi(customizeThemeDialog);
		customizeOptionsDialog->retranslateUi(customizeOptionsDialog);
		tagEditor->retranslateUi(tagEditor);

		// (need to be tested with Arabic language)
		if (tr("LTR") == "RTL") {
			QApplication::setLayoutDirection(Qt::RightToLeft);
		}
	} else {
		QMainWindow::changeEvent(event);
	}
}

void MainWindow::closeEvent(QCloseEvent */*event*/)
{
	Settings *settings = Settings::getInstance();
	settings->setValue("mainWindowGeometry", saveGeometry());
	settings->setValue("splitterState", splitter->saveState());
	settings->setValue("leftTabsIndex", leftTabs->currentIndex());
	settings->setVolume(volumeSlider->value());
	settings->sync();
}

void MainWindow::dropEvent(QDropEvent *event)
{
	// Ignore Drag & Drop if the source is a part of this player
	if (event->source() != NULL) {
		return;
	}
	dragDropDialog->setMimeData(event->mimeData());

	QRadioButton *radioButtonDD = customizeOptionsDialog->findChild<QRadioButton*>(Settings::getInstance()->dragAndDropBehaviour());
	if (radioButtonDD == customizeOptionsDialog->radioButtonDDAddToLibrary) {
		// TODO
	} else if (radioButtonDD == customizeOptionsDialog->radioButtonDDAddToPlaylist) {
		tabPlaylists->addExtFolders(dragDropDialog->externalLocations());
	} else if (radioButtonDD == customizeOptionsDialog->radioButtonDDOpenPopup) {
		dragDropDialog->show();
	}
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
	event->acceptProposedAction();
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event)
{
	event->acceptProposedAction();
}

void MainWindow::moveEvent(QMoveEvent *event)
{
	playbackModeWidgetFactory->move();
	QMainWindow::moveEvent(event);
}

void MainWindow::bindShortcut(const QString &objectName, int keySequence)
{
	Settings::getInstance()->setShortcut(objectName, keySequence);
	QAction *action = findChild<QAction*>("action" + objectName.left(1).toUpper() + objectName.mid(1));
	// Connect actions first
	if (action) {
		action->setShortcut(QKeySequence(keySequence));
	} else {
		// Is this really necessary? Everything should be in the menu
		MediaButton *button = findChild<MediaButton*>(objectName + "Button");
		if (button) {
			button->setShortcut(QKeySequence(keySequence));
		}
	}
}

/** Draw the library widget by calling subcomponents. */
void MainWindow::drawLibrary(bool b)
{
	bool isEmpty = Settings::getInstance()->musicLocations().isEmpty();
	quickStart->setVisible(isEmpty);
	library->setVisible(!isEmpty);
	actionScanLibrary->setEnabled(!isEmpty);
	widgetSearchBar->setVisible(!isEmpty);
	this->toggleTagEditor(false);
	if (isEmpty) {
		quickStart->searchMultimediaFiles();
	} else {
		// Warning: This function violates the object-oriented principle of modularity.
		// However, getting access to the sender might be useful when many signals are connected to a single slot.
		if (sender() == actionScanLibrary) {
			b = true;
		}
		//library->beginPopulateTree(b);
		//_libraryModel->loadFromFile();
		_librarySqlModel->loadFromFile();
	}
}

/** Displays a simple message box about MmeMiamMiamMusicPlayer. */
void MainWindow::aboutM4P()
{
	QString message = tr("This software is a MP3 player very simple to use.<br><br>It does not include extended functionalities like lyrics, or to be connected to the Web. It offers a highly customizable user interface and enables favorite tracks.");
	QMessageBox::about(this, QString("Madame MiamMiam's Music Player v").append(qApp->applicationVersion()), message);
}

void MainWindow::toggleTagEditor(bool b)
{
	tagEditor->setVisible(b);
	tabPlaylists->setVisible(!b);
	seekSlider->setVisible(!b);
	timeLabel->setVisible(!b);
	foreach (MediaButton *button, mediaButtons) {
		button->setVisible(!b && Settings::getInstance()->isMediaButtonVisible(button->objectName()));
	}
	volumeSlider->setVisible(!b);
}