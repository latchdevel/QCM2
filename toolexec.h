#ifndef _TOOLEXEC_H_
#define _TOOLEXEC_H_

#include <QProcess>
#include "ui_toolexec.h"

class ToolExecutor : public QDialog, public Ui::ToolExecutor
{
  Q_OBJECT

  public:
    QString toolCommand;
    QStringList toolArgs;
    QProcess *toolProc;

    ToolExecutor(QWidget *, QString &, QStringList &);
    ~ToolExecutor();

  public slots:
    void execute();
    void toolStarted();
    void toolFinished(int, QProcess::ExitStatus);
    void toolReadyReadStandardOutput();
    void toolReadyReadStandardError();
    void toolError(QProcess::ProcessError);
    void toolStateChanged(QProcess::ProcessState);

  protected:
};

#endif
