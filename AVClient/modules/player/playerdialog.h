/*
 * playerdialog.h
 * ------------------------------------------------------------
 * 播放器主界面类声明。
 *
 * PlayerDialog 只负责 UI 层工作：
 * 1. 打开文件按钮；
 * 2. 播放/暂停/停止按钮；
 * 3. QLabel 显示视频帧；
 * 4. QSlider 显示进度，并通过鼠标点击触发 seek；
 * 5. QLabel 显示当前时间/总时间。
 *
 * 真正的音视频解码逻辑全部放在 VideoPlayer 中，UI 通过信号槽与它通信。
 */
#ifndef PLAYERDIALOG_H
#define PLAYERDIALOG_H

#include <QDialog>
#include <QImage>
#include <QTimer>
#include <QEvent>

#include "videoplayer.h"

QT_BEGIN_NAMESPACE
namespace Ui { class PlayerDialog; }
QT_END_NAMESPACE

class PlayerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PlayerDialog(QWidget *parent = nullptr);
    ~PlayerDialog() override;

public slots:
    void playLocalFile(const QString &filePath);

protected:
    // 不单独新增 VideoSlider 子类时，用事件过滤器拦截进度条鼠标点击，
    // 实现“点击进度条任意位置即可跳转”的效果。
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void on_pb_start_clicked();        // 打开文件
    void on_pb_openUrl_clicked();      // 打开网络URL
    void on_pb_resume_clicked();       // 播放/恢复
    void on_pb_pause_clicked();        // 暂停
    void on_pb_stop_clicked();         // 停止

    void slot_setImage(QImage img);                    // 接收并显示视频帧
    void slot_PlayerStateChanged(int state);           // 播放状态变化
    void slot_getTotalTime(qint64 uSec);               // 获取总时长
    void slot_TimerTimeOut();                          // 定时刷新当前播放时间
    void slot_videoSliderValueChanged(int value);      // 点击进度条跳转

private:
    QString formatTime(qint64 uSec) const;
    void setButtonsForState(PlayerState state);

private:
    Ui::PlayerDialog *ui;
    VideoPlayer *m_player;
    QTimer m_timer;       // 每 500ms 刷新一次当前播放时间和进度条
    bool m_isStop;
};

#endif // PLAYERDIALOG_H
