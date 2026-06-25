#include "recorderdialog.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    RecorderDialog w;
    w.show();
    return a.exec();
}
