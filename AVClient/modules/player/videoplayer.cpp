/*
 * videoplayer.cpp
 * ------------------------------------------------------------
 * 播放器核心实现文件。
 *
 * 你可以按下面顺序阅读本文件：
 * 1. VideoPlayer::setFileName/play/pause/stop/seek：对外播放控制接口；
 * 2. VideoPlayer::run：读取线程主函数，负责打开文件、初始化音视频、读取 packet；
 * 3. audio_callback/audio_decode_frame：SDL 音频回调和音频解码；
 * 4. video_thread/timer_callback/decode_one_video_frame：视频解码和音视频同步；
 * 5. find_stream_index/put_flush_packet 等辅助函数。
 *
 * 本版本保留“上一节同步补充”的两种同步场景：
 * - 有音频：音频时钟作为主时钟，视频线程等待 audio_clock；
 * - 无音频：使用 SDL_AddTimer() 按帧率定时解码视频，避免纯视频播放过快。
 *
 * 同时加入“播放控制”课件中的内容：
 * - 打开文件、播放、暂停、停止；
 * - 当前时间/总时间显示；
 * - 进度条 seek 跳转；
 * - seek 后清空队列和解码器缓存，减少花屏/杂音。
 */
#include "videoplayer.h"

#include <QByteArray>
#include <QDebug>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <ctime>

// 音频解码临时缓冲区大小。
// 课程示例中使用 192000，约等于 48kHz、32bit、1 秒音频的缓冲规模。
#define AVCODEC_MAX_AUDIO_FRAME_SIZE 192000
// SDL 每次向 audio_callback() 请求的采样数量。
// 数值越小，声音延迟越低，但回调更频繁；数值越大，延迟更高但更稳定。
#define SDL_AUDIO_BUFFER_SIZE 1024
// 队列缓存阈值：读线程读得太快时暂停读取，防止内存暴涨。
// audioq->size / videoq->size 表示队列中压缩 packet 数据总字节数。
#define MAX_AUDIO_SIZE (1024 * 16 * 25 * 10)
#define MAX_VIDEO_SIZE (1024 * 255 * 25 * 2)

// 跳转时放入队列的特殊 packet。解码线程取到它后，只做 avcodec_flush_buffers，不做正常解码。
#define FLUSH_DATA "FLUSH"
#define FLUSH_DATA_SIZE 6

/*
 * VideoDecodeContext
 * ------------------------------------------------------------
 * 保存视频解码和像素格式转换过程中会反复使用的资源。
 *
 * 为什么要单独封装？
 * 课程 timer_callback 示例中每次定时器触发都重新 av_frame_alloc、sws_getContext、av_malloc，
 * 逻辑直观但效率较低。本项目把这些资源初始化一次、重复使用，播放结束时统一释放。
 */
struct VideoDecodeContext
{
    AVFrame *frame = nullptr;
    AVFrame *frameRGB = nullptr;
    SwsContext *swsCtx = nullptr;
    uint8_t *rgbBuffer = nullptr;
    int rgbBufferSize = 0;
};

// ------------------------- 网络连接中断回调 -------------------------
// RTMP/HLS 等网络协议基于 TCP，连接异常时 avformat_open_input() 会阻塞，
// 导致程序无响应。通过 interrupt_callback 设置超时中断，避免卡死。

// 中断回调的参数：记录开始连接时间和是否已连接成功
typedef struct {
    time_t lasttime;    // 开始连接的时间
    bool connected;     // 是否已连接成功
} Runner;

// 中断回调函数：FFmpeg 在阻塞 IO 操作期间会周期性调用此函数。
// 返回 1 表示中断当前阻塞操作，返回 0 表示继续等待。
static int interrupt_callback(void *p)
{
    Runner *r = static_cast<Runner *>(p);
    if (!r)
        return 0;

    if (r->lasttime > 0)
    {
        // 连接超过 5 秒仍未成功，中断阻塞
        if (time(nullptr) - r->lasttime > 5 && !r->connected)
            return 1;
    }
    return 0;
}

// ------------------------- 静态辅助函数声明 -------------------------
// SDL 音频设备回调函数：SDL 需要声音数据时自动调用。
static void audio_callback(void *userdata, Uint8 *stream, int len);

// 从音频队列取 packet，解码并重采样为 SDL 可播放的 PCM 数据。
static int audio_decode_frame(VideoState *is, uint8_t *audio_buf, int buf_size);

// 查找媒体文件中的视频流和音频流下标。
static int find_stream_index(AVFormatContext *pformat_ctx, int *video_stream, int *audio_stream);

// 有音频时使用的视频解码线程。它会以 audio_clock 为主时钟进行同步。
static int video_thread(void *arg);

// 无音频时使用的 SDL 定时器回调。按帧率定时解码一帧视频。
static Uint32 timer_callback(Uint32 interval, void *param);

// 根据 PTS 和帧间隔更新 video_clock，返回当前帧显示时间。
static double synchronize_video(VideoState *is, AVFrame *src_frame, double pts);

// 初始化/释放视频解码上下文。
static bool init_video_decode_context(VideoDecodeContext *ctx, AVCodecContext *pCodecCtx);
static void free_video_decode_context(VideoDecodeContext *ctx);

// 解码并显示一帧视频。syncToAudio=true 时执行音视频同步等待。
static bool decode_one_video_frame(VideoState *is, VideoDecodeContext *ctx, bool syncToAudio);

// 根据视频帧率计算 SDL 定时器间隔，单位毫秒。
static Uint32 calc_video_timer_interval_ms(AVStream *video_st, AVCodecContext *codec_ctx);

// 判断 packet 是否是 seek 后用于清空解码器缓存的特殊 FLUSH packet。
static bool is_flush_packet(const AVPacket *packet);

// 向队列放入 FLUSH packet。
static void put_flush_packet(PacketQueue *queue);

/*
 * VideoPlayer 构造函数
 * ------------------------------------------------------------
 * 初始化播放器状态为 Stop，并清空 VideoState。
 * 此时还没有打开文件，也没有启动线程。
 */
VideoPlayer::VideoPlayer(QObject *parent)
    : QThread(parent),
      m_playerState(PlayerState::Stop)
{
    resetVideoState();
}

/*
 * VideoPlayer 析构函数
 * ------------------------------------------------------------
 * 窗口关闭或对象销毁时，必须先 stop(true)，确保：
 * 1. 读线程退出；
 * 2. SDL 音频设备关闭；
 * 3. 音视频队列和 FFmpeg 资源释放。
 */
VideoPlayer::~VideoPlayer()
{
    stop(true);
}

/*
 * resetVideoState
 * ------------------------------------------------------------
 * 重置一次播放任务的所有状态。
 * 每次打开新文件前都会调用，避免上一次播放留下的指针、时钟、标志位影响新文件。
 */
void VideoPlayer::resetVideoState()
{
    std::memset(&m_videoState, 0, sizeof(VideoState));

    m_videoState.audioStream = -1;
    m_videoState.videoStream = -1;
    m_videoState.audioID = 0;
    m_videoState.video_timer = 0;
    m_videoState.video_timer_interval = 40;
    m_videoState.readThreadFinished = true;
    m_videoState.videoThreadFinished = true;
    m_videoState.m_player = this;

    av_init_packet(&m_videoState.audio_pkt);
    m_videoState.audio_pkt.data = nullptr;
    m_videoState.audio_pkt.size = 0;
}

/*
 * closeAudioDevice
 * ------------------------------------------------------------
 * 关闭 SDL 音频设备。
 *
 * SDL_OpenAudioDevice() 后，SDL 会在内部创建音频回调线程。
 * 停止播放时必须先暂停并关闭设备，否则 audio_callback 可能继续访问已释放的 VideoState。
 */
