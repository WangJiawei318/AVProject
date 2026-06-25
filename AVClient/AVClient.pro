QT += core gui widgets multimedia opengl network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TEMPLATE = app
TARGET = AVClient
CONFIG += c++11
CONFIG -= app_bundle

DEFINES += SDL_MAIN_HANDLED

DESTDIR = $$PWD/bin

SOURCES += \
    main.cpp \
    MainWindow.cpp \
    pages/PlayerPage.cpp \
    pages/RecorderPage.cpp \
    pages/RemoteMediaPage.cpp \
    pages/SettingsPage.cpp \
    modules/network/AVNetworkClient.cpp \
    modules/network/TcpClient.cpp \
    modules/player/PacketQueue.cpp \
    modules/player/myopenglwidget.cpp \
    modules/player/playerdialog.cpp \
    modules/player/videoplayer.cpp \
    modules/recorder/audio_read.cpp \
    modules/recorder/picinpic_read.cpp \
    modules/recorder/picturewidget.cpp \
    modules/recorder/recorderdialog.cpp \
    modules/recorder/savevideofilethread.cpp

HEADERS += \
    MainWindow.h \
    pages/PlayerPage.h \
    pages/RecorderPage.h \
    pages/RemoteMediaPage.h \
    pages/SettingsPage.h \
    modules/network/AVNetworkClient.h \
    modules/network/TcpClient.h \
    modules/network/av_protocol.h \
    modules/player/PacketQueue.h \
    modules/player/myopenglwidget.h \
    modules/player/playerdialog.h \
    modules/player/videoplayer.h \
    modules/recorder/audio_read.h \
    modules/recorder/common.h \
    modules/recorder/picinpic_read.h \
    modules/recorder/picturewidget.h \
    modules/recorder/recorderdialog.h \
    modules/recorder/savevideofilethread.h

FORMS += \
    modules/player/playerdialog.ui \
    modules/recorder/picturewidget.ui \
    modules/recorder/recorderdialog.ui

INCLUDEPATH += \
    $$PWD/pages \
    $$PWD/modules/network \
    $$PWD/modules/player \
    $$PWD/modules/recorder \
    $$PWD/../MediaPlayer/ffmpeg-4.2.2/include \
    $$PWD/../MediaPlayer/SDL2-2.0.10/include \
    $$PWD/../VideoRecorder/opencv-release/include/opencv2 \
    $$PWD/../VideoRecorder/opencv-release/include

LIBS += \
    -lws2_32 \
    -lopengl32 \
    $$PWD/../MediaPlayer/ffmpeg-4.2.2/lib/avcodec.lib \
    $$PWD/../MediaPlayer/ffmpeg-4.2.2/lib/avdevice.lib \
    $$PWD/../MediaPlayer/ffmpeg-4.2.2/lib/avfilter.lib \
    $$PWD/../MediaPlayer/ffmpeg-4.2.2/lib/avformat.lib \
    $$PWD/../MediaPlayer/ffmpeg-4.2.2/lib/avutil.lib \
    $$PWD/../MediaPlayer/ffmpeg-4.2.2/lib/postproc.lib \
    $$PWD/../MediaPlayer/ffmpeg-4.2.2/lib/swresample.lib \
    $$PWD/../MediaPlayer/ffmpeg-4.2.2/lib/swscale.lib \
    $$PWD/../MediaPlayer/SDL2-2.0.10/lib/x86/SDL2.lib \
    $$PWD/../VideoRecorder/opencv-release/lib/libopencv_*.dll.a
