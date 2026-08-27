# 四周源码学习计划

节奏：每周 6 天，每天 3-4 小时。建议固定为：阅读 90 分钟、画图/笔记 40 分钟、口述 30 分钟、调试 40 分钟、自测 20 分钟。第 7 天休息或只复盘错题。

## 第 1 周：项目、客户端与播放器

### Day 1：全局地图
- 阅读：`README.md`、`01_PROJECT_OVERVIEW.md`、两端 `main.cpp`、`MainWindow.cpp`。
- 回答：项目闭环是什么？两端分别负责什么？
- 手画：总体模块图和线程图。
- 口述：30 秒、1 分钟项目介绍。
- 调试：启动客户端，逐 tab 操作并观察对象职责。
- 自测：不看文档写出 10 个核心类。

### Day 2：页面与跨页协作
- 阅读：四个 Page、`MainWindow`、`02_AVCLIENT_CALL_CHAIN.md`。
- 回答：为何共享网络对象？为何由 MainWindow 转发播放？
- 手画：Qt signal/slot 页面关系。
- 口述：下载并播放完整调用链。
- 调试：在下载完成 signal 与 `slotPlayLocalFile` 设断点。
- 自测：解释每页所有权和析构顺序。

### Day 3：播放器打开与解封装
- 阅读：`playerdialog.cpp`、`videoplayer.cpp` 1-972 行。
- 回答：packet/frame、stream/codec、读取线程是什么？
- 手画：open -> stream -> decoder -> queues。
- 口述：3 分钟播放流程。
- 调试：在 `avformat_open_input`、`av_read_frame` 观察 stream_index。
- 自测：写出 `VideoPlayer::run()` 前半段步骤。

### Day 4：音频与同步
- 阅读：`audio_callback`、`audio_decode_frame`、`synchronize_video`。
- 回答：为何音频主时钟？swr 做什么？
- 手画：audio queue -> decoder -> swr -> SDL。
- 口述：音频时钟与视频等待策略。
- 调试：观察 audio_clock/video_clock 与队列长度。
- 自测：回答播放器追问 1-8。

### Day 5：视频、OpenGL 与 seek
- 阅读：`decode_one_video_frame`、PacketQueue、MyOpenGLWidget。
- 回答：seek 为何 flush 两层？引用计数为何必要？
- 手画：video packet -> QImage -> GL texture。
- 口述：seek 与 EOF 生命周期。
- 调试：多次拖动 slider，观察 flush packet。
- 自测：回答播放器追问 9-15。

### Day 6：第一轮模拟面试
- 阅读：`03_PLAYER_PIPELINE.md` 与本周笔记。
- 回答：抽取 15 道 Qt/FFmpeg 题。
- 手画：10 分钟内重画客户端和播放器图。
- 口述：5 分钟项目介绍 + 8 分钟播放器深挖。
- 调试：复现暂停、seek、stop、下载后播放。
- 自测：记录答不出的函数，形成第一份错题表。

## 第 2 周：录制器与网络协议

### Day 7：录制入口与采集
- 阅读：RecorderPage/Dialog、PicInPic_Read。
- 回答：摄像头是否真正写入录制文件？桌面在哪个线程抓取？
- 手画：desktop/camera 两条路径。
- 口述：如实解释当前“画中画”边界。
- 调试：断点观察 QImage 与 YUV buffer 大小。
- 自测：区分采集、编码、封装。

### Day 8：音频采集与格式转换
- 阅读：Audio_Read、`open_audio/add_audio_stream`。
- 回答：S16、FLTP、采样率、声道布局分别是什么？
- 手画：QAudioInput -> PCM -> swr -> AAC frame。
- 口述：2 分钟麦克风链路。
- 调试：观察 `bytesReady/nFrameCount/mAudioOneFrameSize`。
- 自测：找出分配释放不匹配风险。

### Day 9：编码、PTS 与 FLV
- 阅读：SaveVideoFileThread 全文、`04_RECORDER_PIPELINE.md`。
- 回答：send/receive encoder、time_base、interleaved write。
- 手画：两队列 -> compare_ts -> H.264/AAC -> FLV。
- 口述：3 分钟录制主链。
- 调试：观察 video/audio next_pts 和输出文件增长。
- 自测：回答录制追问 1-15。

### Day 10：TcpClient
- 阅读：TcpClient、协议头。
- 回答：connect timeout、阻塞接收线程、sendAll/recvAll。
- 手画：4 字节长度帧与粘包半包。
- 口述：Windows 客户端 TCP 生命周期。
- 调试：服务端未启动/中途断开。
- 自测：解释为什么 UI 线程不直接 recv。

### Day 11：AVNetworkClient 与协议
- 阅读：AVNetworkClient、`05_NETWORK_AND_PROTOCOL.md`。
- 回答：每组请求/响应字段和动态 payload 校验。
- 手画：全部协议状态图。
- 口述：主机字节序与 packed struct 风险。
- 调试：抓日志对应 type 20001-20020。
- 自测：随机抽 5 个协议默写字段。

