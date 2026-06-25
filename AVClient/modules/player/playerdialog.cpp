/*
 * playerdialog.cpp
 * ------------------------------------------------------------
 * 主界面实现文件。
 *
 * 本文件不直接做 FFmpeg 解码，它主要负责：
 * 1. 响应按钮点击：打开、播放、暂停、停止；
 * 2. 接收 VideoPlayer 发来的 QImage 并显示；
 * 3. 接收总时长，设置进度条范围；
 * 4. 用 QTimer 每 500ms 查询当前播放时间并刷新 UI；
 * 5. 用 eventFilter() 实现点击进度条任意位置跳转。
 *
 * 可以把 PlayerDialog 理解为“遥控器 + 显示屏”，VideoPlayer 才是“播放器引擎”。
 */
#include "playerdialog.h"
#include "ui_playerdialog.h"

#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QPixmap>
#include <QMouseEvent>
#include <QStyle>
#include <QtGlobal>

//#define _DEF_PATH "D:/colin/project/VideoPlayer/test/test.mp4"
#define _DEF_PATH "rtmp://192.168.44.130:1935/vod//102.mp4"   //点播形式
#define _DEF_LIVE_PATH "rtmp://192.168.44.130/videotest/user=100"   //直播形式
//#define _DEF_PATH "http://111.40.196.9/PLTV/88888888/224/3221225628/index.m3u8"
//#define _DEF_PATH "rtmp://ns8.indexforce.com/home/mystream"

/*
 * PlayerDialog 构造函数
 * ------------------------------------------------------------
 * 初始化 UI、创建 VideoPlayer、连接信号槽、安装进度条事件过滤器。
 */
PlayerDialog::PlayerDialog(QWidget *parent)
    : QDialog(parent),
      ui(new Ui::PlayerDialog),
      m_player(new VideoPlayer(this)),
      m_isStop(true)
{
    ui->setupUi(this);

    // 视频帧显示：VideoPlayer 解码出一帧 RGB 图像后，通过信号交给界面线程显示。
    connect(m_player, SIGNAL(SIG_getOneImage(QImage)),
            this, SLOT(slot_setImage(QImage)));

    // 播放状态变化：Stop / Playing / Pause，用于更新按钮、清屏、启停定时器。
    connect(m_player, SIGNAL(SIG_PlayerStateChanged(int)),
            this, SLOT(slot_PlayerStateChanged(int)));

    // 总时长：run() 成功解析文件信息后发送，用于设置进度条范围。
    connect(m_player, SIGNAL(SIG_TotalTime(qint64)),
            this, SLOT(slot_getTotalTime(qint64)));

    // 当前时间：用 UI 定时器主动查询 VideoPlayer 的音频/视频时钟。
    connect(&m_timer, SIGNAL(timeout()),
            this, SLOT(slot_TimerTimeOut()));
    m_timer.setInterval(500);

    // 进度条点击跳转：不额外新增 VideoSlider 文件，直接给现有 QSlider 安装事件过滤器。
    // 鼠标点击进度条时，在 eventFilter() 中计算点击位置对应的秒数，并调用 seek()。
    ui->slider_progress->installEventFilter(this);

    slot_PlayerStateChanged(PlayerState::Stop);
}

/*
 * PlayerDialog 析构函数
 * ------------------------------------------------------------
 * 关闭窗口时先停止播放器，避免界面销毁后解码线程继续发图像信号。
 */
PlayerDialog::~PlayerDialog()
{
    if (m_player)
        m_player->stop(true);
    delete ui;
}


/*
 * eventFilter
 * ------------------------------------------------------------
 * 事件过滤器，用于拦截 slider_progress 的鼠标点击。
 *
 * 为什么不用单独的 VideoSlider 类？
 * 老师课件里是继承 QSlider 并重写 mousePressEvent()；
 * 这里为了不新增 videoslider.cpp/.h，直接在 PlayerDialog 里安装 eventFilter，
 * 实现效果一致：点击进度条任意位置，立刻跳转到对应时间。
 */
bool PlayerDialog::eventFilter(QObject *watched, QEvent *event)
{
    // 只处理播放进度条的鼠标左键点击。
    // Qt 原生 QSlider 点击空白位置默认是“按步进移动”，不是直接跳到点击位置，
    // 所以这里手动把鼠标 x 坐标换算成 slider 的 value，再调用 VideoPlayer::seek()。
    if (watched == ui->slider_progress && event->type() == QEvent::MouseButtonPress)
    {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton)
        {
            int value = QStyle::sliderValueFromPosition(
                        ui->slider_progress->minimum(),
                        ui->slider_progress->maximum(),
                        mouseEvent->pos().x(),
                        ui->slider_progress->width());

            ui->slider_progress->setValue(value);
            slot_videoSliderValueChanged(value);

            // 返回 true 表示这个鼠标事件已经处理完，不再交给 QSlider 默认逻辑。
            return true;
        }
    }

    return QDialog::eventFilter(watched, event);
}

/*
 * formatTime
 * ------------------------------------------------------------
 * 把微秒时间转换成 HH:MM:SS 字符串。
 *
 * VideoPlayer 内部时间单位统一使用微秒，界面显示时需要转换为时分秒。
 */
