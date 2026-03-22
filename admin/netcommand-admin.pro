QT += core gui widgets network

CONFIG += c++17
TARGET  = netcommand-admin

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    server.cpp \
    clientpanel.cpp

HEADERS += \
    mainwindow.h \
    server.h \
    clientpanel.h \
    ../common/protocol.h \
    ../common/nc_socket.h

INCLUDEPATH += ../common

# Platform-specific
win32 {
    LIBS += -lws2_32
}
unix:mac {
    LIBS += -framework CoreGraphics
}
unix:!mac {
    # Linux — nothing extra for Qt networking
}
