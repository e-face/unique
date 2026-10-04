QT += core gui network widgets

TARGET = dsagent
TEMPLATE = app
CONFIG += c++11

CODECFORSRC = UTF-8
CODECFORTR  = UTF-8
win32-msvc*: QMAKE_CXXFLAGS += /utf-8
win32-g++: QMAKE_CXXFLAGS += -finput-charset=UTF-8 -fexec-charset=UTF-8

DESTDIR     = $$PWD/bin
OBJECTS_DIR = $$PWD/build/obj
MOC_DIR     = $$PWD/build/moc
RCC_DIR     = $$PWD/build/rcc
UI_DIR      = $$PWD/build/ui

INCLUDEPATH += $$PWD/src

HEADERS += \
    src/agent.h \
    src/agenttool.h \
    src/appicons.h \
    src/applocale.h \
    src/appsettings.h \
    src/balancedialog.h \
    src/chatsession.h \
    src/composerinput.h \
    src/contextoptimizer.h \
    src/conversationwidget.h \
    src/debugagenttools.h \
    src/filechangestore.h \
    src/deepseekclient.h \
    src/groqclient.h \
    src/journalwidget.h \
    src/knowledgestore.h \
    src/llmclient.h \
    src/llmnetworkworker.h \
    src/llmtypes.h \
    src/mainwindow.h \
    src/markdownformatter.h \
    src/modalityapi.h \
    src/modalitytools.h \
    src/modelcapabilities.h \
    src/modelprofilepresets.h \
    src/openaicompatibleclient.h \
    src/projectbrief.h \
    src/projectfiletools.h \
    src/projecttreedock.h \
    src/usagestatswidget.h \
    src/projectworkspace.h \
    src/qwencloudclient.h \
    src/settingspage.h \
    src/worker.h \
    src/workergraphwidget.h \
    src/workermanager.h \
    src/workertools.h \
    src/workertypes.h

SOURCES += \
    src/agent.cpp \
    src/appicons.cpp \
    src/applocale.cpp \
    src/appsettings.cpp \
    src/balancedialog.cpp \
    src/chatsession.cpp \
    src/composerinput.cpp \
    src/contextoptimizer.cpp \
    src/conversationwidget.cpp \
    src/debugagenttools.cpp \
    src/filechangestore.cpp \
    src/deepseekclient.cpp \
    src/groqclient.cpp \
    src/journalwidget.cpp \
    src/knowledgestore.cpp \
    src/llmclient.cpp \
    src/llmnetworkworker.cpp \
    src/main.cpp \
    src/mainwindow.cpp \
    src/markdownformatter.cpp \
    src/modalityapi.cpp \
    src/modalitytools.cpp \
    src/modelcapabilities.cpp \
    src/modelprofilepresets.cpp \
    src/openaicompatibleclient.cpp \
    src/projectbrief.cpp \
    src/projectfiletools.cpp \
    src/projecttreedock.cpp \
    src/usagestatswidget.cpp \
    src/projectworkspace.cpp \
    src/qwencloudclient.cpp \
    src/settingspage.cpp \
    src/worker.cpp \
    src/workergraphwidget.cpp \
    src/workermanager.cpp \
    src/workertools.cpp

RESOURCES += icons.qrc

# Qt 5.9 грузит OpenSSL 1.0.2 (libeay32.dll / ssleay32.dll) из каталога exe.
win32 {
    OPENSSL_DIR = $$PWD/3rdparty/openssl
    QMAKE_POST_LINK += $$escape_expand(\\n\\t) \
        $(COPY_FILE) $$shell_path($$OPENSSL_DIR/libeay32.dll) $$shell_path($$DESTDIR)
    QMAKE_POST_LINK += $$escape_expand(\\n\\t) \
        $(COPY_FILE) $$shell_path($$OPENSSL_DIR/ssleay32.dll) $$shell_path($$DESTDIR)
    QMAKE_POST_LINK += $$escape_expand(\\n\\t) \
        $$shell_quote($$shell_path($$[QT_INSTALL_BINS]/windeployqt)) --release --compiler-runtime --no-translations $$shell_quote($$shell_path($$DESTDIR/$${TARGET}.exe))
}
