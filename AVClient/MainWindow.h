#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class AVNetworkClient;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    AVNetworkClient *m_networkClient;
};

#endif // MAINWINDOW_H
