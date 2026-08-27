# 阶段 11：MySQL 用户认证与媒体归属

## 1. 阶段目标与范围

本阶段在阶段 10 的 epoll LT Reactor、有界动态线程池、completionQueue 和 eventfd 基础上，加入最小用户业务闭环：注册、真实登录、连接身份、媒体 owner、PUBLIC/MINE 列表和按 mediaId 下载。

边界保持克制：不做 upload_tasks 表、JWT、长期 session、TLS、管理员 UI、角色、配额、私有媒体、SHA-256、Redis 或 ORM。视频文件仍在磁盘，上传恢复任务仍是 `temp/tasks/*.task`。

## 2. 为什么现在引入 MySQL

阶段 1 至 10 的公共媒体目录不需要业务归属，扫描 `media/` 足够。阶段 11 出现用户名唯一性、账号状态、密码哈希、owner 外键、PUBLIC/MINE 和稳定 mediaId，关系数据库能用唯一索引、外键和 Prepared Statement 清晰表达这些约束。

MySQL 是业务元数据源，不是视频存储。大视频继续由文件系统顺序读写，避免 BLOB 导致数据库备份膨胀、复制压力和文件传输路径重写。

## 3. 数据模型

### 3.1 users 表

`users(id, username, password_hash, status, created_at)`。用户名有唯一索引；`status=1` 正常、`status=0` 禁用。数据库只保存 libsodium 生成的 Argon2id 完整字符串，不保存原始密码或单独明文盐。

### 3.2 media 表

`media(id, owner_user_id, original_name, stored_name, storage_path, extension, file_size, file_hash, status, created_at, updated_at)`。owner 外键指向 users；stored name 有唯一索引；`file_hash` 本阶段保持 NULL。

`original_name` 用于 UI，允许不同用户上传同名文件；`stored_name` 使用 `<transferId>.<extension>`，用于服务器磁盘真实文件名；客户端只使用 mediaId，不接触 storage path。

## 4. 数据库部署与最小权限

MySQL 与 AVServer 位于同一 Ubuntu，应用连接 `127.0.0.1:3306`。`sql/000_create_database.sql.example` 创建 `avproject` 和专用 `avapp@127.0.0.1`，只授予 SELECT、INSERT、UPDATE。真实密码只写入被忽略的 `config/db.conf`，不写入源码、Makefile 或文档。

`sql/001_init_auth_media.sql` 创建 users/media。阶段前遗留在 `media/` 但没有数据库记录的文件视为 legacy media，不自动分配给任何用户，也不会出现在数据库列表中。

## 5. DatabaseConnectionPool

连接池启动时读取 `config/db.conf` 并建立固定数量连接，默认 4。内部使用 mutex、condition variable、空闲连接队列和固定 slot；`ConnectionLease` 负责 RAII 归还。worker 获取连接最多等待 2500 ms，超时返回 `database temporarily unavailable`，不会永久阻塞。

借出前执行 `mysql_ping`；连接失效时关闭并尝试重新建立。每个 worker 线程初始化 MySQL thread-local 状态。池固定为 4 而动态 worker 最大为 8，是为了限制数据库并发；不是每个 worker 都必须拥有一个永久连接。

不能让所有 worker 共用同一个 `MYSQL*`，因为连接包含当前协议状态、结果集和错误状态；并发调用会竞态。也不应每次请求新建连接，因为 TCP/MySQL 鉴权成本和数据库连接压力都不必要。

## 6. Prepared Statement 与 SQL 注入

查询用户名、插入用户、登录查询、插入 media、PUBLIC/MINE 列表和按 mediaId 查询都使用：

```text
mysql_stmt_init
mysql_stmt_prepare
mysql_stmt_bind_param
mysql_stmt_execute
mysql_stmt_bind_result
```

SQL 模板与用户参数分开，`' OR 1=1 --` 只会作为普通字符串参数。Prepared Statement 降低注入风险，但仍配合字节长度、包大小、文件名和数值范围校验。日志只返回稳定业务错误，不回显内部 SQL、数据库密码或 password hash。

## 7. Argon2id、Salt 与 TLS

注册调用 `crypto_pwhash_str`，登录调用 `crypto_pwhash_str_verify`。libsodium 的完整字符串包含算法、成本参数、随机盐和哈希。随机盐不需要保密，它让相同密码产生不同结果；原始密码才必须保密。

