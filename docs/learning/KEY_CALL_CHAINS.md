# 关键调用链

> 本文件只保存已经由当前源码核对过的调用链。后续课程逐步补充参数、线程、所有权、异常分支和时序图。

## 1. 客户端启动

```text
AVClient/main.cpp
main
  -> SDL_SetMainReady
  -> QApplication::QApplication
  -> av_register_all
  -> avformat_network_init
  -> MainWindow::MainWindow
       -> new AVNetworkClient(this)
            -> new TcpClient(this)
       -> new QTabWidget(this)
       -> new PlayerPage
            -> new PlayerDialog
                 -> new VideoPlayer（见 PlayerDialog 构造函数）
       -> new RecorderPage
            -> new RecorderDialog
                 -> new SaveVideoFileThread
       -> new RemoteMediaPage(shared AVNetworkClient)
       -> new SettingsPage(shared AVNetworkClient)
  -> MainWindow::show
  -> QApplication::exec
```

主要对象通过 Qt parent 对象树释放；`MainWindow::m_networkClient` 由 `MainWindow` 持有并被 Remote Media、Settings 两页非拥有地共享。

## 2. 服务端启动

```text
AVServer/src/main.cpp
main
  -> AVServer::AVServer
  -> AVServer::start(port)
  -> EpollServer::start(port)
       -> ProtocolDispatcher::initialize
            -> MediaManager::ensureMediaDir
            -> UploadManager::initialize
       -> createListenSocket
       -> epoll_create1
       -> createCompletionEvent
       -> add listen fd / eventfd to epoll
       -> ThreadPool::start（创建 4 个核心 worker）
       -> epoll_wait 循环
```

退出信号把 `g_stopRequested` 置位并写 `eventfd` 唤醒 Reactor；`EpollServer::closeAll` 先停线程池，再关闭连接、eventfd 和 epoll fd。

## 3. 客户端连接与 Ping

```text
SettingsPage::slotConnectClicked
  -> AVNetworkClient::connectToServer
  -> TcpClient::connectToServer
       -> 非阻塞 connect + select 超时等待
       -> 切回阻塞 socket
       -> 创建 TcpClient::recvLoop 接收线程

SettingsPage::slotPingClicked
  -> AVNetworkClient::sendPing
  -> TcpClient::sendPacket
  -> TcpClient::sendAll
  -> TCP
  -> EpollServer::handleRead
  -> EpollServer::parseFrames
  -> ProtocolDispatcher::dispatch
  -> ProtocolDispatcher::handlePing
  -> EpollServer::queueResponse / flushSendQueue
  -> TCP
  -> TcpClient::recvLoop / recvAll
  -> AVNetworkClient::onPacketReceived
  -> AVNetworkClient::pingResponse
  -> SettingsPage::slotPingResponse
```

Ping 不在 `isBusinessPacket` 列表中，所以当前由 Reactor 线程直接处理；媒体列表和上传/下载请求会提交到 worker。

### 客户端 TCP 长度帧

```text
发送：
业务协议包体（首字段为 PackType）
  -> TcpClient::sendPacket
  -> 前置 int32_t bodyLength（当前为主机字节序）
  -> sendAll 循环，直到长度头和包体全部交给 socket

接收线程：
recvLoop
  -> recvAll(4字节长度)
  -> 校验 0 < length <= 1 MiB
  -> 分配 QByteArray(length)
  -> recvAll(length字节包体)
  -> emit packetReceived(QByteArray)
  -> AVNetworkClient::onPacketReceived
  -> 读取 PackType 并分发响应
```

客户端连接成功后使用阻塞 socket 和单独接收线程，因此 `recvAll` 可以循环等待半包；每次只请求当前帧还缺少的字节，多出来的下一帧数据保留在内核接收缓冲区，下一轮读取，从而处理粘包。服务端使用非阻塞 socket、每连接 `receiveBuffer` 和增量拆包，机制不同。

### 分块消息的外层长度与内层长度

上传分块请求的真实线性布局（`#pragma pack(1)`）：