void VideoPlayer::closeAudioDevice()
{
    if (m_videoState.audioID != 0)
    {
        // 暂停并关闭 SDL 音频设备，停止 SDL 的音频回调线程。
        SDL_PauseAudioDevice(m_videoState.audioID, 1);
        SDL_CloseAudioDevice(m_videoState.audioID);
        m_videoState.audioID = 0;
    }
}

/*
 * sendBlackImage
 * ------------------------------------------------------------
 * 向界面发送一张黑图，用于停止播放后清空画面。
 */
void VideoPlayer::sendBlackImage()
{
    QImage img(16, 16, QImage::Format_RGB32);
    img.fill(Qt::black);
    SendGetOneImage(img);
}

/*
 * setFileName
 * ------------------------------------------------------------
 * 设置媒体文件路径并启动播放线程。
 *
 * 注意：这个函数不只是“设置路径”，而是“打开文件并播放”的入口。
 * 按照课程设计，如果当前不是 Stop 状态，应先由 UI 调用 stop(true) 停掉旧任务，
 * 再调用 setFileName() 打开新文件。
 */
void VideoPlayer::setFileName(const QString &fileName)
{
    // 课件中的逻辑：setFileName 不只是保存路径，还会直接启动播放线程。
    // 如果当前不是 Stop，说明已有播放任务，应先 stop(true) 后再打开新文件。
    if (m_playerState != PlayerState::Stop)
        return;

    m_fileName = fileName;
    m_playerState = PlayerState::Playing;
    emit SIG_PlayerStateChanged(PlayerState::Playing);
    start();
}

/*
 * play
 * ------------------------------------------------------------
 * 从暂停状态恢复播放。
 *
 * 本函数只负责把 isPause 改为 false，并通知 UI 更新按钮。
 * 真正恢复的逻辑发生在三个循环位置：
 * 1. run() 读线程继续 av_read_frame；
 * 2. video_thread/timer_callback 继续解码视频；
 * 3. audio_callback 继续填充 SDL 音频缓冲区。
 */
void VideoPlayer::play()
{
    if (m_playerState != PlayerState::Pause)
        return;

    // 三个执行位置都会检查 isPause：读线程、视频线程/定时器、音频回调。
    m_videoState.isPause = false;
    m_playerState = PlayerState::Playing;
    emit SIG_PlayerStateChanged(PlayerState::Playing);
}

/*
 * pause
 * ------------------------------------------------------------
 * 暂停播放。
 *
 * 这里不关闭文件、不释放解码器，只设置 isPause。
 * 读线程、视频线程、音频回调检测到 isPause 后会短暂 SDL_Delay 并跳过解码。
 */
void VideoPlayer::pause()
{
    if (m_playerState != PlayerState::Playing)
        return;

    m_videoState.isPause = true;
    m_playerState = PlayerState::Pause;
    emit SIG_PlayerStateChanged(PlayerState::Pause);
}

/*
 * stop
 * ------------------------------------------------------------
 * 停止播放。
 *
 * 参数 isWait：
 * true  ：阻塞等待 run() 线程退出，适合打开新文件前或窗口关闭时调用；
 * false ：只发出停止请求，不等待，适合某些非阻塞场景。
 *
 * stop 的核心是设置 quit=true，让各线程在自己的循环里安全退出。
 */
void VideoPlayer::stop(bool isWait)
{
    // stop 可能在未播放时被调用，保持幂等。
    m_videoState.quit = true;
    m_videoState.isPause = false;

    // 停止纯视频定时器。若 timer_callback 正在执行，后续清理会通过 mutex 避免并发释放。
    if (m_videoState.video_timer != 0)
    {
        SDL_RemoveTimer(m_videoState.video_timer);
        m_videoState.video_timer = 0;
    }

    closeAudioDevice();

    if (isWait && isRunning() && QThread::currentThread() != this)
    {
        // 等待读线程 run() 完成资源释放。wait 比手动 while + SDL_Delay 更符合 Qt 线程用法。
        wait(3000);
    }

    if (m_playerState != PlayerState::Stop)
    {
        m_playerState = PlayerState::Stop;
        emit SIG_PlayerStateChanged(PlayerState::Stop);
    }
}

/*
 * seek
 * ------------------------------------------------------------
 * 请求跳转到指定位置。
 *
 * pos 单位：微秒。
 *
 * 这里不直接调用 av_seek_frame()，而是只设置 seek_req/seek_pos。
 * 原因：av_read_frame() 和 av_seek_frame() 都操作同一个 AVFormatContext，
 * 应放在同一个读线程中执行，避免多线程并发访问导致不稳定。
 */
void VideoPlayer::seek(qint64 pos)
{
    if (m_playerState == PlayerState::Stop)
        return;

    // UI 线程只设置请求，真正的 av_seek_frame 放在读线程中执行，避免与 av_read_frame 并发。
    //
    // 快退问题修复说明：
    // 如果当前播放到 100 秒，用户点击回 20 秒，视频线程可能已经拿到 100 秒附近的旧 packet，
    // 并在“video_pts > audio_clock”的同步等待循环里等待音频追上。
    // 因此 seek 一发起，就立即重置同步状态，并要求音频/视频线程在收到 FLUSH packet 前丢弃旧 packet。
    if (!m_videoState.seek_req)
    {
        m_videoState.seek_pos = pos;
        m_videoState.seek_req = 1;

        m_videoState.seek_time = static_cast<double>(pos);
        m_videoState.audio_clock = static_cast<double>(pos);
        m_videoState.video_clock = 0.0;
        m_videoState.seek_flag_audio = 1;
        m_videoState.seek_flag_video = 1;
        m_videoState.seek_wait_audio_flush = 1;
        m_videoState.seek_wait_video_flush = 1;

        // 清掉 SDL 音频回调还没播放完的旧 PCM 缓冲，避免 seek 后先听到一小段旧声音。
        if (m_videoState.audioID != 0)
            SDL_LockAudioDevice(m_videoState.audioID);

        m_videoState.audio_buf_size = 0;
        m_videoState.audio_buf_index = 0;
        m_videoState.audio_pkt_size = 0;
        m_videoState.audio_pkt_data = nullptr;
        if (m_videoState.audio_pkt_valid)
        {
            av_packet_unref(&m_videoState.audio_pkt);
            m_videoState.audio_pkt_valid = false;
        }

        if (m_videoState.audioID != 0)
            SDL_UnlockAudioDevice(m_videoState.audioID);
    }
}

/*
 * getCurrentTime
 * ------------------------------------------------------------
 * 返回当前播放时间，单位微秒。
 *
 * 有音频时：以 audio_clock 为准，因为音频播放通常更连续、更稳定；
 * 无音频时：以 video_clock 为准，用于纯视频文件进度显示。
 */
double VideoPlayer::getCurrentTime() const
{
    // 有音频时以音频时钟为准；纯视频时以视频时钟为准。
    if (m_videoState.audioStream != -1)
        return m_videoState.audio_clock;
    return m_videoState.video_clock;
}

/*
 * getTotalTime
 * ------------------------------------------------------------
 * 返回媒体总时长，单位微秒。
 * pFormatCtx->duration 是 FFmpeg 在 avformat_find_stream_info() 后得到的总时长。
 */
qint64 VideoPlayer::getTotalTime() const
{
    if (m_videoState.pFormatCtx)
    {
        qint64 dur = m_videoState.pFormatCtx->duration;
        // 直播流（RTMP/HLS）的 duration 为 0 或 AV_NOPTS_VALUE，返回 0 表示无总时长
        if (dur == AV_NOPTS_VALUE || dur <= 0)
            return 0;
        return dur;
    }
    return -1;
}