Argon2id 解决数据库泄露后的离线破解风险，TLS 解决网络链路监听和篡改风险。当前协议没有 TLS，REGISTER/LOGIN 会在明文 TCP 中传送原始密码，只适用于本机、局域网和受限测试环境。客户端先做 MD5 不能替代 TLS，因为固定摘要会成为可重放凭据。

## 8. 注册流程

```text
REGISTER_RQ(username,password)
 -> Reactor 完成长度帧解析
 -> 提交 worker
 -> 借 DB 连接并查询用户名
 -> Argon2id hash
 -> Prepared INSERT users
 -> REGISTER_RS(result,errorCode,userId,message)
 -> completionQueue/eventfd
 -> Reactor sendQueue
```

用户名限制 3 至 32 UTF-8 字节，密码限制 6 至 64 字节。重复用户名由预查询和数据库唯一索引双重处理，稳定返回 USERNAME_EXISTS。注册成功不会自动登录。

## 9. 登录流程与 Reactor 回投

LOGIN 在 worker 中查询 userId、password_hash、status，执行 Argon2id verify。错误用户名和错误密码统一返回 INVALID_USERNAME_OR_PASSWORD，减少用户名枚举信息；正确密码但 status 非 1 返回 USER_DISABLED。

worker 不能持有 `ConnectionContext*`。成功结果写入 CompletedTask 的 `hasAuthUpdate/authSuccess/authenticatedUserId/authenticatedUsername`。Reactor 被 eventfd 唤醒后，先用 connectionId 验证连接仍存在并排队响应，再更新 ConnectionContext。若连接已断开或 fd 已复用，结果直接丢弃。

## 10. ConnectionContext 认证状态

每个新连接初始化：

```text
authenticated=false
userId=0
username=""
```

登录成功后只有 Reactor 修改这些字段。连接销毁时身份自然销毁；重连必须重新登录，没有 session token。AVNetworkClient 的 userId 仅用于 UI 显示，服务端不会从普通媒体包读取客户端自报 userId。

## 11. 访问控制

PING、REGISTER、LOGIN 允许未登录。MEDIA_LIST、UPLOAD_INIT/RESUME/BLOCK/FINISH 和 DOWNLOAD_INIT/BLOCK/FINISH 都在 Dispatcher 入口检查认证状态；未登录直接构造对应协议的 AUTH_REQUIRED 响应，不调用数据库或文件业务。

## 12. ownerUserId 与 activeOwnerConnectionId

上传任务新增 `owner_user_id` 并写入 `.task`：

- ownerUserId：永久业务所有者，来自可信 ConnectionContext.userId；
- activeOwnerConnectionId：当前活动 TCP 会话的排他绑定，只在内存中存在。

RESUME 同时校验 transferId、恒定时间比较的 resumeToken、ownerUserId、文件名和大小。BLOCK/FINISH 也校验 owner 与当前连接绑定。用户重连后 connectionId 改变，但重新登录得到相同 userId，因而可以恢复自己的任务；其他用户即使拿到 ID 和 token 也会收到 PERMISSION_DENIED。

旧 `.task` 缺少 owner 字段时按 owner 0 加载并记录 `legacy upload task has no ownerUserId`，认证模式下拒绝恢复，绝不偷偷绑定给当前用户。

## 13. `.task` 为什么不迁移 MySQL

`.task` 与 `.part` 是同一恢复状态的两个磁盘侧证据，现有原子小文件更新已经支持 AVServer 重启恢复。本阶段 MySQL 只负责正式用户和已发布媒体，把上传状态同时迁移会引入状态机、事务补偿和迁移兼容，超出最小闭环。

## 14. 上传完成与一致性策略

FINISH 校验任务 owner、连接绑定、声明大小、累计 offset 和 `.part` 实际大小。成功后：

1. `.part` rename 为 `media/<transferId>.<extension>`；
2. 用 Prepared Statement 插入 media；
3. 插入成功后删除 `.task` 并从内存移除任务；
4. 返回 original name 和 mediaId。

若数据库插入失败，不向客户端返回成功，并尝试把正式文件 rename 回 `.part`，保留任务供稍后重新 FINISH。这是简单补偿，不是文件系统与 MySQL 的真正跨资源事务。极端情况下若回滚 rename 也失败，服务端记录 critical 日志，需要人工检查。