```text
TCP应用帧
┌─────────────────────┬──────────────────────────────────────────────────────────┐
│ outerLength：4字节  │ 包体：outerLength 字节                                   │
│ = 112 + dataSize    │ ┌──────────────────────────────┬───────────────────────┐ │
│                     │ │ 固定业务头：112字节          │ 动态文件数据           │ │
│                     │ │ type：4                      │ dataSize 字节          │ │
│                     │ │ transferId：96               │ 最大 65,536 字节       │ │
│                     │ │ offset：8                    │                       │ │
│                     │ │ dataSize：4（内层长度）      │                       │ │
│                     │ └──────────────────────────────┴───────────────────────┘ │
└─────────────────────┴──────────────────────────────────────────────────────────┘
```

外层 `outerLength` 负责从 TCP 字节流中确定完整消息包体；内层 `dataSize` 负责说明固定业务头之后有多少文件数据。接收方必须验证：

```text
0 < dataSize <= 64 KiB
outerLength == sizeof(固定业务头) + dataSize
```

下载分块响应采用同一模式，但固定头为 404 字节：`type(4) + result(4) + fileName(256) + offset(8) + dataSize(4) + message(128)`，后面紧跟 `dataSize` 字节文件数据。

### 当前协议请求—响应地图

| 业务 | 请求携带 | 响应携带 | 状态/磁盘特征 |
|---|---|---|---|
| Ping | 文本 | Pong 文本 | 轻量、无状态、无磁盘 |
| 注册/登录 | 用户名、密码 | MySQL + Argon2id | worker 校验，Reactor 回投连接身份 |
| 媒体列表 | 仅类型 | 固定响应头＋UTF-8动态列表载荷 | 扫描 `media/` |
| 上传初始化 | 文件名、扩展名、大小 | transferId、resumeToken、确认偏移、最终文件名 | 创建持久上传任务和临时文件 |
| 上传恢复 | 任务ID、恢复令牌、文件名、期望大小 | 服务端确认偏移 | 恢复服务端持久任务 |
| 上传分块 | 固定头＋最多64KiB动态数据 | 服务端确认的新偏移 | 按offset写临时文件 |
| 上传完成 | 任务ID、文件名、大小 | 最终文件名和结果 | 校验并完成临时文件 |
| 下载初始化 | 文件名、客户端偏移、预期大小/修改时间 | 服务端文件元数据、接受偏移 | 校验源文件，不保存下载会话 |
| 下载分块 | 文件名、offset、请求大小 | 固定头＋最多64KiB动态数据 | 按offset读取文件 |
| 下载完成 | 文件名、客户端文件大小 | 校验结果 | 校验服务端当前文件大小 |

每个协议包体首字段都是 `PackType`。`AVNetworkClient` 把页面参数转换为二进制请求，把完整响应转换为业务含义明确的 Qt 信号；`TcpClient` 只负责连接和长度帧收发。

### 客户端响应跨线程通知

```text
TcpClient::recvLoop（std::thread）
  -> 阻塞读满长度和包体
  -> emit packetReceived(QByteArray)
  -> Qt AutoConnection 跨线程排队
  -> AVNetworkClient::onPacketReceived（GUI线程）
  -> 校验 PackType / 固定大小 / 动态载荷
  -> emit 业务信号
  -> SettingsPage / RemoteMediaPage 槽（GUI线程）
  -> 更新界面、文件任务和传输进度
```

`TcpClient` 与 `AVNetworkClient` 对象都由 GUI 线程创建并具有 GUI 线程亲和性；只有 `recvLoop` 函数运行在 `std::thread`。接收线程不直接操作页面。主动断开会先把运行标志置 false，再 `shutdown + closesocket` 唤醒阻塞 `recv`，最后 join 接收线程。

## 4. 服务端业务请求的通用路径

```text
EPOLLIN
  -> EpollServer::handleRead
  -> ConnectionContext::receiveBuffer 追加字节
  -> EpollServer::parseFrames
  -> ProtocolDispatcher::isBusinessPacket == true
  -> EpollServer::submitBusinessTask
       -> connection.businessTaskInFlight = true
       -> ThreadPool::submit（捕获包副本、connectionId、当时的 fd）
  -> worker: ProtocolDispatcher::dispatch
  -> worker: pushCompletion
       -> mutex 保护的 completion queue
       -> write(eventfd)
  -> Reactor 收到 eventfd EPOLLIN
  -> EpollServer::handleCompletionEvent
       -> 用 connectionId 再查当前连接，过滤已关闭/陈旧 fd
       -> businessTaskInFlight = false
       -> queueResponse
       -> parseFrames（继续处理该连接已缓存的下一帧）
       -> flushSendQueue
```

## 5. 普通上传主链