/*
 * playerState
 * ------------------------------------------------------------
 * 返回当前播放状态，供 UI 判断按钮行为。
 */
PlayerState VideoPlayer::playerState() const
{
    return m_playerState;
}

/*
 * SendGetOneImage
 * ------------------------------------------------------------
 * 从 SDL 视频线程/定时器线程向 Qt UI 线程发送图像。
 *
 * 不能在 SDL 线程里直接操作 QLabel，否则会跨线程访问 UI，容易崩溃。
 * 正确做法是 emit 信号，由 Qt 主线程中的槽函数显示图像。
 */
void VideoPlayer::SendGetOneImage(const QImage &img)
{
    emit SIG_getOneImage(img);
}

/*
 * run
 * ------------------------------------------------------------
 * VideoPlayer 的读线程主函数。
 *
 * 主要流程：
 * 1. 初始化 FFmpeg/SDL；
 * 2. 打开媒体文件；
 * 3. 查找音频流、视频流；
 * 4. 打开对应解码器；
 * 5. 创建音频队列、视频队列；
 * 6. 有音频则启动音频设备，有视频则启动视频线程或视频定时器；
 * 7. 循环 av_read_frame()，把 packet 放入 audioq/videoq；
 * 8. 处理暂停、停止、seek、读完文件、资源释放。
 */
void VideoPlayer::run()
{
    qDebug() << "VideoPlayer::run";
//run() 第一阶段：启动前检查和状态初始化
    if (m_fileName.isEmpty())
    {
        qDebug() << "file name is empty";
        m_playerState = PlayerState::Stop;
        emit SIG_PlayerStateChanged(PlayerState::Stop);
        return;
    }

    resetVideoState();
    m_videoState.readThreadFinished = false;
    m_videoState.videoThreadFinished = true;
    m_videoState.isPause = false;
    m_videoState.quit = false;
//run() 第二阶段：初始化 FFmpeg 和 SDL
    // FFmpeg 4.2.2 中仍可保留。以后升级到 FFmpeg 5/6 后可删除。0
    av_register_all();

    // 初始化 FFmpeg 网络子系统（Windows 上会调用 WSAStartup）。
    // 不调用此函数，RTMP/HTTP/HLS 等网络协议均无法打开。
    avformat_network_init();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0)
    {
        qDebug() << "Couldn't init SDL:" << SDL_GetError();
        m_playerState = PlayerState::Stop;
        emit SIG_PlayerStateChanged(PlayerState::Stop);
        m_videoState.readThreadFinished = true;
        return;
    }
//run() 第三阶段：打开媒体文件
    /*AVFormatContext 是整个媒体文件的总管理对象。它里面包含：
     * 文件封装格式信息
     * 媒体总时长
     * 所有音视频流信息
     * 每一路 stream 的 time_base
     * 读取 packet 所需的内部状态*/
    AVFormatContext *pFormatCtx = avformat_alloc_context();
    if (!pFormatCtx)
    {
        qDebug() << "Could not allocate AVFormatContext";
        m_playerState = PlayerState::Stop;
        emit SIG_PlayerStateChanged(PlayerState::Stop);
        m_videoState.readThreadFinished = true;
        return;
    }

    QByteArray pathBytes = m_fileName.toLocal8Bit();
    const char *file_path = pathBytes.constData();

    // 设置网络连接中断回调，避免 RTMP/HLS 等网络协议连接异常时 avformat_open_input 阻塞卡死。
    Runner input_runner = {0};
    pFormatCtx->interrupt_callback.callback = interrupt_callback;
    pFormatCtx->interrupt_callback.opaque = &input_runner;
    input_runner.lasttime = time(nullptr);
    input_runner.connected = false;

    /*avformat_open_input()
      它负责真正打开文件。打开成功后，FFmpeg 知道这是一个什么封装格式，例如：MP4 MKV AVI FLV MP3 WAV*/
    AVDictionary *options = nullptr;
    // 网络流超时：10秒（单位微秒），与 interrupt_callback 双重保护
    av_dict_set(&options, "rw_timeout", "10000000", 0);

    int ret = avformat_open_input(&pFormatCtx, file_path, nullptr, &options);
    av_dict_free(&options);
    if (ret != 0)
    {
        char errbuf[128] = {0};
        av_strerror(ret, errbuf, sizeof(errbuf));
        qDebug() << "can't open file:" << m_fileName << "error code:" << ret << "error:" << errbuf;

        avformat_free_context(pFormatCtx);
        m_playerState = PlayerState::Stop;
        emit SIG_PlayerStateChanged(PlayerState::Stop);
        m_videoState.readThreadFinished = true;
        return;
    }
    // 连接成功，标记已连接，后续 IO 操作不再被中断回调拦截
    input_runner.connected = true;
    /*avformat_find_stream_info()
    它会进一步读取一部分数据，分析文件里有哪些流。*/
    if (avformat_find_stream_info(pFormatCtx, nullptr) < 0)
    {
        qDebug() << "Couldn't find stream information";
        avformat_close_input(&pFormatCtx);
        m_playerState = PlayerState::Stop;
        emit SIG_PlayerStateChanged(PlayerState::Stop);
        m_videoState.readThreadFinished = true;
        return;
    }
//run() 第四阶段：查找音视频流
    /*find_stream_index() 做的事情是遍历：pFormatCtx->streams[i]，判断每一路流的类型：找到后保存下标。
     * 如果两者都是 -1，说明文件里没有可播放的音视频流，直接退出。*/
    int videoStream = -1;
    int audioStream = -1;
    find_stream_index(pFormatCtx, &videoStream, &audioStream);

    if (videoStream == -1 && audioStream == -1)
    {
        qDebug() << "No audio stream or video stream found";
        avformat_close_input(&pFormatCtx);
        m_playerState = PlayerState::Stop;
        emit SIG_PlayerStateChanged(PlayerState::Stop);
        m_videoState.readThreadFinished = true;
        return;
    }

    m_videoState.pFormatCtx = pFormatCtx;
    m_videoState.videoStream = videoStream;
    m_videoState.audioStream = audioStream;

    // 总时长给 UI，用于设置进度条范围和右侧总时长标签。
    emit SIG_TotalTime(getTotalTime());

//run() 第五阶段：初始化视频解码器
    /*AVCodecContext 可以理解为：某一路编码流的解码上下文*/
    //////////////////////////// 初始化视频 ////////////////////////////
    if (videoStream != -1)
    {
        AVCodecContext *pCodecCtx = pFormatCtx->streams[videoStream]->codec;
        AVCodec *pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
        if (!pCodec)
        {
            qDebug() << "video codec not found";
            m_videoState.videoStream = -1;
        }
        else if (avcodec_open2(pCodecCtx, pCodec, nullptr) < 0)
        {
            qDebug() << "could not open video codec";
            m_videoState.videoStream = -1;
        }
        else
        {
            m_videoState.video_st = pFormatCtx->streams[videoStream];
            m_videoState.pCodecCtx = pCodecCtx;
            m_videoState.videoq = new PacketQueue;
            packet_queue_init(m_videoState.videoq);
        }
    }

