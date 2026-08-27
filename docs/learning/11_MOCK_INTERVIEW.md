# 分层模拟面试（85 题）

使用方法：先只读问题口答，再看“答题抓手”。标有“追问”的题要能定位到真实文件/函数。回答结构统一用：结论 -> 当前源码 -> 设计原因 -> 边界/改进。

## 第一轮：项目介绍

### 30 秒版本

AVProject 是一个 Windows/Ubuntu C/S 音视频系统。Windows 客户端用 Qt、FFmpeg、SDL、OpenGL 和 OpenCV完成本地播放、桌面/麦克风录制以及远程媒体管理；Ubuntu 服务端用 epoll LT Reactor、动态线程池和自定义 TCP 长度帧提供媒体列表、64 KB 上传下载及双向断点续传。项目重点是把音视频流水线、网络状态机和 Linux 高并发服务端串成可运行闭环。

### 1 分钟版本

项目由 AVClient 和 AVServer 组成。客户端将原播放器和录制器封装进统一 Qt Widgets 主窗口，播放器完成 FFmpeg 解封装解码、SDL 音频输出、视频追随音频时钟和 OpenGL 显示；录制器采集桌面、摄像头预览和麦克风，将桌面 YUV420P 与音频 FLTP 编码为 H.264/AAC 并封装 FLV。网络采用 4 字节长度头加协议包体，支持 MySQL 注册登录、PUBLIC/MINE 媒体、64 KB 串行分片和下载后播放。服务端用 epoll LT 管理非阻塞连接，把 MySQL、Argon2id 和文件 I/O 放到 4 至 8 个 worker 的有界线程池，completion 经 eventfd 回 Reactor；connectionId 防止 fd 复用误投。上传任务带 ownerUserId 双端持久化，下载按 mediaId 保持服务端无状态。当前没有 TLS、长期 session 和 SHA-256，我会明确把它定位为可学习、可演示而非生产平台。

### 3 分钟版本

先讲业务：用户可以播放本地文件、录制桌面和麦克风，把录制或其他媒体上传到 Ubuntu，刷新远程列表，再下载到 cache 并自动播放；网络、客户端或服务端重启后可以恢复传输。

客户端由 MainWindow 组合四页并共享 AVNetworkClient。播放使用一个读取线程解封装并把 packet 放到音视频队列，SDL 回调消费音频，视频线程解码并依据音频时钟控制展示，QImage 最终交给 OpenGL widget。录制由 PicInPic_Read 采集桌面和摄像头预览，Audio_Read 采集 PCM 并转 FLTP，SaveVideoFileThread 比较两条 PTS 后交错编码 H.264/AAC 到 FLV。当前摄像头是悬浮预览，不夸大为像素级合成。

网络层用 TcpClient 管理 Winsock 和完整长度帧，AVNetworkClient 管理协议结构体和 Qt 信号。服务端 Reactor 管 accept/recv/send、每连接缓冲和发送队列；业务 packet 副本交给有界动态线程池，worker 不碰 socket，结果通过 completionQueue 和 eventfd 返回。每连接一个在途业务保证顺序，connectionId 过滤 fd 复用后的旧结果。

上传 INIT 创建 transferId/token/ownerUserId、`.part/.task`，每块 fsync 并原子更新元数据，重启后扫描恢复；下载按 mediaId/offset 无状态读取，客户端用 `.part` 和 JSON 计算 safeOffset，服务端用 size/mtime 接受或拒绝。权衡是可靠和易解释优先，代价包括频繁 fsync、UploadManager 粗锁、每块媒体查询、packed 主机序协议和没有强哈希。

### 5 分钟版本

在 3 分钟版本后补四个“为什么”：为什么远程播放先下载（复用成熟播放器、缩小首版范围）；为什么上传有服务端状态而下载无状态（写操作需保护未完成任务，读操作可随机读取）；为什么 epoll 后还要线程池（epoll 不消除磁盘 I/O 阻塞）；为什么 worker 结果还要回 Reactor（保持 socket 单线程所有权）。最后主动给边界：无鉴权/TLS/SHA-256，协议仅适合当前小端双方，播放器/录制器仍有旧 API 与线程安全风险。

## 第二轮：整体与 Qt（1-12）

