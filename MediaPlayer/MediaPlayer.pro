# MediaPlayer.pro
# ------------------------------------------------------------
# Qt qmake 工程配置文件。
#
# 主要作用：
# 1. 指定 Qt 模块：core/gui/widgets；
# 2. 指定 C++ 标准；
# 3. 加入 FFmpeg、SDL2 的头文件目录和 lib 链接库；
# 4. 列出当前工程需要参与编译的 .cpp、.h、.ui 文件。
#
# 注意：本工程使用的是 FFmpeg 4.2.2 + SDL2 2.0.10 的 Windows 静态导入库路径。
# 如果你的目录名或库路径不同，需要在 INCLUDEPATH 和 LIBS 中对应修改。

QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

# 关键：让 SDL 不接管 main 函数，避免 Qt + SDL 的 main 冲突
DEFINES += SDL_MAIN_HANDLED

SOURCES += \
    packetqueue.cpp \
    main.cpp \
    playerdialog.cpp \
    videoplayer.cpp

HEADERS += \
    packetqueue.h \
    playerdialog.h \
    videoplayer.h

FORMS += \
    playerdialog.ui

include(./opengl/opengl.pri)

INCLUDEPATH += ./opengl/

INCLUDEPATH += $$PWD/ffmpeg-4.2.2/include \
               $$PWD/SDL2-2.0.10/include

LIBS += $$PWD/ffmpeg-4.2.2/lib/avcodec.lib \
        $$PWD/ffmpeg-4.2.2/lib/avdevice.lib \
        $$PWD/ffmpeg-4.2.2/lib/avfilter.lib \
        $$PWD/ffmpeg-4.2.2/lib/avformat.lib \
        $$PWD/ffmpeg-4.2.2/lib/avutil.lib \
        $$PWD/ffmpeg-4.2.2/lib/postproc.lib \
        $$PWD/ffmpeg-4.2.2/lib/swresample.lib \
        $$PWD/ffmpeg-4.2.2/lib/swscale.lib \
        $$PWD/SDL2-2.0.10/lib/x86/SDL2.lib

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
