#include <QCryptographicHash>
#include <QHeaderView>
#include <QDir>
#include <QFileInfo>
#include <QScrollBar>
#include <QTest>
#include <QMap>

#include "romalyzer.h"
#include "qmc2main.h"
#include "options.h"
#include "gamelist.h"
#include "macros.h"
#include "unzip.h"

// external global variables
extern MainWindow *qmc2MainWindow;
extern QSettings *qmc2Config;
extern Gamelist *qmc2Gamelist;
extern ROMAlyzer *qmc2ROMAlyzer;
extern bool qmc2ReloadActive;
extern bool qmc2ROMAlyzerActive;
extern bool qmc2ROMAlyzerPaused;
extern bool qmc2CleaningUp;
extern bool qmc2EarlyStartup;
extern bool qmc2StopParser;
extern QMap<QString, QTreeWidgetItem *> qmc2GamelistItemMap;
extern QMap<QString, QTreeWidgetItem *> qmc2HierarchyItemMap;

/*
  HOWTO: Calculate the 32-bit CRC of a QByteArray with zlib:

  #include <QByteArray>
  #include <zlib.h>
  ...
  QByteArray ba("This is a Test 123 :)!");
  ulong crc = crc32(0, NULL, 0);
  crc = crc32(crc, (const Bytef *)ba.data(), ba.size());
  printf("CRC-32 = 0x%x\n", crc);
*/

/*
  INFO: How MAME searches for ROMs & CHDs of a game

  <rompath> = <first_rompath>
  for all ROM and CHD <file>'s {
    1) try <rompath>/<game>/<file> - if okay skip to 6)
    2) try <file> from <rompath1>/<game>.zip - if okay skip to 6)
    3) if more rompaths exists, retry 1) and 2) for <next_rompath>...
    4) if a <merge> exists, retry 1), 2) and 3) for <romof>/<merge> instead of <game>/<file>
    5) <file> was not found - stop
    6) load <file> and check CRC
    7) ...
  }

  Backward engineering powered by strace :)! 
*/

ROMAlyzer::ROMAlyzer(QWidget *parent)
#if defined(Q_WS_WIN)
  : QDialog(parent, Qt::Dialog)
#else
  : QDialog(parent, Qt::Dialog | Qt::SubWindow)
#endif
{
#ifdef QMC2_DEBUG
  qmc2MainWindow->log(QMC2_LOG_FRONTEND, "DEBUG: ROMAlyzer::ROMAlyzer(QWidget *parent = 0x" + QString::number((ulong)parent, 16) + ")");
#endif
  
  setupUi(this);

#if defined(QMC2_SDLMESS)
  treeWidgetChecksums->headerItem()->setText(0, tr("Machine / File"));
  checkBoxSelectGame->setText(tr("Select machine"));
  checkBoxSelectGame->setToolTip(tr("Select machine in machine list if selected in analysis report?"));
#endif

  treeWidgetChecksums->header()->setSortIndicatorShown(FALSE);
  if ( qmc2Config->value(QMC2_FRONTEND_PREFIX + "GUI/RestoreLayout").toBool() ) {
    treeWidgetChecksums->header()->restoreState(qmc2Config->value(QMC2_FRONTEND_PREFIX + "Layout/ROMAlyzer/ReportHeaderState", QByteArray()).toByteArray());
    tabWidgetAnalysis->setCurrentIndex(qmc2Config->value(QMC2_FRONTEND_PREFIX + "Layout/ROMAlyzer/AnalysisTab", 0).toInt());
    move(qmc2Config->value(QMC2_FRONTEND_PREFIX + "Layout/ROMAlyzer/Position", QPoint()).toPoint());
    resize(qmc2Config->value(QMC2_FRONTEND_PREFIX + "Layout/ROMAlyzer/Size", QSize()).toSize());
  }

  chdCompressionTypes << tr("none") << tr("zlib") << tr("zlib+") << tr("A/V codec");

  // adjust icon sizes of buttons
  QFont f;
  f.fromString(qmc2Config->value(QMC2_FRONTEND_PREFIX + "GUI/Font").toString());
  qApp->setFont(f);
  QFontMetrics fm(f);
  QSize iconSize(fm.height() - 3, fm.height() - 3);
  pushButtonAnalyze->setIconSize(iconSize);
  pushButtonPause->setIconSize(iconSize);
  pushButtonClose->setIconSize(iconSize);
  pushButtonSearchForward->setIconSize(iconSize);
  pushButtonSearchBackward->setIconSize(iconSize);
  pushButtonPause->setVisible(FALSE);

  // hide yet unsupported features for now...
  groupBoxExportOptions->setVisible(FALSE);

  connect(&animTimer, SIGNAL(timeout()), this, SLOT(animationTimeout()));
}

ROMAlyzer::~ROMAlyzer()
{
#ifdef QMC2_DEBUG
  qmc2MainWindow->log(QMC2_LOG_FRONTEND, "DEBUG: ROMAlyzer::~ROMAlyzer()");
#endif

}

void ROMAlyzer::on_pushButtonClose_clicked()
{
#ifdef QMC2_DEBUG
  qmc2MainWindow->log(QMC2_LOG_FRONTEND, "DEBUG: ROMAlyzer::on_pushButtonClose_clicked()");
#endif

  if ( qmc2ROMAlyzerActive )
    on_pushButtonAnalyze_clicked();
}

void ROMAlyzer::on_pushButtonAnalyze_clicked()
{
#ifdef QMC2_DEBUG
  qmc2MainWindow->log(QMC2_LOG_FRONTEND, "DEBUG: ROMAlyzer::on_pushButtonAnalyze_clicked()");
#endif

  if ( qmc2ReloadActive ) {
    qmc2MainWindow->log(QMC2_LOG_FRONTEND, tr("please wait for reload to finish and try again"));
  } else if ( qmc2ROMAlyzerActive ) {
    // stop ROMAlyzer
    log(tr("stopping analysis"));
    qmc2StopParser = TRUE;
  } else if ( qmc2Gamelist->numGames > 0 ) {
    // start ROMAlyzer
    log(tr("starting analysis"));
    qmc2StopParser = FALSE;
    QTimer::singleShot(0, this, SLOT(analyze()));
  }
}

void ROMAlyzer::on_pushButtonPause_clicked()
{
#ifdef QMC2_DEBUG
  qmc2MainWindow->log(QMC2_LOG_FRONTEND, "DEBUG: ROMAlyzer::on_pushButtonPause_clicked()");
#endif

  qmc2ROMAlyzerPaused = !qmc2ROMAlyzerPaused;
  if ( qmc2ROMAlyzerPaused ) {
    log(tr("pausing analysis"));
    pushButtonPause->setEnabled(FALSE);
  } else {
    log(tr("resuming analysis"));
    pushButtonPause->setText(tr("&Pause"));
  }
}

void ROMAlyzer::on_pushButtonSearchForward_clicked()
{
#ifdef QMC2_DEBUG
  qmc2MainWindow->log(QMC2_LOG_FRONTEND, "DEBUG: ROMAlyzer::on_pushButtonSearchForward_clicked()");
#endif

  if ( !textBrowserLog->find(lineEditSearchString->text()) ) {
    lineEditSearchString->setEnabled(FALSE);
    QTimer::singleShot(QMC2_ROMALYZER_FLASH_TIME, this, SLOT(enableSearchEdit()));
  }
}

void ROMAlyzer::on_pushButtonSearchBackward_clicked()
{
#ifdef QMC2_DEBUG
  qmc2MainWindow->log(QMC2_LOG_FRONTEND, "DEBUG: ROMAlyzer::on_pushButtonSearchBackward_clicked()");
#endif

  if ( !textBrowserLog->find(lineEditSearchString->text(), QTextDocument::FindBackward) ) {
    lineEditSearchString->setEnabled(FALSE);
    QTimer::singleShot(QMC2_ROMALYZER_FLASH_TIME, this, SLOT(enableSearchEdit()));
  }
}

void ROMAlyzer::on_lineEditGames_textChanged(QString text)
{
#ifdef QMC2_DEBUG
  qmc2MainWindow->log(QMC2_LOG_FRONTEND, QString("DEBUG: ROMAlyzer::on_lineEditGames_textChanged(QString text = %1)").arg(text));
#endif

  if ( !qmc2ROMAlyzerActive )
    pushButtonAnalyze->setEnabled(!text.isEmpty());
}