QString PlayerDialog::formatTime(qint64 uSec) const
{
    if (uSec < 0) uSec = 0;

    qint64 sec = uSec / 1000000;
    qint64 h = sec / 3600;
    qint64 m = (sec / 60) % 60;
    qint64 s = sec % 60;

    return QString("%1:%2:%3")
            .arg(h, 2, 10, QChar('0'))
            .arg(m, 2, 10, QChar('0'))
            .arg(s, 2, 10, QChar('0'));
}

/*
 * setButtonsForState
 * ------------------------------------------------------------
 * 根据当前播放状态调整按钮显示。
 *
 * Playing：显示暂停按钮，隐藏播放/恢复按钮；
 * Pause：显示播放/恢复按钮，隐藏暂停按钮；
 * Stop：回到初始状态，停止按钮不可用。
 */
void PlayerDialog::setButtonsForState(PlayerState state)
{
    switch (state)
    {
    case PlayerState::Playing:
        ui->pb_resume->hide();
        ui->pb_pause->show();
        ui->pb_stop->setEnabled(true);
        break;
    case PlayerState::Pause:
        ui->pb_pause->hide();
        ui->pb_resume->show();
        ui->pb_stop->setEnabled(true);
        break;
    case PlayerState::Stop:
    default:
        ui->pb_pause->hide();
        ui->pb_resume->show();
        ui->pb_stop->setEnabled(false);
        break;
    }
}

/*
 * on_pb_start_clicked
 * ------------------------------------------------------------
 * “打开文件”按钮槽函数。
 *
 * 流程：
 * 1. 如果当前正在播放，先 stop(true) 安全停止；
 * 2. 弹出文件选择框；
 * 3. 检查文件存在；
 * 4. 设置文件名显示；
 * 5. 调用 VideoPlayer::setFileName() 开始播放。
 */
void PlayerDialog::on_pb_start_clicked()
{
    // 打开新文件前，必须先停止旧的播放任务，否则旧的读线程/SDL 回调还会继续访问资源。
    if (m_player->playerState() != PlayerState::Stop)
        m_player->stop(true);

    //打开浏览选择文件
    QString path = QFileDialog::getOpenFileName(this,
                                                "选择要播放的文件",
                                                "./",
                                                "视频文件 (*.flv *.rmvb *.avi *.mp4 *.MP4 *.mkv);;音频文件 (*.mp3 *.aac *.wav);;所有文件 (*.*)");
    if (path.isEmpty())
        return;

    QFileInfo info(path);
    if (!info.exists())
    {
        QMessageBox::information(this, "提示", "打开文件失败");
        return;
    }

    ui->lb_videoName->setText(info.fileName());
    m_player->setFileName(path);

    //m_player->setFileName(_DEF_PATH);
    //m_player->setFileName(_DEF_LIVE_PATH);
    m_player->start();
    slot_PlayerStateChanged(PlayerState::Playing);
}

/*
 * on_pb_openUrl_clicked
 * ------------------------------------------------------------
 * "打开URL"按钮槽函数。
 *
 * 流程：
 * 1. 如果当前正在播放，先 stop(true) 安全停止；
 * 2. 弹出输入框让用户输入网络媒体URL；
 * 3. 设置URL名称显示；
 * 4. 调用 VideoPlayer::setFileName() 开始播放。
 *
 * 底层 avformat_open_input() 已支持 RTMP/HLS/HTTP 等网络协议，
 * 这里只是提供了 UI 入口，无需修改解码逻辑。
 */
void PlayerDialog::on_pb_openUrl_clicked()
{
    if (m_player->playerState() != PlayerState::Stop)
        m_player->stop(true);

    QString url = QInputDialog::getText(
        this, "打开网络URL",
        "请输入网络媒体地址：",
        QLineEdit::Normal,
        _DEF_PATH);

    if (url.isEmpty())
        return;

    ui->lb_videoName->setText(url);
    m_player->setFileName(url);
    m_player->start();
    slot_PlayerStateChanged(PlayerState::Playing);
}

/*
 * slot_setImage
 * ------------------------------------------------------------
 * 接收 VideoPlayer 解码出的 QImage，并显示到 QLabel 上。
 *
 * 注意：视频原图尺寸可能和 QLabel 不一致，所以这里按比例缩放，避免图像变形。
 */
void PlayerDialog::slot_setImage(QImage img)
{
//    if (img.isNull())
//        return;

//    QPixmap pixmap = QPixmap::fromImage(img.scaled(ui->lb_show->size(),
//                                                   Qt::KeepAspectRatio,
//                                                   Qt::SmoothTransformation));
//    ui->lb_show->setPixmap(pixmap);
    //用OpenGL 实现 视频加速渲染（当前视频清晰度低，因为qt自带的.scaled函数缩放会对画质有损失）
    ui->wdg_show->slot_setImage(img);
}

/*
 * on_pb_resume_clicked
 * ------------------------------------------------------------
 * “播放/恢复”按钮槽函数。
 *
 * 本项目的播放按钮主要用于“暂停后恢复”，不是从 Stop 状态直接开始。
 * Stop 状态要先通过“打开文件”选择媒体文件。
 */
