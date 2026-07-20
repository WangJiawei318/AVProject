# 阶段 10：有界动态业务线程池

## 1. 阶段目标

本阶段在 epoll LT 单线程 Reactor 之后加入一个有界动态业务线程池。Reactor 继续独占网络和连接状态，worker 执行媒体目录扫描、上传任务操作、文件读写和业务响应生成，再通过 completion queue 与 eventfd 把结果交回 Reactor。

阶段 1 至 9 的协议和客户端行为保持兼容。本阶段不实现 SHA-256、数据库、用户系统、任务优先级、工作窃取、多任务客户端并行或生产级负载算法。

## 2. 为什么 epoll Reactor 之后还需要线程池

epoll 解决的是“一个线程如何高效等待许多 socket”。它不会让目录扫描、`fsync`、文件读取或元数据 rename 自动变成非阻塞操作。阶段 7 的 Reactor 如果在处理一个上传分片时等待磁盘，其他已经就绪的连接也只能等待。

线程池把可能阻塞的业务和文件 I/O 移出事件线程。这样 Reactor 可以尽快回到 `epoll_wait`，不同连接的业务任务也可以由多个 CPU 线程并行推进。

## 3. Reactor 与工作线程的职责边界

Reactor 负责 listen socket、accept、non-blocking recv/send、长度帧解析、`ConnectionContext`、send queue、`epoll_ctl`、completion queue 消费和 eventfd。

worker 只接收 `connectionId + 日志 fd + 协议包副本`，调用 `ProtocolDispatcher` 和业务 Manager，生成响应包体后投递 completion。worker 不持有连接指针，不读写 socket，也不修改 epoll 关注事件。

## 4. 为什么 recv/send 不放入线程池

socket 已经是非阻塞的，而且 epoll 能准确告诉 Reactor 哪个 fd 可读或可写。若把 recv/send 分散给 worker，需要处理同一 fd 多线程读写、事件重复、部分发送 offset、关闭竞态和锁顺序，反而破坏 Reactor 的单一所有权。

保留“网络状态只在 Reactor 修改”后，连接关闭和发送队列仍是串行状态机，容易证明不会双重发送或 double close。

## 5. 为什么 worker 不能操作 ConnectionContext

客户端可能在 worker 执行期间断开，`ConnectionContext` 随即由 Reactor 销毁。传递裸指针会产生 use-after-free。即使改成共享指针，worker 修改 send queue 仍会与 Reactor 发生数据竞争。

当前任务只携带值类型副本。结果回来后 Reactor 用 `connectionId` 重新查找当前连接；不存在就丢弃结果，因此 worker 不需要延长连接对象寿命。

## 6. 固定线程池与动态线程池的区别

固定线程池启动后线程数不变，行为最简单，但低负载时可能浪费线程，高峰时也不能临时增加并行度。动态线程池保留固定核心线程，并允许任务积压时创建少量非核心线程，空闲后再回收。

本项目的动态范围很小：4 至 8 个 worker。它用于学习扩缩容和线程生命周期，不是根据 CPU、内存和延迟实时调参的生产调度器。

## 7. 核心线程和非核心线程

4 个核心线程由 `start()` 创建，空闲时使用 condition variable 持续等待，不因超时退出。非核心线程只在积压超过空闲 worker 数量时增加，最多使总数达到 8。

核心线程保证稳定的基础处理能力，非核心线程吸收短时突发。两类线程执行相同任务，差别仅在空闲退出策略。

## 8. 动态扩容规则

`submit()` 在 mutex 保护下完成以下操作：检查运行状态和队列容量、把任务加入队列、比较 `pending > idle`，并在 `current < max` 时最多创建一个非核心 worker。

线程计数和 worker 容器都在同一把锁下更新，因此多个连续 submit 不会把总数突破 8。创建线程失败时会回滚 current/idle 计数，任务仍可由已有 worker 处理。

## 9. 空闲缩容规则

非核心 worker 使用 `condition_variable::wait_for(60s)`。连续 60 秒没有任务，并且当前线程数大于核心数时，它在锁内减少 current/idle，设置自己的 `finished` 标志后退出。

