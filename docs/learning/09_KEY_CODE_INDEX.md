# 核心源码索引

## 1. 优先级定义

- **A：必须能默写流程**：不看源码可画调用链、说明线程和状态变化。
- **B：必须能解释**：能定位函数并说明输入、输出、调用者和边界。
- **C：了解即可**：知道为什么存在，遇到追问能回到源码。

## 2. 客户端核心类

| 模块/类 | 文件 | 核心函数 | 调用与作用 | 级别 |
|---|---|---|---|---|
| MainWindow | `AVClient/MainWindow.cpp` | ctor、`slotPlayLocalFile` | main 创建；组合页面，共享网络，转发播放 | A |
| PlayerPage | `AVClient/pages/PlayerPage.cpp` | ctor、`playLocalFile` | MainWindow 创建；薄适配到 PlayerDialog | B |
| PlayerDialog | `AVClient/modules/player/playerdialog.cpp` | `playLocalFile`、pause/stop、timer/slider slots | UI 入口；调用 VideoPlayer、接收帧/状态 | A |
| VideoPlayer | `AVClient/modules/player/videoplayer.cpp` | `run`、`play/pause/stop/seek`、`audio_decode_frame`、`decode_one_video_frame` | 解封装、解码、同步和生命周期 | A |
| PacketQueue | `AVClient/modules/player/PacketQueue.cpp` | init/put/get/flush/destroy | 读取线程与音视频消费者之间传 packet | A |
| MyOpenGLWidget | `AVClient/modules/player/myopenglwidget.cpp` | `slot_setImage`、`paintGL` | PlayerDialog 调用；纹理显示 QImage | C |
| RecorderPage | `AVClient/pages/RecorderPage.cpp` | ctor | 嵌入 RecorderDialog 并启用 embedded mode | B |
| RecorderDialog | `AVClient/modules/recorder/recorderdialog.cpp` | start/stop slots | 采集参数与录制生命周期入口 | A |
| SaveVideoFileThread | `AVClient/modules/recorder/savevideofilethread.cpp` | `slot_setInfo`、`run`、`write_frame`、队列方法 | 消费原始帧，H.264/AAC 编码及 FLV 封装 | A |
| Audio_Read | `AVClient/modules/recorder/audio_read.cpp` | `slot_openAudio`、`slot_readMore` | QAudioInput 采集 S16，swr 转 FLTP | B |
| PicInPic_Read | `AVClient/modules/recorder/picinpic_read.cpp` | `run`、`slot_getVideoFrame`、`ImageToYuvBuffer` | 摄像头预览、桌面抓取、RGB 转 YUV | B |
| PictureWidget | `AVClient/modules/recorder/picturewidget.cpp` | `slot_setImage` | 显示摄像头置顶预览 | C |
| SettingsPage | `AVClient/pages/SettingsPage.cpp` | connect/disconnect/ping slots | 操作共享网络连接 | B |
| RemoteMediaPage | `AVClient/pages/RemoteMediaPage.cpp` | upload/download 响应 slots、`sendNextUploadBlock`、`prepareSafeDownloadOffset` | 两套传输状态机与任务 UI | A |
| AVNetworkClient | `AVClient/modules/network/AVNetworkClient.cpp` | 所有 `sendXxx`、`onPacketReceived` | 业务对象与二进制协议互转 | A |
| TcpClient | `AVClient/modules/network/TcpClient.cpp` | connect、`sendPacket`、`recvLoop`、send/recvAll | Winsock、长度帧和接收线程 | A |
| UploadTaskStore | `AVClient/modules/network/UploadTaskStore.cpp` | `loadAll/save/remove` | QSaveFile 原子保存上传恢复状态 | B |
| DownloadTaskStore | `AVClient/modules/network/DownloadTaskStore.cpp` | `loadAll/save/remove` | 保存下载状态并约束 cache 路径 | B |

## 3. 服务端核心类

| 模块/类 | 文件 | 核心函数 | 调用与作用 | 级别 |
|---|---|---|---|---|
| AVServer | `AVServer/src/AVServer.cpp` | `start` | main 到 EpollServer 的外观层 | C |
| EpollServer | `AVServer/src/EpollServer.cpp` | `start`、`handleRead`、`parseFrames`、`submitBusinessTask`、`handleCompletionEvent`、`flushSendQueue` | Reactor 主循环、线程池桥接与 socket 所有权 | A |
| ConnectionContext | `AVServer/include/ConnectionContext.h` | ctor；数据成员 | 每连接接收缓冲、发送队列、ID 和 in-flight 状态 | A |
| ProtocolDispatcher | `AVServer/src/ProtocolDispatcher.cpp` | `dispatch`、`isBusinessPacket`、各 handle | 类型路由、校验请求、调用 Manager、构造响应 | A |
| ThreadPool | `AVServer/src/ThreadPool.cpp` | `start/submit/workerLoop/stop/reapFinishedWorkers` | 有界队列、动态扩缩容与 worker 生命周期 | A |
| MediaManager | `AVServer/src/MediaManager.cpp` | `buildMediaListPayload` | 扫描 media，过滤并生成文本列表 | B |
| UploadManager | `AVServer/src/UploadManager.cpp` | create/resume/write/finish、persist/reconcile/load/cleanup | 上传持久任务、一致偏移、排他所有权 | A |
| DownloadManager | `AVServer/src/DownloadManager.cpp` | `initializeDownload`、`readBlock`、`validateCompletion` | 无状态文件信息校验与随机分片读取 | A |

## 4. 最重要的 20 个函数

1. `MainWindow::slotPlayLocalFile()`：跨页导航与播放转发。
2. `PlayerDialog::playLocalFile()`：统一播放入口。
3. `VideoPlayer::run()`：播放全生命周期。
4. `audio_decode_frame()`：音频 packet 到 SDL PCM。
5. `decode_one_video_frame()`：视频 packet 到同步后的 QImage。
6. `packet_queue_put()`：引用计数与生产入队。
7. `packet_queue_get()`：阻塞消费与所有权转移。
8. `RecorderDialog::on_pb_start_clicked()`：录制参数入口。
9. `SaveVideoFileThread::slot_setInfo()`：输出容器/编码器初始化。
10. `SaveVideoFileThread::run()`：音视频交错编码与收尾。
11. `RemoteMediaPage::sendNextUploadBlock()`：64 KB 串行上传推进。
12. `RemoteMediaPage::prepareSafeDownloadOffset()`：下载恢复安全前缀。
13. `AVNetworkClient::onPacketReceived()`：所有响应分发中心。
14. `TcpClient::recvLoop()`：客户端长度帧接收。
15. `EpollServer::start()`：服务端生命周期与事件循环。
16. `EpollServer::parseFrames()`：服务端增量拆帧与同连接串行化。
17. `EpollServer::handleCompletionEvent()`：worker 结果回到 Reactor。
18. `ThreadPool::workerLoop()`：任务消费与动态缩容。
19. `UploadManager::writeBlock()`：可靠偏移、fsync 与元数据提交。
20. `DownloadManager::initializeDownload()`：恢复元数据和 acceptedOffset 校验。

## 5. 推荐阅读路径

先从 A 级调用链读：`main -> MainWindow -> RemoteMediaPage/PlayerDialog -> AVNetworkClient/TcpClient`，再读 `server main -> EpollServer -> ProtocolDispatcher -> Manager`。播放器和录制器分别按生产者/消费者画图；最后回到 B/C 级补格式转换、渲染和状态文件细节。

