#ifndef TOOLCATALOG_H
#define TOOLCATALOG_H

#include <QList>
#include <QString>
#include <QStringList>

class Agent;
class QObject;

//----------------------------------------------

/**
 * @brief One tool a text worker can be given.
 *        Built-in tools and tools from plugins/ share this list.
 */
struct ToolOffer
{
    ToolOffer()
        : builtin(false)
        , decisionsOnly(false)
    {
    }

    QString id;
    /** Russian source label for a built-in tool, or the plugin's own title. */
    QString title;
    /**
     * Russian source description for a built-in tool, or the plugin's own text.
     * Shown as the checkbox tooltip.
     */
    QString description;
    QString libraryId;
    QString libraryTitle;
    bool    builtin;
    bool    decisionsOnly;
};

//----------------------------------------------

/**
 * @brief Built-in text-worker tools plus tool libraries from plugins/.
 */
class ToolCatalog
{
public:
    static ToolCatalog *instance();

    void ensureLoaded();
    QList<ToolOffer> offers();
    QStringList builtinIds();

    /**
     * @brief Add plugin tools whose ids are enabled and not already installed.
     */
    void addPlugins(Agent *agent, const QStringList &enabledIds);

private:
    ToolCatalog();

    QList<ToolOffer> _offers;
    bool             _loaded;
};

#endif // TOOLCATALOG_H
