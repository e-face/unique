#include "mainwindow.h"
//----------------------------------------------
#include "applocale.h"
#include "appsettings.h"
#include "mcphub.h"
//----------------------------------------------
#include <QApplication>
#include <QIcon>
#include <QTimer>
#ifdef Q_OS_WIN
#include <shlobj.h>
#include <stdio.h>
#include <wchar.h>
#include <windows.h>
#endif

//----------------------------------------------

#ifdef Q_OS_WIN
static LONG WINAPI uniqueCrashFilter(EXCEPTION_POINTERS *info)
{
    wchar_t appData[MAX_PATH];
    wchar_t dir[MAX_PATH];
    wchar_t path[MAX_PATH];
    if (SHGetFolderPathW(0, CSIDL_APPDATA | CSIDL_FLAG_CREATE, 0, 0, appData) != S_OK)
        return EXCEPTION_CONTINUE_SEARCH;
    _snwprintf(dir, MAX_PATH, L"%s\\Unique", appData);
    CreateDirectoryW(dir, 0);
    _snwprintf(dir, MAX_PATH, L"%s\\Unique\\Unique", appData);
    CreateDirectoryW(dir, 0);
    _snwprintf(path, MAX_PATH, L"%s\\Unique_crash.log", dir);
    FILE *file = _wfopen(path, L"w");
    if (file && info && info->ExceptionRecord)
    {
        fprintf(file, "code=0x%08lX addr=%p\n",
                (unsigned long)info->ExceptionRecord->ExceptionCode,
                info->ExceptionRecord->ExceptionAddress);
        fclose(file);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

//----------------------------------------------

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(uniqueCrashFilter);
#endif
    QApplication app(argc, argv);
    app.setOrganizationName(QString::fromUtf8("Unique"));
    app.setApplicationName(QString::fromUtf8("Unique"));
    app.setWindowIcon(QIcon(QString::fromUtf8(":/icons/unique.png")));
    AppSettings::instance()->load();
    AppLocale::instance()->setLanguage(AppSettings::instance()->uiLanguage());

    MainWindow window;
    window.show();
    // MCP handshake can take a long time. The window is shown first;
    // the nested wait still processes events, so the UI stays usable.
    QTimer::singleShot(0, McpHub::instance(), SLOT(sltConnectAll()));
    return app.exec();
}
