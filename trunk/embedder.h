#ifndef _EMBEDDER_H_
#define _EMBEDDER_H_

#include <QtGui>
#if defined(Q_WS_X11)
#include <QX11EmbedContainer>
#include "embedderopt.h"

class Embedder : public QWidget
{
  Q_OBJECT

  public:
    bool embedded;
    bool optionsShown;
    WId winId;
    QX11EmbedContainer *embedContainer;
    EmbedderOptions *embedderOptions;
    QGridLayout *gridLayout;
    QString gameName;
    QString gameID;
    QSize nativeResolution;
    int cmLeft, cmTop, cmRight, cmBottom;
    bool pauseKeyPressed;
    bool isPaused;

    Embedder(QString name, QString id, WId wid, QWidget *parent = 0);
    ~Embedder();

  public slots:
    void embed();
    void embed(WId wid) { winId = wid; embed(); }
    void release();
    void clientEmbedded();
    void clientClosed();
    void clientError(QX11EmbedContainer::Error);
    void toggleOptions();
    void adjustIconSizes();
    void forceFocus();
    void simulatePauseKey();
    void pause();
    void resume();
    void showEventDelayed();
    void hideEventDelayed();

  protected:
    void closeEvent(QCloseEvent *);
    void showEvent(QShowEvent *);
    void hideEvent(QHideEvent *);

  signals:
    void closing();
};

#endif

#endif