void ROMAlyzer::animationTimeout()
{
#ifdef QMC2_DEBUG
  qmc2MainWindow->log(QMC2_LOG_FRONTEND, "DEBUG: ROMAlyzer::animationTimeout()");
#endif

  switch ( ++animSeq ) {
    case 0:
    case 1:
      pushButtonAnalyze->setIcon(QIcon(QString::fromUtf8(":/data/img/viewmag+.png")));
      break;

    case 2:
    case 3:
      pushButtonAnalyze->setIcon(QIcon(QString::fromUtf8(":/data/img/viewmag-.png")));
      break;

    default:
      break;
  }
  if ( animSeq > 2 )
      animSeq = -1;
}

void ROMAlyzer::closeEvent(QCloseEvent *e)
{
#ifdef QMC2_DEBUG
  qmc2MainWindow->log(QMC2_LOG_FRONTEND, QString("DEBUG: ROMAlyzer::closeEvent(QCloseEvent *e = %1)").arg((qulonglong)e));
#endif

  if ( e && qmc2ROMAlyzerActive )
    on_pushButtonAnalyze_clicked();

  // save settings
  qmc2Config->setValue(QMC2_FRONTEND_PREFIX + "ROMAlyzer/AppendReport", checkBoxAppendReport->isChecked());
  qmc2Config->setValue(QMC2_FRONTEND_PREFIX + "ROMAlyzer/ExpandFiles", checkBoxExpandFiles->isChecked());
  qmc2Config->setValue(QMC2_FRONTEND_PREFIX + "ROMAlyzer/ExpandChecksums", checkBoxExpandChecksums->isChecked());
  qmc2Config->setValue(QMC2_FRONTEND_PREFIX + "ROMAlyzer/AutoScroll", checkBoxAutoScroll->isChecked());
  qmc2Config->setValue(QMC2_FRONTEND_PREFIX + "ROMAlyzer/CalculateCRC", checkBoxCalculateCRC->isChecked());
  qmc2Config->setValue(QMC2_FRONTEND_PREFIX + "ROMAlyzer/CalculateSHA1", checkBoxCalculateSHA1->isChecked());
  qmc2Config->setValue(QMC2_FRONTEND_PREFIX + "ROMAlyzer/CalculateMD5", checkBoxCalculateMD5->isChecked());
  qmc2Config->setValue(QMC2_FRONTEND_PREFIX + "ROMAlyzer/SelectGame", checkBoxSelectGame->isChecked());
  qmc2Config->setValue(QMC2_FRONTEND_PREFIX + "ROMAlyzer/MaxFileSize", spinBoxMaxFileSize->value());
  qmc2Config->setValue(QMC2_FRONTEND_PREFIX + "ROMAlyzer/MaxLogSize", spinBoxMaxLogSize->value());
  if ( qmc2Config->value(QMC2_FRONTEND_PREFIX + "GUI/SaveLayout").toBool() ) {
    qmc2Config->setValue(QMC2_FRONTEND_PREFIX + "Layout/ROMAlyzer/ReportHeaderState", treeWidgetChecksums->header()->saveState());
    qmc2Config->setValue(QMC2_FRONTEND_PREFIX + "Layout/ROMAlyzer/AnalysisTab", tabWidgetAnalysis->currentIndex());
    if ( !qmc2CleaningUp ) {
      qmc2Config->setValue(QMC2_FRONTEND_PREFIX + "Layout/ROMAlyzer/Visible", FALSE);
      qmc2Config->setValue(QMC2_FRONTEND_PREFIX + "Layout/ROMAlyzer/Position", pos());
      qmc2Config->setValue(QMC2_FRONTEND_PREFIX + "Layout/ROMAlyzer/Size", size());
    }
  }

  if ( e )
    e->accept();
}

void ROMAlyzer::hideEvent(QHideEvent *e)
{
#ifdef QMC2_DEBUG
  qmc2MainWindow->log(QMC2_LOG_FRONTEND, QString("DEBUG: ROMAlyzer::hideEvent(QHideEvent *e = %1)").arg((qulonglong)e));
#endif

  closeEvent(NULL);

  if ( e )
    e->accept();
}

void ROMAlyzer::showEvent(QShowEvent *e)
{
#ifdef QMC2_DEBUG
  qmc2MainWindow->log(QMC2_LOG_FRONTEND, QString("DEBUG: ROMAlyzer::showEvent(QShowEvent *e = %1)").arg((qulonglong)e));
#endif

  // restore settings
  checkBoxAppendReport->setChecked(qmc2Config->value(QMC2_FRONTEND_PREFIX + "ROMAlyzer/AppendReport", FALSE).toBool());
  checkBoxExpandFiles->setChecked(qmc2Config->value(QMC2_FRONTEND_PREFIX + "ROMAlyzer/ExpandFiles", FALSE).toBool());
  checkBoxExpandChecksums->setChecked(qmc2Config->value(QMC2_FRONTEND_PREFIX + "ROMAlyzer/ExpandChecksums", FALSE).toBool());
  checkBoxAutoScroll->setChecked(qmc2Config->value(QMC2_FRONTEND_PREFIX + "ROMAlyzer/AutoScroll", TRUE).toBool());
  checkBoxCalculateCRC->setChecked(qmc2Config->value(QMC2_FRONTEND_PREFIX + "ROMAlyzer/CalculateCRC", TRUE).toBool());
  checkBoxCalculateSHA1->setChecked(qmc2Config->value(QMC2_FRONTEND_PREFIX + "ROMAlyzer/CalculateSHA1", TRUE).toBool());
  checkBoxCalculateMD5->setChecked(qmc2Config->value(QMC2_FRONTEND_PREFIX + "ROMAlyzer/CalculateMD5", TRUE).toBool());
  checkBoxSelectGame->setChecked(qmc2Config->value(QMC2_FRONTEND_PREFIX + "ROMAlyzer/SelectGame", TRUE).toBool());
  spinBoxMaxFileSize->setValue(qmc2Config->value(QMC2_FRONTEND_PREFIX + "ROMAlyzer/MaxFileSize", 0).toInt());
  spinBoxMaxLogSize->setValue(qmc2Config->value(QMC2_FRONTEND_PREFIX + "ROMAlyzer/MaxLogSize", 0).toInt());
  if ( qmc2Config->value(QMC2_FRONTEND_PREFIX + "GUI/SaveLayout").toBool() )
    qmc2Config->setValue(QMC2_FRONTEND_PREFIX + "Layout/ROMAlyzer/Visible", TRUE);

  if ( e )
    e->accept();
}

void ROMAlyzer::on_spinBoxMaxLogSize_valueChanged(int value)
{
#ifdef QMC2_DEBUG
  qmc2MainWindow->log(QMC2_LOG_FRONTEND, QString("DEBUG: ROMAlyzer::on_spinBoxMaxLogSize_valueChanged(int value = %1)").arg(value));
#endif

  textBrowserLog->document()->setMaximumBlockCount(spinBoxMaxLogSize->value());
}

void ROMAlyzer::moveEvent(QMoveEvent *e)
{
#ifdef QMC2_DEBUG
  qmc2MainWindow->log(QMC2_LOG_FRONTEND, QString("DEBUG: ROMAlyzer::moveEvent(QMoveEvent *e = %1)").arg((qulonglong)e));
#endif

  if ( !qmc2CleaningUp && !qmc2EarlyStartup )
    if ( qmc2Config->value(QMC2_FRONTEND_PREFIX + "GUI/SaveLayout").toBool() )
      qmc2Config->setValue(QMC2_FRONTEND_PREFIX + "Layout/ROMAlyzer/Position", pos());

  if ( e )
    e->accept();
}

void ROMAlyzer::resizeEvent(QResizeEvent *e)
{
#ifdef QMC2_DEBUG
  qmc2MainWindow->log(QMC2_LOG_FRONTEND, QString("DEBUG: ROMAlyzer::resizeEvent(QResizeEvent *e = %1)").arg((qulonglong)e));
#endif

  if ( !qmc2CleaningUp && !qmc2EarlyStartup )
    if ( qmc2Config->value(QMC2_FRONTEND_PREFIX + "GUI/SaveLayout").toBool() )
      qmc2Config->setValue(QMC2_FRONTEND_PREFIX + "Layout/ROMAlyzer/Size", size());

  if ( e )
    e->accept();
}

