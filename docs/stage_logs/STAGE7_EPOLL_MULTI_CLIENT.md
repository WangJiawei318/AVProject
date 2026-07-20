# 阶段 7：epoll 多客户端服务器改造

## 1. 阶段目标

本阶段把主线 `AVServer` 从“阻塞式、一次只服务一个连接”改造成 Linux non-blocking socket + epoll LT 的多客户端服务器，同时保持阶段 1 至 5 的协议和 Windows 客户端不变。

本阶段只处理网络并发基础：连接生命周期、增量收包、协议分发、非阻塞发送和连接级资源隔离。不加入线程池、断点续传、哈希、取消、MySQL、登录鉴权或在线播放。

完成后的目标数据流是：

```text
non-blocking listen socket
  -> epoll_wait
  -> ConnectionContext(fd)
  -> receiveBuffer 增量组帧
  -> ProtocolDispatcher
  -> sendQueue
  -> non-blocking send / EPOLLOUT
```

## 2. 原阻塞式服务器存在的问题

旧实现 `accept()` 一个连接后进入该客户端的阻塞式读包循环，只有连接断开才重新 `accept()`。因此一个 AVClient 长连接会一直占住服务端，第二个客户端即使 TCP 已进入系统队列，也无法得到业务响应。

阻塞式 `readExact()` 和 `sendAll()` 还把单个客户端的网络速度直接变成整个服务端的等待时间。慢客户端、半包或异常断线都可能让其他用户看起来“服务端无响应”。旧结构把 socket 循环与 Ping、列表、上传、下载分发放在同一个类中，也不利于单独扩展连接管理。

## 3. epoll Reactor 架构

```text
AVServer::start
  -> EpollServer::start
      -> listen fd: EPOLLIN
      -> epoll_wait
          -> acceptClients()
          -> handleRead(client fd)
          -> parseFrames(ConnectionContext)
          -> ProtocolDispatcher::dispatch
          -> queueResponse(ConnectionContext)
          -> flushSendQueue(client fd)
          -> closeConnection(client fd)
```

这是单线程 Reactor：epoll 负责报告“哪些 fd 已就绪”，事件线程依次推进连接状态。多个连接可以并发等待和交替处理，但协议回调并不在多个 CPU 核心上并行执行。

## 4. LT 与 ET 的区别，以及本阶段为什么选择 LT

LT（Level Triggered）只要 fd 仍处于可读或可写状态，后续 `epoll_wait()` 仍会报告它，编程语义接近 poll。ET（Edge Triggered）通常只在状态从未就绪变为就绪时通知，必须一次循环处理到 `EAGAIN`；如果漏读，可能很久收不到下一次通知。

本阶段选择 LT，因为目标是先验证多连接生命周期和长度帧状态机。代码仍然把 accept、recv 和 send 循环执行到 `EAGAIN`，避免无谓唤醒，也为以后评估 ET 做准备，但不为理论上的少量事件数收益增加排错复杂度。

## 5. 新增文件清单

| 文件 | 作用 |
| --- | --- |
| `AVServer/include/EpollServer.h` | Reactor 网络层接口、资源和限制常量 |
| `AVServer/src/EpollServer.cpp` | non-blocking socket、epoll、增量收发和连接关闭 |
| `AVServer/include/ConnectionContext.h` | 每连接状态定义 |
| `AVServer/src/ConnectionContext.cpp` | 连接上下文和发送项初始化 |
| `AVServer/include/ProtocolDispatcher.h` | 协议业务分发接口 |
| `AVServer/src/ProtocolDispatcher.cpp` | 现有 Ping、列表、上传、下载处理迁移 |
| `tools/concurrent_client_test.py` | Python 标准库并发 Ping/列表冒烟测试 |
| `docs/stage_logs/STAGE7_EPOLL_MULTI_CLIENT.md` | 本阶段实现、验证与面试复盘 |

## 6. 修改文件清单

| 文件 | 修改内容 |
| --- | --- |
| `AVServer/include/AVServer.h` | 改为持有 `EpollServer` 的启动外观 |
| `AVServer/src/AVServer.cpp` | 将启动委托给 `EpollServer` |
| `AVServer/include/UploadManager.h` | 上传操作加入 `ownerFd`，任务记录 `ownerFd` |
| `AVServer/src/UploadManager.cpp` | 校验任务所有者并按连接清理未完成上传 |
| `AVServer/Makefile` | 加入 Reactor、上下文和分发器源码 |
| `README.md` | 当前服务端能力和阶段状态更新 |
| `docs/ARCHITECTURE.md` | 服务端章节更新为 epoll Reactor |
| `docs/BUILD_AND_RUN.md` | 同步清单、日志和并发命令更新 |
| `docs/TEST_WORKFLOW.md` | 自动化与人工多客户端流程更新 |
| `docs/INTERVIEW_QA.md` | 面试表述同步到阶段 7 真实状态 |

