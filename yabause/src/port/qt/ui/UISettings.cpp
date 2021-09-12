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
#include "UISettings.h"
#include "../Settings.h"
#include "../CommonDialogs.h"
#include "ygl.h"
#include "stv.h"
#include "UIPortManager.h"

#include <QDir>
#include <QList>
#include <QDesktopWidget>
#include <QImageWriter>
#include <QStorageInfo>
#include <QToolTip>

#include "VolatileSettings.h"

extern "C" {
extern M68K_struct* M68KCoreList[];
extern SH2Interface_struct* SH2CoreList[];
extern PerInterface_struct* PERCoreList[];
extern CDInterface* CDCoreList[];
extern SoundInterface_struct* SNDCoreList[];
extern VideoInterface_struct* VIDCoreList[];
extern OSD_struct* OSDCoreList[];
}

struct Item
{
	Item( const QString& i, const QString& n, bool e=true, bool s=true, bool z=false, bool p=false)
	{ id = i; Name = n; enableFlag = e; saveFlag = s; ipFlag = z; pathFlag = p;}

	QString id;
	QString Name;
	bool enableFlag;
	bool saveFlag;
	bool ipFlag;
        bool pathFlag;
};

typedef QList<Item> Items;

const Items mSysLanguageID = Items()
	<< Item( "0", "English" )
	<< Item( "1", "Deutsch" )
	<< Item( "2", "French" )
	<< Item( "3", "Spanish" )
	<< Item( "4", "Italian" )
	<< Item( "5", "Japanese" );

const Items mRegions = Items()
  << Item( "E", "Europe (PAL)" )
	<< Item( "U", "North America (NTSC)" )
	<< Item( "J" , "Japan (NTSC)" )
	<< Item( "T", "Asia (NTSC)" );

const Items mCartridgeTypes = Items()
	<< Item( "0", "None", false, false )
	<< Item( "1", "Pro Action Replay", true, false)
	<< Item( "2", "4 Mbit Backup Ram", true, true )
	<< Item( "3", "8 Mbit Backup Ram", true, true )
	<< Item( "4", "16 Mbit Backup Ram", true, true )
	<< Item( "5", "32 Mbit Backup Ram", true, true )
	<< Item( "6", "8 Mbit Dram", false, false )
	<< Item( "7", "32 Mbit Dram", false, false )
	<< Item( "8", "Netlink", false, false, true )
	<< Item( "9", "16 Mbit ROM", true, false )
	<< Item( "10", "Japanese Modem", false, false, true )
	<< Item( "12", "STV Rom game", true, false, false, true )
	<< Item( "13", "128 Mbit Dram", false, false );

const Items mVideoFilterMode = Items()
	<< Item("0", "None")
        << Item("1", "Bilinear")
	<< Item("2", "BiCubic")
	<< Item("3", "Deinterlacing Bob")
	<< Item("4", "Deinterlacing Debug Bob")
	<< Item("5", "Deinterlacing OSSC Bob")
	<< Item("6", "Deinterlacing OSSC Debug Bob");

const Items mUpscaleFilterMode = Items()
	<< Item("0", "None")
	<< Item("1", "HQ4x")
        << Item("2", "4xBRZ")
        << Item("3", "2xBRZ");

const Items mPolygonGenerationMode = Items()
	<< Item("0", "Triangles using perspective correction")
	<< Item("1", "CPU Tesselation")
	<< Item("2", "GPU Tesselation");

	const Items mCSMode = Items()
		<< Item("0", "Off")
		<< Item("1", "On");

const Items mResolutionMode = Items()
	<< Item("1", "Original (original resolution of the Saturn)")
	<< Item("2", "480p")
	<< Item("4", "720p")
	<< Item("8", "1080p")
	<< Item("16", "Native (current resolution of the window)");

const Items mAspectRatio = Items()
	<< Item("0", "Original aspect ratio")
	<< Item("1", "Stretch to window")
	<< Item("2", "Integer scaling");

const Items mScanLine = Items()
	<< Item("0", "Scanline off")
	<< Item("1", "Scanline on");

const Items mMeshMode = Items()
	<< Item("0", "Original")
	<< Item("1", "Improved");

const Items mBandingMode = Items()
	<< Item("0", "Original")
	<< Item("1", "Improved");

const Items mWireframe = Items()
	<< Item("0", "Off")
	<< Item("1", "On");

UISettings::UISettings(QList <translation_struct> *translations, QWidget* p )
	: QDialog( p )
{
	// setup dialog
	setupUi( this );

	QString ipNum("(?:[0-1]?[0-9]?[0-9]|2[0-4][0-9]|25[0-5])");
	leCartridgeModemIP->setValidator(new QRegExpValidator(QRegExp("^" + ipNum + "\\." + ipNum + "\\." + ipNum + "\\." + ipNum + "$"), leCartridgeModemIP));
	leCartridgeModemPort->setValidator(new QIntValidator(1, 65535, leCartridgeModemPort));

	pmPort1->setPort( 1 );
	pmPort1->loadSettings();
	pmPort2->setPort( 2 );
	pmPort2->loadSettings();

	if ( p && !p->isFullScreen() )
	{
		setWindowFlags( Qt::Sheet );
	}

	setupCdDrives();

	// load cores informations
	loadCores();

#ifdef HAVE_LIBMINI18N
	trans = *translations;
	loadTranslations();
#else
   lTranslation->hide();
	cbTranslation->hide();
#endif

	populateShortcutsView();

	// load settings
	loadSettings();

	// connections
	foreach ( QToolButton* tb, findChildren<QToolButton*>() )
	{
		connect( tb, SIGNAL( clicked() ), this, SLOT( tbBrowse_clicked() ) );
	}

	// retranslate widgets
	QtYabause::retranslateWidget( this );
}

