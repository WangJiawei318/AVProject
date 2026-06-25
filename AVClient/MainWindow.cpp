#include "MainWindow.h"

#include "pages/PlayerPage.h"
#include "pages/RecorderPage.h"
#include "pages/SettingsPage.h"

#include <QTabWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    auto *tabs = new QTabWidget(this);
    tabs->addTab(new PlayerPage(tabs), tr("播放"));
    tabs->addTab(new RecorderPage(tabs), tr("录制"));
    tabs->addTab(new SettingsPage(tabs), tr("设置"));

    setCentralWidget(tabs);
    setWindowTitle(tr("AVClient"));
    resize(1180, 760);
}
