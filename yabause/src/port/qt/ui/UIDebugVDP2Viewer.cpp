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

#include "UIDebugVDP2Viewer.h"
#include "CommonDialogs.h"
#include "ygl.h"

#include <QImageWriter>
#include <QGraphicsPixmapItem>

void UIDebugVDP2Viewer::clearItems() {
	while (cbScreen->count() != 0)
		cbScreen->removeItem(0);
}
void UIDebugVDP2Viewer::addItem(int id) {
	switch(id) {
		case NBG0:
			cbScreen->addItem("NBG0", NBG0);
		break;
		case NBG1:
			cbScreen->addItem("NBG1", NBG1);
		break;
		case NBG2:
			cbScreen->addItem("NBG2", NBG2);
		break;
		case NBG3:
			cbScreen->addItem("NBG3", NBG3);
		break;
		case RBG0:
			cbScreen->addItem("RBG0", RBG0);
		break;
		case RBG1:
			cbScreen->addItem("RBG1", RBG1);
		break;
		case SPRITE:
			cbScreen->addItem("SPRITE", SPRITE);
		break;
		default:
		break;
	}
}

int UIDebugVDP2Viewer::exec() {
	return QDialog::exec();
}

UIDebugVDP2Viewer::UIDebugVDP2Viewer( QWidget* p )
	: QDialog( p )
{
	// setup dialog
	setupUi( this );

   QGraphicsScene *scene=new QGraphicsScene(this);
   gvScreen->setScene(scene);

   vdp2texture = NULL;
	 width = 0;
	 height = 0;
	// retranslate widgets
	QtYabause::retranslateWidget( this );
}

void UIDebugVDP2Viewer::displayCurrentScreen()
{
	if (!Vdp2Regs)
			return;
 int index = cbScreen->itemData( cbScreen->currentIndex() ).toInt();

	if (vdp2texture != NULL) free(vdp2texture);

	vdp2texture = Vdp2DebugTexture(index, &width, &height);
	if (vdp2texture != NULL) {
		pbSaveAsBitmap->setEnabled(vdp2texture ? true : false);

		// Redraw screen
		QGraphicsScene *scene = gvScreen->scene();
		QImage::Format format = QImage::Format_ARGB32;
		if (cbOpaque->isChecked()) {
			format = QImage::Format_RGB32;
		}
		QImage img((uchar *)vdp2texture, width, height, format);

		bool YMirrored = true;
		if (index == SPRITE) YMirrored = false;
		QPixmap pixmap = QPixmap::fromImage(img.mirrored(false, YMirrored).rgbSwapped());
		scene->clear();
		scene->setBackgroundBrush(Qt::Dense7Pattern);
		scene->addPixmap(pixmap);
		scene->setSceneRect(scene->itemsBoundingRect());
	}
}

void UIDebugVDP2Viewer::on_cbScreen_currentIndexChanged ( int id)
{
	 displayCurrentScreen();
}

void UIDebugVDP2Viewer::showEvent(QShowEvent *) {
    gvScreen->fitInView(gvScreen->scene()->sceneRect());
}

void UIDebugVDP2Viewer::on_pbSaveAsBitmap_clicked ()
{
	QStringList filters;
	int index = cbScreen->itemData( cbScreen->currentIndex() ).toInt();
	foreach ( QByteArray ba, QImageWriter::supportedImageFormats() )
		if ( !filters.contains( ba, Qt::CaseInsensitive ) )
			filters << QString( ba ).toLower();
	for ( int i = 0; i < filters.count(); i++ )
		filters[i] = QtYabause::translate( "%1 Images (*.%2)" ).arg( filters[i].toUpper() ).arg( filters[i] );

	if (!vdp2texture)
		return;

	// take screenshot of gl view
	QImage::Format format = QImage::Format_ARGB32;
	if (cbOpaque->isChecked()) {
		format = QImage::Format_RGB32;
	}
	bool YMirrored = true;
	if (index == SPRITE) YMirrored = false;
   QImage img((uchar *)vdp2texture, width, height, format);
   img = img.mirrored(false, YMirrored).rgbSwapped();

	// request a file to save to to user
	const QString s = CommonDialogs::getSaveFileName( QString(), QtYabause::translate( "Choose a location for your bitmap" ), filters.join( ";;" ) );

	// write image if ok
	if ( !s.isEmpty() )
		if ( !img.save( s ) )
			CommonDialogs::error( QtYabause::translate( "An error occured while writing file." ) );
}

void UIDebugVDP2Viewer::on_cbOpaque_toggled(bool enable) {
	displayCurrentScreen();
}

