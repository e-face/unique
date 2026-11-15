#ifndef APPICONS_H
#define APPICONS_H

#include "workertypes.h"
//----------------------------------------------
#include <QIcon>

//----------------------------------------------

class AppIcons
{
public:
    /** Unique application icon (window / taskbar). */
    static QIcon appIcon();
    static QIcon clearAll();
    static QIcon mainChat();
    static QIcon workerGraph();
    static QIcon journal();
    static QIcon prevConfirm();
    static QIcon nextConfirm();
    /**
     * @brief Icon for a worker type (chat tabs and the schema).
     */
    static QIcon forWorkerType(WorkerType type);
    static QIcon manager();
    static QIcon executor();
    static QIcon tester();
    static QIcon tool();
};

#endif // APPICONS_H
