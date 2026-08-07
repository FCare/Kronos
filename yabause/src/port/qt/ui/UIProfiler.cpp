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

#include <fstream>

#include "Settings.h"
#include "UIDebugSH2.h"
#include "UIProfiler.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QProcess>
#include <QString>
#include <QStandardItemModel>

namespace {

QString invokeAddr2LineAndFilt(u32 address, const QString& addr2line, const QString& elfPath, const QString& cppFilt) {
   if (addr2line.isEmpty() || elfPath.isEmpty()) {
      return "";
   }

   // The options are:
   //  -i --inlines           Unwind inlined functions
   //  -p --pretty-print      Make the output easier to read for humans
   //  -f --functions         Show function names
   //  -r --no-recurse-limit  Disable a limit on recursion whilst demangling
   //  -s --basenames         Strip directory names
   //  -e --exe=<executable>  Set the input file name (default is a.out)
   const QStringList arguments = { 
     QString("%1").arg(address, 8, 16, QChar('0')),
     "-i", "-p", "-f", "-r", "-s",
     "-e", elfPath
   };
 
   QProcess addr2lineProgram;
   addr2lineProgram.start(addr2line, arguments);
   addr2lineProgram.waitForFinished();

   const QByteArray pStdout = addr2lineProgram.readAllStandardOutput();
   if (addr2lineProgram.exitCode() == 0) {
      if (cppFilt.isEmpty())
      {
         QString result(pStdout);
         result.replace("\n", "");
         result.replace("\r", "");
         return result;
      }
      else {
         QStringList words = QString(pStdout).split(' ', QString::SkipEmptyParts);

         QString result;
         for (const QString& word : words) {
            QStringList cppfiltArgs;
            if (!word.startsWith("__"))
            {
              cppfiltArgs.append("-n");
            }
            cppfiltArgs.append(word);

            QProcess cppFiltProgram;
            cppFiltProgram.start(cppFilt, cppfiltArgs);
            cppFiltProgram.waitForFinished();

            if (cppFiltProgram.exitCode() == 0)
            {
              result += cppFiltProgram.readAllStandardOutput();
              result += " ";
            }
            else {
              result += word;
              result += " ";
            }
         }

         result.replace("\n", "");
         result.replace("\r", "");
         return result;
      }
   }

   return "";
}
   
std::pair<double, double> getTotalExecutionTime()
{
   double totalMasterTime = 0;
   double totalSlaveTime = 0;
   
   for (u32 i = 0; i < PROFILE_NUM_INFOS; ++i)
   {
      SH2_ProfilerInfo* infoM = &MSH2->profilerInfo.profile[i];
      if (infoM->count > 0)
      {
        totalMasterTime += infoM->time;
      }
      
      SH2_ProfilerInfo* infoS = &SSH2->profilerInfo.profile[i];
      if (infoS->count > 0)
      {
        totalSlaveTime += infoS->time;
      }
   }

   return { totalMasterTime, totalSlaveTime };
}

QString addressToHexString(u32 address)
{
   return QString("0x%1").arg(address, 8, 16, QChar('0'))
      .trimmed()
      .toUpper();
}

} // namespace ''

UIProfiler::UIProfiler( YabauseThread *yabauseThread, QWidget* p )
	: QDialog( p )
{
   setupUi(this);
   mYabauseThread = yabauseThread;

   QStringList headers;
   headers << "Count"
           << "Time(ms)"
           << "Percent"
           << "Ptr(H)"
           << "Description";

   mItemModel = new QStandardItemModel();
   mItemModel->setSortRole(Qt::UserRole + 1);
   mItemModel->setColumnCount(headers.size());
   mItemModel->setHorizontalHeaderLabels(headers);

   twProfilerResults->setModel(mItemModel);

   twProfilerResults->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
   twProfilerResults->horizontalHeader()->setStretchLastSection(true);
   twProfilerResults->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

   twProfilerResults->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);  // Count
   twProfilerResults->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);  // Time
   twProfilerResults->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);  // Percent
   twProfilerResults->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);  // Ptr
   twProfilerResults->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);           // Description
   
   twProfilerResults->setAlternatingRowColors(true);

	QtYabause::retranslateWidget( this );

   if (mYabauseThread->init() == 0 && MSH2 && SSH2)
   {
      populateTable();
   }
}
   
