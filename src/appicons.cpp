#include "appicons.h"
//----------------------------------------------
#include <QPixmap>

//----------------------------------------------

static QIcon loadIcon(const char *resourcePath)
{
    const QPixmap pm(QString::fromUtf8(resourcePath));
    if (pm.isNull())
        return QIcon();
    return QIcon(pm);
}

//----------------------------------------------

QIcon AppIcons::appIcon()
{
    return loadIcon(":/icons/unique.png");
}

//----------------------------------------------

QIcon AppIcons::clearAll()
{
    return loadIcon(":/icons/clear.png");
}

//----------------------------------------------

QIcon AppIcons::mainChat()
{
    return loadIcon(":/icons/chat.png");
}

//----------------------------------------------

QIcon AppIcons::workerGraph()
{
    return loadIcon(":/icons/team.png");
}

//----------------------------------------------

QIcon AppIcons::journal()
{
    return loadIcon(":/icons/log.png");
}

//----------------------------------------------

QIcon AppIcons::prevConfirm()
{
    return loadIcon(":/icons/prev_perm.png");
}

//----------------------------------------------

QIcon AppIcons::nextConfirm()
{
    return loadIcon(":/icons/next_perm.png");
}

//----------------------------------------------

QIcon AppIcons::manager()
{
    return loadIcon(":/icons/worker_manager.png");
}

//----------------------------------------------

QIcon AppIcons::executor()
{
    return loadIcon(":/icons/worker_executor.png");
}

//----------------------------------------------

QIcon AppIcons::tester()
{
    return loadIcon(":/icons/worker_tester.png");
}

//----------------------------------------------

QIcon AppIcons::tool()
{
    return loadIcon(":/icons/worker_tool.png");
}

//----------------------------------------------

QIcon AppIcons::forWorkerType(WorkerType type)
{
    switch (type)
    {
    case WorkerTypeTool:
        return tool();
    case WorkerTypeManager:
    case WorkerTypeTester:
    case WorkerTypeExecutor:
    default:
        return executor();
    }
}
