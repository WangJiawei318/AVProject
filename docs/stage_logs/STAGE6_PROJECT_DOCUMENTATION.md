# 阶段 6：项目工程化文档整理

## 1. 本阶段目标

阶段 1 到阶段 5 已经完成本地播放、录制、网络通信、远程媒体列表、上传、下载和下载后播放。本阶段不增加业务功能，也不修改核心源码，目标是把“代码可以运行”整理为：

- 新成员可以按文档理解；
- 开发者可以按文档构建；
- 演示者可以按固定流程测试；
- 面试时可以准确说明设计、取舍和不足；
- Git 不提交构建产物和测试媒体。

## 2. 新增文档清单

| 文档 | 作用 |
| --- | --- |
| `README.md` | 项目总入口、功能、技术栈、目录和快速启动 |
| `docs/ARCHITECTURE.md` | 详细解释客户端、服务端、播放、录制、网络和页面关系 |
| `docs/PROTOCOL_DESIGN.md` | 说明 TCP 分帧、全部包类型、字段、状态机和扩展方向 |
| `docs/BUILD_AND_RUN.md` | 记录 Windows/Ubuntu 依赖、构建、部署、启动和排错 |
| `docs/TEST_WORKFLOW.md` | 提供简洁的完整功能与异常测试流程 |
| `docs/INTERVIEW_QA.md` | 按六类整理 42 个可复述的项目面试问答 |
| `docs/stage_logs/STAGE6_PROJECT_DOCUMENTATION.md` | 记录阶段 6 的范围和结果 |

## 3. 修改文件清单

### `.gitignore`

在原有 Qt、C++、运行目录和媒体文件规则基础上补充：

- `*.part`：上传/下载临时文件；
- `*.wmv`、`*.mp3`：媒体测试文件；
- `/AVServer/AVServer`：Ubuntu 服务端可执行文件。

原规则已经覆盖：

```text
AVClient/bin/
AVClient/build-debug/（由 AVClient/build*/ 覆盖）
AVClient/cache/
AVServer/media/
AVServer/temp/
*.exe
*.dll
*.flv
*.mp4
*.avi
*.mkv
*.o
*.obj
```

## 4. 每个文档的阅读顺序

第一次了解项目：

```text
README
  -> ARCHITECTURE
  -> PROTOCOL_DESIGN
```

准备运行项目：

```text
BUILD_AND_RUN
  -> TEST_WORKFLOW
```

准备面试：

```text
ARCHITECTURE
  -> PROTOCOL_DESIGN
  -> INTERVIEW_QA
  -> 各阶段 stage_logs
```

## 5. 当前项目完整能力

Windows 客户端：

- 统一 Qt Widgets 主窗口；
- 本地文件和 URL 播放；
- 播放、暂停、停止、seek；
- FFmpeg 解封装和解码；
- SDL 音频、OpenGL 视频显示；
- 桌面、摄像头画中画、麦克风录制；
- H.264/AAC 编码与 FLV 输出；
- TCP 连接、断开和 Ping；
- 远程媒体列表；
- 64 KB 分片上传；
- 64 KB 分片下载；
- cache 完成后自动切换播放器。

Ubuntu 服务端：

- TCP 长度帧收发；
- Ping、列表、上传、下载协议分发；
- `media/` 扫描；
- `temp/` 上传任务管理；
- 文件名和后缀检查；
- offset、分片和文件大小校验；
- 同名上传文件自动编号；
- 按 offset 分块读取远程媒体。

## 6. 当前仍未实现

- 主线服务端 epoll 和线程池；
- 多客户端并发；
- 用户注册、登录、权限和配额；
- MySQL 媒体索引；
- TLS 和传输认证；
- MD5/SHA-256 内容校验；
- 断点续传和任务持久化；
- 多任务并行与传输取消；
- 删除、重命名、搜索和分页；
- TCP 边下边播和自定义 AVIO；
- HLS/RTMP 服务端推拉流；
- 自动化测试与 CI。

## 7. 下一阶段建议

建议不要立刻同时增加多个业务功能。优先级如下：

1. 给上传和下载增加统一任务状态、取消、超时和清晰错误码。
2. 增加 SHA-256 或 MD5 完整性校验。
3. 增加“上传最近录制文件”快捷流程。
4. 将 `NetDisk-Server` 的 epoll + 线程池思想整合到主线 `AVServer`。
5. 增加自动化协议测试和异常断线测试。
6. 最后再评估用户系统、数据库索引、删除和重命名。

用于求职展示时，建议补充：

- 主窗口、上传、下载、播放截图；
- 一段 1 到 2 分钟演示视频；
- README 中的运行结果；
- 一张当前架构与未来架构对比图；
- 简历中的量化描述和个人职责边界。

## 8. 建议 commit message

```text
docs(project): add architecture protocol and interview notes
```

本阶段未执行 Git commit。