//run() 第六阶段：初始化音频解码器和 SDL 音频设备
    //////////////////////////// 初始化音频 ////////////////////////////
    if (audioStream != -1)
    {
        AVCodecContext *pAudioCodecCtx = pFormatCtx->streams[audioStream]->codec;
        AVCodec *pAudioCodec = avcodec_find_decoder(pAudioCodecCtx->codec_id);
        if (!pAudioCodec)
        {
            qDebug() << "audio codec not found";
            m_videoState.audioStream = -1;
        }
        else if (avcodec_open2(pAudioCodecCtx, pAudioCodec, nullptr) < 0)
        {
            qDebug() << "could not open audio codec";
            m_videoState.audioStream = -1;
        }
        else
        {
            m_videoState.audio_st = pFormatCtx->streams[audioStream];
            m_videoState.pAudioCodecCtx = pAudioCodecCtx;
            /*创建音频队列和音频帧：*/
            m_videoState.audioq = new PacketQueue;
            packet_queue_init(m_videoState.audioq);
            m_videoState.audioFrame = av_frame_alloc();

            SDL_AudioSpec wanted_spec;
            SDL_AudioSpec spec;
            std::memset(&wanted_spec, 0, sizeof(wanted_spec));
            std::memset(&spec, 0, sizeof(spec));
            /*配置 SDL 音频设备：它告诉 SDL：
             * 我希望你用这个采样率、声道数、采样格式播放音频。
             * 当你需要音频数据时，请调用 audio_callback。
             * audio_callback 里可以通过 userdata 拿到 VideoState。*/
            wanted_spec.freq = pAudioCodecCtx->sample_rate;
            wanted_spec.format = AUDIO_S16SYS;          // 统一输出为 16-bit signed PCM
            wanted_spec.channels = static_cast<Uint8>(pAudioCodecCtx->channels);
            wanted_spec.silence = 0;
            wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
            wanted_spec.callback = audio_callback;
            wanted_spec.userdata = &m_videoState;

            m_videoState.audioID = SDL_OpenAudioDevice(nullptr, 0, &wanted_spec, &spec, 0);
            if (m_videoState.audioID == 0)
            {
                qDebug() << "Couldn't open Audio:" << SDL_GetError();
                m_videoState.audioStream = -1;
            }
            else
            {
                // out_frame 是 swr_convert 的目标格式参数。
                m_videoState.out_frame.format = AV_SAMPLE_FMT_S16;
                m_videoState.out_frame.sample_rate = spec.freq;
                m_videoState.out_frame.channel_layout = av_get_default_channel_layout(spec.channels);
                m_videoState.out_frame.channels = spec.channels;

                SDL_PauseAudioDevice(m_videoState.audioID, 0); // 0 表示开始播放音频设备注意：这并不代表立刻有声音，而是 SDL 音频线程开始运行，并周期性调用 audio_callback() 要 PCM 数据
            }
        }
    }

//run() 第七阶段：启动视频线程或视频定时器
    //////////////////////////// 启动视频线程或纯视频定时器 ////////////////////////////
    if (m_videoState.videoStream != -1)
    {
        m_videoState.videoThreadFinished = false;

        if (m_videoState.audioStream != -1)
        {
            // 有音频：按照同步课件，以音频时钟作为主时钟，视频在线程中等待音频
            /*原因是音频播放由声卡节奏驱动，一般更稳定。
             * 如果视频作为主时钟，声音很容易卡顿或破音。*/
            m_videoState.video_tid = SDL_CreateThread(video_thread, "video_thread", &m_videoState);
            if (!m_videoState.video_tid)
            {
                qDebug() << "SDL_CreateThread failed:" << SDL_GetError();
                m_videoState.videoThreadFinished = true;
            }
        }
        else
        {   /*如果没有音频，就没有 audio_clock
               这时不能让视频线程无限 while 解码，否则视频会跑得飞快。所以项目使用：SDL_AddTimer()
               例如：25 fps → 每 40 ms 解码并显示一帧
                    30 fps → 每 33 ms 解码并显示一帧*/
            // 无音频：沿用上一节同步补充，用 SDL_AddTimer 按帧率驱动视频解码。
            m_videoState.timer_video_ctx = new VideoDecodeContext;
            if (!init_video_decode_context(m_videoState.timer_video_ctx, m_videoState.pCodecCtx))
            {
                qDebug() << "init timer video decode context failed";
                delete m_videoState.timer_video_ctx;
                m_videoState.timer_video_ctx = nullptr;
                m_videoState.videoThreadFinished = true;
            }
            else
            {
                m_videoState.video_timer_mutex = SDL_CreateMutex();
                m_videoState.video_timer_interval = calc_video_timer_interval_ms(m_videoState.video_st,
                                                                                 m_videoState.pCodecCtx);
                m_videoState.video_timer = SDL_AddTimer(m_videoState.video_timer_interval,
                                                        timer_callback,
                                                        &m_videoState);
                if (m_videoState.video_timer == 0)
                {
                    qDebug() << "SDL_AddTimer failed:" << SDL_GetError();
                    m_videoState.videoThreadFinished = true;
                }
            }
        }
    }

    if (m_videoState.videoStream == -1 && m_videoState.audioStream == -1)
    {
        qDebug() << "No usable stream after codec initialization";
        m_videoState.quit = true;
    }

    AVPacket packet;
    av_init_packet(&packet);
    packet.data = nullptr;
    packet.size = 0;

    int delayCount = 0;

