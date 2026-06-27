#include "MainWindow.h"

#include "modules/network/AVNetworkClient.h"
#include "pages/PlayerPage.h"
#include "pages/RecorderPage.h"
#include "pages/RemoteMediaPage.h"
#include "pages/SettingsPage.h"

#include <QTabWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_networkClient(new AVNetworkClient(this)),
      m_tabs(new QTabWidget(this)),
      m_playerPage(new PlayerPage(m_tabs))
{
    auto *remoteMediaPage = new RemoteMediaPage(m_networkClient, m_tabs);
    m_tabs->addTab(m_playerPage, "Player");
    m_tabs->addTab(new RecorderPage(m_tabs), "Recorder");
    m_tabs->addTab(remoteMediaPage, "Remote Media");
    m_tabs->addTab(new SettingsPage(m_networkClient, m_tabs), "Settings");

    connect(remoteMediaPage,
            SIGNAL(requestPlayLocalFile(QString)),
            this,
            SLOT(slotPlayLocalFile(QString)));

    setCentralWidget(m_tabs);
    setWindowTitle("AVClient");
    resize(1180, 760);
}

void MainWindow::slotPlayLocalFile(const QString &filePath)
{
    m_tabs->setCurrentWidget(m_playerPage);
    m_playerPage->playLocalFile(filePath);
}
