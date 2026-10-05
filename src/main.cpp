#include "mainwindow.h"
//----------------------------------------------
#include "applocale.h"
#include "appsettings.h"
//----------------------------------------------
#include <QApplication>
#include <QIcon>
#ifdef Q_OS_WIN
#include <stdio.h>
#include <wchar.h>
#include <windows.h>
#endif

//----------------------------------------------

#ifdef Q_OS_WIN
static LONG WINAPI uniqueCrashFilter(EXCEPTION_POINTERS *info)
{
    wchar_t path[MAX_PATH];
    if (GetModuleFileNameW(0, path, MAX_PATH) == 0)
        return EXCEPTION_CONTINUE_SEARCH;
    wchar_t *slash = wcsrchr(path, L'\\');
    if (slash)
        wcscpy(slash + 1, L"Unique_crash.log");
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
    return app.exec();
}
