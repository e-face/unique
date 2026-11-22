#ifndef TOOLPLUGIN_H
#define TOOLPLUGIN_H

/**
 * @brief C ABI for a tool library loaded from plugins/ next to the program.
 *
 * The file is a shared library (.dll, .so, or .dylib). It does not link Unique.
 * Export the functions below with extern "C" and the platform default calling
 * convention. Strings are UTF-8 and must stay valid while the library is loaded.
 * unique_plugin_invoke may run on a worker thread and must not touch UI.
 *
 * Place the library in <program>/plugins. Libraries without unique_plugin_abi
 * are ignored.
 */

#define UNIQUE_TOOL_PLUGIN_ABI 1

typedef int (*UniqueToolInvokeFn)(const char *argumentsJson,
                                  char *output,
                                  int outputCapacity,
                                  int *ok);

struct UniqueToolSpec
{
    const char         *id;
    const char         *title;
    const char         *description;
    const char         *schemaJson;
    UniqueToolInvokeFn  invoke;
};

/*
 * int unique_plugin_abi(void);                         // UNIQUE_TOOL_PLUGIN_ABI
 * const char *unique_plugin_id(void);                  // stable library id
 * const char *unique_plugin_title(void);               // title shown in the UI
 * int unique_plugin_tool_count(void);
 * int unique_plugin_tool(int index, UniqueToolSpec *spec); // 1 on success
 *
 * unique_plugin_invoke writes a NUL-terminated UTF-8 string into output.
 * Return 0 and set *ok to 1 or 0. Return a positive value when outputCapacity
 * is too small. Return a negative value on failure.
 *
 * When the arguments are a JSON object, the host sets "project_root" to the
 * open project directory and overwrites a value sent by the model. Relative
 * file paths should resolve against that directory.
 */

#endif // TOOLPLUGIN_H