void UISettings::requestFile( const QString& c, QLineEdit* e, const QString& filters, std::optional<QString> proposedPath)
{
	auto const & path = proposedPath.has_value() ? proposedPath.value() : e->text();
	const QString s = CommonDialogs::getOpenFileName(path, c, filters );
	if ( !s.isNull() )
		e->setText( s );
}

void UISettings::requestNewFile( const QString& c, QLineEdit* e, const QString& filters, std::optional<QString> proposedPath)
{
	auto const & path = proposedPath.has_value() ? proposedPath.value() : e->text();
	const QString s = CommonDialogs::getSaveFileName(path, c, filters );
	if ( !s.isNull() )
		e->setText( s );
}

void UISettings::requestFolder( const QString& c, QLineEdit* e, std::optional<QString> proposedPath)
{
	auto const & path = proposedPath.has_value() ? proposedPath.value() : e->text();
	const QString s = CommonDialogs::getExistingDirectory(path, c );
	if ( !s.isNull() ) {
		e->setText( s );
  }
}

void UISettings::requestSTVFolder( const QString& c, QLineEdit* e, std::optional<QString> proposedPath )
{
	auto const & path = proposedPath.has_value() ? proposedPath.value() : e->text();
	const QString existingDirectoryPath = CommonDialogs::getExistingDirectory(path, c );
	if ( !existingDirectoryPath.isNull() ) {
		e->setText( existingDirectoryPath );
	}
	int const nbGames = STVGetRomList(existingDirectoryPath.toStdString().c_str(), 1);
	cbSTVGame->clear();
	for(int i = 0; i< nbGames; i++){
		cbSTVGame->addItem(getSTVGameName(i),i);
	}
	cbSTVGame->model()->sort(0);
}

QStringList getCdDriveList()
{
	QStringList list;

#if defined Q_OS_WIN
	foreach (const QStorageInfo &storage, QStorageInfo::mountedVolumes()) {
		QFileInfo drive(storage.rootPath());
		LPCWSTR driveString = (LPCWSTR)drive.filePath().utf16();
		if (GetDriveTypeW(driveString) == DRIVE_CDROM)
			list.append(storage.rootPath());
	}
#elif defined Q_OS_LINUX
	FILE * f = fopen("/proc/sys/dev/cdrom/info", "r");
	char buffer[1024];
	char drive_name[10];
	char drive_path[255];

	if (f != NULL) {
		while (fgets(buffer, 1024, f) != NULL) {
			if (sscanf(buffer, "drive name:%s", drive_name) == 1) {
				sprintf(drive_path, "/dev/%s", drive_name);

				list.append(drive_path);
			}
		}
		fclose(f);
	}
#elif defined Q_OS_MAC
#endif
	return list;
}

void UISettings::setupCdDrives()
{
	QStringList list=getCdDriveList();
	foreach(QString string, list)
		cbCdDrive->addItem(string);
}

void UISettings::on_leBios_textChanged(const QString & text)
{
	if (QFileInfo(text).exists())
		cbEnableBiosEmulation->setEnabled(true);
	else
		cbEnableBiosEmulation->setEnabled(false);
}

void UISettings::on_leBiosSettings_textChanged(const QString & text){
	if (QFileInfo(text).exists()){
		cbSysLanguageID->setVisible(false);
		lSysLanguageID->setVisible(false);
	}
	else{
		Settings const * const s = QtYabause::settings();
		cbSysLanguageID->setVisible(true);
		lSysLanguageID->setVisible(true);
		cbSysLanguageID->setCurrentIndex( cbSysLanguageID->findData( s->value( "General/SystemLanguageID", mSysLanguageID.at( 0 ).id ).toString() ) );
	}
}