void ROMAlyzer::analyze()
{
#ifdef QMC2_DEBUG
  qmc2MainWindow->log(QMC2_LOG_FRONTEND, "DEBUG: ROMAlyzer::analyze()");
#endif

  qmc2ROMAlyzerActive = TRUE;

  QString myRomPath;
#if defined(QMC2_SDLMAME) || defined(QMC2_MAME)
  if ( qmc2Config->contains("MAME/Configuration/Global/rompath") )
    myRomPath = qmc2Config->value("MAME/Configuration/Global/rompath").toString();
  else
    myRomPath = "roms";
#elif defined(QMC2_SDLMESS) || defined(QMC2_MESS)
  if ( qmc2Config->contains("MESS/Configuration/Global/rompath") )
    myRomPath = qmc2Config->value("MESS/Configuration/Global/rompath").toString();
  else
    myRomPath = "roms";
#endif

  myRomPath = myRomPath.replace("~", QDir::homePath());
  myRomPath = myRomPath.replace("$HOME", QDir::homePath());
  romPaths = myRomPath.split(";");

  int i;
  QStringList analyzerList;
  QStringList patternList = lineEditGames->text().simplified().split(" ");

  if ( !checkBoxAppendReport->isChecked() ) {
    treeWidgetChecksums->clear();
    textBrowserLog->clear();
  }
  qmc2ROMAlyzerPaused = FALSE;
  animSeq = -1;
  animationTimeout();
  animTimer.start(QMC2_ANIMATION_TIMEOUT);
  pushButtonAnalyze->setText(tr("&Stop"));
  pushButtonPause->setVisible(TRUE);
  pushButtonPause->setEnabled(TRUE);
  pushButtonPause->setText(tr("&Pause"));
  lineEditGames->setEnabled(FALSE);
  QTime analysisTimer, elapsedTime;
  analysisTimer.start();
  log(tr("analysis started"));
#if defined(QMC2_SDLMAME) || defined(QMC2_MAME)
  log(tr("determining list of games to analyze"));
#elif defined(QMC2_SDLMESS) || defined(QMC2_MESS)
  log(tr("determining list of machines to analyze"));
#endif
  if ( patternList.count() == 1 ) {
    if ( qmc2GamelistItemMap.contains(patternList[0]) ) {
      // special case for exactly _one_ matching game - no need to search
      analyzerList << patternList[0];
    }
  }

  if ( analyzerList.empty() ) {
    // determine list of games to analyze
#if defined(QMC2_SDLMAME) || defined(QMC2_MAME)
    labelStatus->setText(tr("Searching games"));
#elif defined(QMC2_SDLMESS) || defined(QMC2_MESS)
    labelStatus->setText(tr("Searching machines"));
#endif
    progressBar->setRange(0, qmc2Gamelist->numGames);
    progressBar->reset();
    QMapIterator<QString, QTreeWidgetItem *> it(qmc2GamelistItemMap);
    i = 0;
    while ( it.hasNext() && !qmc2StopParser ) {
      qApp->processEvents();
      it.next();
      progressBar->setValue(i++);
      foreach (QString pattern, patternList) {
        QRegExp regexp(pattern, Qt::CaseSensitive, QRegExp::Wildcard);
        if ( regexp.exactMatch(it.key()) )
          if ( !analyzerList.contains(it.key()) )
            analyzerList << it.key();
      }
    }
    progressBar->reset();
    labelStatus->setText(tr("Idle"));
  }

  if ( !qmc2StopParser ) {
#if defined(QMC2_SDLMAME) || defined(QMC2_MAME)
    log(tr("done (determining list of games to analyze)"));
    log(tr("%n game(s) to analyze", "", analyzerList.count()));
#elif defined(QMC2_SDLMESS) || defined(QMC2_MESS)
    log(tr("done (determining list of machines to analyze)"));
    log(tr("%n machine(s) to analyze", "", analyzerList.count()));
#endif

    i = 0;
    foreach (QString gameName, analyzerList) {
      // wait if paused...
      for (quint64 waitCounter = 0; qmc2ROMAlyzerPaused && !qmc2StopParser; waitCounter++) {
        if ( waitCounter == 0 ) {
          log(tr("analysis paused"));
          pushButtonPause->setText(tr("&Resume"));
          pushButtonPause->setEnabled(TRUE);
          progressBar->reset();
          labelStatus->setText(tr("Paused"));
        }
        QTest::qWait(QMC2_ROMALYZER_PAUSE_TIMEOUT);
      }

      bool filesSkipped = FALSE;
      bool filesUnknown = FALSE;
      bool filesError = FALSE;
      bool filesNotFound = FALSE;

      if ( qmc2StopParser )
        break;

      // analyze game
      log(tr("analyzing '%1'").arg(gameName));
      labelStatus->setText(tr("Analyzing '%1'").arg(gameName) + QString(" - %1").arg(analyzerList.count() - i));

      // step 1: retrieve XML data, insert item with game name
      QTreeWidgetItem *item = new QTreeWidgetItem(treeWidgetChecksums);
      item->setText(QMC2_ROMALYZER_COLUMN_GAME, gameName);
      QString xmlBuffer = getXmlData(gameName);

      if ( qmc2StopParser )
        break;

      // step 2: parse XML data, insert ROMs / CHDs and checksums as they _should_ be
      log(tr("parsing XML data for '%1'").arg(gameName));
      QXmlInputSource xmlInputSource;
      xmlInputSource.setData(xmlBuffer);
      ROMAlyzerXmlHandler xmlHandler(item, checkBoxExpandFiles->isChecked(), checkBoxAutoScroll->isChecked());
      QXmlSimpleReader xmlReader;
      xmlReader.setContentHandler(&xmlHandler);
      if ( xmlReader.parse(xmlInputSource) )
        log(tr("done (parsing XML data for '%1')").arg(gameName));
      else
        log(tr("error (parsing XML data for '%1')").arg(gameName));
      if ( qmc2StopParser )
        break;

      // step 3: check file status of ROMs and CHDs, recalculate checksums
      log(tr("checking %n file(s) for '%1'", "", xmlHandler.fileCounter).arg(gameName));
      progressBar->reset();
      progressBar->setRange(0, xmlHandler.fileCounter);
      int fileCounter;
      int notFoundCounter = 0;
      bool gameOkay = TRUE;
      for (fileCounter = 0; fileCounter < xmlHandler.fileCounter && !qmc2StopParser; fileCounter++) {
        progressBar->setValue(fileCounter + 1);
        qApp->processEvents();
        QByteArray data;
        bool zipped = FALSE;
        bool merged = FALSE;
        QTreeWidgetItem *childItem = xmlHandler.childItems.at(fileCounter);
        QTreeWidgetItem *parentItem = xmlHandler.parentItem;
        QString sha1Calculated, md5Calculated;
        QString effectiveFile = getEffectiveFile(childItem, gameName, 
                                                 childItem->text(QMC2_ROMALYZER_COLUMN_GAME),
                                                 childItem->text(QMC2_ROMALYZER_COLUMN_CRC),
                                                 parentItem->text(QMC2_ROMALYZER_COLUMN_MERGE),
                                                 childItem->text(QMC2_ROMALYZER_COLUMN_MERGE),
                                                 childItem->text(QMC2_ROMALYZER_COLUMN_TYPE),
                                                 &data, &sha1Calculated, &md5Calculated,
                                                 &zipped, &merged, fileCounter); 
        if ( qmc2StopParser )
          continue;

        if ( effectiveFile.isEmpty() ) {
          childItem->setText(QMC2_ROMALYZER_COLUMN_FILESTATUS, tr("not found"));
          childItem->setForeground(QMC2_ROMALYZER_COLUMN_FILESTATUS, xmlHandler.greyBrush);
          childItem->setIcon(QMC2_ROMALYZER_COLUMN_GAME, QIcon(QString::fromUtf8(":/data/img/remove.png")));
          notFoundCounter++;
        } else {
          QString fileStatus;
          bool somethingsWrong = FALSE;

          if ( effectiveFile != QMC2_ROMALYZER_FILE_NOT_FOUND ) {
            if ( zipped )
              childItem->setIcon(QMC2_ROMALYZER_COLUMN_GAME, QIcon(QString::fromUtf8(":/data/img/zip.png")));
            else if ( childItem->text(QMC2_ROMALYZER_COLUMN_TYPE) == QObject::tr("CHD") )
              childItem->setIcon(QMC2_ROMALYZER_COLUMN_GAME, QIcon(QString::fromUtf8(":/data/img/disk2.png")));
            else
              childItem->setIcon(QMC2_ROMALYZER_COLUMN_GAME, QIcon(QString::fromUtf8(":/data/img/fileopen.png")));
          }

          if ( effectiveFile == QMC2_ROMALYZER_FILE_TOO_BIG ) {
            fileStatus = tr("skipped");
            filesSkipped = TRUE;
          } else if ( effectiveFile == QMC2_ROMALYZER_FILE_NOT_SUPPORTED ) {
            fileStatus = tr("skipped");
            filesUnknown = TRUE;
          } else if ( effectiveFile == QMC2_ROMALYZER_FILE_ERROR ) {
            fileStatus = tr("error");
            childItem->setIcon(QMC2_ROMALYZER_COLUMN_GAME, QIcon(QString::fromUtf8(":/data/img/warning.png")));
            filesError = TRUE;
          } else if ( effectiveFile == QMC2_ROMALYZER_FILE_NOT_FOUND ) {
            fileStatus = tr("not found");
            childItem->setIcon(QMC2_ROMALYZER_COLUMN_GAME, QIcon(QString::fromUtf8(":/data/img/remove.png")));
            filesNotFound = TRUE;
            notFoundCounter++;
          } else {
            QTreeWidgetItem *fileItem = new QTreeWidgetItem(childItem);
            fileItem->setText(QMC2_ROMALYZER_COLUMN_GAME, tr("Checksums"));
            childItem->setExpanded(FALSE);

            // Size
            QString sizeStr = childItem->text(QMC2_ROMALYZER_COLUMN_SIZE);
            if ( !sizeStr.isEmpty() ){
              fileItem->setText(QMC2_ROMALYZER_COLUMN_SIZE, QString::number(data.size()));
              fileStatus += tr("SIZE ");
              if ( data.size() != sizeStr.toLongLong() ) {
                somethingsWrong = TRUE;
                fileItem->setForeground(QMC2_ROMALYZER_COLUMN_SIZE, xmlHandler.redBrush);
              }
              pushButtonPause->setIcon(QIcon(QString::fromUtf8(":/data/img/time.png")));
              qApp->processEvents();
            }

            // CRC
            QString crcStr = childItem->text(QMC2_ROMALYZER_COLUMN_CRC);
            if ( !crcStr.isEmpty() && checkBoxCalculateCRC->isChecked() ) {
              ulong crc = crc32(0, NULL, 0);
              crc = crc32(crc, (const Bytef *)data.data(), data.size());
              fileItem->setText(QMC2_ROMALYZER_COLUMN_CRC, QString::number(crc, 16).rightJustified(8, '0'));
              fileStatus += tr("CRC ");
              if ( crc != crcStr.toULongLong(0, 16) ) {
                somethingsWrong = TRUE;
                fileItem->setForeground(QMC2_ROMALYZER_COLUMN_CRC, xmlHandler.redBrush);
              }
              qApp->processEvents();
            }

            // SHA1
            QString sha1Str = childItem->text(QMC2_ROMALYZER_COLUMN_SHA1);
            if ( !sha1Str.isEmpty() && checkBoxCalculateSHA1->isChecked() ) {
              fileItem->setText(QMC2_ROMALYZER_COLUMN_SHA1, sha1Calculated);
              fileStatus += tr("SHA1 ");
              if ( sha1Str != sha1Calculated ) {
                somethingsWrong = TRUE;
                fileItem->setForeground(QMC2_ROMALYZER_COLUMN_SHA1, xmlHandler.redBrush);
              }
              qApp->processEvents();
            }

            // MD5
            QString md5Str = childItem->text(QMC2_ROMALYZER_COLUMN_MD5);
            if ( !md5Str.isEmpty() && checkBoxCalculateMD5->isChecked() ) {
              fileItem->setText(QMC2_ROMALYZER_COLUMN_MD5, md5Calculated);
              fileStatus += tr("MD5 ");
              if ( md5Str != md5Calculated ) {
                somethingsWrong = TRUE;
                fileItem->setForeground(QMC2_ROMALYZER_COLUMN_MD5, xmlHandler.redBrush);
              }
              qApp->processEvents();
            }
          }

          childItem->setText(QMC2_ROMALYZER_COLUMN_FILESTATUS, fileStatus);
          if ( somethingsWrong ) {
            gameOkay = FALSE;
            childItem->setForeground(QMC2_ROMALYZER_COLUMN_FILESTATUS, xmlHandler.redBrush);
            childItem->setIcon(QMC2_ROMALYZER_COLUMN_GAME, QIcon(QString::fromUtf8(":/data/img/warning.png")));
            log(tr("WARNING: ROM file '%1' loaded from '%2' has incorrect checksums").arg(childItem->text(QMC2_ROMALYZER_COLUMN_GAME), effectiveFile));
          } else {
            if ( fileStatus == tr("skipped") )
              childItem->setForeground(QMC2_ROMALYZER_COLUMN_FILESTATUS, xmlHandler.blueBrush);
            else if ( fileStatus == tr("not found") )
              childItem->setForeground(QMC2_ROMALYZER_COLUMN_FILESTATUS, xmlHandler.greyBrush);
            else
              childItem->setForeground(QMC2_ROMALYZER_COLUMN_FILESTATUS, xmlHandler.greenBrush);
          }

          if ( checkBoxExpandChecksums->isChecked() )
            childItem->setExpanded(TRUE);

          qApp->processEvents();
        }
      }
      if ( xmlHandler.fileCounter == 0 )
        progressBar->setRange(0, 1);
      progressBar->reset();
      qApp->processEvents();

      if ( qmc2StopParser ) 
        log(tr("interrupted (checking %n file(s) for '%1')", "", xmlHandler.fileCounter).arg(gameName));
      else {
        gameOkay |= filesError;
        filesSkipped |= filesUnknown;
        if ( gameOkay ) {
          if ( notFoundCounter == xmlHandler.fileCounter ) {
            xmlHandler.parentItem->setText(QMC2_ROMALYZER_COLUMN_FILESTATUS, tr("not found"));
            xmlHandler.parentItem->setForeground(QMC2_ROMALYZER_COLUMN_FILESTATUS, xmlHandler.greyBrush);
          } else if ( notFoundCounter > 0 ) {
            if ( filesSkipped )
              xmlHandler.parentItem->setText(QMC2_ROMALYZER_COLUMN_FILESTATUS, tr("good / not found / skipped"));
            else
              xmlHandler.parentItem->setText(QMC2_ROMALYZER_COLUMN_FILESTATUS, tr("good / not found"));
            xmlHandler.parentItem->setForeground(QMC2_ROMALYZER_COLUMN_FILESTATUS, xmlHandler.brownBrush);
          } else {
            if ( filesSkipped )
              xmlHandler.parentItem->setText(QMC2_ROMALYZER_COLUMN_FILESTATUS, tr("good / skipped"));
            else
              xmlHandler.parentItem->setText(QMC2_ROMALYZER_COLUMN_FILESTATUS, tr("good"));
            xmlHandler.parentItem->setForeground(QMC2_ROMALYZER_COLUMN_FILESTATUS, xmlHandler.greenBrush);
          }
        } else {
          if ( notFoundCounter > 0 ) {
            if ( filesSkipped )
              xmlHandler.parentItem->setText(QMC2_ROMALYZER_COLUMN_FILESTATUS, tr("bad / not found / skipped"));
            else
              xmlHandler.parentItem->setText(QMC2_ROMALYZER_COLUMN_FILESTATUS, tr("bad / not found"));
            xmlHandler.parentItem->setForeground(QMC2_ROMALYZER_COLUMN_FILESTATUS, xmlHandler.redBrush);
          } else {
            if ( filesSkipped )
              xmlHandler.parentItem->setText(QMC2_ROMALYZER_COLUMN_FILESTATUS, tr("bad / skipped"));
            else
              xmlHandler.parentItem->setText(QMC2_ROMALYZER_COLUMN_FILESTATUS, tr("bad"));
            xmlHandler.parentItem->setForeground(QMC2_ROMALYZER_COLUMN_FILESTATUS, xmlHandler.redBrush);
          }
        }
        log(tr("done (checking %n file(s) for '%1')", "", xmlHandler.fileCounter).arg(gameName));
      }
      if ( qmc2StopParser )
        break;
      
      i++;
      log(tr("done (analyzing '%1')").arg(gameName));
#if defined(QMC2_SDLMAME) || defined(QMC2_MAME)
      log(tr("%n game(s) left", "", analyzerList.count() - i));
#elif defined(QMC2_SDLMESS) || defined(QMC2_MESS)
      log(tr("%n machine(s) left", "", analyzerList.count() - i));
#endif
    }
  }

  animTimer.stop();
  pushButtonAnalyze->setText(tr("&Analyze"));
  pushButtonPause->setVisible(FALSE);
  pushButtonAnalyze->setIcon(QIcon(QString::fromUtf8(":/data/img/find.png")));
  lineEditGames->setEnabled(TRUE);

  progressBar->reset();
  labelStatus->setText(tr("Idle"));
  qApp->processEvents();
  elapsedTime = elapsedTime.addMSecs(analysisTimer.elapsed());
  log(tr("analysis ended") + " - " + tr("elapsed time = %1").arg(elapsedTime.toString("hh:mm:ss.zzz")));
  qmc2ROMAlyzerActive = FALSE;
}

