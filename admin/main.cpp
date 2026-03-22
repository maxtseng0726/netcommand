// admin/main.cpp
#include <QApplication>
#include "mainwindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("NetCommand");
    app.setApplicationVersion("1.0");
    app.setOrganizationName("NetCommand");

    MainWindow win;
    win.show();
    return app.exec();
}
