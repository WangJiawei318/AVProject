# 上传下载与断点续传

## 1. 普通上传

```text
RemoteMediaPage::slotUploadClicked
 -> sendUploadInit
 <- transferId/token/offset=0
 -> 保存 UploadTaskStore
 -> sendNextUploadBlock（64 KB，串行等确认）
 <- receivedOffset
 -> 更新 confirmedOffset 与 JSON
 -> sendUploadFinish
 <- saved file name
 -> 删除本地状态、刷新媒体列表
```

服务端 `createUpload()` 创建唯一 `.part` 与 `.task`，`writeBlock()` 以 `pwrite` 写严格 offset、fsync 并原子更新元数据，`finishUpload()` 核对三份大小后 rename 到 `media/`。

## 2. 上传续传

客户端 `UploadTaskStore` 保存本地路径、大小、mtime、服务器地址、transferId、resumeToken、confirmedOffset、状态。重启后加载；恢复前检查文件仍存在且大小/mtime 未变、服务器地址匹配。

`UPLOAD_RESUME_RQ` 同时提交 transferId、token、文件名和大小。服务端：

1. 查内存中由 `.task` 恢复的任务。
2. 常量时间风格比较 token。
3. 校验文件名、expectedSize、uploading、72 小时有效期。
4. 拒绝其他活动 connectionId。
5. `reconcileTask()` 取 `min(expectedSize, metadata receivedSize, actual part size)`，必要时截断并持久化。
6. 绑定新 `activeOwnerConnectionId`，返回权威 resumeOffset。

fd 不能做永久身份，因为重连/重启后会变化且可复用。`transferId + resumeToken` 是持久任务凭证，`activeOwnerConnectionId` 只是当前会话排他绑定。

## 3. 重复块与跳跃块

- `offset == receivedSize`：正常写入。
- `offset < receivedSize` 且整块已确认：不重复写，返回成功及当前偏移，使丢失响应可幂等恢复。
- 旧块只部分落在已确认区：offset mismatch。
- `offset > receivedSize`：拒绝，避免文件空洞和乱序。

每块成功的含义是数据写完、`.part` fsync、实际大小核对且 `.task` 原子更新完成。频繁 fsync 可靠但吞吐较低。

## 4. 服务端上传持久化

`temp/tasks/<transferId>.task` 是键值文本，字段含 ID、token、原始/最终文件名、期望/已收大小、临时路径、创建/更新时间、status。`persistTask()` 写 `.tmp`、fsync 后 rename；启动时扫描 `.task`，校验路径和字段，并报告无元数据的孤立 `.part`。

断开只 `unbindConnection()`，不删除数据。无 active owner 且 72 小时未更新的 uploading 任务由启动扫描或 5 分钟维护任务删除。

## 5. 普通下载

```text
startDownload(playAfter)
 -> DOWNLOAD_INIT_RQ(offset=0, expected=0)
 <- size/mtime/accepted=0
 -> 创建 cache/name.part 与 DownloadTaskStore
 -> DOWNLOAD_BLOCK_RQ(offset, 64KB)
 <- header + bytes
 -> 写入/flush，更新 confirmedOffset JSON
 -> DOWNLOAD_FINISH_RQ
 <- success
 -> 核对 part 大小、rename 正式文件、删除状态
 -> 可选 emit requestPlayLocalFile
```

## 6. 下载续传与 safe_offset

服务端下载保持无状态，每个 block 都是 `filename + offset + requestSize` 独立读取。客户端才保存 `.download.json` 和 `.part`。

恢复前 `prepareSafeDownloadOffset()` 计算：

```text
safeOffset = min(state.confirmedOffset, actualPartSize)
```

若 part 更大，截断到 confirmedOffset；若更小，降低状态偏移并原子保存。客户端发送 safeOffset、上次 fileSize 和 mtime；服务端当前大小/mtime都相同才返回 acceptedOffset，否则 `remote file changed`，绝不静默拼接新旧内容。