核心 worker 不执行超时退出。多个非核心线程同时超时时也会在同一 mutex 下依次检查，因此总线程数不会低于 4。

## 10. 为什么不强制终止运行中线程

C++11 没有安全的通用“杀线程”操作。强制停止可能让 mutex 永久锁住、文件只写一半或任务元数据与 `.part` 不一致。

缩容只作用于空闲 worker；关闭时线程池停止接收新任务，但让已入队任务处理完，再唤醒并 join 全部线程。

## 11. ThreadPool 内部结构

`ThreadPool` 保存核心/最大线程数、队列容量、空闲超时、任务 deque、Worker 对象容器、current/idle 计数、mutex、condition variable 和 started/stopping 状态。

每个 Worker 保存 ID、是否核心、`finished` 标志和对应的 `std::thread`。类禁用复制，析构调用 `stop()`，没有 detach。

## 12. 有界任务队列

任务队列容量固定为 256。达到上限后 `submit()` 返回 false，不继续分配内存。Reactor 清除该连接的 in-flight 标志，并构造与原请求匹配的失败响应，message 为 `server busy`。

媒体列表旧协议没有 result/message 字段，因此忙响应使用文本 payload `server busy`；现有客户端会在远程媒体日志中明确显示无效行。上传和下载使用各自原有 RS 结构返回失败。

## 13. 生产者—消费者模型

Reactor 是任务生产者：解析出业务帧后向队列 submit。worker 是消费者：等待队列、取出一个 `std::function<void()>`、解锁后执行，再回到等待状态。

任务执行期间不持有线程池 mutex，因此其他 worker 可以取任务，Reactor 也可以继续提交。

## 14. mutex 与 condition_variable

mutex 保护 task queue、worker 容器、started/stopping 和线程计数。condition variable 只负责“队列有任务或正在停止”的通知。

worker 使用带谓词的 wait/wait_for，可抵抗虚假唤醒；没有任务时阻塞休眠，不循环轮询消耗 CPU。

## 15. 空闲线程计数

worker 创建时先计为空闲；取到任务时在锁内减一；任务执行完再在锁内加一；退出时再减去自己的空闲计数。

submit 使用该近似即时计数判断积压。计数不是性能监控指标，只为简单扩容决策服务。

## 16. workerCount 计数

`currentThreadCount` 表示尚未退出的线程，而 worker 容器还可能暂时保存“已退出但待 join”的对象。worker 退出时立即在锁内减少 current，因此扩容判断不会被旧 `std::thread` 对象误导。

查询 worker、idle 和 pending 数量时同样加锁，避免读取撕裂或与扩缩容并发冲突。

## 17. 动态 worker 的退出和 thread 对象回收

非核心线程退出前设置 Worker 的 `finished=true`。后续 `submit()` 调用 `reapFinishedWorkers()`：先在锁内把已结束 Worker 移到局部容器，再解锁并 join。

`stop()` 会把剩余 Worker 对象整体移到局部容器并逐个 join。join 不在池 mutex 内执行，worker 也不会调用 stop，因此没有 self-join 或“主线程持锁等待 worker、worker 等同一锁”的死锁。

## 18. 任务队列满时的处理

业务帧已从 receive buffer 取出后，如果 submit 失败，Reactor 立即把 `businessTaskInFlight` 恢复为 false，构造 `server busy` 响应并继续处理连接。连接不会因为拒绝而永久停在 in-flight 状态。

当前没有重试或优先级。客户端可在收到失败后由用户重新操作。

## 19. 每连接一个在途任务

`ConnectionContext::businessTaskInFlight` 由 Reactor 独占。业务任务提交成功后设为 true；即使 socket 后续仍可读，收到的字节只追加到该连接 receive buffer，不再分发后续帧。

completion 到达并确认连接仍存在后，Reactor 清除标志、排队响应，再继续解析缓冲区。不同连接可在不同 worker 并行，同一连接保持严格顺序。

## 20. 如何保证上传分片顺序

AVClient 本身按“发送一块、等待 ACK、再发送下一块”运行。服务端进一步限制同一连接只有一个业务任务在途，因此两个 UPLOAD_BLOCK 不会在不同 worker 同时执行。