void UISettings::tbBrowse_clicked()
{
	// get toolbutton sender
	QToolButton* tb = qobject_cast<QToolButton*>( sender() );

	if ( tb == tbBios )
		requestFile( QtYabause::translate( "Choose a bios file" ), leBios );
	else if ( tb == tbBiosSettings )
		requestNewFile( QtYabause::translate("Choose a file to store bios settings"), leBiosSettings);
	else if ( tb == tbCdRom )
	{
		if ( cbCdRom->currentText().contains( "dummy", Qt::CaseInsensitive ) )
		{
			CommonDialogs::information( QtYabause::translate( "The dummies cores don't need configuration." ) );
			return;
		}
		else if ( cbCdRom->currentText().contains( "iso", Qt::CaseInsensitive ) )
			requestFile( QtYabause::translate( "Select your iso/cue/bin/zip file" ), leCdRom, QtYabause::translate( "CD Images (*.iso *.ISO *.cue *.CUE *.bin *.BIN *.mds *.MDS *.ccd *.CCD *.zip *.ZIP *.chd *.CHD)" ) );
		else
			requestFolder( QtYabause::translate( "Choose a cdrom drive/mount point" ), leCdRom );
	}
	else if ( tb == tbSaveStates )
		requestFolder( QtYabause::translate( "Choose a folder to store save states" ), leSaveStates );
	else if (tb == tbScreenshots)
	{
		requestFolder(QtYabause::translate("Choose a folder to store screenshots"), leScreenshots);
	}
	else if ( tb == tbCartridge )
	{
		auto pathProposal = leCartridge->text();
		if (leCartridge->text().isEmpty())
		{
			auto path = QtYabause::DefaultPaths::Cartridge();
			if (mCartridgeTypes[cbCartridge->currentIndex()].saveFlag)
			{
				auto suggestedName = mCartridgeTypes[cbCartridge->currentIndex()].Name;
				suggestedName = suggestedName.remove(' ');
				path = path.append("/").append(suggestedName).append(".ramfile");
			}
			else if (mCartridgeTypes[cbCartridge->currentIndex()].enableFlag)
			{
				path = path.append("/");
			}
			pathProposal = path;
		}
		if (mCartridgeTypes[cbCartridge->currentIndex()].pathFlag) {
            requestSTVFolder( QtYabause::translate( "Choose a STV cartridge folder" ), leCartridge, pathProposal);
        } 
		else if (mCartridgeTypes[cbCartridge->currentIndex()].saveFlag)
		{
			requestNewFile( QtYabause::translate( "Choose a cartridge file" ), leCartridge, QString(), pathProposal);
		}
		else if(mCartridgeTypes[cbCartridge->currentIndex()].enableFlag)
		{
			requestFile( QtYabause::translate( "Open a cartridge file" ), leCartridge, QString(), pathProposal);
		}
		updateVolatileSettings();
	}
	else if ( tb == tbMemory )
		requestNewFile( QtYabause::translate( "Choose a memory file" ), leMemory );
	else if ( tb == tbMpegROM )
		requestFile( QtYabause::translate( "Open a mpeg rom" ), leMpegROM );
}

void UISettings::on_cbInput_currentIndexChanged( int id )
{
	PerInterface_struct* core = QtYabause::getPERCore( cbInput->itemData( id ).toInt() );
        core->Init();

	Q_ASSERT( core );

	pmPort1->setCore( core );
	pmPort2->setCore( core );
}

void UISettings::on_cbCdRom_currentIndexChanged( int id )
{
	CDInterface* core = QtYabause::getCDCore( cbCdRom->itemData( id ).toInt() );

	Q_ASSERT( core );

	switch (core->id)
	{
		case CDCORE_DUMMY:
			cbCdDrive->setVisible(false);
			leCdRom->setVisible(false);
			tbCdRom->setVisible(false);
			break;
		case CDCORE_ISO:
			cbCdDrive->setVisible(false);
			leCdRom->setVisible(true);
			tbCdRom->setVisible(true);
			break;
		case CDCORE_ARCH:
			cbCdDrive->setVisible(true);
			leCdRom->setVisible(false);
			tbCdRom->setVisible(false);
			break;
		default: break;
	}
}

void UISettings::on_cbClockSync_stateChanged( int state )
{
	dteBaseTime->setVisible( state == Qt::Checked );
}

void UISettings::changeAspectRatio(int id)
{
    if (VIDCore != NULL) VIDCore->SetSettingValue(VDP_SETTING_ASPECT_RATIO, (mAspectRatio.at(id).id).toInt());
}

void UISettings::changeScanLine(int id)
{
    if (VIDCore != NULL) VIDCore->SetSettingValue(VDP_SETTING_SCANLINE, (mScanLine.at(id).id).toInt());
}

void UISettings::changeWireframe(int id)
{
    if (VIDCore != NULL) VIDCore->SetSettingValue(VDP_SETTING_WIREFRAME, (mWireframe.at(id).id).toInt());
}

void UISettings::changeMeshMode(int id)
{
    if (VIDCore != NULL) VIDCore->SetSettingValue(VDP_SETTING_MESH_MODE, (mMeshMode.at(id).id).toInt());
}

void UISettings::changeBandingMode(int id)
{
    if (VIDCore != NULL) VIDCore->SetSettingValue(VDP_SETTING_BANDING_MODE, (mBandingMode.at(id).id).toInt());
}

void UISettings::changeResolution(int id)
{
    if (VIDCore != NULL) VIDCore->SetSettingValue(VDP_SETTING_RESOLUTION_MODE, (mResolutionMode.at(id).id).toInt());
}

void UISettings::changeFilterMode(int id)
{
    if (VIDCore != NULL) VIDCore->SetSettingValue(VDP_SETTING_FILTERMODE, (mVideoFilterMode.at(id).id).toInt());
}

void UISettings::changeVideoMode(int id)
{
	if (VIDCoreList[id]->id == 1) {//OpenGL
		//Tesselation on
		Tesselation->setVisible(true);
		cbPolygonGeneration->setVisible(true);
		//Gouraud off
		BandingMode->setVisible(false);
		cbBandingModeFilter->setVisible(false);
		//Wireframe off
		Wireframe->setVisible(false);
		cbWireframeFilter->setVisible(false);
	}
	if (VIDCoreList[id]->id == 2) {//Compute Shader
		//Tesselation offcol4
		Tesselation->setVisible(false);
		cbPolygonGeneration->setVisible(false);
		//Gouraud on
		BandingMode->setVisible(true);
		cbBandingModeFilter->setVisible(true);
		//Wireframe on
		Wireframe->setVisible(true);
		cbWireframeFilter->setVisible(true);
	}
}

