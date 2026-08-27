# AVServer epoll Reactor

## 1. 入口与启动链

```text
AVServer/src/main.cpp::main
  -> 解析端口（默认 8000）
  -> AVServer::start(port)
  -> EpollServer::start(port)
```

`EpollServer::start()` 初始化 `ProtocolDispatcher` 的 media/temp/tasks 目录，创建非阻塞 listen socket、epoll fd、eventfd，注册 listen/eventfd，启动线程池，然后进入 `epoll_wait()`。

## 2. listen、accept 与非阻塞

`createListenSocket()` 设置 `SO_REUSEADDR`、`O_NONBLOCK`，bind `INADDR_ANY` 并 listen backlog 128。`acceptClients()` 在 LT 可读后循环 accept，直到 EAGAIN；每个 client 也设非阻塞和 `TCP_NODELAY`。

使用非阻塞是因为就绪只表示“现在操作大概率可推进”，不保证读/写到业务期望长度。Reactor 不能因单连接阻塞而停住所有连接。

## 3. ConnectionContext

文件：`AVServer/include/ConnectionContext.h`

每连接保存：`fd`、单调 `connectionId`、对端 IP/端口、`receiveBuffer`、`sendQueue`、`queuedBytes`、`lastActivity`、`closing`、`businessTaskInFlight`。网络状态只由 Reactor 线程访问，worker 不持有它的裸指针。

`PendingSend` 保存完整长度帧和已发送 `offset`，用于短发送后继续。

## 4. EPOLLIN 与增量解析

`handleRead()` 循环 `recv()` 到 EAGAIN，把数据追加到该连接 `receiveBuffer`，限制为 4 MB。`parseFrames()`：

1. 至少 4 字节时读取主机序 `packetLength`。
2. 校验 `sizeof(PackType) <= length <= 256 KB`。
3. 缓冲不足一整帧则保留等待下次。
4. 足够则复制包体并从缓冲删除这一帧。
5. Ping/Login 直接在 Reactor 分发；业务协议提交线程池。

一次 recv 不等于一个协议包，因为 TCP 没有消息边界；增量缓冲同时解决粘包和半包。

## 5. 每连接一个在途任务

业务提交前设置 `businessTaskInFlight=true`。该连接后续完整帧仍可留在 receiveBuffer，但暂停分发；completion 回来后清 false，再调用 `parseFrames()`。这保证同一连接的上传块按线序执行，也避免为协议增加 request ID/重排表。代价是同连接无法流水线并行业务。

## 6. EPOLLOUT 与非阻塞发送队列

`queueResponse()` 把 `[length][body]` 组帧，累计队列上限 4 MB。`flushSendQueue()` 循环 send：

- 成功：推进 `PendingSend::offset` 和 `queuedBytes`。
- EAGAIN：用 `modifyClientEvents(fd, true)` 临时监听 EPOLLOUT。
- 队列空：关闭 EPOLLOUT，只保留 EPOLLIN/EPOLLRDHUP。

不能永久监听 EPOLLOUT，因为大多数 socket 大部分时间可写，会使 `epoll_wait` 高频唤醒。一次 send 可能只发送一部分，必须记住 offset。

## 7. 业务线程与 completion

`submitBusinessTask()` 只捕获 `connectionId`、日志用 fd、协议类型和 packet 副本。worker 调 `ProtocolDispatcher::dispatch()` 生成包体列表，构造 `CompletedTask`，`pushCompletion()` 加锁入 `m_completionQueue` 并写 eventfd。

Reactor 收到 eventfd EPOLLIN 后 `handleCompletionEvent()` 批量交换队列，按 connectionId 查当前 fd 并再次核对 context。有效才排入发送队列；连接已关闭或 fd 已复用则丢弃陈旧结果。

## 8. 为什么 connectionId 必要

fd 是进程描述符表的小整数，close 后很快可被新连接复用。只凭 worker 保存的 fd 可能把旧响应发给新客户端。每次 accept 分配新的 64 位单调 connectionId，completion 必须同时命中 ID 映射和 context，因而复用 fd 也不会误投递。