### Day 12：第二轮模拟面试
- 阅读：第 2 周笔记和风险清单。
- 回答：15 题录制 + 15 题 TCP/协议。
- 手画：录制图、长度帧图。
- 口述：5 分钟录制与 5 分钟协议设计。
- 调试：完成录制 -> 上传 -> 列表闭环。
- 自测：把答错题定位到文件和函数。

## 第 3 周：epoll 与动态线程池

### Day 13：服务端启动与连接
- 阅读：server main、AVServer、EpollServer start/create/accept。
- 回答：listen、非阻塞、LT、TCP_NODELAY。
- 手画：socket -> bind -> listen -> epoll。
- 口述：服务端从 main 到 accept。
- 调试：多客户端连接并观察 connectionId。
- 自测：select/poll/epoll 对比。

### Day 14：收包与发包
- 阅读：handleRead/parseFrames/queueResponse/flushSendQueue。
- 回答：receiveBuffer、短写、EPOLLOUT 开关。
- 手画：增量拆帧与发送队列。
- 口述：粘包/半包在服务端如何解决。
- 调试：观察 partial send 或提高日志理解 offset。
- 自测：回答 Reactor 题 1-12。

### Day 15：connectionId 与 completion
- 阅读：submitBusinessTask/pushCompletion/handleCompletionEvent/closeConnection。
- 回答：fd 复用、陈旧结果、为何不传 context 指针。
- 手画：worker completion 回 Reactor。
- 口述：任务期间断开场景。
- 调试：业务请求后断开并观察 completion dropped。
- 自测：回答 Reactor 题 13-20。

### Day 16：线程池基础
- 阅读：ThreadPool start/submit/workerLoop。
- 回答：生产者消费者、condition_variable、计数维护。
- 手画：任务入队、worker 取任务。
- 口述：扩容规则和有界队列。
- 调试：运行线程池测试并观察 4 -> 8。
- 自测：推演第 257 个排队任务。

### Day 17：缩容、回收与关闭
- 阅读：mark/reap/stop、`07_DYNAMIC_THREAD_POOL.md`。
- 回答：为什么锁外 join？为何不强杀？
- 手画：非核心 60 秒退出与回收。
- 口述：优雅关闭顺序。
- 调试：观察 8 -> 4 与 Ctrl+C。
- 自测：回答线程池 25 题。

### Day 18：第三轮模拟面试
- 阅读：`06_AVSERVER_REACTOR.md`、本周错题。
- 回答：20 分钟服务端源码追问。
- 手画：Reactor + ThreadPool + eventfd 一张图。
- 口述：5 分钟服务端架构。
- 调试：多连接 Ping/列表/传输并发。
- 自测：从 completion 反向说回 packet 接收链。

## 第 4 周：传输恢复与综合面试

### Day 19：普通上传与持久任务
- 阅读：RemoteMediaPage 上传段、UploadTaskStore、UploadManager create/write。
- 回答：为何每块确认、为何 fsync、元数据字段。
- 手画：INIT/BLOCK/FINISH。
- 口述：普通上传 3 分钟。
- 调试：观察 `.part/.task/.upload.json`。
- 自测：默写三层 offset。

### Day 20：上传续传
- 阅读：resume/reconcile/load/cleanup。
- 回答：token、activeOwnerConnectionId、重复/跳跃块。
- 手画：客户端/服务端重启恢复。
- 口述：一致性边界和 72 小时清理。
- 调试：中断后恢复，观察非零 offset。
- 自测：非法 token、多客户端竞争推演。

### Day 21：下载与恢复
- 阅读：RemoteMediaPage 下载段、DownloadTaskStore/Manager。
- 回答：无状态原因、safe/accepted offset、mtime。
- 手画：普通/恢复下载对比。
- 口述：为何不需要 download token。
- 调试：截断/扩展 part 后恢复。
- 自测：远程文件变化场景。

### Day 22：全项目风险与改进
- 阅读：所有文档“边界/风险”段。
- 回答：SHA-256、TLS、协议序列化、旧 FFmpeg、粗锁。
- 手画：当前版与生产版差异。
- 口述：每项优化先解决什么问题、引入什么复杂度。
- 调试：只观察，不改源码；复核日志与状态文件。
- 自测：列出 10 个不足，不夸大能力。

### Day 23：简历与分时介绍
- 阅读：`11_MOCK_INTERVIEW.md`。
- 回答：个人贡献、难点、权衡、测试证据。
- 手画：白板版总架构。
- 口述：30 秒/1/3/5 分钟各录音两次。
- 调试：按演示脚本走完整闭环。
- 自测：删掉任何无法用源码证明的表述。

### Day 24：第四轮综合模拟
- 阅读：随机抽取源码，不预热。
- 回答：至少 40 道，覆盖异常和改进。
- 手画：播放器、录制、Reactor、续传四图，限时 20 分钟。
- 口述：项目介绍 + 两轮追问共 45 分钟。
- 调试：任选一个故障，从日志定位到函数。
- 自测：按“准确、结构、源码证据、边界”四项各 5 分评分。

