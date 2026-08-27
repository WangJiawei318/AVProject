# 动态业务线程池

## 1. 实际配置

`EpollServer` 构造 `ThreadPool(core=4, max=8, queue=256, idleTimeout=60s)`。Makefile 使用 C++11 和 `-pthread`。线程池只处理业务与文件 I/O，socket I/O 仍归 Reactor。

## 2. 数据结构

文件：`AVServer/include/ThreadPool.h`

- `m_tasks`：有界 `deque<function<void()>>`。
- `Worker`：id、是否核心、finished 标志、`std::thread`。
- `m_workers`：拥有 worker 对象。
- `m_currentThreadCount`、`m_idleThreadCount`：扩缩容依据。
- `m_mutex + m_condition`：保护队列、worker 状态与等待。

## 3. 启动与 worker 创建

`start()` 校验 core/max/queue/timeout，设置 started，然后在锁内通过 `addWorkerLocked(true)` 创建 4 个核心线程。创建前先增加 current/idle，线程构造失败会回滚计数。

核心 worker 使用 `condition_variable::wait(predicate)` 无限等待；非核心 worker 使用 `wait_for(60s, predicate)`。

## 4. submit 与扩容

`submit()`：

1. `reapFinishedWorkers()` 回收已自然退出的非核心 thread。
2. 在锁内检查 started/stopping 与队列容量。
3. task 入队。
4. 若 `pending > idle` 且 `current < max`，单次增加一个非核心 worker。
5. `notify_one()`。

扩容规则简单、可解释，不观察 CPU、磁盘延迟或历史吞吐，因此不能称为自适应生产调度。

## 5. 消费、空闲计数与缩容

worker 在锁内取队首任务，取到后 `idle--`，解锁执行，结束后再锁住 `idle++`。非核心 worker 等待超时且队列仍空、current 大于 core 时，`markWorkerFinishedLocked()` 减计数并标记 finished，然后线程返回。运行中的任务不会被强杀。

后续 submit 调 `reapFinishedWorkers()`，把 finished worker 移出共享 vector，在锁外 `join()`。stop 则统一 join 所有剩余 worker。

## 6. 有界队列与 server busy

队列达到 256 时 `submit()` 返回 false。`EpollServer::submitBusinessTask()` 清除该连接 in-flight，并让 `ProtocolDispatcher::buildErrorResponse()` 按原请求类型生成 `server busy`。有界队列是背压：系统宁可显式拒绝，也不无限增长内存与延迟。

## 7. Reactor 与 worker 边界

```text
Reactor: packet copy
  -> ThreadPool::submit(lambda)
  -> worker: ProtocolDispatcher::dispatch()
  -> CompletedTask.responses
  -> EpollServer::pushCompletion()
  -> completionQueue + eventfd
  -> Reactor::handleCompletionEvent()
  -> queueResponse()/flushSendQueue()
```

worker 不直接 send，不调 epoll_ctl，不访问 ConnectionContext。这样 socket、发送 offset 与连接生命周期保持单线程所有权。

## 8. 每连接一个任务与上传顺序

`businessTaskInFlight` 让同一连接一次只提交一个业务包。completion 到达后才继续解析该连接缓冲。`UploadManager::writeBlock()` 又要求 offset 等于 receivedSize，并用 mutex 串行保护任务 map。两层约束共同保证分片顺序。

## 9. UploadManager 粗锁

加入 worker 后，上传 map、文件名预留、active owner、receivedSize、清理都可能并发。所有公共入口使用一个 `std::mutex`，状态关系简单，但 64 KB `pwrite + fsync + .task fsync/rename` 期间其他上传也等待。下载和媒体扫描用局部文件句柄，可在其他 worker 并行。

## 10. stop 与异常

`stop()` 设置 stopping、notify_all、把 worker 所有权移到局部 vector，在不持锁时 join。worker 在 stopping 且队列空时退出，因此已排队任务会被处理完。task 异常在 workerLoop 捕获；业务 lambda 还会捕获异常并产生关闭 completion。

## 11. 为什么 epoll 后仍需线程池

epoll 只解决 socket 就绪等待。MySQL、Argon2id、`stat`、文件打开、64 KB 读写、fsync 和元数据 rename 都可能阻塞。放在 Reactor 中会让其他已就绪连接等待；放到有限 worker 可并行推进并保护事件循环响应性。

## 12. 当前方案不足

无优先级、超时/取消、工作窃取、请求 ID、任务指标与动态参数；扩容只看 pending/idle；每连接无法流水线；UploadManager 粗锁；停机可能等待慢磁盘任务。它是边界清楚的学习实现，不是通用生产线程池。

## 13. 面试追问（25 题）

1. **线程池解决什么？** 复用有限线程执行可能阻塞的业务，避免 Reactor 阻塞和每请求建线程。
2. **为什么 core=4/max=8？** 教学配置，不是通用最优值，应按 CPU/磁盘/压测调整。
3. **核心与非核心区别？** 核心长期保留，非核心空闲 60 秒可退出。
4. **为什么任务队列有界？** 提供背压并限制内存与排队延迟。
5. **队列满如何处理？** submit false，返回原协议类型对应 `server busy`。
6. **condition_variable 作用？** 无任务时休眠，入队时唤醒，避免忙等。
7. **为什么 wait 要 predicate？** 处理虚假唤醒并统一检查 stopping/queue。
8. **扩容条件是什么？** 入队后 pending 大于 idle 且 current 小于 max。
9. **为何一次 submit 只扩一个？** 控制创建突发；持续入队会逐步扩容。
10. **缩容条件是什么？** 非核心超时、队列空、current 大于 core。
11. **为何不能强杀 worker？** 可能持锁或处于半写文件状态，破坏一致性。
12. **finished worker 如何回收？** 自标记，后续 submit 锁外 join；stop 统一 join。
13. **为什么 join 在锁外？** 退出线程可能需要同一 mutex，锁内 join 会死锁。
14. **idleCount 如何变化？** 创建 +1、取任务 -1、完成 +1、退出 -1。
15. **计数为何要同一锁？** 扩缩容判断需要一致快照，避免超过 max 或低于 core。
16. **worker 为什么不 send？** send 会短写/EAGAIN，需与 Reactor 的队列和 epoll 状态统一管理。
17. **completionQueue 如何安全？** mutex 保护 push/swap，Reactor 批量消费。
18. **eventfd 会不会一任务一读？** 不必；计数可合并，Reactor 醒来后交换整个 completion 队列。
19. **worker 完成时连接已断开？** connectionId 查找失败，响应丢弃并解除可能的上传绑定。
20. **为什么不能只校验 fd？** fd 可被新连接复用。
21. **每连接一在途的优点？** 顺序清楚、不需 request ID、上传状态机简单。
22. **它的缺点？** 同连接请求无法并行，长业务阻塞后续 Ping/请求。
23. **UploadManager 为什么加锁？** 多 worker 会同时修改任务 map、偏移、所有权和文件名预留。
24. **粗锁瓶颈如何优化？** 可按 transferId 分锁并单独保护全局命名，但生命周期和清理锁序会更复杂。
25. **怎样生产化？** 指标、任务 deadline/cancel、优先级、隔离不同 I/O 池、配置化、压测调参和更细状态机。
