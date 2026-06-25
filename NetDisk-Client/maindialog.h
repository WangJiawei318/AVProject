#ifndef MAINDIALOG_H
#define MAINDIALOG_H

#include <QDialog>
#include<QCloseEvent>

QT_BEGIN_NAMESPACE
namespace Ui { class MainDialog; }
QT_END_NAMESPACE

class MainDialog : public QDialog
{
    Q_OBJECT
signals:
    void SIG_close();
public:
    MainDialog(QWidget *parent = nullptr);
    ~MainDialog();
    void closeEvent(QCloseEvent * event);

private:
    Ui::MainDialog *ui;
};
#endif // MAINDIALOG_H


//点击×  ——>  执行关闭事件 ——>  弹窗询问 ——>发送关闭信号  核心类接收 然后回收资源