UploadManager 还要求 block offset 等于服务端 received size，并用 mutex 保护任务表。客户端、连接调度和业务状态三层共同维持连续分片。

## 21. connectionId 设计

Reactor 为每次 accept 分配从 1 开始单调递增的 `uint64_t connectionId`。`ConnectionContext` 同时保存 fd 和 ID，服务端维护 `fd -> context` 及 `connectionId -> fd` 两个索引。

任务和 completion 使用 connectionId 作为会话身份，fd 只用于日志。上传任务的当前绑定也改为 `activeOwnerConnectionId`，但不会持久化；永久恢复凭证仍是 transfer ID + resume token。

## 22. fd 复用风险

Linux 关闭 fd 后可能很快把同一个数字分配给新连接。若 worker 只带旧 fd，迟到响应可能被错误发送给新客户端。

completion 先按 connectionId 查索引，再确认找到的 context ID 完全相同。旧连接已经删除时直接丢弃，即使 fd 数字已复用也不会命中新会话。

## 23. completionQueue

`CompletedTask` 保存 connectionId、日志 fd、协议类型、响应包体数组、关闭标志和错误字符串。worker 在自己的栈上构造结果，然后在 completion mutex 下移动到全局 queue。

Reactor 收到通知后把整个全局 queue swap 到局部 queue，快速释放锁，再逐个处理。这避免 Reactor 排队响应时阻塞 worker 提交完成结果。

## 24. eventfd 工作流程

服务端只创建一个 `eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)` 并注册到 epoll。每个 worker 入队 completion 后写入 uint64 值 1。

Reactor 读 eventfd 计数直到 EAGAIN，再批量取 completion。eventfd 把普通线程的完成事件转换成 epoll 可观察的 fd 事件，不需要轮询 completion queue，也不为每个任务创建管道。

## 25. 客户端断开后的陈旧结果处理

断开时 Reactor 先删除 connectionId 索引，再关闭 fd 和销毁 context。正在运行的 worker 不被强杀；它完成后照常投递结果。

Reactor 找不到 connectionId 时记录 `completion dropped` 并再次调用上传解绑。这个二次解绑处理了一个细节：上传 INIT 可能在断开回调之后才创建任务，若只在断开瞬间解绑，该任务会错误地保持活动绑定。

## 26. UploadManager 线程安全

UploadManager 的 initialize、create、resume、write block、finish、unbind 和 cleanup 公共入口都由同一个 `std::mutex` 保护。内部辅助函数只在持锁调用链中访问任务 map，不向外返回内部 UploadTask 指针。

`activeOwnerFd` 已替换为 `activeOwnerConnectionId`。连接断开、恢复竞争、过期清理和文件重名选择都在同一串行临界区内完成。

## 27. 粗粒度锁的优缺点

优点是状态关系清楚，容易保证任务 map、`.part`、元数据和最终文件名一致，也减少死锁风险。缺点是一个上传任务执行 64 KB 写入、fsync 和元数据 rename 时，其他上传任务暂时不能进入 UploadManager。

阶段 10 优先正确性和可复述性。未来可按 transfer ID 分片加锁或把 fsync 策略批量化，但需要重新设计任务删除、重名和锁生命周期。

## 28. MediaManager 和 DownloadManager 的并行特性

MediaManager 扫描时使用局部 DIR、stat 和字符串流，没有共享游标。目录可能在扫描期间变化，单次列表允许反映某个近似时刻，但会跳过已消失条目。

DownloadManager 继续按 `filename + offset + request_size` 每请求独立打开文件，不共享文件游标，也不增加服务端下载任务表。多个客户端可以在不同 worker 读取同一正式媒体。

## 29. 线程池停止和析构

SIGINT/SIGTERM 设置停止标志并写 eventfd 唤醒 epoll。Reactor 停止接收新连接后调用 ThreadPool::stop；stop 不再接受 submit，让已入队任务排空，notify_all 并 join 全部核心和非核心 worker。

worker 全部结束后，服务端丢弃关闭期间积累的 completion，再解绑并关闭客户端，最后关闭 eventfd 和 epoll fd。析构再次调用 closeAll 是幂等的。

## 30. 新增文件清单