原始 `MediaPlayer`、`VideoRecorder`、`NetDisk-Client` 和 `NetDisk-Server` 未修改；Windows `AVClient` 的协议和业务代码也未修改。

## 7. EpollServer 说明

`EpollServer` 负责网络资源，不负责解释媒体业务。启动时依次忽略 `SIGPIPE`、初始化业务管理器、创建 listen socket、设置 `SO_REUSEADDR` 和 `O_NONBLOCK`、bind/listen、创建 epoll fd 并注册 listen fd。

listen fd 就绪后，`acceptClients()` 循环接收连接直到 `EAGAIN`。每个 client socket 设置 `O_NONBLOCK` 和 `TCP_NODELAY`，创建上下文后注册 `EPOLLIN | EPOLLRDHUP`。事件循环处理 `EPOLLERR`、`EPOLLIN`、`EPOLLOUT`、`EPOLLHUP` 和 `EPOLLRDHUP`，任何单连接错误只关闭对应 fd。

当前限制常量为：最大单包体 256 KB、单连接待发送数据 4 MB、单次栈上读取缓冲 16 KB、一次 `epoll_wait` 最多返回 64 个事件。

## 8. ConnectionContext 说明

`ConnectionContext` 以 client fd 为键保存在 `map<int, unique_ptr<ConnectionContext>>` 中，包含：

- `fd`、`peerIp`、`peerPort`；
- `receiveBuffer`：只属于该连接的未解析字节；
- `sendQueue`：该连接待发送的完整长度帧；
- `PendingSend::offset`：队首数据已发送位置；
- `queuedBytes`：未发送字节总量；
- `lastActivity`：最近成功收发时间；
- `closing`：为后续延迟关闭或跨线程状态预留。

上下文只由 Reactor 线程访问，因此本阶段无需给连接表和缓冲区加 mutex。

## 9. ProtocolDispatcher 说明

网络层从缓冲中提取一个完整包体后调用 `dispatch(fd, packet, responses)`。分发器读取 `PackType`，调用 Ping、Login、媒体列表、上传 INIT/BLOCK/FINISH 或下载 INIT/BLOCK/FINISH 处理函数。

分发器持有 `MediaManager`、`UploadManager`、`DownloadManager`，负责具体结构大小校验和响应构造，但不直接调用 `send()`。返回值是响应包体数组，长度头由 `EpollServer` 统一添加。这使业务逻辑不依赖阻塞或非阻塞网络实现。

## 10. 每连接接收缓冲区设计

TCP 只提供有序字节流，不保存应用消息边界。不同连接可能分别停在长度头第 2 字节、包体中间或三包粘连之后，因此必须独立保存尚未消费的字节。

`handleRead()` 循环 `recv()` 到 `EAGAIN`，每次收到的数据追加到当前连接 `receiveBuffer`，随后调用解析器。解析器消费完整帧，剩余半帧保留到下一次 EPOLLIN。任何连接都不会读取或删除另一个连接的缓冲数据。

## 11. 长度帧增量解析

协议继续使用：

```text
[int32_t packetLength][packetLength bytes body]
```

解析步骤：

1. 少于 4 字节，保留并等待；
2. 用 `memcpy` 读取长度，避免未对齐指针访问；
3. 长度小于 `sizeof(PackType)` 或超过 256 KB，记录 fd 和长度并关闭该连接；
4. 缓冲不足 `4 + packetLength`，保留并等待；
5. 提取完整包体，交给 `ProtocolDispatcher`；
6. 继续循环，处理缓冲中的下一帧。

长度头沿用当前客户端兼容格式，即 x86 Windows/Ubuntu 上的主机小端序。本阶段没有改成网络字节序；协议版本化和端序统一留作后续升级。

## 12. 非阻塞发送和 EPOLLOUT

业务响应先加长度头并进入该连接 `deque<PendingSend>`。`flushSendQueue()` 尽量立即发送：成功时推进 offset 和 `queuedBytes`，队首完成后弹出；`EINTR` 时重试；`EAGAIN` 时保留现场并注册 EPOLLOUT；其他错误关闭连接。

