/*
 * videoplayer.h
 * ------------------------------------------------------------
 * 播放器核心类声明。
 *
 * 本文件是整个项目中最关键的头文件，主要包含三部分：
 * 1. PlayerState：播放器状态枚举，区分 Playing / Pause / Stop。
 * 2. VideoState：一次播放任务的共享状态，读线程、音频回调、视频线程都会访问。
 * 3. VideoPlayer：继承 QThread，负责打开文件、读取 packet、控制播放、暂停、停止、跳转。
 *
 * 代码整体对应老师“播放控制”课件中的结构：
 * - setFileName()：打开文件并开始播放；
 * - play()/pause()/stop()：播放控制；
 * - getCurrentTime()/getTotalTime()：进度显示；
 * - seek()：进度条跳转；
 * - SIG_PlayerStateChanged/SIG_TotalTime：通知 UI 更新。
 */
#ifndef VIDEOPLAYER_H
#define VIDEOPLAYER_H

#include <QImage>
#include <QThread>
#include <QString>

#include "packetqueue.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <SDL.h>
}

// 播放器状态：课件中用该枚举区分播放、暂停、停止。
enum PlayerState
{
    Playing = 0,
    Pause,
    Stop
};

class VideoPlayer;
struct VideoDecodeContext;

// 统一保存一次播放过程中音频、视频、控制、跳转相关的所有状态。
// 课件中强调：音频解码线程、视频解码线程、读取线程都需要访问同一份 VideoState。
typedef struct VideoState
{
    AVFormatContext *pFormatCtx;       // 视频/音频文件上下文，可以理解为“文件指针”

    //////////////////////////// 音频相关 ////////////////////////////
    AVStream *audio_st;                // 音频流
    PacketQueue *audioq;               // 音频 packet 队列
    AVCodecContext *pAudioCodecCtx;    // 音频解码器上下文
    int audioStream;                   // 音频流下标，找不到时为 -1
    double audio_clock;                // 音频时钟，单位：微秒。音视频同步时以它为主时钟
    SDL_AudioDeviceID audioID;         // SDL 音频设备 ID
    AVFrame out_frame;                 // 重采样输出格式参数
    uint8_t audio_buf[(192000 * 3) / 2];
    unsigned int audio_buf_size;
    unsigned int audio_buf_index;
    AVFrame *audioFrame;               // 音频解码输出 frame

    // 一个音频 packet 里可能包含多帧音频数据，所以需要保存当前 packet 的剩余部分
    AVPacket audio_pkt;
    uint8_t *audio_pkt_data;
    int audio_pkt_size;
    bool audio_pkt_valid;

    //////////////////////////// 视频相关 ////////////////////////////
    AVStream *video_st;                // 视频流
    PacketQueue *videoq;               // 视频 packet 队列
    AVCodecContext *pCodecCtx;         // 视频解码器上下文
    int videoStream;                   // 视频流下标，找不到时为 -1
    double video_clock;                // 视频时钟，单位：微秒
    SDL_Thread *video_tid;             // 有音频时，视频通过独立 SDL 线程解码

    // 上一节课同步补充：没有音频时使用 SDL 定时器控制视频帧节奏，避免纯视频播放过快。
    SDL_TimerID video_timer;
    SDL_mutex *video_timer_mutex;
    VideoDecodeContext *timer_video_ctx;
    Uint32 video_timer_interval;

    //////////////////////////// 播放控制 ////////////////////////////
    bool isPause = false;                      // 暂停标志：读线程、视频线程、音频回调都会检查
    bool quit = false;                         // 停止/退出标志
    bool readFinished = true;                 // av_read_frame 已经读到文件末尾
    bool readThreadFinished = true;           // VideoPlayer::run 是否已经结束
    bool videoThreadFinished = true;          // 视频线程或纯视频定时器是否已经结束

    //////////////////////////// 跳转控制 ////////////////////////////
    int seek_req;                      // 跳转请求标志，由 UI 线程设置，读线程执行
    int64_t seek_pos;                  // 目标跳转位置，单位：微秒
    int seek_flag_audio;               // 跳转后音频是否需要丢弃关键点到目标点之间的数据
    int seek_flag_video;               // 跳转后视频是否需要丢弃关键点到目标点之间的数据

    // 快退修复：seek 发起后，解码线程可能已经从队列中取到了旧位置 packet。
    // 在收到 FLUSH packet 之前，音频/视频线程应丢弃旧 packet，避免旧视频帧继续按旧 PTS 等待新的 audio_clock。
    int seek_wait_audio_flush;         // 音频线程是否正在等待 FLUSH packet
    int seek_wait_video_flush;         // 视频线程是否正在等待 FLUSH packet

    double seek_time;                  // 目标跳转时间，单位：微秒

    int64_t start_time;
    VideoPlayer *m_player;             // 用于在 SDL 线程/回调中触发 Qt 信号
} VideoState;

/*
 * VideoPlayer
 * ------------------------------------------------------------
 * 真正的播放器工作类。
 *
 * 为什么继承 QThread？
 * av_read_frame() 是一个持续循环读取媒体文件的过程，如果直接放在 UI 线程，界面会卡死。
 * 因此把“读取 packet + 分发到队列”的工作放到 VideoPlayer::run() 中执行。
 *
 * 线程分工：
 * UI 线程：按钮、进度条、画面显示；
 * VideoPlayer::run()：读取文件，把 packet 投放到 audioq/videoq；
 * SDL 音频线程：audio_callback() 从 audioq 取数据并解码播放；
 * SDL 视频线程或 SDL 定时器：从 videoq 取数据并解码显示。
 */
class VideoPlayer : public QThread
{
    Q_OBJECT
public:
    explicit VideoPlayer(QObject *parent = nullptr);
    ~VideoPlayer() override;

    void run() override;

    //////////////////////////// 播放控制接口 ////////////////////////////
    void setFileName(const QString &fileName);  // 设置文件并立即开始播放
    void play();                                // 从暂停恢复播放
    void pause();                               // 暂停播放
    void stop(bool isWait = true);              // 停止播放；isWait=true 表示等待读线程退出
    void seek(qint64 pos);                      // 跳转，pos 单位：微秒

    double getCurrentTime() const;              // 当前播放时间，单位：微秒
    qint64 getTotalTime() const;                // 总时长，单位：微秒
    PlayerState playerState() const;

    // SDL 线程不是 QObject 线程，不能直接 emit 私有信号；通过公有函数转发更清晰。
    void SendGetOneImage(const QImage &img);

signals:
    void SIG_getOneImage(QImage img);
    void SIG_PlayerStateChanged(int state);
    void SIG_TotalTime(qint64 uSec);

private:
    void resetVideoState();
    void closeAudioDevice();
    void sendBlackImage();

private:
    QString m_fileName;
    VideoState m_videoState;
    PlayerState m_playerState;
};

#endif // VIDEOPLAYER_H