- `AVServer/include/ThreadPool.h`
- `AVServer/src/ThreadPool.cpp`
- `tools/thread_pool_concurrency_test.py`
- `docs/stage_logs/STAGE10_DYNAMIC_THREAD_POOL.md`

## 31. 修改文件清单

- `AVServer/Makefile`
- `AVServer/include/ConnectionContext.h`
- `AVServer/src/ConnectionContext.cpp`
- `AVServer/include/EpollServer.h`
- `AVServer/src/EpollServer.cpp`
- `AVServer/include/ProtocolDispatcher.h`
- `AVServer/src/ProtocolDispatcher.cpp`
- `AVServer/include/UploadManager.h`
- `AVServer/src/UploadManager.cpp`
- `README.md`
- `docs/ARCHITECTURE.md`
- `docs/PROTOCOL_DESIGN.md`
- `docs/BUILD_AND_RUN.md`
- `docs/TEST_WORKFLOW.md`
- `docs/INTERVIEW_QA.md`

AVClient 和四个原始参考项目未修改。

## 32. 构建步骤

```bash
cd ~/AVProject/AVServer
make clean
make
./AVServer 8000
```

Makefile 增加 `-pthread` 并编译 `src/ThreadPool.cpp`。默认参数固定为 core=4、max=8、queue=256、idle timeout=60s。

## 33. 自动化测试

```bash
cd ~/AVProject
python3 tools/thread_pool_concurrency_test.py \
  192.168.44.130 8000 \
  --clients 10 --requests 20 --business-requests 5
```

可选加入下载块读取：

```bash
python3 tools/thread_pool_concurrency_test.py \
  192.168.44.130 8000 --clients 10 --requests 20 \
  --download-file test.mp4 --download-blocks 4
```

脚本使用 Python 标准库，同时启动多个连接，执行 Ping、媒体列表、可选下载块，并让一个客户端在业务请求后主动断开，最后输出成功、失败和总耗时。

本轮本地已用 MinGW C++11 对 `ThreadPool.cpp` 与 `ProtocolDispatcher.cpp` 做语法检查，并以正式参数 `core=4、max=8、queue=256` 运行独立线程池冒烟测试：确认启动为 4、积压扩至 8、空闲超时回落到 4、第 257 个排队任务被拒绝且 stop 能 join 全部线程。Python 工具已通过语法解析和 `--help` 检查。由于当前 Windows 环境没有 Linux epoll/eventfd 头文件，完整 AVServer 构建和端到端并发场景仍需在 Ubuntu 执行。

## 34. 六类人工测试

1. **基础多客户端**：A 上传，B 连续 Ping，C 列表并下载，三者均正常。
2. **并行文件操作**：A 上传、B 下载、C 扫描列表，日志显示不同 connectionId 在不同 worker 推进。
3. **任务期间断开**：关闭正在执行业务的客户端，确认 completion 被丢弃，其他客户端正常。
4. **续传回归**：分别验证上传/下载的网络断开和客户端重启恢复。
5. **服务端关闭**：有连接和任务时 Ctrl+C，确认任务排空、worker 退出、进程不挂死。
6. **动态扩缩容**：启动为 4，积压时不超过 8，空闲 60 秒后回落为 4，再次请求仍正常。

## 35. 常见问题

### 线程数没有扩到 8

目录和 64 KB 文件操作可能很快，任务未形成积压。增加 `--clients` 和 `--business-requests`，不要在正式业务中加入 sleep。

### 收到 server busy

全局待处理队列达到 256 或服务器正在停止。当前无自动重试，稍后由用户重新发起请求。

### completion dropped 是否是错误

如果客户端主动断开，这是预期日志，说明 connectionId 防护生效。若正常连接频繁出现，则检查连接超时、网络和关闭原因。

### 上传并行度不明显

UploadManager 使用粗粒度锁，fsync 期间其他上传会等待；下载和媒体扫描仍可在其他 worker 并行。

### Ctrl+C 后退出需要一点时间

停止策略会处理完已入队任务，不强杀正在进行的文件操作。任务完成后所有 thread 才 join。

## 36. 当前不足