为避免 MySQL 等待间接阻塞 Reactor 的断线解绑，FINISH 在锁内完成校验、rename 并把内存任务标记为 `publishing`，随后释放 UploadManager 锁再执行数据库 INSERT。publishing 状态会拒绝并发 RESUME/BLOCK；回调结束后重新加锁完成删除或回滚状态。该状态不写入 `.task`，因此进程恰好在 rename 与 INSERT 之间崩溃时仍可能需要人工检查，这是简单补偿方案的边界。

## 15. PUBLIC、MINE 与 mediaId 下载

MediaRepository 用 Prepared Statement 查询：

- PUBLIC：所有 `status='published'`；
- MINE：published 且 `owner_user_id=currentUserId`。

响应文本字段为 mediaId、ownerUserId、ownerName、originalName、fileSize、extension、createdAt、status。文件名禁止协议分隔符和控制字符，输出还做基础字段清理。

DOWNLOAD 请求改携带 mediaId。每次 INIT/BLOCK/FINISH 都查询 published media，验证数据库 storage path 与 `media/<storedName>` 一致，再把内部 stored name 交给 DownloadManager。客户端不能发送服务器路径。

下载仍保持服务端无状态。客户端 DownloadTaskStore 新增 mediaId，继续保存 server、size、mtime、confirmed offset、`.part` 和 play-after-download。阶段 9 的旧下载状态没有 mediaId，加载时被标记为不完整，避免按同名文件错误恢复。

## 16. 新增文件

- `AVServer/sql/000_create_database.sql.example`
- `AVServer/sql/001_init_auth_media.sql`
- `AVServer/config/db.conf.example`
- `AVServer/include/DatabaseConnectionPool.h`
- `AVServer/src/DatabaseConnectionPool.cpp`
- `AVServer/include/AuthService.h`
- `AVServer/src/AuthService.cpp`
- `AVServer/include/MediaRepository.h`
- `AVServer/src/MediaRepository.cpp`
- `AVClient/pages/AuthPage.h`
- `AVClient/pages/AuthPage.cpp`
- `tools/auth_media_test.py`
- `docs/stage_logs/STAGE11_MYSQL_AUTH_MEDIA_OWNERSHIP.md`

## 17. 主要修改文件

- 协议：`AVServer/include/av_protocol.h`、客户端转发头、AVNetworkClient。
- Reactor：ConnectionContext、EpollServer、ProtocolDispatcher。
- 上传：UploadManager 及 `.task` owner 字段。
- 客户端：MainWindow、RemoteMediaPage、DownloadTaskStore、AVClient.pro。
- 构建：AVServer/Makefile、`.gitignore`。
- 文档：README、架构、协议、构建、测试和面试文档。

播放器、录制器和四个原始参考项目未修改。

## 18. 构建与数据库初始化

```bash
sudo apt update
sudo apt install build-essential default-libmysqlclient-dev libsodium-dev mysql-server
cd ~/AVProject/AVServer
# 先编辑示例中的 CHANGE_ME
sudo mysql < sql/000_create_database.sql.example
mysql -u avapp -p avproject < sql/001_init_auth_media.sql
cp config/db.conf.example config/db.conf
chmod 600 config/db.conf
make clean && make
./AVServer 8000
```

Windows：

```powershell
cd D:\colin\project\AVProject\AVClient\build-debug
D:\Software\Qt\5.12.11\mingw73_32\bin\qmake.exe ..\AVClient.pro -spec win32-g++ "CONFIG+=debug"
D:\Software\Qt\Tools\mingw730_32\bin\mingw32-make.exe -j4
```

## 19. 自动化测试

```bash
cd ~/AVProject
python3 tools/auth_media_test.py --host 127.0.0.1 --port 8000
```

脚本只用标准库，覆盖未登录列表、userA 注册、重复注册、错误密码、SQL 注入式用户名、正确登录、PUBLIC/MINE，以及 userB 注册登录。完整上传、下载和续传使用 AVClient 人工测试。

## 20. 人工验收

1. 数据库、服务端和客户端 clean build。
2. 注册、重复注册、错误密码、正确登录、禁用用户。
3. 断线重连后必须重新登录；未登录媒体协议返回 AUTH_REQUIRED。
4. userA/userB 分别上传，PUBLIC 都可见，MINE 只见自己。
5. userA 的 `.task` 包含 owner；userB 持同一 token 仍不能恢复。
6. 普通上传、上传续传、普通下载、下载续传、下载后播放回归。
7. MySQL 停止时数据库业务有限失败，另一连接 Ping 仍响应。
8. 多客户端和动态 worker 扩缩容不回退。
9. 本地播放和录制不回退。
10. 日志和数据库中无明文密码、完整 token 或配置密码。

