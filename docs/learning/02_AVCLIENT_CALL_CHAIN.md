# AVClient 调用链

## 1. 启动入口

文件：`AVClient/main.cpp`

```text
main
├── SDL_SetMainReady()
├── QApplication
├── av_register_all()
├── avformat_network_init()
├── MainWindow window
├── window.show()
└── app.exec()
```

`SDL_SetMainReady()` 允许应用自行提供 `main`；`av_register_all()` 是旧版 FFmpeg 的全局注册接口；`avformat_network_init()` 初始化 FFmpeg 网络能力。源码没有在 `main` 中显式调用 `avformat_network_deinit()` 或 `SDL_Quit()`，当前依靠进程退出回收。

## 2. MainWindow 组合关系

文件：`AVClient/MainWindow.cpp`

`MainWindow::MainWindow()` 创建一个 `QTabWidget`，再创建：

```text
MainWindow
├── m_networkClient = new AVNetworkClient(this)
├── PlayerPage
├── RecorderPage
├── RemoteMediaPage(m_networkClient)
└── SettingsPage(m_networkClient)
```

### 为什么共享 AVNetworkClient

设置页负责连接、断开和 Ping，远程媒体页负责列表与传输；二者必须观察同一个 socket 状态。由 `MainWindow` 创建并以 QObject 父子关系持有，可避免每页各建连接、连接状态不一致和析构顺序不清晰。

### 下载后播放

```text
RemoteMediaPage::finishDownloadState()
  -> emit requestPlayLocalFile(finalPath)
  -> MainWindow::slotPlayLocalFile(filePath)
  -> QTabWidget::setCurrentWidget(m_playerPage)
  -> PlayerPage::playLocalFile(filePath)
  -> PlayerDialog::playLocalFile(filePath)
```

由 `MainWindow` 转发是因为它拥有页面导航权；`RemoteMediaPage` 不需要知道 tab 下标或直接持有 `PlayerPage`，降低页面间耦合。

## 3. 页面职责

### MainWindow

- 成员：`m_tabs`、`m_playerPage`、`m_networkClient`。
- slot：`slotPlayLocalFile()`。
- 关系：组合四页；共享网络对象；转发下载后播放。

### PlayerPage

- 文件：`AVClient/pages/PlayerPage.cpp`。
- 成员：`PlayerDialog *m_playerDialog`。
- 关键函数：构造时把原播放器对话框嵌入布局；`playLocalFile()` 继续转发。

### RecorderPage

- 文件：`AVClient/pages/RecorderPage.cpp`。
- 成员：`RecorderDialog *m_recorderDialog`。
- 关键关系：调用 `setEmbeddedMode(true)`，开始录制时不最小化统一主窗口。

### SettingsPage

- 文件：`AVClient/pages/SettingsPage.cpp`。
- 成员：共享 `AVNetworkClient`、IP/端口输入和日志控件。
- slots：`slotConnectClicked()`、`slotDisconnectClicked()`、`slotPingClicked()`、连接/日志/Pong 响应。
- 默认地址：`192.168.44.130:8000`。

### RemoteMediaPage

- 文件：`AVClient/pages/RemoteMediaPage.cpp`。
- 成员：共享网络对象、媒体表、上传/下载任务表、活动 `QFile`、进度状态、两个 TaskStore。
- signal：`requestPlayLocalFile(QString)`。
- 关键 slots：列表刷新；上传 INIT/RESUME/BLOCK/FINISH；下载 INIT/BLOCK/FINISH；恢复与放弃。
- 约束：`updateActionStates()` 保证同一时刻只有一个活动传输。

## 4. 网络对象链路

```text
SettingsPage/RemoteMediaPage
  -> AVNetworkClient::sendXxx()
  -> 构造 av_protocol.h 结构体/动态包体
  -> TcpClient::sendPacket()
  -> [长度头][包体]

TcpClient::recvLoop()
  -> recvAll(length)
  -> recvAll(body)
  -> emit packetReceived(QByteArray)
  -> AVNetworkClient::onPacketReceived()
  -> emit 具体业务 signal
  -> 页面 slot 更新 UI/任务状态
```

`TcpClient` 管理 Winsock、socket、接收线程和完整帧；`AVNetworkClient` 管理业务协议与 Qt 语义，页面不直接解释二进制结构体。

## 5. 播放对象关系

```text
PlayerPage -> PlayerDialog -> VideoPlayer(QThread)
                              ├── PacketQueue(video)
                              ├── PacketQueue(audio)
                              ├── SDL audio callback
                              └── SDL video thread/timer
PlayerDialog -> MyOpenGLWidget
```

`PlayerDialog` 负责按钮、滑块、计时器和画面转发；`VideoPlayer` 负责媒体生命周期和解码；`PacketQueue` 负责跨线程 packet 传递；`MyOpenGLWidget` 负责绘制 `QImage`。

## 6. 录制对象关系

```text
RecorderPage -> RecorderDialog -> SaveVideoFileThread(QThread)
                                  ├── PicInPic_Read(QThread)
                                  └── Audio_Read(QObject/QAudioInput)
RecorderDialog -> PictureWidget（摄像头预览悬浮窗）
```

`RecorderDialog::on_pb_start_clicked()` 收集参数，`slot_setInfo()` 建输出上下文并启动编码线程，`slot_openVideo()` 启动采集。停止时先停采集，再令编码线程排空队列、写 trailer 并释放资源。

## 7. 关闭与资源释放

- `AVNetworkClient::~AVNetworkClient()` 调用断开；`TcpClient::~TcpClient()` 停接收线程并 `WSACleanup()`。
- `PlayerDialog::~PlayerDialog()` 停止 `VideoPlayer`；`VideoPlayer::~VideoPlayer()` 再次执行停止保护。
- `RecorderDialog::~RecorderDialog()` 若正在录制则停止并最多等待 3 秒，再删除编码和预览对象。
- QObject 父子关系负责窗口、页面和共享网络对象的基本析构顺序。

## 8. 核心类速查

| 类 | 关键成员/信号槽 | 学习时先看 |
|---|---|---|
| MainWindow | tabs、shared network、`slotPlayLocalFile` | 页面所有权与跨页转发 |
| PlayerPage | `m_playerDialog` | 适配层为何很薄 |
| PlayerDialog | `m_player`、UI timer、frame/state slots | UI 到播放线程 |
| VideoPlayer | `VideoState`、`run/play/pause/stop/seek` | 完整播放生命周期 |
| RecorderPage | `m_recorderDialog` | 嵌入模式 |
| RecorderDialog | `m_saveFileThread`、开始/停止 slots | 录制入口 |
| SaveVideoFileThread | 两个采集器、帧队列、OutputStream | 编码封装主链 |
| SettingsPage | shared network、连接/Ping slots | 连接入口 |
| RemoteMediaPage | TaskStore、活动文件、协议响应 slots | 传输状态机 |
| AVNetworkClient | `m_tcpClient`、所有 send/response signals | 协议适配 |
| TcpClient | socket、recv thread、mutex/atomic | 长度帧收发 |

