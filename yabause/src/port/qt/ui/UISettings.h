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
#ifndef UISETTINGS_H
#define UISETTINGS_H

#include "ui_UISettings.h"
#include "../QtYabause.h"
#include "UIYabause.h"

#include <optional>

QStringList getCdDriveList();

class UISettings : public QDialog, public Ui::UISettings
{
	Q_OBJECT

public:
	UISettings(QList <translation_struct> *translations, QWidget* parent = 0 );

protected:
	QList <translation_struct> trans;
	QList <QAction*> actionsList;

	void requestFile( const QString& caption, QLineEdit* edit, const QString& filters = QString(), std::optional<QString> proposedPath = std::optional<QString>());
	void requestNewFile( const QString& caption, QLineEdit* edit, const QString& filters = QString(), std::optional<QString> proposedPath = std::optional<QString>());
	void requestFolder( const QString& caption, QLineEdit* edit, std::optional<QString> proposedPath = std::optional<QString>());
	void requestSTVFolder(const QString & caption, QLineEdit * edit, std::optional<QString> proposedPath = std::optional<QString>());
	void setupCdDrives();
	void loadCores();
	void loadTranslations();
	void populateShortcutsView();
	void applyShortcuts();
	void loadSettings();
	void saveSettings();

protected slots:
	void on_leBios_textChanged(const QString & text);
	void on_leBiosSettings_textChanged(const QString & text);
	void tbBrowse_clicked();
	void on_cbInput_currentIndexChanged( int id );
	void on_cbCdRom_currentIndexChanged( int id );
	void on_cbClockSync_stateChanged( int state );
	void on_cbCartridge_currentIndexChanged( int id );
	void accept();
	void changeResolution(int id);
        void changeFilterMode(int id);
				void changeVideoMode(int id);
        void changeUpscaleMode(int id);
        void changeAspectRatio(int id);
        void changeScanLine(int id);
				void changeWireframe(int id);
				void changeMeshMode(int id);
				void changeBandingMode(int id);
				void changePolygonMode(int id);
				void changeCSMode(int id);

private:
	QString getCartridgePathSettingsKey(std::optional<int> cartridgeType = std::optional<int>()) const;
	void updateVolatileSettings() const;

	int selectedCartridgeType = 0;
};

#endif // UISETTINGS_H