//run() 第八阶段：主循环读取 AVPacket 是整个run（）的核心
    /*while (!m_videoState.quit)
{
    处理 seek
    处理 pause
    控制队列大小
    av_read_frame()
    按 stream_index 分发 packet
    av_packet_unref()
}*/
    //////////////////////////// 读线程主循环 ////////////////////////////
    while (!m_videoState.quit)
    {
        //////////////////////////// 处理跳转请求 ////////////////////////////
        // 1. 为什么先处理 seek，再处理 pause？
        // 跳转请求放在暂停判断之前：这样暂停状态下点击进度条也能先完成 seek，恢复播放后从新位置开始。
        if (m_videoState.seek_req)   //2. seek 的具体执行逻辑 这段里面最难的是时间基转换
        {
            int stream_index = -1;
            int64_t seek_target = m_videoState.seek_pos; // 微秒

            if (m_videoState.videoStream >= 0)
                stream_index = m_videoState.videoStream;
            else if (m_videoState.audioStream >= 0)
                stream_index = m_videoState.audioStream;

            if (stream_index >= 0)
            {
                AVRational timeBase = {1, AV_TIME_BASE};
                int64_t targetInStreamTimeBase = av_rescale_q(seek_target,
                                                              timeBase,
                                                              pFormatCtx->streams[stream_index]->time_base);
                //3.为什么 seek 前要做 av_rescale_q()？
                /*为什么 seek 前要做av_rescale_q()？
                  因为 项目内部统一使用：微秒，这对应 FFmpeg 的：AV_TIME_BASE，所以 UI 进度条传进来的 seek_pos 是微秒。
                   但是 av_seek_frame() 要求传入的时间戳单位不是固定微秒，而是目标 stream 自己的：stream->time_base
                    例如视频流 time_base 可能是1/90000 所以必须通过：av_rescale_q()完成单位转换,如果不转换，这就完全跳错位置了*/
                if (av_seek_frame(pFormatCtx, stream_index, targetInStreamTimeBase, AVSEEK_FLAG_BACKWARD) < 0)
                {   //4. 为什么用 AVSEEK_FLAG_BACKWARD？
                    /*为什么用 AVSEEK_FLAG_BACKWARD？
                     * 这个标志表示：尽量跳到目标时间之前的关键帧
                     * 视频不能随便从任意一帧开始解码，尤其是 H.264/H.265 这类编码。因为普通 P 帧、B 帧通常依赖前面的参考帧。只有关键帧，通常也就是 I 帧，才适合作为解码起点。
                     * 所以 seek 到 20 秒时，FFmpeg 可能实际跳到：18.6 秒的关键帧
                     * 然后解码线程会继续解码，但丢弃 18.6 到 20 秒之间的帧，直到到达目标时间后再显示/播放。
                     * 这就是项目里这两个标志的作用：m_videoState.seek_flag_audio = 1;m_videoState.seek_flag_video = 1;
                     * 它们表示：seek 后还没真正到达目标时间，关键帧到目标点之间的数据需要丢弃。*/
                    qDebug() << "error while seeking";
                }
                else
                {
                    // 清空队列，并放入 FLUSH packet。只清队列不清解码器，会把跳转前后的参考帧混在一起，导致花屏或杂音。
                    if (m_videoState.audioStream >= 0 && m_videoState.audioq)
                    {
                        packet_queue_flush(m_videoState.audioq);
                        put_flush_packet(m_videoState.audioq);
                    }
                    if (m_videoState.videoStream >= 0 && m_videoState.videoq)
                    {
                        packet_queue_flush(m_videoState.videoq);
                        put_flush_packet(m_videoState.videoq);
                    }

                    m_videoState.audio_clock = static_cast<double>(seek_target);
                    m_videoState.video_clock = 0.0;  // 快退时必须清 0，否则视频可能一直等待旧的音频时钟
                    m_videoState.seek_time = static_cast<double>(seek_target);
                    m_videoState.seek_flag_audio = 1;
                    m_videoState.seek_flag_video = 1;

                    // 清空队列后放入 FLUSH，但解码线程可能已经拿到旧 packet。
                    // 这两个标志让解码线程在真正收到 FLUSH 前丢弃旧 packet，修复“向左快退视频不动/响应慢”。
                    m_videoState.seek_wait_audio_flush = 1;
                    m_videoState.seek_wait_video_flush = 1;

                    m_videoState.readFinished = false;
                }
            }

            m_videoState.seek_req = 0;
        }

        // 暂停时读线程也暂停，避免暂停期间队列继续被大量填满。
        if (m_videoState.isPause)
        {
            SDL_Delay(10);
            continue;
        }

        //5. 队列水位控制
        // 控制队列大小，避免读线程一次性把文件读完导致内存暴涨。
        if (m_videoState.audioStream != -1 && m_videoState.audioq &&
            m_videoState.audioq->size > MAX_AUDIO_SIZE)//队列水位控制，防止读线程太快。
            /*如果没有这个限制，av_read_frame() 可能很快把整个文件的 packet 都读进内存，导致：
             * 内存占用持续增长
             * seek 响应变慢
             * stop 时清理耗时
             * 音视频队列积压严重
             * 所以这里相当于做了一个简单的缓存水位控制：
             * 队列没满：继续读
             * 队列太满：读线程暂时休眠，让解码线程消费*/
        {
            SDL_Delay(10);
            continue;
        }
        if (m_videoState.videoStream != -1 && m_videoState.videoq &&
            m_videoState.videoq->size > MAX_VIDEO_SIZE)
        {
            SDL_Delay(10);
            continue;
        }
        //6.读包
        int ret = av_read_frame(pFormatCtx, &packet);/*读包，成功时，packet 里面会包含：
                                                                    packet.stream_index
                                                                    packet.pts
                                                                    packet.dts
                                                                    packet.data
                                                                    packet.size
                                                                    packet.duration ，其中：
                                                                                packet.data / packet.size：压缩数据本体
                                                                                packet.stream_index：属于哪一路流
                                                                                packet.pts / dts：时间戳*/

//run() 第九阶段：读到文件末尾怎么办？
        /*这里的设计思想是：
         * av_read_frame() 读不到新 packet 了，不代表马上可以释放播放器。因为队列里可能还有未播放完的数据。
         * 如果读线程立刻释放资源，音频回调和视频线程就会访问已经释放的解码器、队列或格式上下文。
         * 所以项目使用：readFinished = true 告诉音频/视频消费者：文件已经读完了，你们把队列消费完后就可以结束。
         * 真正结束通常由音频或视频线程检测到：readFinished == true 并且 audioq / videoq 已经空了 然后设置：is->quit = true; 读线程主循环才退出。*/
        if (ret < 0)
        {
            // 课件里强调：读到末尾不应立刻释放退出，要给解码线程时间把队列里剩余数据播完，读线程主循环才退出。。
            delayCount++;
            if (delayCount >= 300)
            {
                m_videoState.readFinished = true;
                delayCount = 0;
            }
            SDL_Delay(10);
            continue;
        }
        delayCount = 0;

        //7. 按 stream_index 分发 packet
        /*这就是 demuxer 的核心职责：
         * 从一个混合媒体文件中读出 packet
         * 判断 packet 属于音频还是视频
         * 分别放入 audioq 或 videoq*/
        if (packet.stream_index == m_videoState.videoStream && m_videoState.videoq)
        {
            packet_queue_put(m_videoState.videoq, &packet);
        }
        else if (packet.stream_index == m_videoState.audioStream && m_videoState.audioq)
        {
            packet_queue_put(m_videoState.audioq, &packet);
        }
        //注意最后必须：av_packet_unref(&packet);因为 packet_queue_put() 内部已经 av_packet_ref() 了。
        //也就是说：队列拥有一份引用，run() 局部 packet 用完后释放自己的引用，如果不 av_packet_unref()，会内存泄漏。
        av_packet_unref(&packet);
    }

//run() 第十阶段：退出和资源释放
    /*主循环结束后，会执行清理：
     * 1. 移除 SDL 定时器
     * 2. 等待 SDL 视频线程退出
     * 3. 关闭 SDL 音频设备
     * 4. 释放视频定时器上下文
     * 5. 销毁 videoq
     * 6. 销毁 audioq
     * 7. 释放 audio_pkt
     * 8. 释放 audioFrame
     * 9. 关闭音频解码器
     * 10. 关闭视频解码器
     * 11. 关闭 AVFormatContext
     * 12. 设置状态为 Stop
     * 13. 通知 UI
     * 这里的释放顺序很重要。尤其是：先停线程 / 音频设备，再释放队列和解码器
     * 如果先释放队列，再关闭 SDL 音频设备，那么 SDL 的 audio_callback() 可能还在运行，会导致崩溃*/
    //////////////////////////// 等待解码线程结束 ////////////////////////////
    if (m_videoState.video_timer != 0)
    {
        SDL_RemoveTimer(m_videoState.video_timer);
        m_videoState.video_timer = 0;
    }

    if (m_videoState.video_tid)
    {
        SDL_WaitThread(m_videoState.video_tid, nullptr);
        m_videoState.video_tid = nullptr;
    }

    closeAudioDevice();

    //////////////////////////// 释放视频定时器资源 ////////////////////////////
    if (m_videoState.video_timer_mutex)
        SDL_LockMutex(m_videoState.video_timer_mutex);

    if (m_videoState.timer_video_ctx)
    {
        free_video_decode_context(m_videoState.timer_video_ctx);
        delete m_videoState.timer_video_ctx;
        m_videoState.timer_video_ctx = nullptr;
    }

    if (m_videoState.video_timer_mutex)
    {
        SDL_UnlockMutex(m_videoState.video_timer_mutex);
        SDL_DestroyMutex(m_videoState.video_timer_mutex);
        m_videoState.video_timer_mutex = nullptr;
    }

    //////////////////////////// 清空队列 ////////////////////////////
    if (m_videoState.videoq)
    {
        packet_queue_destroy(m_videoState.videoq);
        delete m_videoState.videoq;
        m_videoState.videoq = nullptr;
    }
    if (m_videoState.audioq)
    {
        packet_queue_destroy(m_videoState.audioq);
        delete m_videoState.audioq;
        m_videoState.audioq = nullptr;
    }

    //////////////////////////// 关闭解码器和文件 ////////////////////////////
    if (m_videoState.audio_pkt_valid)
    {
        av_packet_unref(&m_videoState.audio_pkt);
        m_videoState.audio_pkt_valid = false;
    }

    if (m_videoState.audioFrame)
    {
        av_frame_free(&m_videoState.audioFrame);
        m_videoState.audioFrame = nullptr;
    }

    if (m_videoState.pAudioCodecCtx)
    {
        avcodec_close(m_videoState.pAudioCodecCtx);
        m_videoState.pAudioCodecCtx = nullptr;
    }

    if (m_videoState.pCodecCtx)
    {
        avcodec_close(m_videoState.pCodecCtx);
        m_videoState.pCodecCtx = nullptr;
    }

    if (pFormatCtx)
    {
        avformat_close_input(&pFormatCtx);
        m_videoState.pFormatCtx = nullptr;
    }

    m_videoState.readThreadFinished = true;
    m_videoState.videoThreadFinished = true;

    m_playerState = PlayerState::Stop;
    emit SIG_PlayerStateChanged(PlayerState::Stop);
}

