# 录制器流水线

## 1. 录制入口

文件：`AVClient/modules/recorder/recorderdialog.cpp`

```text
RecorderDialog::on_pb_start_clicked()
  -> 构造 STRU_AV_FORMAT
  -> SaveVideoFileThread::slot_setInfo(format)
      -> 创建 FLV 输出上下文/视频音频流/编码器
      -> avformat_write_header()
      -> SaveVideoFileThread::start()
  -> SaveVideoFileThread::slot_openVideo()
      -> PicInPic_Read::slot_openVideo()
      -> Audio_Read::slot_openAudio()
```

默认输出为应用目录下 `recordings/record_yyyyMMdd_hhmmss.flv`，视频参数为主屏尺寸、25 fps、约 1.4 Mbps；音频为 44.1 kHz、双声道、AAC 64 kbps。

## 2. 桌面与摄像头采集

`PicInPic_Read::slot_openVideo()` 用 OpenCV `VideoCapture::open(0)` 打开默认摄像头并启动 QThread。`run()` 以 `1000 / FRAME_RATE` 控制采集节奏，调用 `slot_getVideoFrame()`：

1. `cap.read()` 取得 BGR 摄像头帧并转 RGB，发 `SIG_sendPicInPic()` 给悬浮 `PictureWidget`。
2. 通过 BlockingQueuedConnection 发 `SIG_getDeskImg()`，在对象所属线程调用 `QScreen::grabWindow()` 截取桌面。
3. `ImageToYuvBuffer()` 用 `sws_scale()` 把桌面 RGB24 转为 YUV420P。
4. 发 `SIG_sendVideoFrameData()` 给编码线程队列，同时发桌面预览图。

当前所谓“画中画”在录制文件数据链路中并未把摄像头像素叠加到桌面图；摄像头画面显示在单独的置顶 `PictureWidget` 上。面试时应按源码如实表述为“摄像头预览/悬浮画中画效果”，不能声称已完成像素级合成编码。

## 3. 麦克风采集与重采样

`Audio_Read::slot_openAudio()` 创建 `QAudioInput`，输入配置目标为 44.1 kHz、双声道、16-bit PCM、小端。20 ms 定时器触发 `slot_readMore()`：

1. 从 `QIODevice` 读取可用 PCM。
2. 按 `OneAudioSize` 聚合完整音频帧。
3. `swr_convert()` 把 packed S16 转为 AAC 编码器使用的 planar FLTP。
4. 为左右声道整理平面数据，发 `SIG_sendAudioFrameData()`。

FLTP 的每个声道数据分开存放，适合当前 AAC encoder 的 `sample_fmt`。具体编码器支持格式应以 `codec->sample_fmts` 为准，不能把“所有 AAC 都必须 FLTP”当作协议定律。

## 4. 视频与音频队列

`SaveVideoFileThread::slot_writeVideoFrameData()` 为视频数据记录相对毫秒时间，调用 `videoDataQuene_Input()`；音频 slot 调用 `audioDataQuene_Input()`。两个 `QList<BufferDataNode*>` 分别由 `QMutex` 保护。

`videoDataQuene_get(time)` 会淘汰早于目标时间的帧；没有新帧时 `write_video_frame()` 可复用 `lastVideoNode`，用于稳定输出帧率。音频按 FIFO 逐帧取出。

## 5. FFmpeg 编码与封装

`slot_setInfo()` 调用：

- `avformat_alloc_output_context2(..., "flv", ...)` 创建复用器。
- `add_video_stream()` 配置 H.264、YUV420P、尺寸、bitrate、`time_base=1/frame_rate`。
- `add_audio_stream()` 配置 AAC、FLTP、44.1 kHz、双声道、`time_base=1/sample_rate`。
- `open_video()`/`open_audio()` 打开编码器、分配可复用 `AVFrame` 和缓冲。
- `avio_open()` 与 `avformat_write_header()` 打开输出并写容器头。

编码使用新式 `avcodec_send_frame()`/`avcodec_receive_packet()`，压缩 packet 经 `av_packet_rescale_ts()` 转到 stream time base，再由 `av_interleaved_write_frame()` 交错写入 FLV。

## 6. PTS、DTS 与时间基