1. **项目解决什么问题？** 答题抓手：本地播放录制 + C/S 远程媒体闭环，不是实时流媒体。
2. **为什么整合三个方向？** 把媒体产生、消费和分发串起来，能体现跨平台、音视频和网络工程能力。
3. **完整闭环是什么？** 录制 -> 本地文件 -> 分片上传 -> media 列表 -> 下载 cache -> 本地播放。
4. **你的主要技术贡献如何讲？** 统一客户端、协议、Reactor/线程池、双向恢复；同时说明参考项目来源，避免声称所有底层算法从零发明。
5. **为什么 MainWindow 持有共享网络对象？** 单连接状态、统一生命周期、Settings/RemoteMedia 协作。
6. **为什么 RemoteMediaPage 不直接持有 PlayerPage？** 导航权归 MainWindow，signal 解耦页面。
7. **Qt signal/slot 跨线程如何执行？** AutoConnection 根据对象线程归属变 queued，UI slot 在主线程执行。
8. **网络线程能直接改 UI 吗？** 不能；QWidget 非线程安全，应发 signal。
9. **QObject 父子关系解决什么？** 确定基本所有权和析构；std::thread/QThread 仍需先停止。
10. **为什么页面适配层很薄？** 保留原播放器/录制器逻辑，降低阶段 1 整合风险。
11. **为什么不直接删除四个原项目？** 保留学习基线和回归参照，主线独立演进。
12. **当前不是生产系统的证据？** 无 TLS/用户/哈希，旧 FFmpeg API，简单线程池与协议。

## 第三轮：播放器（13-27）

13. **播放入口在哪里？** `PlayerDialog::playLocalFile -> VideoPlayer::setFileName/run`。
14. **`avformat_open_input` 做什么？** 打开媒体并创建解封装上下文，不等于打开解码器。
15. **如何找音视频流？** `find_stream_index()` 遍历 stream codec type。
16. **packet 与 frame？** 压缩数据与解码数据，不是一一对应。
17. **为何 PacketQueue 要 `av_packet_ref`？** 延长底层 buffer 生命周期。
18. **队列为何用 mutex/condition？** 多线程生产消费与阻塞等待。
19. **音频链路？** queue -> decode -> swr S16 -> SDL callback -> device。
20. **视频链路？** queue -> decode -> timestamp/sync -> sws RGB -> QImage -> OpenGL。
21. **为何音频作主时钟？** 硬件节奏稳定且音频不连续更敏感。
22. **视频领先/落后怎么办？** 领先等待，落后缩短等待/可能丢旧帧；当前实现较简化。
23. **seek 调用链？** UI 提交目标，读取线程 `av_seek_frame`，flush 队列并注入 flush packet。
24. **为何 flush 解码器？** 内部参考帧和重排缓存仍属于旧时间点。
25. **EOF 为何等待？** 队列和 codec 可能仍有尾部帧。
26. **当前播放器明显风险？** `start()` 冗余、共享状态数据竞争、旧 API、每帧上下文/纹理开销。
27. **追问：如何升级？** 新 send/receive decode API、原子状态、持久转换上下文、现代 GL 与更完整同步。

## 第四轮：录制器（28-40）

28. **录制入口？** `RecorderDialog::on_pb_start_clicked -> slot_setInfo -> slot_openVideo`。
29. **桌面如何采集？** `QScreen::grabWindow()`，经信号在对象线程执行。
30. **摄像头如何采集？** OpenCV `VideoCapture(0)`；当前主要用于悬浮预览。
31. **画中画是否写入文件？** 没有像素合成，必须如实说明。
32. **RGB 为何转 YUV420P？** 当前 H.264 encoder 输入格式及压缩效率。
33. **麦克风如何采集？** QAudioInput 读 S16 packed PCM。
34. **为何转 FLTP？** 当前 AAC encoder 配置为 planar float。
35. **视频/音频队列职责？** 解耦采集速度与编码/磁盘速度。
36. **如何决定先写哪条流？** `av_compare_ts(next_pts)` 选时间更早的流。
37. **PTS/DTS/time base？** 展示/解码时间与单位；复用前 rescale。
38. **FLV 封装步骤？** output context -> streams/codecs -> header -> interleaved packets -> trailer。
39. **停止为何要 trailer/flush？** trailer 完成容器；编码器延迟 packet 需 flush，当前源码显式 encoder flush 不完整。
40. **当前录制风险？** malloc/delete[] 不匹配、跨线程普通标志、错误路径 exit、旧 API 和高频分配。

## 第五轮：TCP 与协议（41-53）

41. **为什么 TCP？** 文件传输需要有序可靠字节流，省去应用层重传排序。
42. **TCP 是否保留消息边界？** 不保留，所以有粘包/半包。
43. **4 字节长度头作用？** 给包体定界，接收方可增量组帧。
44. **客户端和服务端如何拆帧不同？** 客户端 recvAll；服务端每连接 buffer 增量解析。
45. **一次 send 为何不一定发完？** 内核缓冲空间与信号中断；必须 sendAll 或发送队列。
46. **动态 payload 如何组织？** 固定 header 后接文本或二进制，声明长度必须等于实际长度。
47. **64 KB 如何选择？** 控制单包内存和确认粒度，调用次数仍可接受；不是理论最优。
48. **协议有什么跨平台问题？** packed struct + 主机序，缺版本；大端/ABI 演进风险。
49. **为什么 `#pragma pack(1)`？** 固定当前字段偏移，但不能解决字节序和协议演进。
50. **如何改为严格协议？** 显式序列化网络序整数、版本、错误码、长度限定，或 schema。
51. **LOGIN 是否是真鉴权？** 不是，固定成功的协议测试接口。
52. **连接断开客户端如何处理？** 停止块推进、关闭活动文件、持久任务为等待恢复。
53. **追问：为何没有 TLS？** 当前学习范围；生产化必须防窃听、篡改和 token 泄漏。