void UISettings::changeUpscaleMode(int id)
{
    if (VIDCore != NULL) VIDCore->SetSettingValue(VDP_SETTING_UPSCALMODE, (mUpscaleFilterMode.at(id).id).toInt());
}

void UISettings::changePolygonMode(int id)
{
    if (VIDCore != NULL) VIDCore->SetSettingValue(VDP_SETTING_POLYGON_MODE, (mPolygonGenerationMode.at(id).id).toInt());
}

void UISettings::changeCSMode(int id)
{
    if (VIDCore != NULL) VIDCore->SetSettingValue(VDP_SETTING_COMPUTE_SHADER, (mCSMode.at(id).id).toInt());
}

void UISettings::on_cbCartridge_currentIndexChanged( int id )
{
	Settings const* const s = QtYabause::settings();
	auto const path = s->value(getCartridgePathSettingsKey(id)).toString();

	if (mCartridgeTypes[id].enableFlag)
	{
		auto const prevSelectedCartridgeType = selectedCartridgeType;
		if(prevSelectedCartridgeType != id || leCartridge->text().isEmpty())
		{
			if((mCartridgeTypes[id].pathFlag && QDir().exists(path)) || (QFile::exists(path)))
			{
				leCartridge->setText(path);
			}
			else
			{
				leCartridge->clear();
				tbCartridge->click();
			}
		}
	}
	else
	{
		leCartridge->clear();
	}

	leCartridge->setVisible(mCartridgeTypes[id].enableFlag);
	tbCartridge->setVisible(mCartridgeTypes[id].enableFlag);
	lCartridgePath->setVisible(mCartridgeTypes[id].enableFlag);
	lCartridgeModemIP->setVisible(mCartridgeTypes[id].ipFlag);
	leCartridgeModemIP->setVisible(mCartridgeTypes[id].ipFlag);
	lCartridgeModemPort->setVisible(mCartridgeTypes[id].ipFlag);
	leCartridgeModemPort->setVisible(mCartridgeTypes[id].ipFlag);
    if (mCartridgeTypes[id].pathFlag) {
		QString const & str = leCartridge->text();
    	int const nbGames = STVGetRomList(str.toStdString().c_str(), 0);
        cbSTVGame->clear();
        for(int i = 0; i < nbGames; i++){
			cbSTVGame->addItem(getSTVGameName(i),i);
        }
        cbSTVGame->model()->sort(0);
    }
    cbSTVGame->setVisible(mCartridgeTypes[id].pathFlag);
	lRegion->setVisible(mCartridgeTypes[id].pathFlag);
	cbRegion->setVisible(mCartridgeTypes[id].pathFlag);
	selectedCartridgeType = id;
	updateVolatileSettings();
}