```text
RemoteMediaPage::slotUploadClicked
  -> AVNetworkClient::sendUploadInit
  -> 服务端 ProtocolDispatcher::handleUploadInit
  -> UploadManager::createUpload
  -> 客户端 AVNetworkClient::onPacketReceived
  -> RemoteMediaPage::slotUploadInitResponse
  -> saveCurrentUploadTask
  -> continueUploadAfterConfirmation
  -> sendNextUploadBlock
  -> AVNetworkClient::sendUploadBlock
  -> 服务端 ProtocolDispatcher::handleUploadBlock
  -> UploadManager::writeBlock
  -> RemoteMediaPage::slotUploadBlockResponse
  -> 校验 receivedOffset 后继续下一块
  -> 最后一块确认后 AVNetworkClient::sendUploadFinish
  -> ProtocolDispatcher::handleUploadFinish
  -> UploadManager::finishUpload
  -> RemoteMediaPage::slotUploadFinishResponse
```

客户端每收到一块确认才发送下一块；服务端每连接又限制一个在途业务任务，因此同一连接上的块顺序被串行化。

## 6. 普通下载与下载后播放主链

```text
RemoteMediaPage::slotDownloadClicked / slotDownloadAndPlayClicked
  -> startDownload
  -> AVNetworkClient::sendDownloadInit
  -> ProtocolDispatcher::handleDownloadInit
  -> DownloadManager::initializeDownload
  -> RemoteMediaPage::slotDownloadInitResponse
       -> 创建 cache/<name>.part
       -> 保存 DownloadTaskState
  -> requestNextDownloadBlock
  -> AVNetworkClient::sendDownloadBlock
  -> ProtocolDispatcher::handleDownloadBlock
  -> DownloadManager::readBlock
  -> RemoteMediaPage::slotDownloadBlockResponse
       -> 校验 fileName / offset / size
       -> 写入并 flush `.part`
       -> 保存 confirmedOffset
       -> 请求下一块
  -> AVNetworkClient::sendDownloadFinish
  -> DownloadManager::validateCompletion
  -> RemoteMediaPage::slotDownloadFinishResponse
       -> `.part` rename 为最终文件
       -> 若选择 Download and play，emit requestPlayLocalFile
  -> MainWindow::slotPlayLocalFile
  -> PlayerPage::playLocalFile
  -> PlayerDialog::playLocalFile
```

## 7. 本地播放入口

```text
PlayerDialog::playLocalFile / 打开按钮
  -> VideoPlayer::setFileName
  -> QThread::start
  -> VideoPlayer::run
       -> avformat_open_input
       -> avformat_find_stream_info
       -> 查找并打开音/视频解码器
       -> 创建 audioq / videoq
       -> SDL_OpenAudioDevice（音频回调线程）
       -> SDL_CreateThread(video_thread) 或 SDL_AddTimer(timer_callback)
       -> av_read_frame 循环（读线程生产 Packet）
       -> 按 stream_index 放入 audioq / videoq

音频消费者：
SDL audio_callback
  -> audio_decode_frame
  -> packet_queue_get(audioq)
  -> avcodec_decode_audio4
  -> swr_convert 为设备所需 S16 PCM
  -> 填充 SDL stream
  -> 声卡播放

视频消费者（存在音频）：
SDL video_thread
  -> decode_one_video_frame(syncToAudio=true)
  -> packet_queue_get(videoq)
  -> avcodec_decode_video2
  -> best_effort_timestamp 换算为微秒
  -> video_pts 领先 audio_clock 时等待
  -> sws_scale 为 RGB32
  -> QImage::copy
  -> Qt 跨线程信号
  -> PlayerDialog::slot_setImage
  -> MyOpenGLWidget::slot_setImage / paintGL

纯视频消费者：
SDL timer_callback
  -> 按 r_frame_rate / avg_frame_rate / codec time_base / 默认 25fps 计算间隔
  -> decode_one_video_frame(syncToAudio=false)
  -> 转换并发送图像
```

seek 主链：

```text
PlayerDialog::slot_videoSliderValueChanged（秒转微秒）
  -> VideoPlayer::seek（GUI 线程只设置 seek_req/seek_pos，清旧 PCM）
  -> VideoPlayer::run 读线程处理请求
  -> av_rescale_q（微秒转目标 stream time_base）
  -> av_seek_frame(..., AVSEEK_FLAG_BACKWARD)
  -> packet_queue_flush（清除未解码旧 Packet）
  -> put_flush_packet
  -> 音视频消费者识别 FLUSH
  -> avcodec_flush_buffers（清理解码器内部缓存）
  -> 从目标前关键帧开始解码
  -> 丢弃 PTS 小于 seek_time 的 Frame
  -> 恢复输出
```

