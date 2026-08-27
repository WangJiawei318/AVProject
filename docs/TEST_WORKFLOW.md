# AVProject 测试流程

## 1. 测试前准备

- Ubuntu 已安装并启动 MySQL，`users`/`media` 表已初始化。
- `AVServer/config/db.conf` 使用专用 `avapp` 账号，MySQL 只监听本机或受控网络。
- Windows 客户端与 Ubuntu 服务端可互相访问 `192.168.44.130:8000`。
- 准备一个较大的受支持媒体文件，便于中途断开传输。

## 2. 启动

Ubuntu：

```bash
cd ~/AVProject/AVServer
make clean && make
./AVServer 8000
```

确认日志包含数据库池、4 个核心 worker、epoll LT、eventfd 和监听端口。Windows 启动 `AVClient/bin/AVClient.exe`，在 Settings 连接服务器并先执行 Ping。

## 3. 认证测试

1. 在 Account 页注册 userA，确认成功后不会自动登录。
2. 再次注册 userA，确认返回用户名已存在。
3. 使用错误密码登录，确认失败。
4. 使用正确密码登录，确认显示 userId 和用户名。
5. 断开并重连，确认登录状态消失且必须重新登录。
6. 注册并登录 userB，重复后续多用户测试。

禁用用户可在 Ubuntu 执行：

```sql
UPDATE users SET status=0 WHERE username='userA';
UPDATE users SET status=1 WHERE username='userA';
```

禁用后登录应返回 USER_DISABLED。检查 `SELECT username,password_hash FROM users;`，密码列应是 Argon2id 字符串而不是原始密码。

## 4. 访问控制

新建未登录连接，确认 Ping 可用，但 MEDIA_LIST、UPLOAD 和 DOWNLOAD 均返回 AUTH_REQUIRED。业务协议包不携带可信 userId，服务端日志中的 userId 应来自 ConnectionContext。

## 5. 媒体归属闭环

1. userA 登录，切换 Public media，刷新成功。
2. userA 上传 A.mp4，完成后检查 `media/` 中是唯一 stored name，MySQL 出现 owner 为 userA 的记录。
3. 切换 My media，只看到 userA 自己的媒体。
4. userB 上传 B.mp4；userB 的 My media 只显示 B，Public media 显示 A 和 B。
5. 选择表格中的 mediaId 下载，确认 `.part` 完成后改名并可播放。
6. 选择 Download and play，确认完成后切换 Player 页播放本地 cache 文件。

历史 `media/` 文件若没有 MySQL 记录，不应出现在列表中，也不应自动归属第一个用户。

## 6. 续传回归

上传到 20% 至 50% 时断开。确认 `.task` 包含 `owner_user_id`，同一用户重新登录后可恢复；userB 即使得到 transferId 和 token 也必须收到 PERMISSION_DENIED。重启 AVServer 后 userA 仍可恢复，旧的 owner=0 任务应被拒绝。

下载到 20% 至 50% 时断开。确认客户端 `.download.json` 保存 mediaId，重启 AVClient、重新连接并登录后从非零 safe offset 恢复。AVServer 重启不需要下载任务元数据。远程文件 size/mtime 改变时应拒绝拼接；完成后仍可自动播放。

## 7. 数据库异常与并发

1. 正常登录后停止 MySQL，再从另一连接发起 LOGIN/MEDIA_LIST。
2. 数据库业务应在有限等待后返回 DATABASE_UNAVAILABLE，不应永久卡住 worker。
3. 同时从其他连接持续发送 Ping，确认 Reactor 仍响应。
4. 恢复 MySQL 后重试；连接池会在借用时 ping 并尝试重连。若当前客户端库环境无法恢复，记录后重启 AVServer。
5. 使用多个 AVClient 同时执行 Ping、PUBLIC/MINE、上传和下载，观察 worker 扩缩容且不超过 8。

## 8. 自动化烟测

```bash
cd ~/AVProject
python3 tools/auth_media_test.py --host 127.0.0.1 --port 8000
python3 tools/concurrent_client_test.py 127.0.0.1 8000 5 --pings 10
```

第一条覆盖未登录列表、重复注册、错误密码、SQL 注入式用户名、正确登录、userA/userB 以及 PUBLIC/MINE 基础请求。完整上传、下载、断点恢复和播放仍使用 AVClient 人工验证。

## 9. 安全检查

- 客户端和服务端日志不得出现原始密码、password hash、完整 resume token 或数据库密码。
- 用户名 `' OR 1=1 --` 不能绕过登录；所有用户输入 SQL 都使用 Prepared Statement。
- MySQL 不使用 root 应用账号，不为 AVProject 对公网开放 3306。
- 当前 TCP 没有 TLS，只能用于本机、局域网和受限测试环境。

## 10. 本地音视频回归

最后重新测试本地文件播放、暂停、seek、停止；桌面/摄像头/麦克风录制；录制文件播放。阶段 11 不应改变播放器、录制器及其第三方库配置。