void UISettings::loadCores()
{
	// CD Drivers
	for ( int i = 0; CDCoreList[i] != NULL; i++ )
		cbCdRom->addItem( QtYabause::translate( CDCoreList[i]->Name ), CDCoreList[i]->id );

	// VDI Drivers
	for ( int i = 0; VIDCoreList[i] != NULL; i++ )
		cbVideoCore->addItem( QtYabause::translate( VIDCoreList[i]->Name ), VIDCoreList[i]->id );

		connect(cbVideoCore, SIGNAL(currentIndexChanged(int)), this, SLOT(changeVideoMode(int)));

#if YAB_PORT_OSD
	// OSD Drivers
	for ( int i = 0; OSDCoreList[i] != NULL; i++ )
		cbOSDCore->addItem( QtYabause::translate( OSDCoreList[i]->Name ), OSDCoreList[i]->id );
#else
	delete cbOSDCore;
	delete lOSDCore;
#endif

	// Video FilterMode
	foreach(const Item& it, mVideoFilterMode)
		cbFilterMode->addItem(QtYabause::translate(it.Name), it.id);

        connect(cbFilterMode, SIGNAL(currentIndexChanged(int)), this, SLOT(changeFilterMode(int)));

        //Upscale Mode
        foreach(const Item& it, mUpscaleFilterMode)
		cbUpscaleMode->addItem(QtYabause::translate(it.Name), it.id);

        connect(cbUpscaleMode, SIGNAL(currentIndexChanged(int)), this, SLOT(changeUpscaleMode(int)));

	// Polygon Generation
	foreach(const Item& it, mPolygonGenerationMode)
		cbPolygonGeneration->addItem(QtYabause::translate(it.Name), it.id);

		connect(cbPolygonGeneration, SIGNAL(currentIndexChanged(int)), this, SLOT(changePolygonMode(int)));

		// Compute shader Mode
		foreach(const Item& it, mCSMode){
			cbGPURBG->addItem(QtYabause::translate(it.Name), it.id);
		}

		connect(cbGPURBG, SIGNAL(currentIndexChanged(int)), this, SLOT(changeCSMode(int)));

	// Resolution
  foreach(const Item& it, mResolutionMode)
    cbResolution->addItem(QtYabause::translate(it.Name), it.id);

  connect(cbResolution, SIGNAL(currentIndexChanged(int)), this, SLOT(changeResolution(int)));

  foreach(const Item& it, mAspectRatio)
    cbAspectRatio->addItem(QtYabause::translate(it.Name), it.id);

  connect(cbAspectRatio, SIGNAL(currentIndexChanged(int)), this, SLOT(changeAspectRatio(int)));

  foreach(const Item& it, mScanLine)
    cbScanlineFilter->addItem(QtYabause::translate(it.Name), it.id);

  connect(cbScanlineFilter, SIGNAL(currentIndexChanged(int)), this, SLOT(changeScanLine(int)));

	foreach(const Item& it, mWireframe)
    cbWireframeFilter->addItem(QtYabause::translate(it.Name), it.id);

  connect(cbWireframeFilter, SIGNAL(currentIndexChanged(int)), this, SLOT(changeWireframe(int)));

	foreach(const Item& it, mMeshMode)
		cbMeshModeFilter->addItem(QtYabause::translate(it.Name), it.id);

	foreach(const Item& it, mBandingMode)
		cbBandingModeFilter->addItem(QtYabause::translate(it.Name), it.id);

	connect(cbMeshModeFilter, SIGNAL(currentIndexChanged(int)), this, SLOT(changeMeshMode(int)));

	connect(cbBandingModeFilter, SIGNAL(currentIndexChanged(int)), this, SLOT(changeBandingMode(int)));

	// SND Drivers
	for ( int i = 0; SNDCoreList[i] != NULL; i++ )
		cbSoundCore->addItem( QtYabause::translate( SNDCoreList[i]->Name ), SNDCoreList[i]->id );

	// Cartridge Types
	foreach ( const Item& it, mCartridgeTypes )
		cbCartridge->addItem( QtYabause::translate( it.Name ), it.id );

	// Input Drivers
	for ( int i = 0; PERCoreList[i] != NULL; i++ )
		cbInput->addItem( QtYabause::translate( PERCoreList[i]->Name ), PERCoreList[i]->id );

	// Regions
	foreach ( const Item& it, mRegions )
		cbRegion->addItem( QtYabause::translate( it.Name ), it.id );

	// images filter that qt can write
	QStringList filters;
	foreach(QByteArray ba, QImageWriter::supportedImageFormats())
		if (!filters.contains(ba, Qt::CaseInsensitive))
			filters << QString(ba).toLower();
	for (auto entry : filters)
	{
		cbScreenshotImageFormat->addItem(entry, entry);
	}

	// System Language
	foreach ( const Item& it, mSysLanguageID  )
		cbSysLanguageID ->addItem( QtYabause::translate( it.Name ), it.id );

	// SH2 Interpreters
	for ( int i = 0; SH2CoreList[i] != NULL; i++ )
		cbSH2Interpreter->addItem( QtYabause::translate( SH2CoreList[i]->Name ), SH2CoreList[i]->id );

   //68k cores
   for (int i = 0; M68KCoreList[i] != NULL; i++)
      cb68kCore->addItem(QtYabause::translate(M68KCoreList[i]->Name), M68KCoreList[i]->id);

}

void UISettings::loadTranslations()
{
	cbTranslation->addItem(QString::fromUtf8(_("Use System Locale")), "");
	cbTranslation->addItem("English", "#");
	for (int i = 0; i < this->trans.count(); i++)
		cbTranslation->addItem(trans[i].name.left(1).toUpper()+trans[i].name.mid(1), trans[i].file);

}

void UISettings::populateShortcutsView()
{
	QList<QAction *> actions = parent()->findChildren<QAction *>();
	foreach ( QAction* action, actions )
	{
		if (action->text().isEmpty() || !action->isShortcutVisibleInContextMenu())
			continue;

		actionsList.append(action);
	}

	int row=0;
	twShortcuts->setRowCount(actionsList.count());
	foreach ( QAction* action, actionsList )
	{
		QString text = QtYabause::translate(action->text());
		text = text.remove('&');
		QTableWidgetItem *tblItem = new QTableWidgetItem(text);
		tblItem->setFlags(tblItem->flags() ^ Qt::ItemIsEditable);
		twShortcuts->setItem(row, 0, tblItem);
		tblItem = new QTableWidgetItem(action->shortcut().toString());
		twShortcuts->setItem(row, 1, tblItem);
		row++;
	}
	QHeaderView *headerView = twShortcuts->horizontalHeader();
#if QT_VERSION >= 0x04FF00
	headerView->setSectionResizeMode(QHeaderView::Stretch);
	headerView->setSectionResizeMode(1, QHeaderView::Interactive);
#else
	headerView->setResizeMode(QHeaderView::Stretch);
	headerView->setResizeMode(1, QHeaderView::Interactive);
#endif
}

void UISettings::applyShortcuts()
{
	for (int row = 0; row < (int)actionsList.size(); ++row)
	{
		QAction *action = actionsList[row];
		action->setShortcut(QKeySequence(twShortcuts->item(row, 1)->text()));
	}
}

