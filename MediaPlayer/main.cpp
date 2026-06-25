/*
 * main.cpp
 * ------------------------------------------------------------
 * 程序入口文件。
 *
 * 本项目同时使用 Qt、FFmpeg、SDL2：
 * 1. Qt：负责界面显示、按钮、进度条、事件循环。
 * 2. FFmpeg：负责打开媒体文件、查找音视频流、解封装、解码。
 * 3. SDL2：负责音频播放回调、视频解码线程/定时器辅助。
 *
 * 注意：SDL2 默认会尝试接管 main 函数，因此 .pro 文件中定义了
 * SDL_MAIN_HANDLED，这里再调用 SDL_SetMainReady()，明确告诉 SDL：
 * 当前 main 函数由 Qt 程序自己管理。
 */
#include "playerdialog.h"

#include <QApplication>
#include <iostream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <SDL.h>
}

// Windows + SDL2 环境下经常会出现 main 被 SDL 重定义的问题。
// 这里保留 #undef main，是为了和课程示例、Qt 工程保持兼容。
#undef main

int main(int argc, char *argv[])
{
    // 配合 .pro 中的 DEFINES += SDL_MAIN_HANDLED，避免 SDL 接管 main。
    SDL_SetMainReady();

    // 创建 Qt 应用对象，后续所有 Qt 控件、信号槽都依赖 QApplication 的事件循环。
    QApplication a(argc, argv);

    // FFmpeg 4.2.2 仍可保留 av_register_all()。
    // 如果以后升级到 FFmpeg 5/6，这个函数已废弃，可以删除。
    av_register_all();

    // 在主线程中尽早初始化 FFmpeg 网络子系统（Windows 上会调用 WSAStartup）。
    // 必须在子线程打开网络 URL 之前完成，否则 RTMP/HTTP 等协议可能连接失败。
    avformat_network_init();

    // 简单打印 FFmpeg 版本，便于确认程序链接到 FFmpeg 库。
    std::cout << "Hello FFmpeg!" << std::endl;
    std::cout << "version is: " << avcodec_version() << std::endl;

    // 主播放器窗口。
    PlayerDialog w;
    w.show();

    // 进入 Qt 事件循环：按钮点击、定时器刷新、信号槽分发都在这里持续运行。
    return a.exec();
}
