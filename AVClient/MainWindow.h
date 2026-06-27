#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class AVNetworkClient;
class PlayerPage;
class QTabWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void slotPlayLocalFile(const QString &filePath);

private:
    AVNetworkClient *m_networkClient;
    QTabWidget *m_tabs;
    PlayerPage *m_playerPage;
};

#endif // MAINWINDOW_H
