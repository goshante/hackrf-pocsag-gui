#include "PagerSender.h"
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    PagerSender w;
    w.show();
    return a.exec();
}