EPOLLOUT 只在队列尚未发送完时注册。队列清空后通过 `EPOLL_CTL_MOD` 取消，否则大多数始终可写的 socket 会不断触发事件，造成 CPU 空转。单连接未发送数据超过 4 MB 时关闭该慢连接，防止内存无限增长并保护其他客户端。

## 13. 连接关闭与资源释放

下列情况触发单连接关闭：`recv()` 返回 0、不可恢复的 recv/send 错误、EPOLLERR、EPOLLHUP、EPOLLRDHUP、非法包长、响应包非法或发送队列超限。

`closeConnection()` 先通知分发器清理该 fd 的未完成上传，再从 epoll 删除 fd、关闭 socket、删除上下文并记录剩余连接数。其他上下文和已完成媒体文件不受影响。服务器对象析构时逐连接执行相同清理，再关闭 listen fd 和 epoll fd。

## 14. 上传任务 owner_fd 设计

`UploadTask` 现在保存 `ownerFd`。INIT 创建任务时绑定当前 fd；BLOCK 和 FINISH 除 upload_id 外还必须匹配 owner fd，否则返回“任务属于其他连接”。这避免一个客户端猜到或拿到 upload_id 后推进另一个客户端的任务。

上传 ID 由时间、进程 ID 和单线程递增序号组成，在当前单进程生命周期内唯一。临时路径使用 upload_id，多个客户端同名上传不会共用 `.part`。FINISH 在单 Reactor 线程中顺序选择可用文件名并 rename；未来业务并行后，这段重名提交需要原子创建或锁。

连接断开时 `abortByOwner(fd)` 只遍历并删除该连接未完成任务的 `.part`，不会调用全局 `abortAll()`，也不会影响其他连接或已完成文件。

## 15. 多客户端下载无共享文件游标设计

下载 BLOCK 请求携带 `filename + offset + request_size`。`DownloadManager::readBlock()` 每次独立打开文件、seek 到请求 offset、读取当前块并关闭，不保存全局流对象或共享 offset。

因此两个客户端可以读取同一文件的不同位置，某个连接断开也没有服务端下载游标需要回收。代价是每块都会打开文件，系统调用较多；后续可为每任务维护文件句柄，但必须保证句柄归属和生命周期仍按连接或任务隔离。

## 16. 构建方法

阶段 7 依赖 Linux 的 `<sys/epoll.h>`，只能在 Ubuntu/Linux 构建：

```bash
cd ~/AVProject/AVServer
make clean
make
./AVServer 8000
```

查看监听：

```bash
ss -lntp | grep 8000
```

不需要 Qt、FFmpeg、OpenCV、SDL、MySQL 或第三方服务端库。编译选项为 C++11、`-Wall -Wextra -Wpedantic -g`。

## 17. 自动化并发测试方法

在包含 `tools/` 的项目根目录运行：

```bash
python3 tools/concurrent_client_test.py 192.168.44.130 8000 5
python3 tools/concurrent_client_test.py 192.168.44.130 8000 5 --pings 10 --media-list
```

工具使用 `ThreadPoolExecutor` 建立指定数量的连接，用 `Barrier` 让它们同时开始。每个连接重复发送结构兼容的 PING_RQ、校验 PING_RS；可选发送 MEDIA_LIST_RQ 并校验类型、payload 长度和 UTF-8。成功标准是汇总 `success=5 failed=0`，总 Ping 响应数等于 `clients * pings`。

## 18. 人工上传下载并发测试

1. Ubuntu 启动 AVServer；
2. Windows 启动 A、B、C 三个 AVClient 实例并全部连接；
3. A 上传一个较大媒体，观察上传进度；
4. B 在 A 上传期间多次发送 Ping；
5. C 同时刷新列表并下载另一个媒体；
6. 强制关闭 A，确认 B、C 连接仍可用；
7. 检查 A 未完成的 `temp/*.part` 已清理；
8. 再让 B 上传、C 下载，确认角色互换后也正常；
9. 回归单客户端的列表、上传、下载和下载后播放。

## 19. 常见问题排查

