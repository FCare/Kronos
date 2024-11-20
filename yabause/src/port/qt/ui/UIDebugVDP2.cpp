/*	Copyright 2012 Theo Berkau <cwx@cyberwarriorx.com>

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

#include "UIDebugVDP2.h"
#include "UIDebugVDP2Viewer.h"
#include "CommonDialogs.h"

typedef struct {
	void (*debugStats)(char *, int *);
	QGroupBox *cb;
	QPlainTextEdit *pte;
} debugItem_s;

void UIDebugVDP2::updateScreenInfos() {
	debugItem_s items[8] = {
		{Vdp2DebugStatsNBG0, NBG0Debug, pteNBG0Info},
		{Vdp2DebugStatsNBG1, NBG1Debug, pteNBG1Info},
		{Vdp2DebugStatsNBG2, NBG2Debug, pteNBG2Info},
		{Vdp2DebugStatsNBG3, NBG3Debug, pteNBG3Info},
		{Vdp2DebugStatsRBG0, RBG0Debug, pteRBG0Info},
		{Vdp2DebugStatsRBG1, RBG1Debug, pteRBG1Info},
		{Vdp2DebugStatsSprite, SPRITEDebug, pteSPRITEInfo},
		{Vdp2DebugStatsGeneral, GeneralDebug, pteGeneralInfo}
	};

   if (Vdp2Regs)
   {
		 	int index = 0;
			viewer->clearItems();
			for (int i=0; i<8; i++) {
				DebugGrid->removeWidget(items[i].cb);
				bool isVisible = updateInfoDisplay(items[i].debugStats, items[i].cb, items[i].pte);
				if (isVisible) {
					DebugGrid->addWidget(items[i].cb, index/3, index%3);
					index+=1;
					viewer->addItem(i);
				}
			}
   }

	// retranslate widgets
	QtYabause::retranslateWidget( this );
}

UIDebugVDP2::UIDebugVDP2( QWidget* p , YabauseLocker *lock)
	: QDialog( p )
{
	// setup dialog
	setupUi( this );
	mLock = lock;
	viewer = new UIDebugVDP2Viewer( this );
	updateScreenInfos();
}

bool UIDebugVDP2::updateInfoDisplay(void (*debugStats)(char *, int *), QGroupBox *cb, QPlainTextEdit *pte)
{
   char tempstr[2048] = {0};
   int isScreenEnabled=false;

   debugStats(tempstr, &isScreenEnabled);

   if (isScreenEnabled)
   {
      cb->setVisible(true);
      pte->clear();
      pte->appendPlainText(tempstr);
      pte->moveCursor(QTextCursor::Start);
   }
   else {
		 cb->setVisible(false);
	 }
	 return (isScreenEnabled==true);
}

void UIDebugVDP2::on_pbViewer_clicked()
{
	viewer->exec();
}

void UIDebugVDP2::on_pbNextButton_clicked() {
  if (mLock != NULL) {
    mLock->step();
    updateScreenInfos();
  }
}