## 第六轮：epoll Reactor（54-67）

54. **select/poll/epoll 区别？** select 位图/限制，poll 数组线扫，epoll 维护关注集合并返回就绪项。
55. **LT/ET？** LT 条件持续就绪会重报；ET 只报边沿，必须读写到 EAGAIN。
56. **为何非阻塞？** 任何一个 socket 都不能卡住事件循环。
57. **ConnectionContext 保存什么？** fd/id/peer、receiveBuffer、sendQueue、queuedBytes、activity、in-flight。
58. **如何处理半包？** 缓冲不足 frameSize 就保留到下次。
59. **为什么限制接收/发送队列？** 防资源失控和慢客户端拖垮服务端。
60. **为何 EPOLLOUT 按需开启？** 永久监听会因长期可写导致忙唤醒。
61. **为何每连接一个业务 in-flight？** 保序、简化上传状态机，不需 request ID。
62. **fd 复用风险？** 旧 worker 响应可能误发新连接。
63. **connectionId 如何解决？** 每 accept 新 ID，completion 用 ID 二次核对。
64. **worker 为何不能持 context 指针？** 连接可先销毁，且跨线程修改网络状态会竞态。
65. **eventfd 作用？** 把 worker 完成/停止信号变成 epoll 可监听事件。
66. **任务期间断开怎么办？** worker 可结束，Reactor 丢弃 completion，再解绑上传。
67. **优雅关闭顺序？** 停接入、排空 join worker、清 completion、关连接、eventfd、epoll。

## 第七轮：线程池（68-77）

68. **为什么 epoll 后还需线程池？** epoll 不消除目录、文件、fsync 的阻塞。
69. **构造参数？** core 4、max 8、queue 256、非核心 idle 60 秒。
70. **扩容规则？** 入队后 pending > idle 且 current < max，增加一个非核心。
71. **缩容规则？** 非核心 wait_for 超时、队列空、current > core。
72. **condition_variable 为何用 predicate？** 处理虚假唤醒并检查停止/任务。
73. **为何回收线程要锁外 join？** 防线程退出获取同一锁造成死锁。
74. **队列满怎么办？** submit false，Dispatcher 构造 server busy。
75. **为什么 worker 不 send？** 保持 socket、部分发送和 epoll 状态单线程所有权。
76. **UploadManager 粗锁权衡？** 一致性简单，但 fsync 期间其他上传等待。
77. **线程池如何生产化？** 指标、优先级、deadline/cancel、隔离池、配置化和压测。

## 第八轮：上传下载恢复（78-85）

78. **上传为何有 token？** token 证明客户端持有任务恢复凭据，ownerUserId 再校验真实用户归属；两者联合使用，但没有 TLS 时仍存在链路泄露风险。
79. **上传权威偏移？** 服务端取 expected/metadata/actual part 的安全最小值。
80. **重复块与跳跃块？** 完全已确认重复块幂等成功；跳跃或部分重叠拒绝。
81. **服务端重启如何恢复上传？** 扫描 `.task`、校验 `.part`、active owner 清零、恢复 map。
82. **下载为何服务端无状态？** 每次按 mediaId 查可信 storedName，再按 offset 独立随机读；恢复信息只需客户端持有。
83. **safeOffset 如何算？** `min(confirmedOffset, partSize)`，必要时截断或下调状态。
84. **远程文件变化如何识别？** size 与 mtime 必须等于初始化值；不足以提供强内容一致性。
85. **如何加入 SHA-256？** 初始化记录摘要，完成时 worker 流式计算并比对；需进度、取消和资源限制。

## 异常场景追加追问

- **`.task` 存在但 `.part` 更小？** reconcile 降到实际安全大小并持久化。
- **`.part` 存在但无 `.task`？** 记录 orphan，不盲目恢复。
- **上传响应丢失？** 客户端恢复；服务端对完整重复块返回当前 confirmed offset。
- **远程文件同名替换但 size/mtime 相同？** 当前可能误判未变化，必须承认无强哈希。
- **队列满且客户端在上传？** 收到 server busy 后保留任务，重新连接/恢复；没有自动重试。
- **播放器频繁 seek？** 旧 seek 数据要清队列/codec；共享状态同步仍是风险点。

## 评分表

每题 0-2 分：0 不知道；1 有概念但无源码；2 能说文件/函数、机制和边界。总分 170：136 以上可进入压力追问；110-135 继续补源码定位；低于 110 先按四周计划重读 A 级函数。
