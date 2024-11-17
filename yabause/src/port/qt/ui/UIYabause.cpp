/*  Copyright 2005 Guillaume Duhamel
	Copyright 2005-2006, 2013 Theo Berkau
	Copyright 2008 Filipe Azevedo <pasnox@gmail.com>

	This file is part of Yabause.

	Yabause is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Yabause is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Yabause; if not, write to the Free Software
	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/
#include "UIYabause.h"
#include "../Settings.h"
#include "../VolatileSettings.h"
#include "UISettings.h"
#include "UIBackupRam.h"
#include "UICheats.h"
#include "UICheatSearch.h"
#include "UIDebugSH2.h"
#include "UIDebugVDP1.h"
#include "UIDebugVDP2.h"
#include "UIDebugM68K.h"
#include "UIDebugSCUDSP.h"
#include "UIDebugSCSP.h"
#include "UIDebugSCSPChan.h"
#include "UIDebugSCSPDSP.h"
#include "UIMemoryEditor.h"
#include "UIMemoryTransfer.h"
#include "UIAbout.h"
#include "../YabauseGL.h"
#include "../QtYabause.h"
#include "../CommonDialogs.h"

#include <QKeyEvent>
#include <QTextEdit>
#include <QDockWidget>
#include <QImageWriter>
#include <QUrl>
#include <QDesktopServices>
#include <QDateTime>

#include <QDebug>
#include <QMimeData>

#define ACTION_OPENTRAY 1
#define ACTION_LOADFILE 2
#define ACTION_RESET 3
#define ACTION_SCREENSHOT 4
#define ACTION_SAVESLOT 5
#define ACTION_LOADSLOT 6
#define ACTION_LOADCDROM 7

extern "C" {
extern VideoInterface_struct *VIDCoreList[];
}

//#define USE_UNIFIED_TITLE_TOOLBAR

UIYabause::UIYabause( QWidget* parent )
	: QMainWindow( parent )
{
	mInit = false;
   search.clear();
	searchType = 0;
	mNeedResize = false;
	mLocker = NULL;

	// setup dialog
	setupUi( this );
	toolBar->insertAction( aFileSettings, mFileSaveState->menuAction() );
	toolBar->insertAction( aFileSettings, mFileLoadState->menuAction() );
	toolBar->insertSeparator( aFileSettings );
	setAttribute( Qt::WA_DeleteOnClose );
#ifdef USE_UNIFIED_TITLE_TOOLBAR
	setUnifiedTitleAndToolBarOnMac( true );
#endif
	fSound->setParent( 0, Qt::Popup );
	fVideoDriver->setParent( 0, Qt::Popup );
	fSound->installEventFilter( this );
	fVideoDriver->installEventFilter( this );
	// fill combo driver
	cbVideoDriver->blockSignals( true );
	for ( int i = 0; VIDCoreList[i] != NULL; i++ )
		cbVideoDriver->addItem( VIDCoreList[i]->Name, VIDCoreList[i]->id );
	cbVideoDriver->blockSignals( false );
	// create glcontext
	mYabauseGL = new YabauseGL( );
	// and set it as central application widget
	QWidget *container = QWidget::createWindowContainer(mYabauseGL, this);
	container->setFocusPolicy( Qt::StrongFocus );
	setFocusPolicy( Qt::StrongFocus );
	container->setFocusProxy( this );
	container->setAcceptDrops(false);
	container->installEventFilter( this );
	mYabauseGL->installEventFilter( this );

	this->setAcceptDrops(true);

	//bind auto start to trigger when gl is initialized. before this emulation will fail due to missing GL context
	connect(mYabauseGL, &YabauseGL::glInitialized, [&]
	{
		auto const * const vs = QtYabause::volatileSettings();
		if (vs->value("Video/Fullscreen").toBool()) {
			fullscreenRequested(true);
		}
		if (vs->value("autorun").toBool())
			aEmulationRun->trigger();
	});

	setCentralWidget( container );
	// create log widget
	teLog = new QTextEdit( this );
	teLog->setReadOnly( true );
	teLog->setWordWrapMode( QTextOption::NoWrap );
	teLog->setVerticalScrollBarPolicy( Qt::ScrollBarAlwaysOn );
	teLog->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOn );
	mLogDock = new QDockWidget( this );
	mLogDock->setWindowTitle( "Log" );
	mLogDock->setWidget( teLog );
	addDockWidget( Qt::BottomDockWidgetArea, mLogDock );
	mLogDock->setVisible( false );
	mCanLog = true;
	oldMouseX = oldMouseY = 0;
	mouseCaptured = false;
	cursorShown = false;

	// create emulator thread
	mYabauseThread = new YabauseThread( this );
	// create hide mouse timer
	hideMouseTimer = new QTimer();
	// create mouse cursor timer
	mouseCursorTimer = new QTimer();
	// connections
	connect( mYabauseThread, SIGNAL( requestSize( const QSize& ) ), this, SLOT( sizeRequested( const QSize& ) ) );
	// connect( mYabauseThread, SIGNAL( requestFullscreen( bool ) ), this, SLOT( fullscreenRequested( bool ) ) );
	connect( mYabauseThread, SIGNAL( requestVolumeChange( int ) ), this, SLOT( on_sVolume_valueChanged( int ) ) );
	connect( aViewLog, SIGNAL( toggled( bool ) ), mLogDock, SLOT( setVisible( bool ) ) );
	connect( mLogDock->toggleViewAction(), SIGNAL( toggled( bool ) ), aViewLog, SLOT( setChecked( bool ) ) );
	connect( mYabauseThread, SIGNAL( error( const QString&, bool ) ), this, SLOT( errorReceived( const QString&, bool ) ) );
	connect( mYabauseThread, SIGNAL( pause( bool ) ), this, SLOT( pause( bool ) ) );
	connect( mYabauseThread, SIGNAL( reset() ), this, SLOT( reset() ) );
	connect( hideMouseTimer, SIGNAL( timeout() ), this, SLOT( hideMouse() ));
	connect( mouseCursorTimer, SIGNAL( timeout() ), this, SLOT( cursorRestore() ));
	connect( mYabauseThread, SIGNAL( toggleEmulateMouse( bool, bool ) ), this, SLOT( toggleEmulateMouse( bool, bool ) ) );
	connect( mYabauseGL, SIGNAL( emulationPaused()), this, SLOT (runActions()));
	connect( mYabauseThread, SIGNAL( emulationAlreadyPaused()), this, SLOT (runActionsAlreadyPaused()));

	// Load shortcuts
	VolatileSettings* vs = QtYabause::volatileSettings();
	QList<QAction *> actions = findChildren<QAction *>();
	foreach ( QAction* action, actions )
	{
		if (action->text().isEmpty())
			continue;

		QString text = vs->value(QString("Shortcuts/") + action->text(), "").toString();
		if (text.isEmpty())
			continue;
		action->setShortcut(text);
	}

	// retranslate widgets
	QtYabause::retranslateWidget( this );

	QList<QAction *> actionList = menubar->actions();
	for(int i = 0;i < actionList.size();i++) {
		addAction(actionList.at(i));
	}

	restoreGeometry( vs->value("General/Geometry" ).toByteArray() );
	container->setMouseTracking(true);
	setMouseTracking(true);
	mouseXRatio = mouseYRatio = 1.0;
	mouseXUp = mouseYUp = 0;
	emulateMouse = false;
	mouseSensitivity = vs->value( "Input/GunMouseSensitivity", 100 ).toInt();
	showMenuBarHeight = menubar->height();
	translations = QtYabause::getTranslationList();
}

UIYabause::~UIYabause()
{
	mCanLog = false;
}

void UIYabause::showEvent( QShowEvent* e )
{
	QMainWindow::showEvent( e );

	if ( !mInit )
	{
		VolatileSettings* vs = QtYabause::volatileSettings();

		aEmulationVSync->setChecked( vs->value( "General/EnableVSync", 1 ).toBool() );
		aViewFPS->setChecked( vs->value( "General/ShowFPS" ).toBool() );
		mInit = true;
	}
}

void UIYabause::closeEvent( QCloseEvent* e )
{
	aEmulationPause->trigger();

	if (isFullScreen())
		// Need to switch out of full screen or the geometry settings get saved
		fullscreenRequested( false );
	Settings* vs = QtYabause::settings();
	vs->setValue( "General/Geometry", saveGeometry() );
	vs->sync();

	QMainWindow::closeEvent( e );
}

void UIYabause::keyPressEvent( QKeyEvent* e )
{
	if (emulateMouse && mouseCaptured && e->key() == Qt::Key_Escape) {
		mouseCaptured = false;
		this->setCursor(Qt::ArrowCursor);
	}
	else
		PerKeyDown( e->key() );
}

void UIYabause::keyReleaseEvent( QKeyEvent* e )
{ PerKeyUp( e->key() ); }

void UIYabause::leaveEvent( QEvent* e )
{
	if (emulateMouse && mouseCaptured)
	{
		// lock cursor to center
		if (!cursorShown) {
			int midX = (centralWidget()->size().width()/2); // widget global x
			int midY = centralWidget()->size().height()/2; // widget global y

			QPoint newPos(geometry().x() + centralWidget()->geometry().x() + midX, geometry().y() + centralWidget()->geometry().y() + midY);
			this->cursor().setPos(newPos);
		}
	}
}

void UIYabause::mousePressEvent( QMouseEvent* e )
{
	if (emulateMouse && !mouseCaptured)
	{
		if (!cursorShown) this->setCursor(Qt::BlankCursor);
		else this->setCursor(Qt::CrossCursor);
		mouseCaptured = true;
	}
	else
		PerKeyDown( (1 << 31) | e->button() );
}

void UIYabause::mouseReleaseEvent( QMouseEvent* e )
{
	PerKeyUp( (1 << 31) | e->button() );
}

void UIYabause::hideMouse()
{
	if (!cursorShown) this->setCursor(Qt::BlankCursor);
	else this->setCursor(Qt::CrossCursor);
	hideMouseTimer->stop();
}

void UIYabause::runActionsAlreadyPaused() {
	if (mLocker == NULL) {
		return;
	}
	int action = mLocker->popAction();
	void *bundle = mLocker->popBundle();
	switch (action) {
		case ACTION_OPENTRAY:
		case ACTION_LOADFILE:
		{
			mYabauseThread->OpenTray();
			if (bundle != NULL) {
				loadGameFromFile(*((QString*)bundle));
				delete bundle;
			} else {
				const QString fn = CommonDialogs::getOpenFileName( QtYabause::volatileSettings()->value( "Recents/ISOs" ).toString(), QtYabause::translate( "Select your iso/cue/bin/zip/chd file" ), QtYabause::translate( "CD Images (*.iso *.ISO *.cue *.CUE *.bin *.BIN *.mds *.MDS *.ccd *.CCD *.zip *.ZIP *.chd *.CHD)" ) );
				loadGameFromFile(fn);
			}
			mYabauseThread->resetEmulation();
		}
			break;
		case ACTION_RESET:
			mYabauseThread->resetEmulation();
		break;
		case ACTION_SCREENSHOT:
			takeScreenshot();
		break;
		case ACTION_SAVESLOT:
			if (bundle != NULL) {
				saveSlot(*((int*)bundle));
				free(bundle);
			} else {
				saveSlotAs();
			}
		break;
		case ACTION_LOADSLOT:
			if (bundle != NULL) {
				loadSlot(*((int*)bundle));
				free(bundle);
			} else {
				loadSlotAs();
			}
		break;
		case ACTION_LOADCDROM:
			mYabauseThread->OpenTray();
			loadCDRom();
			mYabauseThread->resetEmulation();
		break;
		default:
		break;
	}
	delete mLocker;
	mLocker = NULL;
}

void UIYabause::runActions() {
	if (mLocker == NULL)  {
		return;
	}
	int action = mLocker->popAction();
	void *bundle = mLocker->popBundle();
	switch (action) {
		case ACTION_OPENTRAY:
		{
			mYabauseThread->OpenTray();
		}
		break;
		case ACTION_LOADFILE:
		{
			if (bundle != NULL) {
				mYabauseThread->SetCdInserted(false);
				mYabauseThread->OpenTray();
				loadGameFromFile(*((QString*)bundle));
				delete bundle;
				mYabauseThread->resetEmulation();
			} else {
				const QString fn = CommonDialogs::getOpenFileName( QtYabause::volatileSettings()->value( "Recents/ISOs" ).toString(), QtYabause::translate( "Select your iso/cue/bin/zip/chd file" ), QtYabause::translate( "CD Images (*.iso *.ISO *.cue *.CUE *.bin *.BIN *.mds *.MDS *.ccd *.CCD *.zip *.ZIP *.chd *.CHD)" ) );
				loadGameFromFile(fn);
			}
		}
		break;
		case ACTION_RESET:
			mYabauseThread->resetEmulation();
		break;
		case ACTION_SCREENSHOT:
			takeScreenshot();
		break;
		case ACTION_SAVESLOT:
			if (bundle != NULL) {
				saveSlot(*((int*)bundle));
				free(bundle);
			} else {
				saveSlotAs();
			}
		break;
		case ACTION_LOADSLOT:
			if (bundle != NULL) {
				loadSlot(*((int*)bundle));
				free(bundle);
			} else {
				loadSlotAs();
			}
		break;
		case ACTION_LOADCDROM:
			loadCDRom();
		break;
		default:
		break;
	}
	delete mLocker;
	mLocker = NULL;
}

void UIYabause::cursorRestore()
{
	this->setCursor(Qt::ArrowCursor);
	mouseCursorTimer->stop();
}

void UIYabause::mouseMoveEvent( QMouseEvent* e )
{
	int midX = (centralWidget()->size().width()/2); // widget global x
	int midY = centralWidget()->size().height()/2; // widget global y


	if (mouseCaptured) {
		mYabauseGL->getScale(&mouseXRatio, &mouseYRatio, &mouseXUp, &mouseYUp);
		if (!cursorShown) {
			int x = (e->x()-midX);
			int y = (midY-e->y());
			//use mouseSensitivity and scale ratio
			x *= (float)mouseSensitivity/100.0;
			y *= (float)mouseSensitivity/100.0;
			x /= mouseXRatio;
			y /= mouseYRatio;
			PerAxisMove((1 << 30), x, y);
		} else {
			int x = (e->x()-mouseXUp)/mouseXRatio;
			int y = (e->y()-mouseYUp)/mouseYRatio;
			PerAxisMove((1 << 30), x, y);
		}
	}

	VolatileSettings* vs = QtYabause::volatileSettings();

	if (!isFullScreen())
	{
		if (emulateMouse && mouseCaptured)
		{
			if (!cursorShown) {
				// lock cursor to center
				QPoint newPos(geometry().x() + centralWidget()->geometry().x() + midX, geometry().y() + centralWidget()->geometry().y() + midY);
				this->cursor().setPos(newPos);
				this->setCursor(Qt::BlankCursor);
			} else {
				this->setCursor(Qt::CrossCursor);
			}
			return;
		}
		else
			this->setCursor(Qt::ArrowCursor);
	}
	else
	{
		if (emulateMouse && mouseCaptured)
		{
			if (!cursorShown) {
				QPoint newPos(geometry().x() + centralWidget()->geometry().x() + midX, geometry().y() + centralWidget()->geometry().y() + midY);
				this->cursor().setPos(newPos);
				this->setCursor(Qt::BlankCursor);
			} else this->setCursor(Qt::CrossCursor);
			return;
		}

		hideMouseTimer->start(3 * 1000);
		this->setCursor(Qt::ArrowCursor);
	}
}

void UIYabause::resizeEvent( QResizeEvent* event )
{
	QMainWindow::resizeEvent( event );
}

void UIYabause::adjustHeight(int & height)
{
   // Compensate for menubar and toolbar
  height += menubar->height();
  height += toolBar->height();
}

void UIYabause::swapBuffers()
{
	mYabauseGL->swapBuffers();
}

void UIYabause::appendLog( const char* s )
{
	if (! mCanLog)
	{
		qWarning( "%s", s );
		return;
	}

	teLog->moveCursor( QTextCursor::End );
	teLog->append( s );

	VolatileSettings* vs = QtYabause::volatileSettings();
	if (( !mLogDock->isVisible( )) && ( vs->value( "View/LogWindow" ).toInt() == 1 )) {
		mLogDock->setVisible( true );
	}
}

bool UIYabause::eventFilter( QObject* o, QEvent* e )
{
	 if (QEvent::MouseButtonPress == e->type()) {
		 QMouseEvent* mouseEvt = (QMouseEvent*)e;
		 mousePressEvent(mouseEvt);
	 }
	 else if (QEvent::MouseButtonRelease == e->type()) {
		 QMouseEvent* mouseEvt = (QMouseEvent*)e;
		 mouseReleaseEvent(mouseEvt);
	 }
	 else if (QEvent::MouseMove == e->type()) {
		 QMouseEvent* mouseEvt = (QMouseEvent*)e;
		 mouseMoveEvent(mouseEvt);
	 }
	 else if (QEvent::KeyPress == e->type()) {
		 QKeyEvent* keyEvt = (QKeyEvent*)e;
		 keyPressEvent(keyEvt);
	 }
	 else if (QEvent::KeyRelease == e->type()) {
		QKeyEvent* keyEvt = (QKeyEvent*)e;
		keyReleaseEvent(keyEvt);
	}
	 else if ( e->type() == QEvent::Hide ) {
		 setFocus();
	 }

	return QMainWindow::eventFilter( o, e );
}

void UIYabause::errorReceived( const QString& error, bool internal )
{
	if ( internal ) {
		QtYabause::appendLog( error.toLocal8Bit().constData() );
	}
	else {
		if (!CommonDialogs::information( error )) exit(-1);
	}
}

void UIYabause::sizeRequested( const QSize& s )
{
	int heightOffset = toolBar->height()+menubar->height();
	int width, height;
	if (s.isNull())
	{
		return;
	}
	else
	{
		width=s.width();
		height=s.height();
	}

	// Compensate for menubar and toolbar
	height += menubar->height();
	height += toolBar->height();

	resize( width, height );
}

void UIYabause::toggleFullscreen( int width, int height, bool f, int videoFormat )
{
}

QPoint preFullscreenModeWindowPosition;

void UIYabause::fullscreenRequested( bool f )
{
	VolatileSettings * vs = QtYabause::volatileSettings();
	vs->setValue("Video/Fullscreen", f);

	if ( isFullScreen() && !f )
	{
#ifdef USE_UNIFIED_TITLE_TOOLBAR
		setUnifiedTitleAndToolBarOnMac( true );
#endif
		showNormal();
		// this->move(preFullscreenModeWindowPosition);

		menubar->show();
		toolBar->show();

		setCursor(Qt::ArrowCursor);
		hideMouseTimer->stop();
	}
	else if ( !isFullScreen() && f )
	{
#ifdef USE_UNIFIED_TITLE_TOOLBAR
		setUnifiedTitleAndToolBarOnMac( false );
#endif
		VolatileSettings* vs = QtYabause::volatileSettings();

		// setMaximumSize( QWIDGETSIZE_MAX, QWIDGETSIZE_MAX );
		// setMinimumSize( 0,0 );
		// preFullscreenModeWindowPosition = this->pos();
		// this->move(0,0);

		showFullScreen();
		menubar->hide();
		toolBar->hide();

		hideMouseTimer->start(3 * 1000);
	}
	if ( aViewFullscreen->isChecked() != f )
		aViewFullscreen->setChecked( f );
	aViewFullscreen->setIcon( QIcon( f ? ":/actions/no_fullscreen.png" : ":/actions/fullscreen.png" ) );
}

void UIYabause::refreshStatesActions()
{
	// reset save actions
	foreach ( QAction* a, findChildren<QAction*>( QRegExp( "aFileSaveState*" ) ) )
	{
		if ( a == aFileSaveStateAs )
			continue;
		int i = a->objectName().remove( "aFileSaveState" ).toInt();
		a->setText( QString( "%1 ... " ).arg( i ) );
		a->setToolTip( a->text() );
		a->setStatusTip( a->text() );
		a->setData( i );
	}
	// reset load actions
	foreach ( QAction* a, findChildren<QAction*>( QRegExp( "aFileLoadState*" ) ) )
	{
		if ( a == aFileLoadStateAs )
			continue;
		int i = a->objectName().remove( "aFileLoadState" ).toInt();
		a->setText( QString( "%1 ... " ).arg( i ) );
		a->setToolTip( a->text() );
		a->setStatusTip( a->text() );
		a->setData( i );
		a->setEnabled( false );
	}
	// get states files of this game
	const QString serial = QtYabause::getCurrentCdSerial();
	const QString mask = QString( "%1_*.yss" ).arg( serial );
	const QString statesPath = QtYabause::volatileSettings()->value( "General/SaveStates", getDataDirPath() ).toString();
	QRegExp rx( QString( mask ).replace( '*', "(\\d+)") );
	QDir d( statesPath );
	foreach ( const QFileInfo& fi, d.entryInfoList( QStringList( mask ), QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase ) )
	{
		if ( rx.exactMatch( fi.fileName() ) )
		{
			int slot = rx.capturedTexts().value( 1 ).toInt();
			const QString caption = QString( "%1 %2 " ).arg( slot ).arg( fi.lastModified().toString( Qt::SystemLocaleDate ) );
			// update save state action
			if ( QAction* a = findChild<QAction*>( QString( "aFileSaveState%1" ).arg( slot ) ) )
			{
				a->setText( caption );
				a->setToolTip( caption );
				a->setStatusTip( caption );
				// update load state action
				a = findChild<QAction*>( QString( "aFileLoadState%1" ).arg( slot ) );
				a->setText( caption );
				a->setToolTip( caption );
				a->setStatusTip( caption );
				a->setEnabled( true );
			}
		}
	}
}

void UIYabause::on_aFileSettings_triggered()
{
	Settings *s = (QtYabause::settings());
	QHash<QString, QVariant> hash;
	const QStringList keys = s->allKeys();
	Q_FOREACH(QString key, keys) {
		hash[key] = s->value(key);
	}

	YabauseLocker locker( mYabauseThread );
	if ( UISettings(&translations, window() ).exec() )
	{
		VolatileSettings* vs = QtYabause::volatileSettings();
		aEmulationVSync->setChecked( vs->value( "General/EnableVSync", 1 ).toBool() );
		aViewFPS->setChecked( vs->value( "General/ShowFPS" ).toBool() );
		mouseSensitivity = vs->value( "Input/GunMouseSensitivity" ).toInt();

		if(isFullScreen())
		{
			menubar->hide();
			toolBar->hide();
		}
		else
		{
			menubar->show();
			toolBar->show();
		}

		//only reset if bios, region, cart,  back up, mpeg, sh2, m68k are changed
		Settings *ss = (QtYabause::settings());
		QHash<QString, QVariant> newhash;
		const QStringList newkeys = ss->allKeys();
		Q_FOREACH(QString key, newkeys) {
			newhash[key] = ss->value(key);
		}
		if(newhash["General/Bios"]!=hash["General/Bios"] ||
			newhash["General/EnableEmulatedBios"]!=hash["General/EnableEmulatedBios"] ||
			newhash["STV/Region"]!=hash["STV/Region"] ||
			newhash["Cartridge/Type"]!=hash["Cartridge/Type"] ||
			newhash["Memory/Path"]!=hash["Memory/Path"] ||
			newhash["MpegROM/Path" ]!=hash["MpegROM/Path" ] ||
			newhash["Advanced/SH2Interpreter" ]!=hash["Advanced/SH2Interpreter" ] ||
         newhash["Advanced/68kCore"] != hash["Advanced/68kCore"] ||
			newhash["General/CdRom"]!=hash["General/CdRom"] ||
			newhash["General/CdRomISO"]!=hash["General/CdRomISO"] ||
		        newhash["General/SystemLanguageID"]!=hash["General/SystemLanguageID"] ||
			newhash["General/ClockSync"]!=hash["General/ClockSync"] ||
			newhash["General/FixedBaseTime"]!=hash["General/FixedBaseTime"]
		)
		{
			if ( mYabauseThread->pauseEmulation( true, true ) )
				refreshStatesActions();
			return;
		}
#ifdef HAVE_LIBMINI18N
		if(newhash["General/Translation"] != hash["General/Translation"])
		{
			mini18n_close();
			retranslateUi(this);
			if ( QtYabause::setTranslationFile() == -1 )
				qWarning( "Can't set translation file" );
			QtYabause::retranslateApplication();
		}
#endif
		if(newhash["Video/VideoCore"] != hash["Video/VideoCore"])
			on_cbVideoDriver_currentIndexChanged(newhash["Video/VideoCore"].toInt());

		if(newhash["General/ShowFPS"] != hash["General/ShowFPS"])
      SetOSDToggle(newhash["General/ShowFPS"].toBool());

		if(newhash["General/EnableVSync"] != hash["General/EnableVSync"]){
			if(newhash["General/EnableVSync"].toBool()) DisableAutoFrameSkip();
			else EnableAutoFrameSkip();
		}

		if (newhash["Sound/SoundCore"] != hash["Sound/SoundCore"])
			ScspChangeSoundCore(newhash["Sound/SoundCore"].toInt());

		if (newhash["Video/WindowWidth"] != hash["Video/WindowWidth"] || newhash["Video/WindowHeight"] != hash["Video/WindowHeight"] ||
			 newhash["Input/GunMouseSensitivity"] != hash["Input/GunMouseSensitivity"])
			sizeRequested(QSize(newhash["Video/WindowWidth"].toInt(),newhash["Video/WindowHeight"].toInt()));

		if (newhash["Video/FullscreenWidth"] != hash["Video/FullscreenWidth"] ||
			newhash["Video/FullscreenHeight"] != hash["Video/FullscreenHeight"] ||
			newhash["Video/Fullscreen"] != hash["Video/Fullscreen"])
		{
			bool f = isFullScreen();
			if (f)
				fullscreenRequested( false );
			fullscreenRequested( f );
		}

		mYabauseThread->reloadControllers();
		refreshStatesActions();
	}
}

void UIYabause::on_aFileOpenISO_triggered()
{

	if (mYabauseThread->IsCDInserted()){
		mYabauseThread->SetCdInserted(false);
		mLocker = new YabauseLocker(mYabauseThread, ACTION_OPENTRAY);
		mLocker->lock();
	}
	else{
		mYabauseThread->SetCdInserted(false);
		mLocker = new YabauseLocker(mYabauseThread, ACTION_LOADFILE);
		mLocker->lock();
	}
}

void UIYabause::loadCDRom()
{
	QStringList list = getCdDriveList();
	int current = list.indexOf(QtYabause::volatileSettings()->value( "Recents/CDs").toString());
	QString fn = QInputDialog::getItem(this, QtYabause::translate("Open CD Rom"),
													QtYabause::translate("Choose a cdrom drive/mount point") + ":",
													list, current, false);
	if (!fn.isEmpty())
	{
		VolatileSettings* vs = QtYabause::volatileSettings();

		QtYabause::settings()->setValue( "Recents/CDs", fn );

		// vs->setValue( "autostart", false );
		vs->setValue( "General/CdRom", QtYabause::defaultCDCore().id );
		vs->setValue( "General/CdRomISO", fn );
		mYabauseThread->SetCdInserted(true);
		mYabauseThread->CloseTray();
		refreshStatesActions();
	}
}

void UIYabause::on_aFileOpenCDRom_triggered()
{
	if (mYabauseThread->IsCDInserted()){
		mYabauseThread->SetCdInserted(false);
		mLocker = new YabauseLocker(mYabauseThread, ACTION_OPENTRAY);
		mLocker->lock();
	} else {
		mLocker = new YabauseLocker(mYabauseThread, ACTION_LOADCDROM);
		mLocker->lock();
	}
}

void UIYabause::saveSlot(int a) {
	if ( YabSaveStateSlot( QtYabause::volatileSettings()->value( "General/SaveStates", getDataDirPath() ).toString().toLatin1().constData(), a ) != 0 )
		CommonDialogs::information( QtYabause::translate( "Couldn't save state file" ) );
	else
		refreshStatesActions();
}

void UIYabause::loadSlot(int a) {
	if ( YabLoadStateSlot( QtYabause::volatileSettings()->value( "General/SaveStates", getDataDirPath() ).toString().toLatin1().constData(), a ) != 0 )
		CommonDialogs::information( QtYabause::translate( "Couldn't load state file" ) );
}

void UIYabause::saveSlotAs() {
	const QString fn = CommonDialogs::getSaveFileName( QtYabause::volatileSettings()->value( "General/SaveStates", getDataDirPath() ).toString(), QtYabause::translate( "Choose a file to save your state" ), QtYabause::translate( "Kronos Save State (*.yss)" ) );
	if ( fn.isNull() )
		return;
	if ( YabSaveState( fn.toLatin1().constData() ) != 0 )
		CommonDialogs::information( QtYabause::translate( "Couldn't save state file" ) );
}

void UIYabause::loadSlotAs() {
	const QString fn = CommonDialogs::getOpenFileName( QtYabause::volatileSettings()->value( "General/SaveStates", getDataDirPath() ).toString(), QtYabause::translate( "Select a file to load your state" ), QtYabause::translate( "Kronos Save State (*.yss)" ) );
	if ( fn.isNull() )
		return;
	if ( YabLoadState( fn.toLatin1().constData() ) != 0 )
		CommonDialogs::information( QtYabause::translate( "Couldn't load state file" ) );
	else
		aEmulationRun->trigger();
}

void UIYabause::on_mFileSaveState_triggered( QAction* a )
{
	if ( a == aFileSaveStateAs )
		return;
	int* bundle = (int*)malloc(sizeof(int));
	*bundle = a->data().toInt();
	mLocker = new YabauseLocker(mYabauseThread, ACTION_SAVESLOT, (void*)bundle);
	mLocker->lock();
}

void UIYabause::on_mFileLoadState_triggered( QAction* a )
{
	if ( a == aFileLoadStateAs )
		return;
	int* bundle = (int*)malloc(sizeof(int));
	*bundle = a->data().toInt();
	mLocker = new YabauseLocker(mYabauseThread, ACTION_LOADSLOT, (void*)bundle);
	mLocker->lock();
}

void UIYabause::on_aFileSaveStateAs_triggered()
{
	mLocker = new YabauseLocker(mYabauseThread, ACTION_SAVESLOT);
	mLocker->lock();
}

void UIYabause::on_aFileLoadStateAs_triggered()
{
	mLocker = new YabauseLocker(mYabauseThread, ACTION_LOADSLOT);
	mLocker->lock();
}

void UIYabause::takeScreenshot(void) {
	#if defined(HAVE_LIBGL) && !defined(QT_OPENGL_ES_1) && !defined(QT_OPENGL_ES_2)
		glReadBuffer(GL_FRONT);
	#endif

		QFileInfo const fileInfo(QtYabause::volatileSettings()->value("General/CdRomISO").toString());

		// take screenshot of gl view
		QImage const screenshot = mYabauseGL->grabFramebuffer();

		auto const directory = QtYabause::volatileSettings()->value(QtYabause::SettingKeys::ScreenshotsDirectory, QtYabause::DefaultPaths::Screenshots()).toString();
		auto const format = QtYabause::volatileSettings()->value(QtYabause::SettingKeys::ScreenshotsFormat, "png").toString();

		// request a file to save to to user
		QString s = directory + "/" + fileInfo.baseName() + QString("_%1." + format).arg(QDateTime::currentDateTime().toString("dd_MM_yyyy-hh_mm_ss"));

		// if the user didn't provide a filename extension, we force it to png
		QFileInfo qfi( s );
		if ( qfi.suffix().isEmpty() )
			s += ".png";

		// write image if ok
		if ( !s.isEmpty() )
		{
			QImageWriter iw( s );
			if ( !iw.write( screenshot ))
			{
				CommonDialogs::information( QtYabause::translate( "An error occur while writing the screenshot: " + iw.errorString()) );
			}
		}
}

void UIYabause::on_aFileScreenshot_triggered()
{
	mLocker = new YabauseLocker(mYabauseThread, ACTION_SCREENSHOT);
	mLocker->lock();
}

void UIYabause::on_aFileQuit_triggered()
{ close(); }

void UIYabause::on_aEmulationRun_triggered()
{
	mYabauseThread->initEmulation();
	if ( mYabauseThread->emulationPaused() )
	{
		mYabauseThread->pauseEmulation( false, false );
		refreshStatesActions();
		if (isFullScreen())
			hideMouseTimer->start(3 * 1000);
	}
}

void UIYabause::on_aEmulationPause_triggered()
{
	if ( !mYabauseThread->emulationPaused() )
		mYabauseThread->pauseEmulation( true, false );
}

void UIYabause::on_aEmulationReset_triggered()
{
	mLocker = new YabauseLocker(mYabauseThread, ACTION_RESET);
	mLocker->lock();
}

void UIYabause::on_aEmulationVSync_toggled( bool toggled )
{
	Settings* vs = QtYabause::settings();
	vs->setValue( "General/EnableVSync", toggled );
	vs->sync();

	if ( !toggled )
		EnableAutoFrameSkip();
	else
		DisableAutoFrameSkip();
}

void UIYabause::on_aToolsBackupManager_triggered()
{
	YabauseLocker locker( mYabauseThread );
	if ( mYabauseThread->init() < 0 )
	{
		CommonDialogs::information( QtYabause::translate( "Kronos is not initialized, can't manage backup ram." ) );
		return;
	}
	UIBackupRam( this ).exec();
}

void UIYabause::on_aToolsCheatsList_triggered()
{
	YabauseLocker locker( mYabauseThread );
	UICheats( this ).exec();
}

void UIYabause::on_aToolsCheatSearch_triggered()
{
   YabauseLocker locker( mYabauseThread );
   UICheatSearch cs(this, &search, searchType);

   cs.exec();

   search = *cs.getSearchVariables( &searchType);
}

void UIYabause::on_aToolsTransfer_triggered()
{
	YabauseLocker locker( mYabauseThread );
	UIMemoryTransfer( mYabauseThread, this ).exec();
}

void UIYabause::on_aViewFPS_triggered( bool toggled )
{
	Settings* vs = QtYabause::settings();
	vs->setValue( "General/ShowFPS", toggled );
	vs->sync();
	SetOSDToggle(toggled ? 1 : 0);
}

void UIYabause::on_aViewLayerVdp1_triggered()
{ ToggleVDP1(); }

void UIYabause::on_aViewLayerNBG0_triggered()
{ ToggleNBG0(); }

void UIYabause::on_aViewLayerNBG1_triggered()
{ ToggleNBG1(); }

void UIYabause::on_aViewLayerNBG2_triggered()
{ ToggleNBG2(); }

void UIYabause::on_aViewLayerNBG3_triggered()
{ ToggleNBG3(); }

void UIYabause::on_aViewLayerRBG0_triggered()
{ ToggleRBG0(); }

void UIYabause::on_aViewLayerRBG1_triggered()
{ ToggleRBG1(); }

void UIYabause::on_aViewFullscreen_triggered( bool b )
{
	fullscreenRequested( b );
}

void UIYabause::breakpointHandlerMSH2(bool displayMessage)
{
	YabauseLocker locker( mYabauseThread );
	if (displayMessage)
		CommonDialogs::information( QtYabause::translate( "Breakpoint Reached" ) );
	UIDebugSH2(UIDebugCPU::PROC_MSH2, mYabauseThread, this ).exec();
}

void UIYabause::breakpointHandlerSSH2(bool displayMessage)
{
	YabauseLocker locker( mYabauseThread );
	if (displayMessage)
		CommonDialogs::information( QtYabause::translate( "Breakpoint Reached" ) );
	UIDebugSH2(UIDebugCPU::PROC_SSH2, mYabauseThread, this ).exec();
}

void UIYabause::breakpointHandlerM68K()
{
	YabauseLocker locker( mYabauseThread );
	CommonDialogs::information( QtYabause::translate( "Breakpoint Reached" ) );
	UIDebugM68K( mYabauseThread, this ).exec();
}

void UIYabause::breakpointHandlerSCUDSP()
{
	YabauseLocker locker( mYabauseThread );
	CommonDialogs::information( QtYabause::translate( "Breakpoint Reached" ) );
	UIDebugSCUDSP( mYabauseThread, this ).exec();
}

void UIYabause::breakpointHandlerSCSPDSP()
{
	YabauseLocker locker( mYabauseThread );
	CommonDialogs::information( QtYabause::translate( "Breakpoint Reached" ) );
	UIDebugSCSPDSP( mYabauseThread, this ).exec();
}

void UIYabause::on_aViewDebugMSH2_triggered()
{
	YabauseLocker locker( mYabauseThread );
	UIDebugSH2( UIDebugCPU::PROC_MSH2, mYabauseThread, this ).exec();
}

void UIYabause::on_aViewDebugSSH2_triggered()
{
	YabauseLocker locker( mYabauseThread );
	UIDebugSH2( UIDebugCPU::PROC_SSH2, mYabauseThread, this ).exec();
}

void UIYabause::on_aViewDebugVDP1_triggered()
{
	YabauseLocker locker( mYabauseThread );
	UIDebugVDP1( NULL , &locker).exec();
}

void UIYabause::on_aViewDebugVDP2_triggered()
{
	YabauseLocker locker( mYabauseThread );
	UIDebugVDP2( NULL, &locker ).exec();
}

void UIYabause::on_aHelpReport_triggered()
{
	QDesktopServices::openUrl(QUrl(aHelpReport->statusTip()));
}

void UIYabause::on_aViewDebugM68K_triggered()
{
	YabauseLocker locker( mYabauseThread );
	UIDebugM68K( mYabauseThread, this ).exec();
}

void UIYabause::on_aViewDebugSCUDSP_triggered()
{
	YabauseLocker locker( mYabauseThread );
	UIDebugSCUDSP( mYabauseThread, this ).exec();
}

void UIYabause::on_aViewDebugSCSP_triggered()
{
	YabauseLocker locker( mYabauseThread );
	UIDebugSCSP( this ).exec();
}

void UIYabause::on_aViewDebugSCSPChan_triggered()
{
      UIDebugSCSPChan(this).exec();
}

void UIYabause::on_aViewDebugMemoryEditor_triggered()
{
	YabauseLocker locker( mYabauseThread );
	UIMemoryEditor( UIDebugCPU::PROC_MSH2, mYabauseThread, this ).exec();
}

void UIYabause::on_aHelpCompatibilityList_triggered()
{ QDesktopServices::openUrl( QUrl( aHelpCompatibilityList->statusTip() ) ); }

void UIYabause::on_aHelpAbout_triggered()
{
	YabauseLocker locker( mYabauseThread );
	UIAbout( window() ).exec();
}

void UIYabause::on_aSound_triggered()
{
	// show volume widget
	sVolume->setValue(QtYabause::volatileSettings()->value( "Sound/Volume").toInt());
	QWidget* ab = toolBar->widgetForAction( aSound );
	fSound->move( ab->mapToGlobal( ab->rect().bottomLeft() ) );
	fSound->show();
}

void UIYabause::on_cbSound_toggled( bool toggled )
{
	if ( toggled )
		ScspUnMuteAudio(SCSP_MUTE_USER);
	else
		ScspMuteAudio(SCSP_MUTE_USER);
	cbSound->setIcon( QIcon( toggled ? ":/actions/sound.png" : ":/actions/mute.png" ) );
}

void UIYabause::on_sVolume_valueChanged( int value )
{
	ScspSetVolume( value );
	Settings* vs = QtYabause::settings();
	vs->setValue("Sound/Volume", value );
}

void UIYabause::on_cbVideoDriver_currentIndexChanged( int id )
{
	VideoInterface_struct* core = QtYabause::getVDICore( cbVideoDriver->itemData( id ).toInt() );
	if ( core )
	{
		if ( VideoChangeCore( core->id ) == 0 )
			mYabauseGL->updateView();
	}
}

void UIYabause::pause( bool paused )
{
	mYabauseGL->pause(paused);

	aEmulationRun->setEnabled( paused );
	aEmulationPause->setEnabled( !paused );
	aEmulationReset->setEnabled( !paused );
}

void UIYabause::reset()
{
	refreshStatesActions();
	mYabauseGL->updateView();
	VolatileSettings* vs = QtYabause::volatileSettings();
	if (vs->value("autostart").toBool()){
		mYabauseThread->pauseEmulation(false, true);
	}
}

void UIYabause::toggleEmulateMouse( bool enable, bool show )
{
	emulateMouse = enable;
	cursorShown = show;
}


int UIYabause::loadGameFromFile(QString const& fileName)
{
	if (QFile::exists(fileName))
	{
		VolatileSettings* vs = QtYabause::volatileSettings();

		QtYabause::settings()->setValue("Recents/ISOs", fileName);

		vs->setValue("General/CdRom", ISOCD.id);
		vs->setValue("General/CdRomISO", fileName);
		mYabauseThread->SetCdInserted(true);
	}

	refreshStatesActions();
	return 0;
}

void UIYabause::dragEnterEvent(QDragEnterEvent* e)
{
	if (e->mimeData()->hasUrls()) {
		e->acceptProposedAction();
	}
}

void UIYabause::dropEvent(QDropEvent* e)
{
	auto urls = e->mimeData()->urls();
	const QUrl& url = urls.first();
	QString* bundle = new QString(url.toLocalFile());

	mYabauseThread->SetCdInserted(false);
	mLocker = new YabauseLocker(mYabauseThread, ACTION_LOADFILE, (void*)bundle);
	mLocker->lock();
;
}