QString &ROMAlyzer::getXmlData(QString gameName)
{
#ifdef QMC2_DEBUG
  qmc2MainWindow->log(QMC2_LOG_FRONTEND, QString("DEBUG: ROMAlyzer::getXmlData(QString gameName = %1)").arg(gameName));
#endif

  static QString xmlBuffer;

  xmlBuffer.clear();
  int i = 0;
#if defined(QMC2_SDLMAME) || defined(QMC2_MAME)
  QString s = "<game name=\"" + gameName + "\"";
  while ( !qmc2Gamelist->xmlLines[i].contains(s) ) i++;
  xmlBuffer = "<?xml version=\"1.0\"?>\n";
  while ( !qmc2Gamelist->xmlLines[i].contains("</game>") )
    xmlBuffer += qmc2Gamelist->xmlLines[i++].simplified() + "\n";
  xmlBuffer += "</game>\n";
#elif defined(QMC2_SDLMESS) || defined(QMC2_MESS)
  QString s = "<machine name=\"" + gameName + "\"";
  while ( !qmc2Gamelist->xmlLines[i].contains(s) ) i++;
  xmlBuffer = "<?xml version=\"1.0\"?>\n";
  while ( !qmc2Gamelist->xmlLines[i].contains("</machine>") )
    xmlBuffer += qmc2Gamelist->xmlLines[i++].simplified() + "\n";
  xmlBuffer += "</machine>\n";
#endif

  return xmlBuffer;
}