/*
 * audio_callback
 * ------------------------------------------------------------
 * SDL 音频回调函数。
 *
 * SDL 需要播放声音时，会自动调用这个函数，要求我们向 stream 填入 len 字节 PCM 数据。
 *
 * 本函数不直接 av_read_frame()，而是从 audioq 中取 packet 解码。
 * 这样可以让“读文件”和“音频播放”解耦，避免视频解码阻塞音频播放。
 */
static void audio_callback(void *userdata, Uint8 *stream, int len)
{
    VideoState *is = static_cast<VideoState *>(userdata);
    if (!is || !stream || len <= 0)
        return;

    // 先清空 SDL 播放缓冲区。暂停时直接返回，避免反复播放上一段残留声音。
    std::memset(stream, 0, len);
    if (is->quit || is->isPause)
        return;

    while (len > 0)
    {
        if (is->audio_buf_index >= is->audio_buf_size)
        {
            int audio_data_size = audio_decode_frame(is, is->audio_buf, sizeof(is->audio_buf));
            if (audio_data_size < 0)
            {
                // 暂时没有音频数据时播放静音。
                is->audio_buf_size = 1024;
                std::memset(is->audio_buf, 0, is->audio_buf_size);
            }
            else
            {
                is->audio_buf_size = static_cast<unsigned int>(audio_data_size);
            }
            is->audio_buf_index = 0;
        }

        int len1 = static_cast<int>(is->audio_buf_size - is->audio_buf_index);
        if (len1 > len)
            len1 = len;

        SDL_MixAudioFormat(stream,
                           is->audio_buf + is->audio_buf_index,
                           AUDIO_S16SYS,
                           len1,
                           SDL_MIX_MAXVOLUME);

        len -= len1;
        stream += len1;
        is->audio_buf_index += len1;
    }
}

/*
 * audio_decode_frame
 * ------------------------------------------------------------
 * 解码一段音频数据，输出到 audio_buf。
 *
 * 返回值：
 * >0：成功解码并重采样后的 PCM 字节数；
 * <0：暂时没有数据或解码失败，audio_callback 会填充静音。
 *
 * 重要细节：一个 AVPacket 中可能包含多帧音频，所以函数内部用
 * audio_pkt_data/audio_pkt_size 保存当前 packet 尚未解码完的部分。
 */
static int audio_decode_frame(VideoState *is, uint8_t *audio_buf, int buf_size)
{
    if (!is || !audio_buf || buf_size <= 0 || !is->pAudioCodecCtx || !is->audioFrame || !is->audioq)
        return -1;

    AVCodecContext *codecCtx = is->pAudioCodecCtx;
    AVFrame *frame = is->audioFrame;

    for (;;)
    {
        if (is->quit)
            return -1;

        if (is->isPause)
        {
            SDL_Delay(10);
            continue;
        }

        while (is->audio_pkt_size > 0)
        {
            int got_frame = 0;
            av_frame_unref(frame);

            AVPacket tmpPkt = is->audio_pkt;
            tmpPkt.data = is->audio_pkt_data;
            tmpPkt.size = is->audio_pkt_size;

            int ret = avcodec_decode_audio4(codecCtx, frame, &got_frame, &tmpPkt);
            if (ret < 0)
            {
                qDebug() << "Error in decoding audio frame";
                is->audio_pkt_size = 0;
                break;
            }

            is->audio_pkt_data += ret;
            is->audio_pkt_size -= ret;

            if (!got_frame || frame->nb_samples <= 0)
                continue;

            // 音频时钟：优先使用 packet pts；没有 pts 时，沿用当前时钟。
            if (is->audio_pkt.pts != AV_NOPTS_VALUE)
                is->audio_clock = is->audio_pkt.pts * av_q2d(is->audio_st->time_base) * 1000000.0;

            // 跳转到关键点后，关键点到目标点之间的音频帧要丢弃，不送入 SDL。
            if (is->seek_flag_audio)
            {
                if (is->audio_clock < is->seek_time)
                    continue;
                is->seek_flag_audio = 0;
            }

            int64_t in_channel_layout = frame->channel_layout;
            if (in_channel_layout == 0)
                in_channel_layout = av_get_default_channel_layout(frame->channels);

            SwrContext *swr_ctx = swr_alloc_set_opts(nullptr,
                                                     is->out_frame.channel_layout,
                                                     static_cast<AVSampleFormat>(is->out_frame.format),
                                                     is->out_frame.sample_rate,
                                                     in_channel_layout,
                                                     static_cast<AVSampleFormat>(frame->format),
                                                     frame->sample_rate,
                                                     0,
                                                     nullptr);
            if (!swr_ctx || swr_init(swr_ctx) < 0)
            {
                if (swr_ctx) swr_free(&swr_ctx);
                qDebug() << "swr_init error";
                return -1;
            }

            int outSamples = swr_convert(swr_ctx,
                                         &audio_buf,
                                         AVCODEC_MAX_AUDIO_FRAME_SIZE,
                                         const_cast<const uint8_t **>(frame->data),
                                         frame->nb_samples);
            swr_free(&swr_ctx);

            if (outSamples <= 0)
                return -1;

            int bytesPerSample = av_get_bytes_per_sample(static_cast<AVSampleFormat>(is->out_frame.format));
            int data_size = outSamples * is->out_frame.channels * bytesPerSample;
            data_size = std::min(data_size, buf_size);

            // 当前 frame 播放结束后，推进音频时钟。
            double duration = frame->nb_samples * 1000000.0 / frame->sample_rate;
            is->audio_clock += duration;

            return data_size;
        }

        if (is->audio_pkt_valid)
        {
            av_packet_unref(&is->audio_pkt);
            is->audio_pkt_valid = false;
        }

        AVPacket pkt;
        av_init_packet(&pkt);
        pkt.data = nullptr;
        pkt.size = 0;

        if (packet_queue_get(is->audioq, &pkt, 0) <= 0)
        {
            if (is->readFinished && is->audioq->nb_packets == 0)
            {
                // 纯音频文件或视频已经结束时，可以通知读线程退出。
                if (is->videoStream == -1 || is->videoThreadFinished || !is->videoq || is->videoq->nb_packets == 0)
                    is->quit = true;
            }
            return -1;
        }

        if (is_flush_packet(&pkt))
        {
            // 跳转后清空音频解码器内部缓存，避免跳转前残留数据影响跳转后的声音。
            avcodec_flush_buffers(is->pAudioCodecCtx);
            av_packet_unref(&pkt);

            // FLUSH 已经被音频线程收到，可以重新开始接收 seek 后的新 packet。
            is->seek_wait_audio_flush = 0;
            is->audio_pkt_size = 0;
            is->audio_pkt_data = nullptr;
            is->audio_buf_size = 0;
            is->audio_buf_index = 0;
            continue;
        }

        // seek 发起后、FLUSH packet 到达前，可能还会取到旧位置的 packet。
        // 这些 packet 必须丢弃，否则会出现声音/时钟短暂回到旧位置的问题。
        if (is->seek_wait_audio_flush)
        {
            av_packet_unref(&pkt);
            continue;
        }

        is->audio_pkt = pkt;
        is->audio_pkt_valid = true;
        is->audio_pkt_data = is->audio_pkt.data;
        is->audio_pkt_size = is->audio_pkt.size;
    }
}