EOF 后读线程不立即释放，而是设置 `readFinished`，等待消费者排空相关队列并设置 `quit`。退出时先停止定时器、视频在线程和 SDL 音频回调，再销毁队列、解码器和 `AVFormatContext`，避免消费者访问已释放资源。

## 8. 录制入口

```text
RecorderDialog::on_pb_start_clicked
  -> 组装 STRU_AV_FORMAT
       -> 当前固定启用桌面、摄像头和音频
       -> 25 FPS、屏幕分辨率、H.264 码率 1.4 Mbps
  -> SaveVideoFileThread::slot_setInfo
       -> avformat_alloc_output_context2("flv")
       -> add_video_stream(H.264)
       -> add_audio_stream(AAC)
       -> open_video / open_audio
       -> avio_open
       -> avformat_write_header
       -> QThread::start -> SaveVideoFileThread::run
  -> SaveVideoFileThread::slot_openVideo
       -> PicInPic_Read::slot_openVideo
            -> OpenCV VideoCapture 打开默认摄像头
            -> PicInPic_Read::run 约 25 FPS 采集
            -> BGR 转 RGB
            -> 发送到置顶 PictureWidget 作为摄像头预览
            -> BlockingQueuedConnection 请求 GUI 线程抓取桌面
            -> 桌面 RGB24 经 sws_scale 转为 YUV420P
            -> 发送视频缓冲区
       -> Audio_Read::slot_openAudio
            -> GUI 线程创建 QAudioInput
            -> QTimer 每约 20ms 调用 slot_readMore
            -> 读取 44.1kHz / 双声道 / 16bit PCM
            -> swr_convert：S16 packed 转 FLTP planar
            -> 发送音频缓冲区
  -> SaveVideoFileThread::slot_writeVideoFrameData / slot_writeAudioFrameData
       -> 记录视频采集相对时间
       -> 互斥锁保护下加入 video/audio QList
  -> SaveVideoFileThread::run 比较两路时间基，编码并 av_interleaved_write_frame
  -> 停止后排空队列、av_write_trailer、关闭编码器和输出文件
```

当前“画中画”依靠置顶摄像头预览窗成为桌面截图的一部分，并非在内存中把摄像头帧缩放后叠加到桌面帧。该方案实现简单，但受窗口可见性、遮挡、桌面合成和 GUI 响应影响。

当前音视频队列没有容量上限和条件变量；编码线程无数据时以 `msleep(1)` 轮询。队列退出条件、编码器 flush、错误处理和内存管理将在后两课审查，现阶段不把“能运行”表述为“生产级安全”。

录制编码与封装主链：

```text
SaveVideoFileThread::run
  -> 比较 video_st.next_pts / video time_base
       与 audio_st.next_pts / audio time_base
  -> 选择时间轴更靠前的一路

视频：
  -> videoDataQuene_get（按目标时间淘汰过旧帧，缺帧时复用上一帧）
  -> YUV420P 填入复用 AVFrame
  -> frame->pts = video_st.next_pts++
  -> avcodec_send_frame(H.264)
  -> avcodec_receive_packet

音频：
  -> audioDataQuene_get
  -> FLTP 填入复用 AVFrame
  -> 按累计 samples_count 计算 frame PTS
  -> 每帧推进 nb_samples
  -> avcodec_send_frame(AAC)
  -> avcodec_receive_packet

两路编码 Packet：
  -> av_packet_rescale_ts（codec time_base 转 stream time_base）
  -> 设置 stream_index
  -> av_interleaved_write_frame
  -> FLV 文件
```

当前视频使用 H.264、YUV420P、25 FPS、GOP 12、约 1.4 Mbps，并设置 `superfast + zerolatency`；音频使用 AAC、44.1 kHz、双声道、FLTP、64 kbps。

录制停止主链：

```text
RecorderDialog 停止按钮
  -> 停止摄像头采集
  -> 停止 QAudioInput 和音频定时器
  -> 设置编码线程 isStop
  -> 编码线程尝试消费完 audio/video 原始帧队列
  -> av_write_trailer
  -> 释放编码器 Frame/Context 和转换上下文
  -> 关闭输出文件
  -> 释放 AVFormatContext
  -> GUI 最多 wait(3000)
```