QString &ROMAlyzer::getEffectiveFile(QTreeWidgetItem *myItem, QString gameName, QString fileName, QString wantedCRC, QString merge, QString mergeFile, QString type, QByteArray *fileData, QString *sha1Str, QString *md5Str, bool *isZipped, bool *mergeUsed, int fileCounter)
{
#ifdef QMC2_DEBUG
  qmc2MainWindow->log(QMC2_LOG_FRONTEND, QString("DEBUG: ROMAlyzer::getEffectiveFile(QTreeWidgetItem *myItem = %1, QString gameName = %2, QString fileName = %3, QString wantedCRC = %4, QString merge = %5, QString mergeFile = %6, QString type = %7, QByteArray *fileData = ..., QString *sha1Str = ..., QString md5Str = ..., bool *isZipped = ..., bool *mergeUsed = ..., int fileCounter = ...)")
                     .arg((qulonglong)myItem).arg(gameName).arg(fileName).arg(wantedCRC).arg(merge).arg(mergeFile).arg(type));
#endif

  static QCryptographicHash sha1Hash(QCryptographicHash::Sha1);
  static QCryptographicHash md5Hash(QCryptographicHash::Md5);
  static QString effectiveFile;
  static char buffer[MAX(QMC2_ROMALYZER_ZIP_BUFFER_SIZE, QMC2_ROMALYZER_FILE_BUFFER_SIZE)];

  effectiveFile.clear();
  fileData->clear();

  bool calcMD5 = checkBoxCalculateMD5->isChecked();
  bool calcSHA1 = checkBoxCalculateSHA1->isChecked();
  bool isCHD = type == tr("CHD");
  bool sizeLimited = spinBoxMaxFileSize->value() > 0;
  QProgressBar *progressWidget;
  QWidget *oldItemWidget;
  qint64 totalSize, myProgress, sizeLeft, len;

  // search for file in ROM paths (first search for "game/file", then "file" in "game.zip"), load file data when found
  foreach (QString romPath, romPaths) {
    progressWidget = NULL;
    oldItemWidget = NULL;
    QString filePath(romPath + "/" + gameName + "/" + fileName);
    if ( isCHD )
      filePath += ".chd";
    if ( QFile::exists(filePath) ) {
      QFileInfo fi(filePath);
      if ( fi.isReadable() ) {
        totalSize = fi.size();
        // load data from normal file
        if ( sizeLimited ) {
          if ( totalSize > (qint64) spinBoxMaxFileSize->value() * QMC2_ONE_MEGABYTE ) {
            log(tr("size of '%1' is greater than allowed maximum -- skipped").arg(filePath));
            *isZipped = FALSE;
            progressBarFileIO->reset();
            effectiveFile = QMC2_ROMALYZER_FILE_TOO_BIG;
            continue;
          }
        }
        QFile romFile(filePath);
        if ( romFile.open(QIODevice::ReadOnly) ) {
          log(tr("loading '%1'%2").arg(filePath).arg(*mergeUsed ? tr(" (merged)") : ""));
          progressBarFileIO->setRange(0, totalSize);
          progressBarFileIO->setValue(0);
          if ( totalSize > QMC2_ROMALYZER_PROGRESS_THRESHOLD && !isCHD ) {
            progressWidget = new QProgressBar(0);
            progressWidget->setRange(0, totalSize);
            progressWidget->setValue(0);
            oldItemWidget = treeWidgetChecksums->itemWidget(myItem, QMC2_ROMALYZER_COLUMN_FILESTATUS);
            treeWidgetChecksums->setItemWidget(myItem, QMC2_ROMALYZER_COLUMN_FILESTATUS, progressWidget);
          }
          sizeLeft = totalSize;
          if ( calcSHA1 )
            sha1Hash.reset();
          if ( calcMD5 )
            md5Hash.reset();
          if ( isCHD ) {
            bool chdVersionOkay = TRUE;
            if ( (len = romFile.read(buffer, QMC2_CHD_HEADER_V3_LENGTH)) > 0 ) {
              log(tr("CHD header information:"));
              QByteArray chdTag(buffer + QMC2_CHD_HEADER_TAG_OFFSET, QMC2_CHD_HEADER_TAG_LENGTH);
              log(tr("  tag: %1").arg(chdTag.constData()));
              quint32 chdVersion = QMC2_TO_UINT32(buffer + QMC2_CHD_HEADER_VERSION_OFFSET);
              log(tr("  version: %1").arg(chdVersion));
              if ( chdVersion == 3 ) {
                quint32 chdCompression = QMC2_TO_UINT32(buffer + QMC2_CHD_HEADER_COMPRESSION_OFFSET);
                log(tr("  compression: %1").arg(chdCompressionTypes[chdCompression]));
		quint32 chdFlags = QMC2_TO_UINT32(buffer + QMC2_CHD_HEADER_FLAGS_OFFSET);
                log(tr("  flags: %1, %2").arg(chdFlags & QMC2_CHD_HEADER_FLAG_HASPARENT ? tr("has parent") : tr("no parent")).arg(chdFlags & QMC2_CHD_HEADER_FLAG_ALLOWSWRITES ? tr("allows writes") : tr("read only")));
                quint32 chdTotalHunks = QMC2_TO_UINT32(buffer + QMC2_CHD_HEADER_V3_TOTALHUNKS_OFFSET);
                log(tr("  number of total hunks: %1").arg(chdTotalHunks));
                quint32 chdHunkBytes = QMC2_TO_UINT32(buffer + QMC2_CHD_HEADER_V3_HUNKBYTES_OFFSET);
                log(tr("  number of bytes per hunk: %1").arg(chdHunkBytes));
                quint64 chdLogicalBytes = QMC2_TO_UINT64(buffer + QMC2_CHD_HEADER_V3_LOGICALBYTES_OFFSET);
                log(tr("  logical size: %1 bytes (%2)").arg(chdLogicalBytes).arg(humanReadable(chdLogicalBytes)));
                QByteArray md5Data((const char *)(buffer + QMC2_CHD_HEADER_V3_MD5_OFFSET), QMC2_CHD_HEADER_V3_MD5_LENGTH);
                log(tr("  MD5 checksum: %1").arg(QString(md5Data.toHex())));
                QByteArray sha1Data((const char *)(buffer + QMC2_CHD_HEADER_V3_SHA1_OFFSET), QMC2_CHD_HEADER_V3_SHA1_LENGTH);
                log(tr("  SHA1 checksum: %1").arg(QString(sha1Data.toHex())));
                if ( chdFlags & QMC2_CHD_HEADER_FLAG_HASPARENT ) {
                  QByteArray md5Data((const char *)(buffer + QMC2_CHD_HEADER_V3_PARENTMD5_OFFSET), QMC2_CHD_HEADER_V3_PARENTMD5_LENGTH);
                  log(tr("  parent CHD's MD5 checksum: %1").arg(QString(md5Data.toHex())));
                  QByteArray sha1Data((const char *)(buffer + QMC2_CHD_HEADER_V3_PARENTSHA1_OFFSET), QMC2_CHD_HEADER_V3_PARENTSHA1_LENGTH);
                  log(tr("  parent CHD's SHA1 checksum: %1").arg(QString(sha1Data.toHex())));
                }
              } else if ( chdVersion == 4 ) {
                quint32 chdCompression = QMC2_TO_UINT32(buffer + QMC2_CHD_HEADER_COMPRESSION_OFFSET);
                log(tr("  compression: %1").arg(chdCompressionTypes[chdCompression]));
		quint32 chdFlags = QMC2_TO_UINT32(buffer + QMC2_CHD_HEADER_FLAGS_OFFSET);
                log(tr("  flags: %1, %2").arg(chdFlags & QMC2_CHD_HEADER_FLAG_HASPARENT ? tr("has parent") : tr("no parent")).arg(chdFlags & QMC2_CHD_HEADER_FLAG_ALLOWSWRITES ? tr("allows writes") : tr("read only")));
                quint32 chdTotalHunks = QMC2_TO_UINT32(buffer + QMC2_CHD_HEADER_V4_TOTALHUNKS_OFFSET);
                log(tr("  number of total hunks: %1").arg(chdTotalHunks));
                quint32 chdHunkBytes = QMC2_TO_UINT32(buffer + QMC2_CHD_HEADER_V4_HUNKBYTES_OFFSET);
                log(tr("  number of bytes per hunk: %1").arg(chdHunkBytes));
                quint64 chdLogicalBytes = QMC2_TO_UINT64(buffer + QMC2_CHD_HEADER_V4_LOGICALBYTES_OFFSET);
                log(tr("  logical size: %1 bytes (%2)").arg(chdLogicalBytes).arg(humanReadable(chdLogicalBytes)));
                QByteArray sha1Data((const char *)(buffer + QMC2_CHD_HEADER_V4_SHA1_OFFSET), QMC2_CHD_HEADER_V4_SHA1_LENGTH);
                log(tr("  SHA1 checksum: %1").arg(QString(sha1Data.toHex())));
                if ( chdFlags & QMC2_CHD_HEADER_FLAG_HASPARENT ) {
                  QByteArray sha1Data((const char *)(buffer + QMC2_CHD_HEADER_V4_PARENTSHA1_OFFSET), QMC2_CHD_HEADER_V4_PARENTSHA1_LENGTH);
                  log(tr("  parent CHD's SHA1 checksum: %1").arg(QString(sha1Data.toHex())));
                }
                QByteArray rawsha1Data((const char *)(buffer + QMC2_CHD_HEADER_V4_RAWSHA1_OFFSET), QMC2_CHD_HEADER_V4_SHA1_LENGTH);
                log(tr("  raw SHA1 checksum: %1").arg(QString(rawsha1Data.toHex())));
              } else {
                log(tr("only CHD v3 and v4 supported -- skipped"));
                effectiveFile = QMC2_ROMALYZER_FILE_NOT_SUPPORTED;
                chdVersionOkay = FALSE;
                romFile.close();
                continue;
              }
              if ( chdVersionOkay ) {
                if ( calcSHA1 || calcMD5 ) {
                  switch ( chdVersion ) {
                    case 3:
                      log(tr("CHD loading not supported for CHD v3, using header checksums instead"));
                      effectiveFile = QMC2_ROMALYZER_FILE_NOT_SUPPORTED;
                      if ( calcSHA1 ) {
                        QByteArray sha1Data((const char *)(buffer + QMC2_CHD_HEADER_V3_SHA1_OFFSET), QMC2_CHD_HEADER_V3_SHA1_LENGTH);
                        *sha1Str = QString(sha1Data.toHex());
                      }
                      if ( calcMD5 ) {
                        QByteArray md5Data((const char *)(buffer + QMC2_CHD_HEADER_V3_MD5_OFFSET), QMC2_CHD_HEADER_V3_MD5_LENGTH);
                        *md5Str = QString(md5Data.toHex());
                      }
                      break;

                    case 4:
                      log(tr("CHD loading not supported for CHD v4, using header checksums instead"));
                      effectiveFile = QMC2_ROMALYZER_FILE_NOT_SUPPORTED;
                      if ( calcSHA1 ) {
                        QByteArray sha1Data((const char *)(buffer + QMC2_CHD_HEADER_V4_SHA1_OFFSET), QMC2_CHD_HEADER_V4_SHA1_LENGTH);
                        *sha1Str = QString(sha1Data.toHex());
                      }
                      break;
                  }
                }
              }
            } else {
              log(tr("WARNING: can't read CHD header information"));
              effectiveFile = QMC2_ROMALYZER_FILE_ERROR;
            }
          } else {
            while ( (len = romFile.read(buffer, QMC2_ROMALYZER_FILE_BUFFER_SIZE)) > 0 && !qmc2StopParser ) {
              QByteArray readData((const char *)buffer, len);
              fileData->append(readData);
              if ( calcSHA1 )
                sha1Hash.addData(readData);
              if ( calcMD5 )
                md5Hash.addData(readData);
              sizeLeft -= len;
              myProgress = totalSize - sizeLeft;
              progressBarFileIO->setValue(myProgress);
              if ( totalSize > QMC2_ROMALYZER_PROGRESS_THRESHOLD )
                if ( progressWidget ) progressWidget->setValue(myProgress);
              qApp->processEvents();
            }
            if ( calcSHA1 )
              *sha1Str = sha1Hash.result().toHex();
            if ( calcMD5 )
              *md5Str = md5Hash.result().toHex();
          }
          romFile.close();
          effectiveFile = filePath;
          *isZipped = FALSE;
          progressBarFileIO->reset();
          if ( totalSize > QMC2_ROMALYZER_PROGRESS_THRESHOLD ) {
            if ( progressWidget ) progressWidget->reset();
            if ( oldItemWidget ) treeWidgetChecksums->setItemWidget(myItem, QMC2_ROMALYZER_COLUMN_FILESTATUS, oldItemWidget);
            if ( progressWidget ) delete progressWidget;
          }
        } else {
          log(tr("WARNING: found '%1' but can't read from it although permissions seem okay - check file integrity").arg(filePath));
        }
      } else
        log(tr("WARNING: found '%1' but can't read from it - check permission").arg(filePath));
    } else {
      if ( isCHD ) {
        log(tr("WARNING: CHD file '%1' not found").arg(filePath));
        if ( mergeFile.isEmpty() && merge.isEmpty() )
          effectiveFile = QMC2_ROMALYZER_FILE_NOT_FOUND;
      }
    }

    if ( effectiveFile.isEmpty() && !qmc2StopParser ) {
      filePath = romPath + "/" + gameName + ".zip";
      if ( QFile::exists(filePath) ) {
        QFileInfo fi(filePath);
        if ( fi.isReadable() ) {
          // load data from ZIP
          unzFile zipFile = unzOpen((const char *)filePath.toAscii());
          if ( zipFile ) {
            // identify file by CRC!
            unz_file_info zipInfo;
            QMap<uLong, QString> crcIdentMap;
            do {
              if ( unzGetCurrentFileInfo(zipFile, &zipInfo, buffer, QMC2_ROMALYZER_ZIP_BUFFER_SIZE, 0, 0, 0, 0) == UNZ_OK )
                crcIdentMap[zipInfo.crc] = QString((const char *)buffer);
            } while ( unzGoToNextFile(zipFile) == UNZ_OK );
            unzGoToFirstFile(zipFile);
            QString fn = "QMC2_DUMMY_FILENAME";
            uLong ulCRC = wantedCRC.toULong(0, 16);
            if ( crcIdentMap.contains(ulCRC) )
              fn = crcIdentMap[ulCRC];
            else if ( mergeFile.isEmpty() ) {
              if ( !isCHD )
                log(tr("WARNING: unable to identify '%1' from '%2' by CRC").arg(fileName).arg(filePath));
              fn = fileName;
            }

            if ( unzLocateFile(zipFile, (const char *)fn.toAscii(), 2) == UNZ_OK ) { // NOT case-sensitive filename compare!
              totalSize = 0;
              if ( unzGetCurrentFileInfo(zipFile, &zipInfo, 0, 0, 0, 0, 0, 0) == UNZ_OK ) 
                totalSize = zipInfo.uncompressed_size;
              if ( sizeLimited ) {
                if ( totalSize > (qint64) spinBoxMaxFileSize->value() * QMC2_ONE_MEGABYTE ) {
                  log(tr("size of '%1' from '%2' is greater than allowed maximum -- skipped").arg(fn).arg(filePath));
                  *isZipped = TRUE;
                  progressBarFileIO->reset();
                  effectiveFile = QMC2_ROMALYZER_FILE_TOO_BIG;
                  continue;
                }
              }
              if ( unzOpenCurrentFile(zipFile) == UNZ_OK ) {
                log(tr("loading '%1' from '%2'%3").arg(fn).arg(filePath).arg(*mergeUsed ? tr(" (merged)") : ""));
                progressBarFileIO->setRange(0, totalSize);
                progressBarFileIO->setValue(0);
                if ( totalSize > QMC2_ROMALYZER_PROGRESS_THRESHOLD ) {
                  progressWidget = new QProgressBar(0);
                  progressWidget->setRange(0, totalSize);
                  progressWidget->setValue(0);
                  oldItemWidget = treeWidgetChecksums->itemWidget(myItem, QMC2_ROMALYZER_COLUMN_FILESTATUS);
                  treeWidgetChecksums->setItemWidget(myItem, QMC2_ROMALYZER_COLUMN_FILESTATUS, progressWidget);
                }
                sizeLeft = totalSize;
                if ( calcSHA1 )
                  sha1Hash.reset();
                if ( calcMD5 )
                  md5Hash.reset();
                while ( (len = unzReadCurrentFile(zipFile, buffer, QMC2_ROMALYZER_ZIP_BUFFER_SIZE)) > 0 && !qmc2StopParser ) {
                  QByteArray readData((const char *)buffer, len);
                  if ( !isCHD )
                    fileData->append(readData);
                  if ( calcSHA1 )
                    sha1Hash.addData(readData);
                  if ( calcMD5 )
                    md5Hash.addData(readData);
                  sizeLeft -= len;
                  myProgress = totalSize - sizeLeft;
                  progressBarFileIO->setValue(myProgress);
                  if ( totalSize > QMC2_ROMALYZER_PROGRESS_THRESHOLD )
                    progressWidget->setValue(myProgress);
                  qApp->processEvents();
                }
                unzCloseCurrentFile(zipFile);
                effectiveFile = filePath;
                *isZipped = TRUE;
                if ( calcSHA1 )
                  *sha1Str = sha1Hash.result().toHex();
                if ( calcMD5 )
                  *md5Str = md5Hash.result().toHex();
                progressBarFileIO->reset();
                if ( totalSize > QMC2_ROMALYZER_PROGRESS_THRESHOLD ) {
                  progressWidget->reset();
                  treeWidgetChecksums->setItemWidget(myItem, QMC2_ROMALYZER_COLUMN_FILESTATUS, oldItemWidget);
                  delete progressWidget;
                }
              } else
                log(tr("WARNING: unable to decompress '%1' from '%2' - check file integrity").arg(fn).arg(filePath));
            }
            unzClose(zipFile);
          } else
            log(tr("WARNING: found '%1' but can't open it for decompression - check file integrity").arg(filePath));
        } else
          log(tr("WARNING: found '%1' but can't read from it - check permission").arg(gameName));
      }
    }

    if ( !effectiveFile.isEmpty() || qmc2StopParser )
      break;
  }

  // try merges, if applicable...
  if ( effectiveFile.isEmpty() && !qmc2StopParser ) {
    if ( mergeFile.isEmpty() && !merge.isEmpty() ) {
      // romof is set, but the merge's file name is missing... use the same file name for the merge
      mergeFile = fileName;
    }
    if ( !mergeFile.isEmpty() && !qmc2StopParser ) {
      // romof is set, and the merge's file name is given
      QString nextMerge = getXmlData(merge).split("\n")[1];
      int romofPosition = nextMerge.indexOf("romof=");
      *mergeUsed = TRUE;
      if ( romofPosition > -1 ) {
        nextMerge = nextMerge.mid(romofPosition + 7);
        nextMerge = nextMerge.left(nextMerge.indexOf("\""));
        effectiveFile = getEffectiveFile(myItem, merge, mergeFile, wantedCRC, nextMerge, mergeFile, type, fileData, sha1Str, md5Str, isZipped, mergeUsed, fileCounter);
      } else
        effectiveFile = getEffectiveFile(myItem, merge, mergeFile, wantedCRC, "", "", type, fileData, sha1Str, md5Str, isZipped, mergeUsed, fileCounter);
    }
  }

  return effectiveFile;
}

