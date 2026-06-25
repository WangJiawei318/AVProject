#include "MainWindow.h"

#include <QApplication>
#include <iostream>

extern "C" {
#include <SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#undef main

int main(int argc, char *argv[])
{
    SDL_SetMainReady();

    QApplication app(argc, argv);
    app.setApplicationName("AVClient");
    app.setOrganizationName("AVProject");

    av_register_all();
    avformat_network_init();

    std::cout << "AVClient FFmpeg version: " << avcodec_version() << std::endl;

    MainWindow window;
    window.show();

    return app.exec();
}