void PlayerDialog::on_pb_resume_clicked()
{
    if (m_isStop)
        return;
    m_player->play();
}

/*
 * on_pb_pause_clicked
 * ------------------------------------------------------------
 * “暂停”按钮槽函数。
 * 调用 VideoPlayer::pause() 后，读线程、音频回调、视频线程都会看到 isPause=true。
 */
void PlayerDialog::on_pb_pause_clicked()
{
    if (m_isStop)
        return;
    m_player->pause();
}

/*
 * on_pb_stop_clicked
 * ------------------------------------------------------------
 * “停止”按钮槽函数。
 * stop(true) 会等待读线程退出并释放资源，适合用户主动停止播放。
 */
void PlayerDialog::on_pb_stop_clicked()
{
    m_player->stop(true);
}

/*
 * slot_PlayerStateChanged
 * ------------------------------------------------------------
 * 接收 VideoPlayer 发出的播放状态变化信号。
 *
 * Stop：停止 UI 定时器、进度归零、清空文件名、黑屏；
 * Playing：启动 UI 定时器，持续刷新当前时间；
 * Pause：停止 UI 定时器，按钮切换为“恢复播放”。
 */
void PlayerDialog::slot_PlayerStateChanged(int state)
{
    PlayerState playerState = static_cast<PlayerState>(state);

    switch (playerState)
    {
    case PlayerState::Stop:
    {
        qDebug() << "VideoPlayer::Stop";
        m_timer.stop();
        ui->slider_progress->setValue(0);
        ui->slider_progress->setEnabled(true);
        ui->lb_totalTime->setText("00:00:00");
        ui->lb_curTime->setText("00:00:00");
        ui->lb_videoName->clear();

        QImage img(ui->wdg_show->width(), ui->wdg_show->height(), QImage::Format_RGB32);
        img.fill(Qt::black);
        slot_setImage(img);

        m_isStop = true;
        setButtonsForState(PlayerState::Stop);
        break;
    }
    case PlayerState::Playing:
        qDebug() << "VideoPlayer::Playing";
        m_timer.start();
        m_isStop = false;
        setButtonsForState(PlayerState::Playing);
        break;
    case PlayerState::Pause:
        qDebug() << "VideoPlayer::Pause";
        m_timer.stop();
        m_isStop = false;
        setButtonsForState(PlayerState::Pause);
        break;
    }

    update();
}

/*
 * slot_getTotalTime
 * ------------------------------------------------------------
 * 接收媒体总时长。
 *
 * VideoPlayer::run() 在 avformat_find_stream_info() 后发送 SIG_TotalTime。
 * 这里把总时长从微秒转换为秒，并设置进度条最大值。
 */
void PlayerDialog::slot_getTotalTime(qint64 uSec)
{
    if (uSec <= 0)
    {
        // 直播流：禁用进度条，总时长显示"直播"
        ui->slider_progress->setRange(0, 0);
        ui->slider_progress->setEnabled(false);
        ui->lb_totalTime->setText("直播");
        return;
    }

    qint64 sec = qMax<qint64>(0, uSec / 1000000);
    ui->slider_progress->setRange(0, static_cast<int>(sec));
    ui->slider_progress->setValue(0);
    ui->slider_progress->setEnabled(true);
    ui->lb_totalTime->setText(formatTime(uSec));
}

/*
 * slot_TimerTimeOut
 * ------------------------------------------------------------
 * UI 定时器槽函数，每 500ms 执行一次。
 *
 * 它不参与解码，只是查询 VideoPlayer::getCurrentTime()，然后刷新：
 * 1. 当前播放时间标签；
 * 2. 进度条当前位置。
 */
void PlayerDialog::slot_TimerTimeOut()
{
    if (m_player->playerState() == PlayerState::Stop)
        return;

    qint64 currentUSec = static_cast<qint64>(m_player->getCurrentTime());
    qint64 sec = qMax<qint64>(0, currentUSec / 1000000);

    // 避免超过最大值后进度条显示异常。
    if (sec > ui->slider_progress->maximum())
        sec = ui->slider_progress->maximum();

    ui->slider_progress->blockSignals(true);
    ui->slider_progress->setValue(static_cast<int>(sec));
    ui->slider_progress->blockSignals(false);

    ui->lb_curTime->setText(formatTime(currentUSec));
}

/*
 * slot_videoSliderValueChanged
 * ------------------------------------------------------------
 * 进度条点击跳转。
 *
 * value 单位是秒，VideoPlayer::seek() 需要微秒，故乘以 1000000。
 * 真正的 av_seek_frame() 不在 UI 线程执行，而是在 VideoPlayer::run() 线程中处理。
 */
void PlayerDialog::slot_videoSliderValueChanged(int value)
{
    if (m_player->playerState() == PlayerState::Stop)
        return;

    // 直播流无总时长，不允许seek
    if (m_player->getTotalTime() <= 0)
        return;

    // 进度条单位是秒；VideoPlayer::seek 使用微秒。
    m_player->seek(static_cast<qint64>(value) * 1000000);
}
