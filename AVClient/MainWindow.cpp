#include "MainWindow.h"

#include "modules/network/AVNetworkClient.h"
#include "pages/PlayerPage.h"
#include "pages/RecorderPage.h"
#include "pages/RemoteMediaPage.h"
#include "pages/SettingsPage.h"

#include <QTabWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_networkClient(new AVNetworkClient(this))
{
    auto *tabs = new QTabWidget(this);
    tabs->addTab(new PlayerPage(tabs), "Player");
    tabs->addTab(new RecorderPage(tabs), "Recorder");
    tabs->addTab(new RemoteMediaPage(m_networkClient, tabs), "Remote Media");
    tabs->addTab(new SettingsPage(m_networkClient, tabs), "Settings");

    setCentralWidget(tabs);
    setWindowTitle("AVClient");
    resize(1180, 760);
}