- UploadManager 粗粒度锁限制多个上传任务并行度；
- 业务任务没有优先级、超时、取消和 request ID；
- 每连接一个业务 in-flight 限制了同连接流水线；
- 文件 I/O 仍是同步系统调用，只是移到 worker；
- 没有 SHA-256、TLS、用户鉴权、数据库和配额；
- 动态扩缩容只依据 pending 与 idle，不看 CPU、磁盘或延迟；
- 线程池和 completion 指标只有基础日志，没有生产监控；
- 协议仍使用 packed struct 和主机字节序。

## 37. 为什么不继续实现更复杂调度

项目目标是形成可运行、可解释的 C/S 音视频闭环。当前线程池已经覆盖有界队列、生产者消费者、动态 worker、eventfd 跨线程唤醒、连接生命周期和共享任务加锁等核心问题。

继续加入优先级、工作窃取、多 Reactor、无锁队列或自适应控制会显著扩大验证面，却不直接改善当前学习闭环。后续更适合进入统一调用链复习和面试准备。

## 38. 面试问答

### Q1：进程与线程有什么区别？

进程拥有独立虚拟地址空间和系统资源边界；同一进程内的线程共享代码、堆和文件描述符，但各自有栈、寄存器和调度状态。线程通信便宜，但共享内存也带来数据竞争、锁和生命周期问题。

### Q2：为什么用了 epoll 还需要线程池？

epoll 只让一个线程高效等待很多 socket，不会消除磁盘 I/O 和业务计算阻塞。线程池让 Reactor 持续处理网络就绪事件，把目录扫描、fsync 和文件读取放到 worker 并行执行。

### Q3：Reactor 线程负责什么？

它独占 accept、recv、send、epoll_ctl、连接创建销毁、长度帧解析和发送队列。它还消费 completion，并在确认 connectionId 有效后恢复该连接的业务分发。

### Q4：worker 线程负责什么？

worker 接收协议包副本，执行 Dispatcher、Manager 和文件 I/O，生成响应包体。它不碰 socket、epoll 或 ConnectionContext，完成后只向线程安全队列投递结果并写 eventfd。

### Q5：为什么不能把 recv/send 全放到线程池？

同一 socket 的可读写状态和部分发送 offset 是连续状态。分散给 worker 会引入同 fd 多线程读写、关闭竞态和复杂锁。non-blocking I/O 本来就适合由 Reactor 串行管理。

### Q6：什么是生产者—消费者模型？

生产者生成任务放入共享队列，消费者从队列取任务执行。这里 Reactor 是单一生产者之一，多个 worker 是消费者；mutex 保护队列，condition variable 负责有任务时唤醒。

### Q7：condition_variable 如何避免忙等？

队列为空时 worker 在条件变量上睡眠，线程不占用 CPU。submit 入队后 notify；worker 被唤醒并在持锁状态重新检查谓词，避免虚假唤醒导致错误取队列。

### Q8：为什么任务队列要加锁？

Reactor 会 push，多名 worker 会 pop，动态线程也会查询大小。`std::deque` 本身不保证并发安全，不加锁可能破坏内部结构或让同一任务被重复取走。

### Q9：为什么队列必须有上限？

无界队列在业务速度低于到达速度时会持续占用内存，并把超时放大。上限提供背压：达到 256 后立即返回 server busy，使故障可见且资源可控。

### Q10：什么是核心线程？

核心线程是服务启动时创建并长期保留的基础 worker。即使空闲也不因超时退出，本项目固定为 4 个。

### Q11：什么是非核心线程？

非核心线程是任务积压时临时创建的 worker，执行能力与核心线程相同，但连续空闲 60 秒后允许退出。它们使总线程数最多从 4 扩到 8。

### Q12：动态线程池如何扩容？

submit 入队后在锁内比较 pending task 与 idle worker。如果 pending 更多且 current 小于 max，就创建一个非核心线程；单次 submit 最多增加一个。

### Q13：动态线程池如何缩容？

非核心线程 wait_for 超时后，在锁内确认队列仍空且 current 大于 core，减少计数、标记 finished 并自然返回。运行中的线程不会被缩容。

### Q14：为什么不能强制杀死线程？

线程可能正持锁、写文件或更新两份状态。强杀会留下死锁和半提交数据。正确做法是停止接收新任务、让当前任务到达安全边界，再 join。