当前缺少在写 trailer 前分别向 H.264/AAC 编码器发送结束信号并持续取出延迟 Packet；`wait(3000)` 返回值未检查，多个停止标志不是原子变量，队列容量、锁边界和裸指针所有权也不够严格。

## 9. 客户端网络与协议主链

```text
页面发起 Ping / 列表 / 上传 / 下载请求
  -> AVNetworkClient 组装对应二进制请求体
  -> TcpClient::sendPacket
       -> 添加 4 字节包体长度
       -> sendAll 循环发送完整长度帧

TcpClient::recvLoop（专用 std::thread）
  -> recvAll 读满 4 字节长度
  -> 校验长度并 recvAll 读满包体
  -> packetReceived(QByteArray)
  -> AVNetworkClient::onPacketReceived（对象属于 GUI 线程）
       -> 根据 PackType 校验并解析固定字段/动态载荷
       -> 发出业务 signal
  -> RemoteMediaPage 等页面槽更新状态和界面
```

上传和下载采用停止等待式分块：每次只发送/请求一个 64 KiB 块，收到服务端确认偏移后再继续。该方案状态清晰、恢复简单，但吞吐量受网络往返时延限制。主动断开时，`TcpClient::disconnectFromServer` 先清运行标志，再 shutdown/close socket 唤醒阻塞 `recv`，最后 join 接收线程。

当前客户端并未彻底异步化：连接等待、`sendAll`、上传文件读取以及下载文件写入/flush/任务落盘仍可能在 GUI 线程执行。发送互斥锁只能防止多个完整帧相互穿插，不能消除这些阻塞；生产化方向应是 socket 由专用网络线程和发送队列独占，文件 I/O 交给任务线程。

## 10. 服务端 epoll LT Reactor 主链

```text
main -> AVServer::start -> EpollServer::start
  -> 创建非阻塞 listen socket
  -> epoll_create1
  -> 注册 listen fd 与 completion eventfd
  -> epoll_wait

新连接：
  -> acceptClients 循环 accept 到 EAGAIN
  -> 客户端 socket 设为非阻塞
  -> 创建 ConnectionContext（单连接状态对象）
  -> 分配单调递增 connectionId（连接唯一身份）
  -> 注册 EPOLLIN | EPOLLRDHUP

读取：
  -> handleRead 循环 recv 到 EAGAIN
  -> 追加 receiveBuffer（接收缓冲区）
  -> parseFrames 按 4 字节长度增量拆包
  -> 完整轻量请求由 Reactor 分发
  -> 耗时文件请求提交 worker

发送：
  -> queueResponse 添加长度头并进入 sendQueue（待发送队列）
  -> flushSendQueue 调用 send（向内核发送缓冲区提交字节）
  -> 部分发送则更新 PendingSend::offset（已发送位置）
  -> EAGAIN 时开启 EPOLLOUT
  -> 后续可写事件继续发送
  -> 队列清空后关闭 EPOLLOUT

worker 完成：
  -> 只返回 CompletedTask（业务完成结果）
  -> eventfd 唤醒 Reactor
  -> Reactor 用 connectionId 再次确认连接身份
  -> 旧连接已关闭或 fd 已复用则丢弃结果
  -> 有效结果进入该连接发送队列
```

连接关闭顺序为：从 `m_connectionFds` 删除连接身份、通知协议分发器、从 epoll 删除 fd、`close` 释放内核 socket、最后从 `m_connections` 擦除 `unique_ptr` 并析构连接状态对象。

## 11. 有界动态线程池主链

```text
EpollServer::start
  -> ThreadPool::start
  -> 创建4个core worker（常驻核心工作线程）

EpollServer::submitBusinessTask
  -> ThreadPool::submit
  -> reapFinishedWorkers（回收已结束线程对象）
  -> 检查started/stopping和256容量上限
  -> 任务进入m_tasks（FIFO待执行任务队列）
  -> 等待任务数 > idle worker数 且 current < 8
       -> 每次提交最多增加一个non-core worker（临时工作线程）
  -> condition_variable::notify_one

ThreadPool::workerLoop
  -> core worker：无任务时持续等待
  -> non-core worker：最多等待60秒
  -> 从队首取任务，在互斥锁外执行
  -> 捕获任务异常，避免整个worker线程意外终止
  -> 完成后恢复为空闲状态
  -> 非核心线程空闲超时且总数大于4
       -> 标记finished并结束线程函数
       -> 后续reap/stop在锁外join

队列已满：
  -> submit返回false
  -> EpollServer构造server busy响应

ThreadPool::stop：
  -> 设置stopping并notify_all
  -> worker排空已有任务后退出
  -> 逐个join
  -> 清理队列和计数器
```

