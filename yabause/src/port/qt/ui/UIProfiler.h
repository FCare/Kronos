/*	Copyright 2025 Romulo Leitão <abra185@gmail.com>

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
#ifndef UIPROFILER_H
#define UIPROFILER_H

#include <vector>

#include "ui_UIProfiler.h"
#include "../YabauseThread.h"
#include "../QtYabause.h"

class QStandardItemModel;

class UIProfiler : public QDialog, public Ui::UIProfiler
{
	Q_OBJECT

public:
	UIProfiler( YabauseThread *mYabauseThread, QWidget* parent = 0 );

protected:
   YabauseThread *mYabauseThread;
   QStandardItemModel* mItemModel;
   std::vector<std::string> mRows;

   void addRow(u64 count, double timeMs, double percent, u32 ptrH, const QString& description);

   void populateTable();

   void clearTable();

protected slots:
	 void on_pbClearResults_clicked();
	 void on_pbExportResults_clicked();
	 void accept() override;
	 void reject() override;
};

#endif // UIProfiler_H