## 21. 当前不足与下一步边界

- 没有 TLS，不能直接公网使用账号协议；
- 没有长期 session/JWT/自动登录；
- 没有登录限速、审计、角色、配额和私有媒体；
- `.task` 仍是文件，不在 MySQL；
- 没有 SHA-256，size/mtime 不是强内容证明；
- Prepared Statement 每次创建，没有 statement cache；
- 上传发布时使用简单 rename 补偿，不是分布式事务；
- 列表 UI 没有复杂分页和搜索；
- 每个下载块查询一次 media，清晰但不是吞吐最优。

下一阶段若继续工程化，优先应是 TLS 和认证限速，而不是立即迁移 upload_tasks。上传任务当前已经能可靠跨进程恢复，迁移会引入双写、状态机和清理一致性，收益不如先解决明文密码链路。

## 22. 常见问题排查

- 启动提示 config missing：从 `AVServer/` 启动并创建真实 `config/db.conf`。
- 连接 MySQL 失败：检查 mysql 服务、127.0.0.1 应用账号、密码和授权。
- 编译找不到 mysql.h：安装 `default-libmysqlclient-dev` 并检查 `mysql_config`。
- 编译找不到 sodium.h：安装 `libsodium-dev`。
- 登录一直失败：检查用户 status 和 hash 列，不要手工写普通字符串 hash。
- media 目录有文件但列表为空：无数据库记录的 legacy 文件不会显示。
- 旧上传任务不能恢复：owner=0 的 legacy 任务按设计拒绝。
- 旧下载任务不显示：阶段 9 JSON 没有 mediaId，不能安全迁移。
- 上传 FINISH 数据库失败：检查 `.part` 是否已回滚以及 critical 日志。
- Qt 页面收不到响应：重新运行 qmake，确保 moc 使用新 signal/slot 签名。

# 面试问答

## Q1：为什么项目需要数据库？

因为阶段 11 不再只是列目录，而要保证用户名唯一、密码哈希、账号状态、媒体 owner 和稳定 mediaId。MySQL 擅长持久化结构关系和条件查询；文件系统继续保存大视频。

## Q2：为什么不把视频存 MySQL？

大视频 BLOB 会放大数据库备份、复制和随机读成本，也会迫使现有文件传输重写。数据库保存元数据和路径，文件系统负责大块顺序 I/O，更符合职责分工。

## Q3：为什么只设计 users/media 两张表？

它们刚好覆盖最小认证和媒体归属。上传恢复已有 `.task`，本阶段不需要 upload_tasks、session、角色或分享表，避免扩大一致性问题。

## Q4：用户名唯一索引有什么作用？

应用层预查询提升提示体验，唯一索引则是最终并发约束。两个 worker 同时注册同名用户时，数据库仍只允许一个 INSERT 成功。

## Q5：密码为什么不能明文存储？

数据库备份、日志或账号一旦泄露，明文会立即暴露用户密码并危及其他站点。密码应保存不可逆、带盐、计算昂贵的密码哈希。

## Q6：MD5 为什么不适合密码？

MD5 计算太快且已不具备碰撞安全性，GPU 可以高速穷举。即使加盐，也缺少合适的内存成本。Argon2id 专为密码哈希设计。

## Q7：Argon2id 是什么？

它是内存困难型密码哈希，综合抵抗 GPU 并行破解和部分侧信道风险。libsodium 提供经过审查的参数和实现，项目不自研密码算法。

## Q8：Salt 的作用是什么？

随机盐让相同密码得到不同哈希，阻止预计算彩虹表和一份计算批量攻击所有用户。盐可以公开，libsodium 已把它编码进 hash 字符串。

## Q9：为什么一个 hash 字符串就够？

`crypto_pwhash_str` 输出包含算法版本、成本参数、盐和哈希。verify 能从字符串解析所需信息，因此无需额外盐列，也降低自定义格式出错概率。

## Q10：密码哈希与 TLS 分别解决什么？

哈希保护数据库落盘后的密码，TLS 保护客户端到服务端链路。当前只有前者，所以不能宣称公网登录安全。

## Q11：什么是 SQL 注入？