void UISettings::loadSettings()
{
	// get settings pointer
	Settings const * const s = QtYabause::settings();

	// general
	leBios->setText( s->value( "General/Bios" ).toString() );
	leBiosSettings->setText( s->value( "General/BiosSettings" ).toString() );
	cbEnableBiosEmulation->setChecked( s->value( "General/EnableEmulatedBios" ).toBool() );
	cbUseCache->setChecked( s->value( "General/SH2Cache" ).toBool() );
	cbCdRom->setCurrentIndex( cbCdRom->findData( s->value( "General/CdRom", QtYabause::defaultCDCore().id ).toInt() ) );
	leCdRom->setText( s->value( "General/CdRomISO" ).toString() );
	if (s->value( "General/CdRom", QtYabause::defaultCDCore().id ).toInt() == CDCORE_ARCH)
		cbCdDrive->setCurrentIndex(leCdRom->text().isEmpty() ? 0 : cbCdDrive->findText(leCdRom->text()));

	leSaveStates->setText( s->value( "General/SaveStates", getDataDirPath() ).toString() );

	//screenshots
	{
		auto const defaultScreenshotsPath = QtYabause::DefaultPaths::Screenshots();
		leScreenshots->setText(s->value(QtYabause::SettingKeys::ScreenshotsDirectory, defaultScreenshotsPath).toString());
		auto screenshotsDirectory = QDir(leScreenshots->text());
		auto checkAndCreateDirectory = [&screenshotsDirectory]{
			if(!screenshotsDirectory.exists())
			{
				return screenshotsDirectory.mkdir(screenshotsDirectory.path());
			}
			return true;
		};
		if (!checkAndCreateDirectory())
		{
			//when there is an invalid entry to the setting fall back to the default directory
			leScreenshots->setText(defaultScreenshotsPath);
			screenshotsDirectory = QDir(leScreenshots->text());
			checkAndCreateDirectory(); //we could show an message box if this fails (no write access to our folder) but if so we have plenty of problems and i do not want to spam the user with the message that screenshots folder cant be created. it would be misleading in with the overall problem to not having write access in general
		}
		cbScreenshotImageFormat->setCurrentIndex(cbScreenshotImageFormat->findData(s->value(QtYabause::SettingKeys::ScreenshotsFormat, cbScreenshotImageFormat->itemData(0))));
	}
	cbSysLanguageID->setCurrentIndex( cbSysLanguageID->findData( s->value( "General/SystemLanguageID", mSysLanguageID.at( 0 ).id ).toString() ) );
#ifdef HAVE_LIBMINI18N
	int i;
	if ((i=cbTranslation->findData(s->value( "General/Translation" ).toString())) != -1)
		cbTranslation->setCurrentIndex(i);
	else
		cbTranslation->setCurrentIndex(0);
#endif
	cbEnableVSync->setChecked( s->value( "General/EnableVSync", 1 ).toBool() );
	cbShowFPS->setChecked( s->value( "General/ShowFPS" ).toBool() );
	cbAutostart->setChecked( s->value( "autostart" ).toBool() );

	bool clocksync = s->value( "General/ClockSync" ).toBool();
	cbClockSync->setChecked( clocksync );
	dteBaseTime->setVisible( clocksync );

	QString dt = s->value( "General/FixedBaseTime" ).toString();
	if (!dt.isEmpty())
		dteBaseTime->setDateTime( QDateTime::fromString( dt,Qt::ISODate) );
	else
		dteBaseTime->setDateTime( QDateTime(QDate(1998, 1, 1), QTime(12, 0, 0)) );

	int numThreads = QThread::idealThreadCount();
	cbEnableMultiThreading->setChecked(s->value( "General/EnableMultiThreading", numThreads <= 1 ? false : true ).toBool());
	sbNumberOfThreads->setValue(s->value( "General/NumThreads", numThreads < 0 ? 1 : numThreads ).toInt());

	// video
	cbVideoCore->setCurrentIndex( cbVideoCore->findData( s->value( "Video/VideoCore", QtYabause::defaultVIDCore().id ).toInt() ) );
	changeVideoMode(cbVideoCore->currentIndex());
#if YAB_PORT_OSD
	cbOSDCore->setCurrentIndex( cbOSDCore->findData( s->value( "Video/OSDCore", QtYabause::defaultOSDCore().id ).toInt() ) );
#endif
	cbFullscreen->setChecked( s->value( "Video/Fullscreen", false ).toBool() );

	cbFilterMode->setCurrentIndex(cbFilterMode->findData(s->value("Video/filter_type", mVideoFilterMode.at(0).id).toInt()));
        cbUpscaleMode->setCurrentIndex(cbUpscaleMode->findData(s->value("Video/upscale_type", mUpscaleFilterMode.at(0).id).toInt()));
	cbPolygonGeneration->setCurrentIndex(cbPolygonGeneration->findData(s->value("Video/polygon_generation_mode", mPolygonGenerationMode.at(1).id).toInt()));
  cbGPURBG->setCurrentIndex(cbGPURBG->findData(s->value("Video/compute_shader_mode", mCSMode.at(0).id).toInt()));
	cbResolution->setCurrentIndex(cbResolution->findData(s->value("Video/resolution_mode", mResolutionMode.at(0).id).toInt()));
  cbAspectRatio->setCurrentIndex(cbAspectRatio->findData(s->value("Video/AspectRatio", mAspectRatio.at(0).id).toInt()));
  cbScanlineFilter->setCurrentIndex(cbScanlineFilter->findData(s->value("Video/ScanLine", mScanLine.at(0).id).toInt()));
	cbWireframeFilter->setCurrentIndex(cbWireframeFilter->findData(s->value("Video/Wireframe", mWireframe.at(0).id).toInt()));
	cbMeshModeFilter->setCurrentIndex(cbMeshModeFilter->findData(s->value("Video/MeshMode", mMeshMode.at(0).id).toInt()));
	cbBandingModeFilter->setCurrentIndex(cbBandingModeFilter->findData(s->value("Video/BandingMode", mBandingMode.at(0).id).toInt()));

	// sound
	cbSoundCore->setCurrentIndex( cbSoundCore->findData( s->value( "Sound/SoundCore", QtYabause::defaultSNDCore().id ).toInt() ) );

	// cartridge/memory
	tbCartridge->setEnabled(false);
	cbCartridge->setCurrentIndex( cbCartridge->findData( s->value( "Cartridge/Type", mCartridgeTypes.at( 7 ).id ).toInt() ) );
	tbCartridge->setEnabled(true);
	leCartridge->setText( s->value(getCartridgePathSettingsKey()).toString() );
	leCartridgeModemIP->setText( s->value( "Cartridge/ModemIP", QString("127.0.0.1") ).toString() );
	leCartridgeModemPort->setText( s->value( "Cartridge/ModemPort", QString("1337") ).toString() );
        cbSTVGame->setCurrentIndex( cbSTVGame->findData( s->value( "Cartridge/STVGame", -1 ).toInt() ) );
	leMemory->setText( s->value( "Memory/Path", getDataDirPath().append( "/bkram.bin" ) ).toString() );
	leMpegROM->setText( s->value( "MpegROM/Path" ).toString() );
	checkBox_extended_internal_backup->setChecked(s->value("Memory/ExtendMemory").toBool());
  	//the path needs to go into the volatile settings since we keep only one cartridge path there to keep things simple.
	updateVolatileSettings();

	// input
	cbInput->setCurrentIndex( cbInput->findData( s->value( "Input/PerCore", QtYabause::defaultPERCore().id ).toInt() ) );
	sGunMouseSensitivity->setValue(s->value( "Input/GunMouseSensitivity", 100).toInt() );

	// advanced
	cbRegion->setCurrentIndex( cbRegion->findData( s->value( "STV/Region", mRegions.at( 0 ).id ).toString() ) );
	cbSH2Interpreter->setCurrentIndex( cbSH2Interpreter->findData( s->value( "Advanced/SH2Interpreter", QtYabause::defaultSH2Core().id ).toInt() ) );
   cb68kCore->setCurrentIndex(cb68kCore->findData(s->value("Advanced/68kCore", QtYabause::default68kCore().id).toInt()));

	// view
	bgShowMenubar->setId( rbMenubarNever, BD_NEVERHIDE );
	bgShowMenubar->setId( rbMenubarFullscreen, BD_HIDEFS );
	bgShowMenubar->setId( rbMenubarAlways, BD_ALWAYSHIDE );
	bgShowMenubar->setId( rbMenubarFullscreenHover, BD_SHOWONFSHOVER );
	bgShowMenubar->button( s->value( "View/Menubar", BD_SHOWONFSHOVER ).toInt() )->setChecked( true );

	bgShowToolbar->setId( rbToolbarNever, BD_NEVERHIDE );
	bgShowToolbar->setId( rbToolbarFullscreen, BD_HIDEFS );
	bgShowToolbar->setId( rbToolbarAlways, BD_ALWAYSHIDE );
	bgShowToolbar->button( s->value( "View/Toolbar", BD_HIDEFS ).toInt() )->setChecked( true );

	//shortcuts
	{
		auto const actions = parent()->findChildren<QAction *>();
		for(auto const action : actions)
		{
			if (action->text().isEmpty() || !action->isShortcutVisibleInContextMenu())
				continue;

			auto const var = s->value(action->text());
			if(!var.isValid() || var.isNull() )
			{
				continue;
			}
			action->setShortcut(QKeySequence(var.toString()));
		}
	}
}