## 12. worker 完成结果回到 Reactor

```text
worker执行ProtocolDispatcher::dispatch
  -> 生成CompletedTask（业务完成结果）
       connectionId（原连接唯一身份）
       clientFd（任务提交时的fd整数）
       protocolType（协议类型）
       responses（待发送响应集合）
       error/closeConnection（错误和关闭要求）
  -> EpollServer::pushCompletion
       -> completionMutex加锁
       -> 放入m_completionQueue（跨线程完成队列）
       -> 解锁
       -> write(eventfd, 1)

epoll_wait发现completionEventFd可读
  -> EpollServer::handleCompletionEvent
       -> read eventfd到EAGAIN
       -> 共享完成队列swap到Reactor本地队列
       -> 尽快释放completionMutex
       -> 对每个CompletedTask：
            1. 用connectionId查m_connectionFds
            2. 用得到的当前fd查m_connections
            3. 再比较ConnectionContext::connectionId
            4. 不存在/不匹配则丢弃陈旧结果
       -> 有效连接businessTaskInFlight=false
       -> responses进入sendQueue
       -> parseFrames继续处理该连接缓存请求
       -> flushSendQueue尝试发送
```

完成队列保存“结果是什么”，eventfd只解决“怎样唤醒正在epoll_wait的Reactor”。两者分工不能互相替代。

## 13. 普通上传主链

```text
RemoteMediaPage::slotUploadClicked
  -> 校验连接、文件类型、非空和本地可读
  -> AVNetworkClient::sendUploadInit
       fileName / extension / fileSize
  -> ProtocolDispatcher::handleUploadInit（worker）
  -> UploadManager::createUpload
       -> 校验安全文件名、扩展名和大小
       -> 生成transferId（上传任务唯一编号）
       -> 生成resumeToken（恢复凭证）
       -> 选择finalFileName（最终保存名）
       -> 创建temp/<transferId>.part
       -> 持久化.task任务记录
       -> 返回resumeOffset=0
  -> 客户端保存本地UploadTaskState

RemoteMediaPage::sendNextUploadBlock
  -> seek到m_confirmedOffset（服务端已确认偏移）
  -> 读取最多64KiB
  -> m_expectedOffset = confirmedOffset + block.size
  -> AVNetworkClient::sendUploadBlock
       transferId / offset / dataSize / payload
  -> ProtocolDispatcher校验动态载荷实际长度
  -> UploadManager::writeBlock（持有上传管理器互斥锁）
       -> 校验任务、连接绑定、块大小和offset
       -> 完整重复块：不重写，返回当前receivedSize
       -> 跳跃或部分重叠：返回offset mismatch
       -> pwrite循环写入.part指定偏移
       -> fsync并stat核对实际大小
       -> 更新receivedSize并持久化.task
       -> 状态持久化失败则truncate回旧大小并回滚偏移
       -> 返回receivedOffset（服务端确认偏移）
  -> 客户端更新confirmedOffset和本地任务记录
  -> 继续下一块

confirmedOffset == fileSize
  -> AVNetworkClient::sendUploadFinish
  -> UploadManager::finishUpload
       -> 校验任务归属、文件名、声明大小
       -> 校验receivedSize和.part实际大小均等于expectedSize
       -> 必要时重新选择无冲突finalFileName
       -> rename .part到media/<finalFileName>
       -> 删除服务端.task并从内存任务表移除
  -> 客户端删除本地任务记录并刷新远程媒体列表
```

客户端发送成功只表示字节进入本机TCP栈，不能作为上传进度；只有服务端完成文件写入、fsync和任务状态持久化后返回的`receivedOffset`才是下一块的可信起点。

## 14. 上传中断、重启与恢复