| 现象 | 排查方向 |
| --- | --- |
| 编译找不到 `sys/epoll.h` | 正在 Windows/MinGW 构建，应切换 Ubuntu |
| 启动提示 address already in use | 用 `ss -lntp` 找到旧进程或更换端口 |
| 工具所有连接都超时 | 检查 VM IP、防火墙、8000 监听和网络模式 |
| unknown packet type | 确认客户端和服务端 `av_protocol.h` 同步，并重建旧二进制 |
| invalid packet length | 检查长度头端序、客户端版本或发送数据是否错位 |
| 只有一个连接很快断开 | 根据带 fd 的日志定位该连接，其他连接应保持正常 |
| send queue limit exceeded | 客户端长期不读或响应生产过快，连接按保护策略关闭 |
| 上传断线后 `.part` 未删除 | 检查日志是否触发 disconnect 和 `abortByOwner(fd)` |
| 并发时偶发卡顿 | 当前磁盘 I/O 仍同步，记录文件大小和磁盘延迟供线程池阶段分析 |

## 20. 当前不足

- 单线程 Reactor 不能并行执行协议和文件操作；
- 目录扫描、fstream 打开、seek、读写、flush、stat 和 rename 可能阻塞事件循环；
- 没有连接空闲超时，`lastActivity` 暂只记录；
- 协议使用 packed struct 和主机小端序，没有版本号、request_id 和标准序列化；
- 无认证、TLS、限流、断点、取消、重试和哈希校验；
- 发送队列超限策略是关闭连接，未提供业务级流控提示；
- 自动化工具覆盖 Ping 和列表，上传下载并发仍需人工回归；
- 本地 Windows 环境不能编译 Linux epoll 源码，最终构建与运行需在 Ubuntu 验证。

## 21. 下一阶段断点续传方案

下一阶段若实现断点续传，建议先扩展任务模型而不是改变 epoll 主循环：

1. 为上传和下载定义稳定 task_id，不再依赖 fd 作为持久身份；
2. 保存文件名、总大小、文件版本/hash、临时路径和已完成 offset/range；
3. 增加 RESUME_QUERY/RS，重连后由服务端返回可继续位置；
4. 上传 `.part` 不在普通网络断开时立即删除，改用过期时间和显式取消清理；
5. 下载客户端保留 `.part` 与元数据，恢复前确认远程文件未变化；
6. FINISH 计算 SHA-256 并只在一致后提交；
7. 若同时接入线程池，同一上传任务的 BLOCK 必须串行化，工作结果通过线程安全队列和 eventfd 回投 Reactor；
8. 为任务数量、保存时长和磁盘占用设置上限。

## 22. 面试问答

### Q1：select、poll 和 epoll 有什么区别？

`select` 用固定大小 fd_set，每次调用都要复制和线性扫描，且常见实现受 fd 数量限制。`poll` 用 pollfd 数组，取消固定 fd_set 限制，但每轮仍传入和扫描全部集合。`epoll` 把关注集合保存在内核，通过 `epoll_ctl` 增删，`epoll_wait` 主要返回就绪项，更适合大量连接中只有少数活跃的场景。

### Q2：epoll 为什么适合大量连接？

它不要求应用每轮重新提交全部 fd，也不需要应用线性检查每个未就绪连接。连接数大而活跃比例低时，就绪事件数量通常远小于总连接数。不过 epoll 只优化就绪通知，业务过慢、内存过大或磁盘阻塞仍会限制吞吐。

### Q3：LT 和 ET 有什么区别？

LT 在条件持续满足时会重复通知，容错更高；ET 主要在状态边沿变化时通知，要求 non-blocking 并处理到 EAGAIN。项目选择 LT 降低第一版状态机调试难度，但读写循环仍按到 EAGAIN 的规范实现。

### Q4：为什么 listen 和 client socket 都要设成非阻塞？

epoll 只告诉程序“此刻可能操作”，状态在真正系统调用前仍可能变化。阻塞 socket 可能让事件线程卡在 accept、recv 或 send，从而停掉所有连接。non-blocking 让调用以 EAGAIN 返回，事件循环可以继续服务其他 fd。

### Q5：为什么每个客户端需要独立 receiveBuffer？

每条 TCP 流的到达位置独立。A 可能只收到半个长度头，B 可能一次收到两帧；共享 buffer 无法判断字节属于哪个连接，也无法独立保留半帧。每连接缓冲把网络状态和 fd 生命周期绑定。

### Q6：为什么一次 recv 不一定得到一个完整包？

TCP 是字节流，不保留应用 send 的边界。一个 send 可能被拆成多次 recv，多次 send 也可能一次 recv 读出。应用必须依靠长度头或分隔符恢复消息边界。

