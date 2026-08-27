# 薄弱点清单

> 只记录经过复述、检查题或实战暴露的问题。2026-07-27 尚未收到摸底答案，因此不预判薄弱点。

| 编号 | 面试薄弱点 | 对应核心源码 | 达标要求 | 状态 |
|---|---|---|---|---|
| W-A01 | 项目架构和核心业务链尚需脱稿表达训练 | 两端 `main`、`MainWindow`、`EpollServer::start` | 2 分钟讲清职责、数据流、线程和设计原因 | 阶段流程已通过，待模拟口述 |
| W-A02 | 播放器和录制器均达到 L4；完整录制介绍容易省略 GUI/摄像头/编码线程分工和完整停止顺序 | `PlayerDialog::playLocalFile`、`VideoPlayer::run`、`RecorderDialog`、`SaveVideoFileThread::run` | 最终模拟时按固定结构一次讲全播放与录制，并主动说明线程、停止和至少三项不足 | 两模块 L4，表达待终面复查 |
| W-A03 | 客户端TCP长度帧和协议分层达到L3；Qt跨线程槽机制及“加发送锁不等于异步化”由学习者主动跳过验证；服务端epoll开始学习 | `TcpClient::recvLoop`、`AVNetworkClient::onPacketReceived`、`RemoteMediaPage`、`EpollServer::handleRead/parseFrames` | 面试时能解释客户端线程链、完整异步化边界、双方拆包差异和非阻塞Reactor | 客户端细节待终面按需复查，服务端学习中 |
| W-A04 | 动态线程池和完成队列/eventfd主链已达到L4；需收紧finished线程仍需join，以及陈旧完成结果的完整双重确认链 | `ThreadPool::workerLoop/reapFinishedWorkers/stop`、`submitBusinessTask`、`pushCompletion`、`handleCompletionEvent` | 最终面试时主动说出线程函数结束与线程对象回收的区别，以及“connectionId反查fd→连接对象再次比对身份” | 阶段通过，终面表达复查 |
| W-A05 | 双向断点续传已达到L4；需在最终面试主动补全双方同时重启条件、异常状态来源，以及“服务端期望哈希＋客户端本地计算”的信任关系 | `UploadManager`、`DownloadManager`、两类 TaskStore | 脱稿讲清上传有状态/下载无状态、安全offset、远程变化和强哈希优化 | 阶段通过，终面表达复查 |
| W-A06 | 尚不能分析并发、异常、当前不足与优化边界 | Reactor、传输管理器、播放器/录制器停止路径 | 能回答至少 3 类异常和替代方案 | 未掌握 |
| W-A07 | Reactor/worker 职责边界已经复查通过；粘包回答首次遗漏“取出一帧后继续循环解析剩余字节” | `EpollServer::start`、`acceptClients`、`handleRead`、`parseFrames` | 能说明 Reactor 自己完成网络事件和轻量分发，只将耗时业务提交 worker；拆包时循环提取完整帧并保留不完整尾部 | Reactor 边界已稳定，粘包完整表达留待终面复查 |

> B/C 级细节不再单独占用薄弱点条目；仅在妨碍核心模块表达时记录。

## 间隔复习规则

- 首次纠错后：下一课开始前复查。
- 仍然模糊：隔 2～3 课再次检查，并安排一次源码定位题。
- 连续两次正确且能回答“为什么/异常会怎样”：标记为已稳定。
- 核心模块即使已稳定，也在大模块模拟面试中再次抽查。