```text
连接中断：
  客户端RemoteMediaPage::slotConnectedChanged(false)
    -> finishUploadState(false)
    -> 保存UploadTaskState为waiting
  服务端ProtocolDispatcher::onClientDisconnected
    -> UploadManager::unbindConnection
    -> activeOwnerConnectionId（当前任务占用连接）清零

客户端重启：
  RemoteMediaPage构造
    -> UploadTaskStore::loadAll
    -> 扫描transfer_state/*.upload.json
    -> 校验transferId、resumeToken、路径、大小、mtime、服务器和offset
    -> 恢复未完成任务表

服务端重启：
  ProtocolDispatcher::initialize
    -> UploadManager::initialize
    -> loadPersistedTasks扫描temp/tasks/*.task
    -> 校验任务字段、文件名、token、大小、状态和tempPath
    -> reconcileTask对照.part实际大小
    -> activeOwnerConnectionId=0
    -> 恢复到m_tasks（内存上传任务表）

用户点击恢复：
  RemoteMediaPage::slotResumeUploadClicked
    -> 确认连接到原serverIp/serverPort
    -> 检查本地文件存在、size和lastModifiedMs未变化
    -> sendUploadResume(transferId, resumeToken, fileName, fileSize)
  UploadManager::resumeUpload
    -> 任务存在、token安全比较、文件元数据匹配、状态可恢复
    -> 未过72小时、未被其他连接占用
    -> reconcileTask：
         safeOffset = min(expectedSize, recordedReceivedSize, actualPartSize)
         actualPartSize > safeOffset则truncate
         receivedSize修正为safeOffset并持久化
    -> 绑定新的connectionId
    -> 返回服务端resumeOffset
  客户端用服务端offset覆盖本地confirmed/expected offset
    -> 保存本地任务
    -> seek并继续逐块上传
```

服务端约每5分钟清理一次“无活动连接且72小时未更新”的上传任务和相关文件。客户端放弃当前只删除本地JSON，服务端任务仍等待过期；没有合法`.task`对应的孤立`.part`当前只打印日志，不会自动删除。

## 15. 下载与客户端断点恢复

```text
新下载：
  RemoteMediaPage::startDownload
    -> sendDownloadInit(fileName, offset=0, expectedSize=0, expectedMtime=0)
  DownloadManager::initializeDownload（服务端无会话状态）
    -> 校验安全文件名、支持扩展名、普通非空媒体文件
    -> stat返回fileSize和modifiedTime
    -> acceptedOffset=0
  客户端创建cache/<fileName>.part
    -> 创建DownloadTaskState（客户端下载恢复记录）
    -> QSaveFile保存<taskId>.download.json

逐块下载：
  RemoteMediaPage::requestNextDownloadBlock
    -> sendDownloadBlock(fileName, m_downloadOffset, <=64KiB)
  DownloadManager::readBlock
    -> 每次重新检查文件、打开、seek并读取
    -> 返回offset + dynamic payload
  RemoteMediaPage::slotDownloadBlockResponse
    -> 校验文件名、offset、dataSize和总大小边界
    -> 写入本地.part并QFile::flush
    -> 更新m_downloadOffset
    -> 保存客户端JSON
    -> 请求下一块

中断恢复：
  客户端加载JSON并检查原serverIp/serverPort
    -> prepareSafeDownloadOffset
    -> safeOffset=min(JSON confirmedOffset, .part实际大小)
    -> .part过长则截断，记录领先则降低记录
    -> sendDownloadInit(fileName, safeOffset, expectedSize, expectedMtime)
  服务端重新stat正式媒体文件
    -> size或mtime与客户端原记录不同：remote file changed，拒绝追加
    -> 一致：acceptedOffset=requestedOffset
  客户端resize/seek到acceptedOffset并继续请求块

完成：
  客户端offset==expectedFileSize
    -> sendDownloadFinish(fileName, fileSize)
  服务端validateCompletion仅再次检查当前远程文件大小
  客户端检查本地.part大小
    -> 删除同名旧缓存
    -> rename .part为cache/<fileName>
    -> 删除下载任务JSON
    -> playAfterDownload时发requestPlayLocalFile切换播放器
```

上传半成品位于服务端，必须保存任务、凭证和服务端确认offset；下载半成品位于客户端，服务端可根据`fileName + offset + requestSize`无会话读取。当前下载只在恢复初始化时比较size/mtime，活跃下载的每块没有绑定稳定文件版本，完成校验也只比大小，因此同大小替换可能产生新旧内容混合文件。
