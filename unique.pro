QT += core gui network widgets

TARGET = Unique
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
    src/appversion.h \
    src/balancedialog.h \
    src/chatsession.h \
    src/composerinput.h \
    src/contextoptimizer.h \
    src/conversationwidget.h \
    src/debugagenttools.h \
    src/docktitlebar.h \
    src/filechangestore.h \
    src/graphthumb.h \
    src/deepseekclient.h \
    src/groqclient.h \
    src/journalwidget.h \
    src/knowledgestore.h \
    src/llmclient.h \
    src/llmnetworkworker.h \
    src/llmtypes.h \
    src/lookuptools.h \
    src/inflate.h \
    src/pdftext.h \
    src/pdfpage.h \
    src/mainwindow.h \
    src/mcphub.h \
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
    src/settingstools.h \
    src/speechinput.h \
    src/teamschemaeditor.h \
    src/toolcatalog.h \
    src/toolplugin.h \
    src/updatechecker.h \
    src/vcsrepo.h \
    src/worker.h \
    src/workergraphwidget.h \
    src/workerschema.h \
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
    src/docktitlebar.cpp \
    src/filechangestore.cpp \
    src/graphthumb.cpp \
    src/deepseekclient.cpp \
    src/groqclient.cpp \
    src/journalwidget.cpp \
    src/knowledgestore.cpp \
    src/llmclient.cpp \
    src/lookuptools.cpp \
    src/inflate.cpp \
    src/pdftext.cpp \
    src/pdfpage.cpp \
    src/llmnetworkworker.cpp \
    src/main.cpp \
    src/mcphub.cpp \
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
    src/settingstools.cpp \
    src/speechinput.cpp \
    src/teamschemaeditor.cpp \
    src/toolcatalog.cpp \
    src/updatechecker.cpp \
    src/vcsrepo.cpp \
    src/worker.cpp \
    src/workergraphwidget.cpp \
    src/workerschema.cpp \
    src/workermanager.cpp \
    src/workertools.cpp

RESOURCES += icons.qrc

# Executable icon (Windows).
win32:RC_ICONS = icon/unique.ico

# plugins/cad/cad.pro builds bin/plugins/cad.dll. Unique loads it from plugins/.
# Qt 5.9 loads OpenSSL 1.0.2 (libeay32.dll / ssleay32.dll) from the executable directory.
# The 64-bit kit uses its own OpenSSL. PDFium in this tree is 32-bit.
win32: LIBS += -lwinmm

win32 {
    equals(QT_ARCH, x86_64) {
        OPENSSL_DIR = $$PWD/3rdparty/openssl-win64
    } else {
        OPENSSL_DIR = $$PWD/3rdparty/openssl
    }
    QMAKE_POST_LINK += $$escape_expand(\\n\\t) \
        $(COPY_FILE) $$shell_path($$OPENSSL_DIR/libeay32.dll) $$shell_path($$DESTDIR)
    QMAKE_POST_LINK += $$escape_expand(\\n\\t) \
        $(COPY_FILE) $$shell_path($$OPENSSL_DIR/ssleay32.dll) $$shell_path($$DESTDIR)
    !equals(QT_ARCH, x86_64) {
        QMAKE_POST_LINK += $$escape_expand(\\n\\t) \
            $(COPY_FILE) $$shell_path($$PWD/3rdparty/pdfium/bin/pdfium.dll) $$shell_path($$DESTDIR)
    }
    QMAKE_POST_LINK += $$escape_expand(\\n\\t) \
        $$shell_quote($$shell_path($$[QT_INSTALL_BINS]/windeployqt)) --release --compiler-runtime --no-translations $$shell_quote($$shell_path($$DESTDIR/$${TARGET}.exe))
}