void ROMAlyzer::on_treeWidgetChecksums_itemSelectionChanged()
{
#ifdef QMC2_DEBUG
  qmc2MainWindow->log(QMC2_LOG_FRONTEND, "DEBUG: ROMAlyzer::on_treeWidgetChecksums_itemSelectionChanged()");
#endif

  if ( checkBoxSelectGame->isChecked() ) {
    QList<QTreeWidgetItem *> items = treeWidgetChecksums->selectedItems();
    if ( items.count() > 0 ) {
      QTreeWidgetItem *item = items[0];
      while ( (void*)item->parent() != (void *)treeWidgetChecksums && item->parent() != 0 ) item = item->parent();
      QStringList words = item->text(QMC2_ROMALYZER_COLUMN_GAME).split(" ");
      selectItem(words[0]);
    }
  }
}

void ROMAlyzer::selectItem(QString gameName)
{
#ifdef QMC2_DEBUG
  qmc2MainWindow->log(QMC2_LOG_FRONTEND, QString("DEBUG: ROMAlyzer::selectItem(QString gameName = %1)").arg(gameName));
#endif

  switch ( qmc2MainWindow->stackedWidgetView->currentIndex() ) {
    case QMC2_VIEWGAMELIST_INDEX: {
      QTreeWidgetItem *gameItem = qmc2GamelistItemMap[gameName];
      if ( gameItem ) {
        qmc2MainWindow->treeWidgetGamelist->clearSelection();
        qmc2MainWindow->treeWidgetGamelist->setCurrentItem(gameItem);
        qmc2MainWindow->treeWidgetGamelist->scrollToItem(gameItem, QAbstractItemView::PositionAtTop);
        gameItem->setSelected(TRUE);
      }
      break;
    }
    case QMC2_VIEWHIERARCHY_INDEX: {
      QTreeWidgetItem *hierarchyItem = qmc2HierarchyItemMap[gameName];
      if ( hierarchyItem ) {
        qmc2MainWindow->treeWidgetHierarchy->clearSelection();
        qmc2MainWindow->treeWidgetHierarchy->setCurrentItem(hierarchyItem);
        qmc2MainWindow->treeWidgetHierarchy->scrollToItem(hierarchyItem, QAbstractItemView::PositionAtTop);
        hierarchyItem->setSelected(TRUE);
      }
      break;
    }
  }
}

