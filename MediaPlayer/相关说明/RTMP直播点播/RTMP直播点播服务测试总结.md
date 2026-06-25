## 📋 RTMP 直播/点播服务测试总结

### 一、服务器架构

虚拟机中 Nginx + RTMP 模块配置：

```nginx
rtmp {
    server {
        listen 1935;                      # RTMP 服务端口
        
        application videotest {           # 直播应用
            live on;                      # 开启直播模式
        }
        
        application vod {                 # 点播应用
            play /home/wjw/video/flv;     # 视频文件存放目录
        }
    }
}
```

---

### 二、功能说明

| 功能 | Application | 说明 |
|:---|:---|:---|
| **直播（Live）** | `videotest` | 实时推流/拉流，支持多端同时观看 |
| **点播（VOD）** | `vod` | 播放服务器上已有的 FLV 视频文件 |

---

### 三、测试流程

#### 📌 前置条件
- 虚拟机 IP：`192.168.44.130`
- 虚拟机状态：**已开机**，Nginx 服务已启动
- 本机与虚拟机网络互通（同一 VMnet8 网段）

#### 1️⃣ 直播测试（摄像头推流）

**第一步 — 推流（本机执行）**
```bash
ffmpeg -f dshow -i video="USB2.0 HD UVC WebCam" -vcodec libx264 -preset:v ultrafast -tune:v zerolatency -f flv rtmp://192.168.44.130/videotest/user=100
```
> 📝 注意：摄像头名称以 `ffmpeg -f dshow -list_devices true -i dummy` 查到的为准

**第二步 — 拉流播放（另开一个终端执行）**
```bash
ffplay rtmp://192.168.44.130/videotest/user=100
```
**注意**：第一步与第二步的命令需要在 `D:\colin\project\VideoPlayer\0422\MediaPlayer\ffmpeg-4.2.2\bin` 目录下执行。

**预期结果**：FFplay 窗口显示摄像头实时画面

---

#### 2️⃣ 点播测试（播放服务器上的 FLV 文件）

**前置准备**：将 FLV 视频文件放入虚拟机 `/home/wjw/video/flv/` 目录

**播放命令（本机执行）**
```bash
ffplay rtmp://192.168.44.130/vod/文件名.flv
```
**示例**：
```bash
ffplay rtmp://192.168.44.130/vod/102.mp4
```

**预期结果**：FFplay 窗口播放该视频文件

---

#### 3️⃣ 其他播放方式

| 播放器 | 直播地址 | 点播地址 |
|:---|:---|:---|
| VLC | `rtmp://192.168.44.130/videotest/user=100` | `rtmp://192.168.44.130/vod/102.mp4` |
| PotPlayer | 同上 | 同上 |
| 浏览器 + flv.js | 需经 Web 转换 | 需经 Web 转换 |

---

### 四、常用命令速查

| 用途 | 命令 |
|:---|:---|
| 列出本机摄像头设备 | `ffmpeg -f dshow -list_devices true -i dummy` |
| 推流（摄像头 → 直播） | `ffmpeg -f dshow -i video="摄像头名称" -vcodec libx264 -preset:v ultrafast -tune:v zerolatency -f flv rtmp://192.168.44.130/videotest/流标识` |
| 拉流播放（直播） | `ffplay rtmp://192.168.44.130/videotest/流标识` |
| 拉流播放（点播） | `ffplay rtmp://192.168.44.130/vod/文件名.flv` |
| 查看本机 IP | `ipconfig`（Windows） |
| 查看虚拟机 IP | `ifconfig` 或 `ip addr`（Linux） |
| 检查 RTMP 服务是否运行 | `sudo netstat -tlnp \| grep 1935`（虚拟机内执行） |

---

### 五、地址规则总结

```
直播推流：  rtmp://{服务器IP}/{application}/{stream_key}
直播播放：  rtmp://{服务器IP}/{application}/{stream_key}
点播播放：  rtmp://{服务器IP}/vod/{文件名}.flv
```

| 占位符 | 说明 | 示例 |
|:---|:---|:---|
| `{服务器IP}` | 虚拟机 IP | `192.168.44.130` |
| `{application}` | 应用名 | `videotest` |
| `{stream_key}` | 流标识（自定义） | `user=100`、`test`、`room1` |

---

### 六、常见问题排查

| 现象 | 可能原因 | 解决方法 |
|:---|:---|:---|
| 推流失败 | 虚拟机未开机或 Nginx 未启动 | 启动虚拟机，执行 `sudo systemctl start nginx` |
| 推流失败 | 端口 1935 被占用 | 检查端口占用：`netstat -ano \| findstr 1935` |
| 无法播放 | 流标识（stream_key）不一致 | 确保推流和拉流地址完全相同 |
| 点播 404 | FLV 文件不存在或路径错误 | 检查文件是否在 `/home/wjw/video/flv/` 目录下 |
| 摄像头识别不到 | 设备名写错或被占用 | 先列出设备名，关闭占用摄像头的程序 |

---

### 七、测试记录（可选）

| 测试日期 | 功能 | 流标识/文件名 | 结果 |
|:---|:---|:---|:---|
| 2026-06-22 | 直播 | user=100 | ✅ 通过 |
| 2026-06-22 | 点播 | 102.mp4 | ✅ 通过 |

---

> 📎 此文档可用于后续项目开发或功能回归测试参考。