/*
 * video_thread
 * ------------------------------------------------------------
 * 有音频流时使用的视频解码线程。
 *
 * 由于音频播放由 SDL callback 驱动，音频时钟通常比较稳定，所以这里采用：
 * 视频 PTS > 音频 clock 时，视频等待；
 * 视频 PTS <= 音频 clock 时，显示视频帧。
 */
static int video_thread(void *arg)
{
    VideoState *is = static_cast<VideoState *>(arg);
    if (!is || !is->pCodecCtx || !is->videoq)
        return -1;

    VideoDecodeContext ctx;
    if (!init_video_decode_context(&ctx, is->pCodecCtx))
    {
        is->videoThreadFinished = true;
        return -1;
    }

    while (!is->quit)
    {
        if (is->isPause)
        {
            SDL_Delay(10);
            continue;
        }

        if (!decode_one_video_frame(is, &ctx, true))
        {
            // 文件读完后，还要等音频队列也播完；否则视频线程先退出会提前触发 quit，导致尾部声音被截断。
            bool videoEmpty = (!is->videoq || is->videoq->nb_packets == 0);
            bool audioEmpty = (is->audioStream == -1 || !is->audioq || is->audioq->nb_packets == 0);
            if (is->readFinished && videoEmpty && audioEmpty)
                break;
            SDL_Delay(1);
        }
    }

    if (!is->quit)
        is->quit = true;

    free_video_decode_context(&ctx);
    is->videoThreadFinished = true;
    return 0;
}

/*
 * timer_callback
 * ------------------------------------------------------------
 * 无音频流时使用的 SDL 定时器回调。
 *
 * 纯视频文件没有 audio_clock，如果仍然用视频线程 while 循环解码，画面会跑得非常快。
 * 所以根据帧率计算 interval，每隔 interval 毫秒解码并显示一帧。
 *
 * 返回 interval：继续定时；
 * 返回 0：停止定时器。
 */
static Uint32 timer_callback(Uint32 interval, void *param)
{
    VideoState *is = static_cast<VideoState *>(param);
    if (!is || is->quit || !is->timer_video_ctx)
        return 0;

    if (is->isPause)
        return interval;

    if (is->video_timer_mutex)
        SDL_LockMutex(is->video_timer_mutex);

    bool ok = decode_one_video_frame(is, is->timer_video_ctx, false);

    if (is->video_timer_mutex)
        SDL_UnlockMutex(is->video_timer_mutex);

    if (!ok && is->readFinished && (!is->videoq || is->videoq->nb_packets == 0))
    {
        is->quit = true;
        is->videoThreadFinished = true;
        return 0;
    }

    return is->quit ? 0 : interval;
}

/*
 * decode_one_video_frame
 * ------------------------------------------------------------
 * 从 videoq 取一个 packet，解码出一帧视频，转换为 RGB32，然后发送给 UI 显示。
 *
 * 参数 syncToAudio：
 * true  ：用于有音频文件，显示前根据 audio_clock 等待；
 * false ：用于纯视频文件，由 SDL 定时器控制节奏，不再等待音频。
 */
static bool decode_one_video_frame(VideoState *is, VideoDecodeContext *ctx, bool syncToAudio)
{
    if (!is || !ctx || !is->pCodecCtx || !is->videoq || !ctx->frame || !ctx->frameRGB || !ctx->swsCtx)
        return false;

    AVPacket packet;
    av_init_packet(&packet);
    packet.data = nullptr;
    packet.size = 0;

    if (packet_queue_get(is->videoq, &packet, 0) <= 0)
        return false;

    if (is_flush_packet(&packet))
    {
        // 跳转后清空视频解码器内部参考帧，否则可能出现花屏。
        avcodec_flush_buffers(is->pCodecCtx);
        is->video_clock = 0.0;
        is->seek_wait_video_flush = 0;
        av_packet_unref(&packet);
        return false;
    }

    // seek 发起后、FLUSH packet 到达前，视频线程可能已经取到了旧位置 packet。
    // 直接丢弃，避免旧帧继续参与同步等待，造成“向左快退时画面不动”。
    if (is->seek_wait_video_flush)
    {
        av_packet_unref(&packet);
        return false;
    }

    av_frame_unref(ctx->frame);
    int got_picture = 0;
    int ret = avcodec_decode_video2(is->pCodecCtx, ctx->frame, &got_picture, &packet);
    av_packet_unref(&packet);

    if (ret < 0)
    {
        qDebug() << "Error decoding video frame";
        return false;
    }

    if (!got_picture)
        return false;

    double video_pts = 0.0;
    int64_t best_ts = ctx->frame->best_effort_timestamp;
    if (best_ts != AV_NOPTS_VALUE)
        video_pts = best_ts * 1000000.0 * av_q2d(is->video_st->time_base);

    video_pts = synchronize_video(is, ctx->frame, video_pts);

    // 跳转只能跳到关键帧；关键帧到目标时间之间的数据需要丢弃，直到到达 seek_time。
    if (is->seek_flag_video)
    {
        if (video_pts < is->seek_time)
            return false;
        is->seek_flag_video = 0;
    }

    // 有音频时：视频时间如果领先音频时间，就等待音频追上来。
    // 队列为空时不能无限等待，否则播放末尾或快退后可能卡死。
    //
    // 快退修复：如果等待过程中用户又点击了进度条，说明当前帧已经是旧位置帧，
    // 不能继续等新的 audio_clock 追上旧 video_pts，应立刻丢弃当前帧，等待 FLUSH 后的新数据。
    if (is->seek_req || is->seek_wait_video_flush || is->seek_wait_audio_flush)
        return false;

    while (!is->quit && syncToAudio && is->audioStream != -1 && is->audio_clock > 0)
    {
        if (is->seek_req || is->seek_wait_video_flush || is->seek_wait_audio_flush)
            return false;

        if (is->isPause)
        {
            SDL_Delay(10);
            continue;
        }
        if (!is->audioq || is->audioq->size == 0)
            break;
        if (video_pts <= is->audio_clock)
            break;
        SDL_Delay(5);
    }

    sws_scale(ctx->swsCtx,
              ctx->frame->data,
              ctx->frame->linesize,
              0,
              is->pCodecCtx->height,
              ctx->frameRGB->data,
              ctx->frameRGB->linesize);

    QImage tmpImg(reinterpret_cast<uchar *>(ctx->rgbBuffer),
                  is->pCodecCtx->width,
                  is->pCodecCtx->height,
                  ctx->frameRGB->linesize[0],
                  QImage::Format_RGB32);

    if (is->m_player)
        is->m_player->SendGetOneImage(tmpImg.copy());

    return true;
}