## 9. eventfd 的作用

worker 完成是内存事件，而 Reactor 阻塞在 epoll 等 fd。eventfd 把跨线程通知转换为可被 epoll 监听的 64 位计数器，无需轮询。信号处理函数也写 eventfd 唤醒服务器停止。

## 10. 连接关闭

`closeConnection()` 先从 connectionId 映射移除，通知 Dispatcher 解除上传绑定，从 epoll 删除并 close fd，最后删除 context。晚到 completion 查不到 ID，会被丢弃并再次调用解除绑定，覆盖“断开后才创建成功”的上传 INIT 竞态。

## 11. 定时维护

事件循环每 5 分钟调 `scheduleMaintenance()`，且用 `m_maintenanceTaskInFlight` 防重复。维护也进入线程池，调用上传 72 小时过期清理；不是每次 `epoll_wait` 都扫描磁盘。

## 12. 服务端关闭

SIGINT/SIGTERM 只设置 `sig_atomic_t` 标志并写 eventfd。循环退出后 `closeAll()`：关闭 listen、`ThreadPool::stop()` 排空并 join、清 completion、逐连接解绑关闭、关闭 eventfd 和 epoll fd。析构再次调用是幂等保护。

## 13. select、poll、epoll；LT 与 ET

- select 有 fd 数量/位图复制限制；poll 使用数组，每轮仍线性扫描；epoll 在内核维护关注集合并返回就绪项，更适合大量连接。
- LT：只要条件仍满足会重复通知，编程容错更好；ET：状态边沿变化时通知，必须彻底读写到 EAGAIN，效率潜力高但更易漏事件。
- 本项目是 LT，但 read/accept/send 仍循环到 EAGAIN，行为稳健且便于将来比较 ET。

## 14. 资源上限与背压

单包 256 KB、单连接接收缓冲 4 MB、发送队列 4 MB、线程池任务队列 256。超限关闭连接或返回 `server busy`，避免恶意/慢客户端无限占用内存。

## 15. Reactor 面试追问（20 题）

1. epoll 解决什么？高效等待大量 fd 就绪，不替代业务计算和磁盘 I/O。
2. 为什么是 Reactor？事件线程接收就绪并分派处理，I/O 状态由它集中管理。
3. 为什么 socket 非阻塞？避免单 fd 阻塞整个事件循环。
4. LT 与 ET 区别？LT 持续通知，ET 只通知边沿；ET 必须处理到 EAGAIN。
5. 为什么 LT 也循环 recv？提高单次唤醒利用率并正确排空当前数据。
6. 粘包在哪里解决？`ConnectionContext::receiveBuffer + parseFrames()`。
7. 为什么限制 packetLength？防止负数、异常分配与内存攻击。
8. 为什么每连接独立缓冲？不同 TCP 流的帧边界和进度互不相同。
9. send 为什么会短写？内核发送缓冲可用空间有限。
10. 为什么发送队列保存 offset？下次 EPOLLOUT 从未发位置续发。
11. 为什么不常驻 EPOLLOUT？可写通常一直成立，会造成忙唤醒。
12. 为什么 worker 不拿 context 指针？连接可能先关闭，造成悬空指针和跨线程竞态。
13. 为什么 packet 要复制？receiveBuffer 会继续 erase/扩容，worker 需独立生命周期。
14. fd 复用是什么？close 后同一整数可分配给新 socket。
15. connectionId 怎样防误投？完成结果按新旧不同 ID 校验，旧结果被丢弃。
16. eventfd 比轮询好在哪？无任务时不耗 CPU，且可纳入同一 epoll。
17. 队列满怎么办？返回协议对应的 `server busy` 响应，不继续积压。
18. 客户端执行中断开怎么办？worker 可完成，但 completion 被丢弃，任务绑定被解除。
19. 优雅关闭为什么先停线程池？防止 worker 在依赖对象和 eventfd 被销毁后回调。
20. 当前瓶颈？单 Reactor、每连接一在途、UploadManager 粗锁、同步文件 I/O 和 packed host-order 协议。