### Q7：为什么一次 send 不一定发送完整？

socket 发送缓冲空间有限，non-blocking send 只能接收当前能容纳的字节，可能返回小于请求长度，甚至 EAGAIN。程序必须记录已发送 offset，稍后继续，不能把部分写当成功完成。

### Q8：EPOLLOUT 在什么时候注册和取消？

响应入队后先主动发送。只有 send 返回 EAGAIN 且队列仍有数据时才注册 EPOLLOUT；事件到达后继续发送，队列清空就取消。永久注册会让通常可写的 socket 反复唤醒，造成 busy loop。

### Q9：慢客户端会怎样影响服务端？

非阻塞发送使它不会直接卡住 Reactor，但它的 sendQueue 会积压并占内存。项目按连接隔离队列，并设置 4 MB 上限，超过后只关闭该连接。生产系统还可增加写超时、速率限制和高低水位背压。

### Q10：如何防止发送队列无限增长？

对每个连接统计尚未发送的字节数，响应入队前检查上限。达到上限时停止继续生产并关闭或降级该连接，同时记录 fd、当前排队量和新响应大小。还应限制单包大小和全局连接总内存。

### Q11：客户端断开时如何清理上传任务？

UploadTask 记录 owner_fd。关闭连接时调用 `abortByOwner(fd)`，只删除该 fd 的任务和临时 `.part`，已完成文件与其他连接任务不受影响。BLOCK/FINISH 也校验 owner_fd，防止跨连接操作。

### Q12：epoll 是并发还是并行？

epoll 本身是 I/O 多路复用。当前单线程 Reactor 可以让多个连接并发推进，但一次只有一个线程执行回调，不是多核并行。加入工作线程或多 Reactor 后才可能并行业务处理。

### Q13：单线程 Reactor 的优缺点是什么？

优点是连接状态只被一个线程访问，几乎没有锁、竞态和跨线程销毁问题，上传块自然按事件处理顺序推进。缺点是任何耗时回调都会拖延所有连接，也不能利用多核执行 CPU 或磁盘任务。

### Q14：为什么本阶段暂不加入线程池？

阶段目标是先把 non-blocking 收发、半包状态和连接关闭做正确。线程池会同时引入任务顺序、ConnectionContext 生命周期、响应回投和共享管理器同步问题。先稳定 Reactor，后续性能数据证明需要时再单独接入，更容易定位错误。

### Q15：后续线程池如何接入并避免上传块乱序？

epoll 线程解析完整请求后投递带 connection token 和 task_id 的任务；同一上传任务使用串行执行器、分片序号或每任务队列，不能任由多个 worker 同时写。worker 不直接访问连接对象，而把结果放入线程安全完成队列，再用 eventfd 唤醒 Reactor，由 Reactor 检查连接代次并入发送队列。

### Q16：多客户端下载同一文件为什么不共享文件指针？

共享流的 seek 会改变全局位置，A 请求 offset 0 后 B seek 到 1 MB，A 的下一次 read 就可能读错。当前请求自带 offset，每块独立打开和 seek，状态无共享；未来复用句柄也应按任务隔离或使用 pread。

### Q17：当前 epoll 服务端还可能被什么操作阻塞？

目录扫描、fstream open/seek/read/write/flush、stat、rename 和慢磁盘都在 Reactor 线程同步执行。epoll 解决 socket 等待，不会自动把文件 I/O 变成异步；这些是下一阶段线程池或异步 I/O 的主要候选。

### Q18：如何测试多个客户端并发？

自动化工具用 Barrier 同时启动 5 个连接，每个多次 Ping 并可请求列表，校验所有响应类型和长度。人工测试再覆盖 A 上传、B Ping、C 下载/列表以及强制关闭 A，确认其他连接继续工作且 A 的临时文件被清理。

### Q19：为什么非法包长要关闭连接？

长度是后续内存和帧边界的基础。负数、过小或超过 256 KB 说明客户端版本错误、数据错位或恶意输入；继续解析无法可靠恢复边界。关闭当前 fd 能限制内存风险，同时不影响其他连接。

### Q20：为什么协议暂时保留主机字节序？

阶段 1 至 5 的 Windows 客户端和 Ubuntu 服务端已经按 x86 小端格式稳定通信，本阶段的目标是替换网络执行模型。同步改变端序会扩大回归范围。文档明确记录这个技术债，后续通过协议版本升级统一为网络字节序或标准序列化。