把用户输入直接拼入 SQL 后，攻击者可能注入引号、条件或注释改变语句结构，例如绕过登录条件。根因是代码和数据没有分离。

## Q12：Prepared Statement 为什么有效？

SQL 模板先解析，用户值后绑定为有类型和长度的数据。参数内容不会再被解释为 SQL 关键字，因此注入式用户名只是普通字符串。

## Q13：为什么登录不能放 Reactor？

MySQL 可能等待网络或池连接，Argon2id 也故意较慢。放在 Reactor 会阻塞所有连接网络事件；放入 worker 后 Ping 和其他 socket 仍可处理。

## Q14：worker 为什么不能改 ConnectionContext？

连接可能在 worker 执行中断开，Context 会销毁，fd 还可能复用。worker 只产出值，Reactor 按 connectionId 验证后更新，避免悬空指针和身份串线。

## Q15：认证结果如何回到 Reactor？

worker 将 LOGIN_RS 和 auth update 写入 CompletedTask，压入 completionQueue，再写 eventfd。Reactor epoll 被唤醒，验证 connectionId，排队响应并更新 authenticated/userId/username。

## Q16：connectionId 在认证中解决什么？

fd 只是可复用槽位，旧任务完成时同一个数字可能已属于新连接。单调 connectionId 标记连接生命周期，旧结果找不到原 ID 就被丢弃。

## Q17：为什么客户端 userId 不可信？

客户端可被篡改。授权若信任包内 userId，攻击者能冒充别人。当前 userId 只由登录后的服务端 Context 提供，普通业务包不携带授权身份。

## Q18：ownerUserId 与 connectionId 有什么区别？

ownerUserId 是持久用户身份，重连不变；connectionId 是当前 TCP 生命周期标识，断开就失效。上传任务需要前者判断归属，后者判断当前活动绑定。

## Q19：用户重连后为什么能恢复上传？

重新登录后虽然 connectionId 改变，但数据库 userId 相同。任务持久化 ownerUserId，再结合 transferId、token 和文件信息即可授权新连接。

## Q20：如何阻止其他用户恢复任务？

即使 token 正确，也必须比较当前 Context.userId 与任务 ownerUserId。不同就返回 permission denied，不绑定连接、不写数据。

## Q21：为什么 `.task` 暂时保留？

它与 `.part` 已形成可靠的上传恢复闭环。迁移 MySQL 会增加双写、状态机、过期清理和旧任务迁移，本阶段没有必要。

## Q22：为什么 media 表只存元数据？

元数据适合索引、过滤和外键；大视频适合文件系统顺序读写。两者分离既支持业务查询，也复用原有 64 KB 传输。

## Q23：originalName 和 storedName 为什么分开？

原名用于用户展示，可能重名；存储名是服务器唯一内部标识。分开后同名上传不会冲突，内部路径也不暴露给客户端。

## Q24：为什么下载改成 mediaId？

文件名不唯一也不稳定，且不能代表权限和状态。mediaId 能查询 published 记录和可信 storage path，避免客户端控制服务器路径。

## Q25：PUBLIC 和 MINE 如何查询？

PUBLIC 只限制 published；MINE 再加 owner_user_id 绑定参数。owner 参数来自服务端 ConnectionContext，不来自客户端。

## Q26：数据库连接池解决什么问题？

它复用已鉴权连接、限制数据库并发，并用 RAII 避免异常路径忘记归还。有限等待防止 worker 永久阻塞。

## Q27：池大小为什么不等于最大 worker 数？

worker 还处理文件任务，不都需要数据库。较小池能限制 DB 压力；当前 4 对核心 worker 4，最大 worker 8，属于学习项目的保守配置。

## Q28：数据库不可用时怎样处理？

借连接前 ping，失败后尝试重连；无法获得可用连接则在约 2.5 秒内返回 DATABASE_UNAVAILABLE。数据库调用在 worker，Ping 仍由 Reactor 响应。

## Q29：当前认证还有哪些安全问题？

没有 TLS、登录限速、长期 session、审计、角色和私有媒体权限。密码链路可被监听，所以只适用于受限测试环境。

## Q30：未来上公网还需要什么？

首先部署 TLS 和服务器证书校验，再加入登录限速、防暴力破解、安全 session、权限模型、审计、密钥与配置管理；文件还应增加 SHA-256 等强一致性校验，并经过威胁建模和安全测试。

## 23. 推荐提交信息

```text
feat(auth): add mysql authentication and media ownership
```