void UISettings::saveSettings()
{
	// get settings pointer
	Settings * const s = QtYabause::settings();
        s->setValue( "General/Version", Settings::programVersion() );

	// general
	s->setValue( "General/Bios", leBios->text() );
	s->setValue( "General/BiosSettings", leBiosSettings->text() );
	s->setValue( "General/EnableEmulatedBios", cbEnableBiosEmulation->isChecked() );
	s->setValue( "General/SH2Cache", cbUseCache->isChecked() );
	s->setValue( "General/CdRom", cbCdRom->itemData( cbCdRom->currentIndex() ).toInt() );
	CDInterface* core = QtYabause::getCDCore( cbCdRom->itemData( cbCdRom->currentIndex() ).toInt() );
	if ( core->id == CDCORE_ARCH )
		s->setValue( "General/CdRomISO", cbCdDrive->currentText() );
	else
		s->setValue( "General/CdRomISO", leCdRom->text() );
	s->setValue( "General/SaveStates", leSaveStates->text() );
	s->setValue(QtYabause::SettingKeys::ScreenshotsDirectory, leScreenshots->text());
	s->setValue(QtYabause::SettingKeys::ScreenshotsFormat, cbScreenshotImageFormat->itemData(cbScreenshotImageFormat->currentIndex()).toString());
	s->setValue( "General/SystemLanguageID", cbSysLanguageID->itemData( cbSysLanguageID->currentIndex() ).toString() );
#ifdef HAVE_LIBMINI18N
	s->setValue( "General/Translation", cbTranslation->itemData(cbTranslation->currentIndex()).toString() );
#endif
	s->setValue( "General/EnableVSync", cbEnableVSync->isChecked() );
	s->setValue( "General/ShowFPS", cbShowFPS->isChecked() );
	s->setValue( "autostart", cbAutostart->isChecked() );

	// video
	s->setValue( "Video/VideoCore", cbVideoCore->itemData( cbVideoCore->currentIndex() ).toInt() );
#if YAB_PORT_OSD
	s->setValue( "Video/OSDCore", cbOSDCore->itemData( cbOSDCore->currentIndex() ).toInt() );
#endif
	// Move Outdated window/fullscreen keys
	s->remove("Video/Width");
	s->remove("Video/Height");

	// Save new version of keys
        s->setValue("Video/AspectRatio", cbAspectRatio->itemData(cbAspectRatio->currentIndex()).toInt());
        s->setValue("Video/ScanLine", cbScanlineFilter->itemData(cbScanlineFilter->currentIndex()).toInt());
				s->setValue("Video/Wireframe", cbWireframeFilter->itemData(cbWireframeFilter->currentIndex()).toInt());
				s->setValue("Video/MeshMode", cbMeshModeFilter->itemData(cbMeshModeFilter->currentIndex()).toInt());
				s->setValue("Video/BandingMode", cbBandingModeFilter->itemData(cbBandingModeFilter->currentIndex()).toInt());

	s->setValue( "Video/Fullscreen", cbFullscreen->isChecked() );
	s->setValue( "Video/filter_type", cbFilterMode->itemData(cbFilterMode->currentIndex()).toInt());
	s->setValue( "Video/upscale_type", cbUpscaleMode->itemData(cbUpscaleMode->currentIndex()).toInt());
	s->setValue( "Video/polygon_generation_mode", cbPolygonGeneration->itemData(cbPolygonGeneration->currentIndex()).toInt());
	s->setValue( "Video/compute_shader_mode", cbGPURBG->itemData(cbGPURBG->currentIndex()).toInt());
	s->setValue("Video/resolution_mode", cbResolution->itemData(cbResolution->currentIndex()).toInt());

	s->setValue( "General/ClockSync", cbClockSync->isChecked() );
	s->setValue( "General/FixedBaseTime", dteBaseTime->dateTime().toString(Qt::ISODate));

	s->setValue( "General/EnableMultiThreading", cbEnableMultiThreading->isChecked() );
	s->setValue( "General/NumThreads", sbNumberOfThreads->value());

	// sound
	s->setValue( "Sound/SoundCore", cbSoundCore->itemData( cbSoundCore->currentIndex() ).toInt() );

	// cartridge/memory
	s->setValue( "Cartridge/Type", cbCartridge->itemData( cbCartridge->currentIndex() ).toInt() );
	s->setValue(getCartridgePathSettingsKey(), leCartridge->text() );
	s->setValue( "Cartridge/ModemIP", leCartridgeModemIP->text() );
	s->setValue( "Cartridge/ModemPort", leCartridgeModemPort->text() );
        s->setValue( "Cartridge/STVGame", cbSTVGame->itemData( cbSTVGame->currentIndex() ).toInt() );
	s->setValue( "Memory/Path", leMemory->text() );
	s->setValue( "MpegROM/Path", leMpegROM->text() );
  s->setValue("Memory/ExtendMemory", checkBox_extended_internal_backup->isChecked());

	// input
	s->setValue( "Input/PerCore", cbInput->itemData( cbInput->currentIndex() ).toInt() );
	s->setValue( "Input/GunMouseSensitivity", sGunMouseSensitivity->value() );

	// advanced
	s->setValue( "STV/Region", cbRegion->itemData( cbRegion->currentIndex() ).toString() );
	s->setValue( "Advanced/SH2Interpreter", cbSH2Interpreter->itemData( cbSH2Interpreter->currentIndex() ).toInt() );
   s->setValue("Advanced/68kCore", cb68kCore->itemData(cb68kCore->currentIndex()).toInt());

	// view
	s->setValue( "View/Menubar", bgShowMenubar->checkedId() );
	s->setValue( "View/Toolbar", bgShowToolbar->checkedId() );

	// shortcuts
	applyShortcuts();
	s->beginGroup("Shortcuts");
	auto const actions = parent()->findChildren<QAction *>();
	for ( auto const * const action : actions )
	{
		if (action->text().isEmpty() || !action->isShortcutVisibleInContextMenu())
			continue;

		s->setValue(action->text(), action->shortcut().toString());
	}
	s->endGroup();
}

void UISettings::accept()
{
	saveSettings();
	QDialog::accept();
}

QString UISettings::getCartridgePathSettingsKey(std::optional<int> cartridgeType) const
{
	auto name = mCartridgeTypes[selectedCartridgeType].Name;
	if (cartridgeType.has_value())
	{
		name = mCartridgeTypes[cartridgeType.value()].Name;
	}
	name = name.remove(' ');
	return "Cartridge/Path/" + name;
}

void UISettings::updateVolatileSettings() const
{
	auto* const volatileSettings = QtYabause::volatileSettings();
	volatileSettings->setValue(QtYabause::VolatileSettingKeys::CartridgePath, leCartridge->text());
}