QString ROMAlyzer::humanReadable(quint64 value)
{
#ifdef QMC2_DEBUG
  qmc2MainWindow->log(QMC2_LOG_FRONTEND, QString("DEBUG: ROMAlyzer::humanReadable(quint64 value = %1)").arg(value));
#endif

  static QString humanReadableString;
  static qreal humanReadableValue;

#if __WORDSIZE == 64
  if ( (qreal)value / (qreal)QMC2_ONE_KILOBYTE < (qreal)QMC2_ONE_KILOBYTE ) {
    humanReadableValue = (qreal)value / (qreal)QMC2_ONE_KILOBYTE;
    humanReadableString = QString::number(humanReadableValue, 'f', 2) + QString(tr(" KB"));
  } else if ( (qreal)value / (qreal)QMC2_ONE_MEGABYTE < (qreal)QMC2_ONE_KILOBYTE ) {
    humanReadableValue = (qreal)value / (qreal)QMC2_ONE_MEGABYTE;
    humanReadableString = QString::number(humanReadableValue, 'f', 2) + QString(tr(" MB"));
  } else if ( (qreal)value / (qreal)QMC2_ONE_GIGABYTE < (qreal)QMC2_ONE_KILOBYTE ) {
    humanReadableValue = (qreal)value / (qreal)QMC2_ONE_GIGABYTE;
    humanReadableString = QString::number(humanReadableValue, 'f', 2) + QString(tr(" GB"));
  } else {
    humanReadableValue = (qreal)value / (qreal)QMC2_ONE_TERABYTE;
    humanReadableString = QString::number(humanReadableValue, 'f', 2) + QString(tr(" TB"));
  }
#else
  if ( (qreal)value / (qreal)QMC2_ONE_KILOBYTE < (qreal)QMC2_ONE_KILOBYTE ) {
    humanReadableValue = (qreal)value / (qreal)QMC2_ONE_KILOBYTE;
    humanReadableString = QString::number(humanReadableValue, 'f', 2) + QString(tr(" KB"));
  } else if ( (qreal)value / (qreal)QMC2_ONE_MEGABYTE < (qreal)QMC2_ONE_KILOBYTE ) {
    humanReadableValue = (qreal)value / (qreal)QMC2_ONE_MEGABYTE;
    humanReadableString = QString::number(humanReadableValue, 'f', 2) + QString(tr(" MB"));
  } else {
    humanReadableValue = (qreal)value / (qreal)QMC2_ONE_GIGABYTE;
    humanReadableString = QString::number(humanReadableValue, 'f', 2) + QString(tr(" GB"));
  }
#endif

  return humanReadableString;
}