/*
 * init_video_decode_context
 * ------------------------------------------------------------
 * 初始化视频解码和图像转换需要的临时资源。
 *
 * 包括：
 * 1. 原始解码帧 frame；
 * 2. RGB 输出帧 frameRGB；
 * 3. swsCtx：像素格式转换上下文；
 * 4. rgbBuffer：存放 RGB32 图像数据的缓冲区。
 */
static bool init_video_decode_context(VideoDecodeContext *ctx, AVCodecContext *pCodecCtx)
{
    if (!ctx || !pCodecCtx)
        return false;

    ctx->frame = av_frame_alloc();
    ctx->frameRGB = av_frame_alloc();
    if (!ctx->frame || !ctx->frameRGB)
    {
        free_video_decode_context(ctx);
        return false;
    }

    ctx->swsCtx = sws_getContext(pCodecCtx->width,
                                 pCodecCtx->height,
                                 pCodecCtx->pix_fmt,
                                 pCodecCtx->width,
                                 pCodecCtx->height,
                                 AV_PIX_FMT_RGB32,
                                 SWS_BICUBIC,
                                 nullptr,
                                 nullptr,
                                 nullptr);
    if (!ctx->swsCtx)
    {
        free_video_decode_context(ctx);
        return false;
    }

    ctx->rgbBufferSize = av_image_get_buffer_size(AV_PIX_FMT_RGB32,
                                                  pCodecCtx->width,
                                                  pCodecCtx->height,
                                                  1);
    if (ctx->rgbBufferSize <= 0)
    {
        free_video_decode_context(ctx);
        return false;
    }

    ctx->rgbBuffer = static_cast<uint8_t *>(av_malloc(ctx->rgbBufferSize));
    if (!ctx->rgbBuffer)
    {
        free_video_decode_context(ctx);
        return false;
    }

    av_image_fill_arrays(ctx->frameRGB->data,
                         ctx->frameRGB->linesize,
                         ctx->rgbBuffer,
                         AV_PIX_FMT_RGB32,
                         pCodecCtx->width,
                         pCodecCtx->height,
                         1);

    return true;
}

/*
 * free_video_decode_context
 * ------------------------------------------------------------
 * 释放 init_video_decode_context() 中申请的所有资源。
 */
static void free_video_decode_context(VideoDecodeContext *ctx)
{
    if (!ctx) return;

    if (ctx->rgbBuffer)
    {
        av_free(ctx->rgbBuffer);
        ctx->rgbBuffer = nullptr;
    }
    if (ctx->swsCtx)
    {
        sws_freeContext(ctx->swsCtx);
        ctx->swsCtx = nullptr;
    }
    if (ctx->frame)
    {
        av_frame_free(&ctx->frame);
        ctx->frame = nullptr;
    }
    if (ctx->frameRGB)
    {
        av_frame_free(&ctx->frameRGB);
        ctx->frameRGB = nullptr;
    }
    ctx->rgbBufferSize = 0;
}

/*
 * calc_video_timer_interval_ms
 * ------------------------------------------------------------
 * 计算纯视频定时器的触发间隔。
 *
 * 优先级：
 * 1. r_frame_rate；
 * 2. avg_frame_rate；
 * 3. codec time_base；
 * 4. 默认 25fps。
 */
static Uint32 calc_video_timer_interval_ms(AVStream *video_st, AVCodecContext *codec_ctx)
{
    double fps = 0.0;

    if (video_st && video_st->r_frame_rate.num > 0 && video_st->r_frame_rate.den > 0)
        fps = av_q2d(video_st->r_frame_rate);

    if (!(fps > 0.0) || fps > 120.0)
    {
        if (video_st && video_st->avg_frame_rate.num > 0 && video_st->avg_frame_rate.den > 0)
            fps = av_q2d(video_st->avg_frame_rate);
    }

    if (!(fps > 0.0) || fps > 120.0)
    {
        if (codec_ctx && codec_ctx->time_base.num > 0 && codec_ctx->time_base.den > 0)
        {
            double tb = av_q2d(codec_ctx->time_base);
            if (tb > 0.0)
                fps = 1.0 / tb;
        }
    }

    if (!(fps > 0.0) || fps > 120.0)
        fps = 25.0;

    double interval = 1000.0 / fps;
    if (interval < 1.0) interval = 1.0;
    if (interval > 1000.0) interval = 40.0;

    return static_cast<Uint32>(std::max(1.0, std::round(interval)));
}

/*
 * synchronize_video
 * ------------------------------------------------------------
 * 更新视频时钟 video_clock。
 *
 * pts 是当前帧的显示时间。如果当前帧没有可靠 pts，就使用已有 video_clock。
 * 然后根据 time_base 和 repeat_pict 推进 video_clock，供下一帧同步使用。
 */
static double synchronize_video(VideoState *is, AVFrame *src_frame, double pts)
{
    if (!is || !src_frame)
        return pts;

    if (pts != 0.0)
        is->video_clock = pts;
    else
        pts = is->video_clock;

    double frame_delay = 0.0;
    if (is->pCodecCtx && is->pCodecCtx->time_base.den != 0)
        frame_delay = av_q2d(is->pCodecCtx->time_base) * 1000000.0;

    if (frame_delay <= 0.0 || frame_delay > 1000000.0)
        frame_delay = static_cast<double>(calc_video_timer_interval_ms(is->video_st, is->pCodecCtx)) * 1000.0;

    frame_delay += src_frame->repeat_pict * (frame_delay * 0.5);
    is->video_clock += frame_delay;
    return pts;
}

/*
 * find_stream_index
 * ------------------------------------------------------------
 * 遍历 pFormatCtx->streams，找到第一个视频流和第一个音频流的下标。
 * 找不到时保持 -1。
 */
static int find_stream_index(AVFormatContext *pformat_ctx, int *video_stream, int *audio_stream)
{
    assert(video_stream != nullptr || audio_stream != nullptr);
    if (!pformat_ctx) return -1;

    int audio_index = -1;
    int video_index = -1;

    for (unsigned int i = 0; i < pformat_ctx->nb_streams; ++i)
    {
        if (pformat_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO && video_index == -1)
            video_index = static_cast<int>(i);
        else if (pformat_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO && audio_index == -1)
            audio_index = static_cast<int>(i);
    }

    if (video_stream) *video_stream = video_index;
    if (audio_stream) *audio_stream = audio_index;

    return (video_index != -1 || audio_index != -1) ? 0 : -1;
}

/*
 * is_flush_packet
 * ------------------------------------------------------------
 * 判断 packet 是否是我们人为插入的 FLUSH packet。
 *
 * seek 后必须清空解码器内部缓存。因为 AVCodecContext 内部可能保存了跳转前的参考帧，
 * 如果不 flush，跳转后视频容易花屏，音频也可能出现残留杂音。
 */
static bool is_flush_packet(const AVPacket *packet)
{
    return packet && packet->data && packet->size >= FLUSH_DATA_SIZE &&
           std::memcmp(packet->data, FLUSH_DATA, FLUSH_DATA_SIZE) == 0;
}

/*
 * put_flush_packet
 * ------------------------------------------------------------
 * 向队列放入一个特殊 packet，内容为 "FLUSH"。
 * 解码线程取到后不会正常解码，而是调用 avcodec_flush_buffers()。
 */
static void put_flush_packet(PacketQueue *queue)
{
    if (!queue) return;

    AVPacket packet;
    av_init_packet(&packet);
    packet.data = nullptr;
    packet.size = 0;

    if (av_new_packet(&packet, FLUSH_DATA_SIZE) == 0)
    {
        std::memcpy(packet.data, FLUSH_DATA, FLUSH_DATA_SIZE);
        packet_queue_put(queue, &packet);
        av_packet_unref(&packet);
    }
}
