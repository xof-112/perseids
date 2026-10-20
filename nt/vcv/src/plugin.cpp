#include "perseids_plugin.hpp"

Plugin* pluginInstance;

void init(Plugin* p)
{
    pluginInstance = p;
    p->addModel(modelPerseids);
}