服务端重启不影响下载恢复，因为它没有 download task、download ID 或 token；正式媒体仍在即可从任意合法 offset 读。

## 7. 为什么下载不需要 resume token

阶段 11 后媒体列表、上传和下载都要求登录。下载使用 mediaId 定位 published 媒体，仍保持服务端无状态；上传恢复除 token 和活动会话绑定外，还校验持久化 ownerUserId，防止跨用户操作。

## 8. 放弃与完成

- 放弃上传：只删除客户端 `.upload.json`，服务端 `.part/.task` 等 72 小时清理。
- 放弃下载：删除客户端 `.download.json` 与 `.part`；服务端无状态，无需取消协议。
- 下载完成：关闭文件，确认 `.part` 大小，处理已有 final 后 rename，删除状态；playAfter 为 true 时通过 MainWindow 播放。

## 9. 一致性边界

上传没有计算源文件哈希；客户端用 size+mtime 防止多数误恢复，服务端用 ID/token/filename/size/actual part offset。下载用远程 size+mtime 判断文件是否变化。内容若变化但大小和 mtime 恰好相同，当前无法识别；FINISH 也只校验大小。后续 SHA-256 应在 worker 中计算，不能长时间占用 Reactor。

## 10. 上传与下载续传对比

| 维度 | 上传 | 下载 |
|---|---|---|
| 服务端状态 | `.task + .part + map` | 无任务状态 |
| 身份 | transferId + token | filename + 连接未来鉴权 |
| 会话竞争 | activeOwnerConnectionId | 客户端限制单活动下载 |
| 权威偏移 | 服务端 actual `.part` | 客户端 safeOffset，经服务端接受 |
| 服务端重启 | 扫描任务恢复 | 无需恢复任务 |
| 文件变化检查 | 客户端源 size/mtime | 服务端正式文件 size/mtime |

## 11. 面试追问（20 题）

1. 断点续传是什么？保留已可靠完成偏移，中断后从确认点继续而非重传全部。
2. 上传偏移谁决定？服务端 `.part` 与元数据协调后的安全值。
3. 为什么不信客户端上传偏移？客户端可能状态滞后、损坏或伪造，服务端才知道可靠落盘字节。
4. transferId 作用？全局定位一个上传任务。
5. resumeToken 作用？在无用户系统时证明持有恢复凭证，防止只猜 ID 操作任务。
6. 为什么还校验文件名和大小？降低凭证/状态错配导致错误恢复。
7. activeOwnerConnectionId 作用？防止两个当前连接同时写同一任务。
8. fd 为什么不够？重连变化、关闭后复用、服务端重启丢失。
9. `.part` 作用？隔离未完成文件，FINISH 前不进入正式媒体库。
10. `.task` 作用？服务端重启后恢复身份、大小、偏移和文件路径。
11. 元数据为何原子 rename？减少崩溃时留下半写文本。
12. 元数据与 part 不一致怎么办？取三者较小安全值，必要时截断并记录修正。
13. 为什么每块 fsync？让确认偏移更接近可靠落盘边界；代价是磁盘 I/O 大。
14. 重复块为何可成功？处理“服务端写成功但响应丢失”的幂等重试。
15. 跳跃块为何拒绝？避免空洞、乱序和更复杂的分片位图。
16. 下载 safeOffset 为什么取 min？状态与文件任一方可能滞后，只能信双方共同拥有的前缀。
17. acceptedOffset 作用？服务端明确同意本次恢复起点，客户端再 seek/请求。
18. 远程文件变化为何拒绝而不归零？静默归零可能把新数据拼进旧 part，造成看似成功的损坏文件。
19. 服务端重启为何仍可续下载？请求是无状态随机读取，正式媒体未变即可。
20. 如何加 SHA-256？初始化保存源摘要/最终服务端 worker 计算，完成前比较；大文件哈希应可取消并有进度。
