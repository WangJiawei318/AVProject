# 播放器流水线

## 1. 打开入口与调用链

文件：`AVClient/modules/player/playerdialog.cpp`、`videoplayer.cpp`

```text
PlayerDialog::playLocalFile(path)
  -> VideoPlayer::stop(true)
  -> VideoPlayer::setFileName(path)
      -> QThread::start()
  -> VideoPlayer::start()   // 当前源码又调用一次，属于冗余风险
  -> VideoPlayer::run()
```

本地“打开文件”与下载完成后的播放最终都进入 `PlayerDialog::playLocalFile()`。

## 2. 初始化与解封装

`VideoPlayer::run()` 重置 `VideoState`，调用 SDL 初始化，然后：

1. 为 `AVFormatContext` 设置中断回调和网络读超时。
2. `avformat_open_input()` 打开本地文件或 URL。
3. `avformat_find_stream_info()` 读取流信息。
4. `find_stream_index()` 找到首个视频流和音频流。
5. 从 `AVStream` 获取/打开解码器，建立音频与视频 `PacketQueue`。
6. 通过 `SIG_TotalTime` 把时长交给 UI。

FFmpeg 中 `AVPacket` 是压缩数据单元，`AVFrame` 是解码后的音视频帧。解封装得到 packet，解码器消费 packet 并产出 frame。

## 3. 读取线程与 PacketQueue

`VideoPlayer::run()` 本身就是 QThread 读取线程：

```text
av_read_frame(formatCtx, packet)
├── audio stream -> packet_queue_put(audioQueue)
└── video stream -> packet_queue_put(videoQueue)
```

`packet_queue_put()` 使用 `av_packet_ref()` 增加底层缓冲引用，随后读取线程可 `av_packet_unref()` 自己的临时 packet。队列由 SDL mutex/condition 保护；消费者通过 `packet_queue_get(..., block=1)` 等待。没有引用计数会使队列指向读取线程已释放的数据。

读取线程设置队列水位，避免无限预读占用内存。到 EOF 后不会马上销毁上下文，而是标记读取结束，等待音视频消费者排空队列；否则消费者会访问已释放的解码器或漏播尾部数据。

## 4. 音频链路

```text
SDL_OpenAudioDevice
  -> SDL 周期调用 audio_callback()
  -> audio_decode_frame()
  -> packet_queue_get(audioQueue)
  -> avcodec_decode_audio4()
  -> swr_convert(源格式 -> S16 双声道)
  -> 拷贝到 SDL stream
  -> 声卡播放
```

`audio_decode_frame()` 维护 packet 剩余数据，可由一个 packet 解出多帧。重采样把解码器输出统一为 SDL 使用的 S16。当前实现每帧创建/释放 `SwrContext`，清晰但开销较大。

## 5. 视频链路与 OpenGL

有音频时 `video_thread()` 解码；纯视频时 SDL timer 按估算帧率调用 `timer_callback()`。

```text
decode_one_video_frame()
  -> packet_queue_get(videoQueue)
  -> avcodec_decode_video2()
  -> best_effort_timestamp
  -> synchronize_video()
  -> sws_scale(YUV/源像素 -> RGB32)
  -> QImage.copy()
  -> VideoPlayer::SendGetOneImage()
  -> PlayerDialog::slot_setImage()
  -> MyOpenGLWidget::slot_setImage()/update()
  -> paintGL()
```

`MyOpenGLWidget` 使用固定管线纹理和 `glTexImage2D()` 上传每帧，再绘制四边形。它完成显示，但属于较旧的 OpenGL 路径。

## 6. 时钟与音视频同步

音频由硬件按稳定采样率消费，连续且对抖动敏感，因此当前以音频时钟为主。`audio_decode_frame()` 根据音频 packet PTS 和已消费时长更新 `audio_clock`；`synchronize_video()` 依据视频 PTS、codec time base 与 `repeat_pict` 更新 `video_clock`。

视频解码后比较视频时钟与音频时钟：视频领先则短暂等待，落后则尽快显示/丢弃旧 seek 区间帧。该实现是教学型“视频追随音频”，并非完整的 ffplay 自适应同步算法。

## 7. pause、stop、seek

- `play()`/`pause()` 修改播放状态与暂停标志，读取和消费者循环观察它。
- `stop(bool)` 设置 quit，唤醒/结束消费者，必要时等待读取线程。
- `seek(pos)` 只提交目标；真正的 `av_seek_frame()` 在读取线程执行，避免多个线程同时操作 `AVFormatContext`。
- seek 成功后 flush 两个 packet 队列，再放入特殊 FLUSH packet；消费者收到后调用 `avcodec_flush_buffers()`。

清队列是为了丢掉旧时间点压缩数据，清解码器是为了丢掉内部参考帧/延迟数据。二者缺一都可能在 seek 后短暂播放旧画面或产生错误依赖。

## 8. 结束顺序

读取结束或 stop 后：停止 timer、等待视频线程、关闭 SDL 音频设备、销毁队列、释放音频 packet/frame、关闭解码器与输入上下文、更新状态。音频设备要先停，避免回调继续访问正在释放的状态。

## 9. 当前源码边界

- 使用 `av_register_all`、`AVStream::codec`、`avcodec_decode_video2/audio4`、`avpicture_*` 等旧 API。
- `VideoState` 多个标志与时钟跨 Qt/SDL 线程普通读写，严格 C++ 内存模型下存在数据竞争风险。
- `PlayerDialog::playLocalFile()` 在 `setFileName()` 已启动线程后又调用 `start()`。
- 每音频帧创建 `SwrContext`，每视频帧重新上传纹理，性能可优化。
- stop 等待和网络输入中断依赖超时；异常慢源仍需更严谨生命周期测试。

## 10. 面试追问（15 题）

1. **packet 与 frame 区别？** packet 是压缩码流，frame 是解码结果；一个 packet 与 frame 不保证一一对应。
2. **为什么要两条队列？** 音视频由不同消费者和时序处理，分队列可解耦解封装与解码。
3. **队列为什么加锁？** 读取线程生产，SDL 回调/视频线程消费，容器和计数必须原子地维护。
4. **为什么 `av_packet_ref`？** 延长压缩缓冲生命周期，允许生产者释放自己的 packet。
5. **为什么音频适合主时钟？** 声卡消费节奏稳定，音频停顿/跳变更明显，视频调整展示时机更自然。
6. **视频领先怎么办？** 等待音频时钟追上；生产实现通常设阈值和最大等待。
7. **视频落后怎么办？** 缩短等待或丢帧，优先保持音频连续。
8. **`best_effort_timestamp` 用途？** 在 PTS/DTS 不完整或有重排时给出更适合展示的时间戳。
9. **seek 为什么在读取线程做？** `AVFormatContext` 与队列分流由它独占，避免并发操作。
10. **seek 后为什么 flush？** 队列和解码器都可能保存旧位置数据及参考帧。
11. **EOF 为什么不能立即释放？** 队列和解码器内部仍可能有待消费数据。
12. **SDL 回调里能阻塞很久吗？** 不能，否则声卡缺数据；应快速填充，取不到就静音。
13. **`swr_convert` 解决什么？** 采样格式、采样率、声道布局转换。
14. **`sws_scale` 解决什么？** 像素格式和尺寸转换，这里转为 RGB 供 Qt/OpenGL 显示。
15. **如何升级生产方案？** 新 FFmpeg send/receive API、原子状态、持久 Swr/Sws、现代 OpenGL、完善同步与错误恢复。