void ROMAlyzer::log(QString message)
{
  QString msg = QTime::currentTime().toString("hh:mm:ss.zzz") + ": " + message;

  textBrowserLog->append(msg);
  qApp->processEvents();
}

ROMAlyzerXmlHandler::ROMAlyzerXmlHandler(QTreeWidgetItem *parent, bool expand, bool scroll)
{
#ifdef QMC2_DEBUG
  qmc2MainWindow->log(QMC2_LOG_FRONTEND, "DEBUG: ROMAlyzerXmlHandler::ROMAlyzerXmlHandler(QTreeWidgetItem *parent = 0x" + QString::number((ulong)parent, 16) + ", ...)");
#endif

  parentItem = parent;
  autoExpand = expand;
  autoScroll = scroll;

  redBrush = QBrush(QColor(255, 0, 0));
  greenBrush = QBrush(QColor(0, 255, 0));
  blueBrush = QBrush(QColor(0, 0, 255));
  yellowBrush = QBrush(QColor(255, 255, 0));
  brownBrush = QBrush(QColor(128, 128, 0));
  greyBrush = QBrush(QColor(128, 128, 128));
}

ROMAlyzerXmlHandler::~ROMAlyzerXmlHandler()
{
#ifdef QMC2_DEBUG
  qmc2MainWindow->log(QMC2_LOG_FRONTEND, "DEBUG: ROMAlyzerXmlHandler::~ROMAlyzerXmlHandler()");
#endif

}

bool ROMAlyzerXmlHandler::startElement(const QString &namespaceURI, const QString &localName, const QString &qName, const QXmlAttributes &attributes)
{
#ifdef QMC2_DEBUG
  qmc2MainWindow->log(QMC2_LOG_FRONTEND, "DEBUG: ROMAlyzerXmlHandler::startElement(...)");
#endif

  QString s;

#if defined(QMC2_SDLMAME) || defined(QMC2_MAME)
  if ( qName == "game" ) {
#elif defined(QMC2_SDLMESS) || defined(QMC2_MESS)
  if ( qName == "machine" ) {
#endif
    parentItem->setText(QMC2_ROMALYZER_COLUMN_MERGE, attributes.value("romof"));
    parentItem->setExpanded(FALSE);
    emuStatus = 0;
    fileCounter = 0;
    currentText.clear();
    childItems.clear();
  } else if ( qName == "rom" || qName == "disk" ) {
    fileCounter++;
    childItem = new QTreeWidgetItem(parentItem);
    childItems << childItem;
    childItem->setText(QMC2_ROMALYZER_COLUMN_GAME, attributes.value("name"));
    childItem->setText(QMC2_ROMALYZER_COLUMN_TYPE, qName == "rom" ? QObject::tr("ROM") : QObject::tr("CHD"));
    childItem->setText(QMC2_ROMALYZER_COLUMN_MERGE, attributes.value("merge"));
    s = attributes.value("status");
    if ( s.isEmpty() || s == "good" ) {
      childItem->setText(QMC2_ROMALYZER_COLUMN_EMUSTATUS, QObject::tr("good"));
      childItem->setForeground(QMC2_ROMALYZER_COLUMN_EMUSTATUS, greenBrush);
      emuStatus |= QMC2_ROMALYZER_EMUSTATUS_GOOD;
    } else if ( s == "nodump" ) {
      childItem->setText(QMC2_ROMALYZER_COLUMN_EMUSTATUS, QObject::tr("no dump"));
      childItem->setForeground(QMC2_ROMALYZER_COLUMN_EMUSTATUS, brownBrush);
      emuStatus |= QMC2_ROMALYZER_EMUSTATUS_NODUMP;
    } else if ( s == "baddump" ) {
      childItem->setText(QMC2_ROMALYZER_COLUMN_EMUSTATUS, QObject::tr("bad dump"));
      childItem->setForeground(QMC2_ROMALYZER_COLUMN_EMUSTATUS, brownBrush);
      emuStatus |= QMC2_ROMALYZER_EMUSTATUS_BADDUMP;
    } else {
      childItem->setText(QMC2_ROMALYZER_COLUMN_EMUSTATUS, QObject::tr("unknown"));
      childItem->setForeground(QMC2_ROMALYZER_COLUMN_EMUSTATUS, blueBrush);
      emuStatus |= QMC2_ROMALYZER_EMUSTATUS_UNKNOWN;
    }
    childItem->setText(QMC2_ROMALYZER_COLUMN_SIZE, attributes.value("size"));
    childItem->setText(QMC2_ROMALYZER_COLUMN_CRC, attributes.value("crc"));
    childItem->setText(QMC2_ROMALYZER_COLUMN_SHA1, attributes.value("sha1"));
    childItem->setText(QMC2_ROMALYZER_COLUMN_MD5, attributes.value("md5"));
  }

  return TRUE;
}

bool ROMAlyzerXmlHandler::endElement(const QString &namespaceURI, const QString &localName, const QString &qName)
{
#ifdef QMC2_DEBUG
  qmc2MainWindow->log(QMC2_LOG_FRONTEND, "DEBUG: ROMAlyzerXmlHandler::endElement(...)");
#endif

#if defined(QMC2_SDLMAME) || defined(QMC2_MAME)
  if ( qName == "game" ) {
#elif defined(QMC2_SDLMESS) || defined(QMC2_MESS)
  if ( qName == "machine" ) {
#endif
    QString s(parentItem->text(QMC2_ROMALYZER_COLUMN_GAME));
    s += " [" + QString::number(fileCounter) + "]";
    parentItem->setText(QMC2_ROMALYZER_COLUMN_GAME, s);
    QString emuStatusStr;
    QBrush myBrush;
    if ( emuStatus == QMC2_ROMALYZER_EMUSTATUS_GOOD ) {
      emuStatusStr = QObject::tr("good");
      myBrush = greenBrush;
    } else if ( emuStatus & QMC2_ROMALYZER_EMUSTATUS_UNKNOWN ) {
      emuStatusStr = QObject::tr("unknown");
      myBrush = blueBrush;
    } else if ( emuStatus & QMC2_ROMALYZER_EMUSTATUS_NODUMP && emuStatus & QMC2_ROMALYZER_EMUSTATUS_BADDUMP ) {
      emuStatusStr = QObject::tr("no / bad dump");
      myBrush = brownBrush;
    } else if ( emuStatus & QMC2_ROMALYZER_EMUSTATUS_NODUMP ) {
      emuStatusStr = QObject::tr("no dump");
      myBrush = brownBrush;
    } else if ( emuStatus & QMC2_ROMALYZER_EMUSTATUS_BADDUMP ) {
      emuStatusStr = QObject::tr("bad dump");
      myBrush = brownBrush;
    } else {
      emuStatusStr = QObject::tr("unknown");
      myBrush = blueBrush;
    }
    parentItem->setText(QMC2_ROMALYZER_COLUMN_EMUSTATUS, emuStatusStr);
    parentItem->setForeground(QMC2_ROMALYZER_COLUMN_EMUSTATUS, myBrush);
    if ( autoExpand )
      parentItem->setExpanded(TRUE);
    if ( autoScroll )
      qmc2ROMAlyzer->treeWidgetChecksums->scrollToItem(parentItem, QAbstractItemView::PositionAtTop);
  }

  return TRUE;
}

bool ROMAlyzerXmlHandler::characters(const QString &str)
{
#ifdef QMC2_DEBUG
  qmc2MainWindow->log(QMC2_LOG_FRONTEND, "DEBUG: ROMAlyzerXmlHandler::characters(...)");
#endif

  currentText += str;
  return TRUE;
}