### Q15：如何防止扩容超过最大值？

检查 current 与创建线程都放在同一 mutex 临界区。每次 submit 只创建一个，创建前要求 current < max，成功前先占用计数，失败则回滚。

### Q16：如何保证缩容不低于核心数？

只有标记为非核心的 worker 能超时退出，而且退出检查和 current 减一在锁内连续完成。current 等于 core 时，其他非核心不会再走超时退出分支。

### Q17：空闲线程数如何维护？

创建时加一，取任务时减一，任务完成时加一，退出时再减一。所有修改都受线程池 mutex 保护，submit 用它做简单积压判断。

### Q18：动态退出的 std::thread 如何回收？

线程自然退出前设置 finished。后续 submit 把已完成 Worker 移出共享容器，解锁后 join；stop 则统一 join 剩余对象。线程不能 join 自己，当前 stop 只由 Reactor/拥有者线程调用。

### Q19：eventfd 是什么？

eventfd 是 Linux 的 64 位计数通知 fd。写入会增加计数并使 fd 可读，读取获得并清零计数；它可设置 non-blocking 并加入 epoll。

### Q20：为什么 eventfd 适合唤醒 Reactor？

worker 完成是内存事件，而 epoll 等待 fd。eventfd 把两者连接起来：worker 写一个整数，Reactor 从 epoll 醒来后批量消费 completion，不需要轮询或每任务创建管道。

### Q21：为什么 worker 不直接 send？

send 可能部分成功或 EAGAIN，需要修改每连接 send offset 和 EPOLLOUT。若 worker 直接发送，会和 Reactor 的队列并发。统一交回 Reactor可保持 socket 单线程所有权。

### Q22：fd 为什么会复用？

fd 只是进程文件描述符表中的小整数。关闭后该槽位可立即分配给新 socket，所以旧任务保存的数字不能证明还是同一连接。

### Q23：connectionId 如何避免陈旧响应？

每次 accept 生成新的 64 位单调 ID。completion 按 ID 查当前映射并再次核对 context；旧连接消失后，即使 fd 被新连接复用，ID 也不同，响应会被丢弃。

### Q24：如何保证上传分片顺序？

每连接最多一个业务任务 in-flight，后续帧留在 receive buffer；完成后才继续分发。UploadManager 还检查 offset 必须等于 received size，并用 mutex 串行修改任务。

### Q25：每连接一个在途任务有什么不足？

它不能在同一连接内流水线多个独立请求，高 RTT 或大量小请求时吞吐有限。优点是无需 request ID 和依赖图，上传顺序及错误处理非常清楚，适合当前阶段。

### Q26：UploadManager 为什么需要 mutex？

加入多个 worker 后，任务 map、文件重名预留、active owner、received size 和过期清理都可能并发访问。不加锁会造成重复命名、偏移覆盖或迭代器失效。

### Q27：粗粒度锁的优缺点是什么？

优点是临界区边界简单、状态一致性强、锁顺序少。缺点是某个上传 fsync 时其他上传也等待，降低并行度。当前项目选择正确性优先。

### Q28：客户端断开后 worker 任务怎么办？

不强杀 worker。任务可完成文件操作并投递 completion；Reactor 查不到 connectionId 就丢弃响应，并再次解绑可能迟创建的上传任务。已可靠写入的分片由下次续传返回权威 offset。

### Q29：如何安全关闭线程池？

先停止 submit，设置 stopping 并 notify_all。worker 继续取完队列；队列空时自然退出。拥有者线程在不持池 mutex 的情况下 join 每个 thread，最后清理队列和计数。

### Q30：动态线程数应该如何配置？

CPU 密集任务通常接近核心数；包含阻塞磁盘 I/O 时可略高，但过多线程会增加上下文切换和磁盘争用。本项目固定 4 到 8，需要用真实文件、磁盘和并发压测再调，不声称是通用最优值。

### Q31：当前线程池方案还有哪些不足？

它没有任务优先级、超时取消、工作窃取、请求追踪和生产监控；扩容只看队列与空闲数；UploadManager 还是粗锁；每连接只有一个业务任务。它是清晰可运行的教学实现，不是生产级调度框架。