void UIProfiler::addRow( u64 count, double timeMs, double percent, u32 ptrH, const QString &description )
{
   int row = mItemModel->rowCount();
   mItemModel->insertRow(row);
   
   // Qt::UserRole + 1 is used here for sorting
   
   QStandardItem* countItem = new QStandardItem(QString::number(count));
   countItem->setData(timeMs, Qt::UserRole + 1);
   countItem->setTextAlignment(Qt::AlignCenter);

   QStandardItem* timeItem = new QStandardItem(QString::number(timeMs, 'f', 2));
   timeItem->setData(timeMs, Qt::UserRole + 1);
   timeItem->setTextAlignment(Qt::AlignCenter);

   QStandardItem* percentItem = new QStandardItem(QString::number(percent, 'f', 2));
   percentItem->setData(percent, Qt::UserRole + 1);
   percentItem->setTextAlignment(Qt::AlignCenter);

   const QString pointerString = addressToHexString(ptrH);

   QStandardItem* pointerItem = new QStandardItem(pointerString);
   pointerItem->setData(pointerString, Qt::UserRole + 1);
   pointerItem->setTextAlignment(Qt::AlignCenter);

   QStandardItem* descriptionItem = new QStandardItem(description.trimmed());
   pointerItem->setData(description, Qt::UserRole + 1);
   pointerItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);

   mItemModel->setItem(row, 0, countItem);
   mItemModel->setItem(row, 1, timeItem);
   mItemModel->setItem(row, 2, percentItem);
   mItemModel->setItem(row, 3, pointerItem);
   mItemModel->setItem(row, 4, descriptionItem);

   QString textRow = QString("%1,%2,%3,%4,%5\n").arg(countItem->text(),
      timeItem->text(), percentItem->text(), pointerItem->text(),
      QString("\"%1\"").arg(descriptionItem->text().replace("\"", "\"\"")));

   mRows.push_back(textRow.toStdString());
}

void UIProfiler::populateTable()
{
   Settings* settings = QtYabause::settings();
   QString addr2line = settings->value( "Debug/Addr2Line" ).toString();
   QString cppfilt = settings->value( "Debug/CppFilt" ).toString();
   QString elfFile = UIDebugSH2::findElfPath();

   const std::pair<double, double> executionTime = getTotalExecutionTime();
   const double totalMasterTime = executionTime.first;
   const double totalSlaveTime = executionTime.second;

   lTitle->setText(QString("Total execution time (M/S): %1 / %2 ms").arg(
      QString::number(totalMasterTime, 'f', 2),
      QString::number(totalSlaveTime, 'f', 2)));
   
   mItemModel->setRowCount(0);
   mRows.clear();
   twProfilerResults->setSortingEnabled(false);

   for (u32 i = 0; i < PROFILE_NUM_INFOS; ++i)
   {
      // Master
      {
         SH2_ProfilerInfo* infoM = &MSH2->profilerInfo.profile[i];
         if (infoM->count > 0)
         {
            u32 address = MSH2->profilerInfo.startMonitorAddress + i;
            QString description = invokeAddr2LineAndFilt(address, addr2line, elfFile, cppfilt);

            addRow(infoM->count, infoM->time, (double)infoM->time / totalMasterTime,
              address, description);
         }
      }

      // Slave
      {
         SH2_ProfilerInfo* infoS = &SSH2->profilerInfo.profile[i];
         if (infoS->count > 0)
         {
            u32 address = SSH2->profilerInfo.startMonitorAddress + i;
            QString description = invokeAddr2LineAndFilt(address, addr2line, elfFile, cppfilt);
            addRow(infoS->count, infoS->time, (double)infoS->time / totalSlaveTime,
              address, description);
         }
      }
   }

   twProfilerResults->setSortingEnabled(true);
   twProfilerResults->sortByColumn(1 /* Time(ms) */, Qt::DescendingOrder);
}

void UIProfiler::clearTable()
{
   lTitle->setText("Total execution time (M/S): 0.00 / 0.00 ms");
   for (u32 i = 0; i < PROFILE_NUM_INFOS; ++i)
   {
      SH2_ProfilerInfo* infoM = &MSH2->profilerInfo.profile[i];
      infoM->time = 0;
      infoM->count = 0;

      SH2_ProfilerInfo* infoS = &SSH2->profilerInfo.profile[i];
      infoS->time = 0;
      infoS->count = 0;
   }

   twProfilerResults->setSortingEnabled(false);
   mItemModel->setRowCount(0);
   mRows.clear();
}

void UIProfiler::on_pbClearResults_clicked()
{
   clearTable();
}

void UIProfiler::on_pbExportResults_clicked()
{
   QString fileName = QFileDialog::getSaveFileName(this,
      tr("Save File"), QString(), tr("CSV (*.csv)"));

   if (!fileName.isEmpty())
   {
      const QByteArray utf8 = fileName.toUtf8();
      std::ofstream outputFile(utf8.data(), std::ios_base::out);
      if (!outputFile.is_open())
      {
         QMessageBox::critical(this, "Error", "Failed to save CSV file.");
         return;
      }

      outputFile << "Count,Time(ms),Percent,Ptr(H),Description\n";
      for (const std::string& row : mRows)
      {
         outputFile << row;
      }

      outputFile << "\n";
      outputFile.close();
   }
}

void UIProfiler::accept()
{
   QDialog::accept();
}

void UIProfiler::reject()
{
   QDialog::reject();
}