- PTS：帧何时展示/播放；DTS：压缩帧何时送入解码顺序。
- `time_base` 是时间戳单位。视频每帧 `next_pts++`，单位为 `1/25s`；音频按累计 sample 数通过 `av_rescale_q()` 得到 codec PTS。
- 复用前把 codec time base 转成 stream time base，保证容器能正确交错。

`SaveVideoFileThread::run()` 用 `av_compare_ts(video_st.next_pts, ..., audio_st.next_pts, ...)` 选择下一次编码音频还是视频，使两条时间线大致同步推进。

## 7. 停止流程

```text
RecorderDialog::on_pb_stop_clicked()
  -> PicInPic_Read::slot_closeVideo()
  -> Audio_Read::slot_closeAudio()
  -> SaveVideoFileThread::isStop = true
  -> 编码线程排空队列
  -> av_write_trailer()
  -> close_stream()/avio_closep()/avformat_free_context()
```

写 trailer 对 FLV 元数据和容器完整性很重要。严格的编码器 flush 通常还应向 `avcodec_send_frame(nullptr)` 送空帧并持续 receive 延迟 packet；当前实现直接排空输入队列后写 trailer，没有显式 flush 编码器，可能丢失有延迟的尾帧，尽管视频设置了 zerolatency。

## 8. 录制文件如何进入上传链路

录制模块只产生本地文件，不直接调用网络。用户在 `RemoteMediaPage::slotUploadClicked()` 选择该 FLV，随后走普通上传/续传链路。职责分离使录制失败不会污染网络状态，也便于先本地回放验证。

## 9. 当前源码风险

- 多处中文注释编码损坏，增加学习和维护成本。
- `isStop`、`m_videoBeginFlag` 等跨线程普通字段存在数据竞争可能。
- `run()` 的停止条件使用“任一已有队列为空即 break”的逻辑，随后再补排空，较绕且需重点回归尾帧。
- 编码错误路径中部分旧代码会 `exit(1)`，会终止整个 GUI 进程。
- `Audio_Read` 的 `malloc` 缓冲以 `delete[]` 释放，分配/释放方式不匹配，是明显未定义行为风险。
- `PicInPic_Read::ImageToYuvBuffer()` 每帧分配 frame/buffer/context，开销大；且使用旧 `avpicture_*`。
- 源码没有像素级合成摄像头与桌面；预览窗与录制输出是两条显示/数据路径。

## 10. 面试追问（15 题）

1. **采集、编码、封装区别？** 采集得到原始像素/PCM，编码压缩为 H.264/AAC，封装把多流及时间戳写入 FLV。
2. **为什么转 YUV420P？** 当前 H.264 encoder 配置需要该格式，且色度抽样降低数据量。
3. **RGB 到 YUV 谁完成？** `ImageToYuvBuffer()` 中的 libswscale。
4. **PCM 到 FLTP 谁完成？** `Audio_Read::slot_readMore()` 中的 libswresample。
5. **packed 与 planar 区别？** packed 声道样本交错，planar 每声道独立平面。
6. **为什么需要 PTS？** 复用和播放必须知道每帧时间位置，才能同步与 seek。
7. **DTS 为什么可能不同于 PTS？** B 帧等帧重排使解码顺序不同于显示顺序。
8. **time base 有何作用？** 定义时间戳单位，不同 codec/stream 间需 rescale。
9. **如何交错写音视频？** 比较两条流的下一 PTS，优先写时间更早者。
10. **为什么队列加锁？** 采集线程生产、编码线程消费，QList 不是并发安全容器。
11. **没有视频帧为何复用上一帧？** 保持固定输出节奏，但静态画面会重复编码。
12. **停止为什么先停采集？** 防止编码线程排空时还有新数据进入。
13. **编码器为什么要 flush？** 编码器内部可能缓存延迟帧；当前实现此处不完整，应如实说明。
14. **FLV 的优势与限制？** 适合 H.264/AAC、结构简单；现代离线兼容性常更偏 MP4，异常结束修复策略也不同。
15. **画中画当前如何实现？** 摄像头独立置顶预览；未在输出 YUV 上做真实叠加，升级可用 OpenCV resize/ROI 合成后再转 YUV。

