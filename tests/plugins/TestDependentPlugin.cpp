#include "TestRuntimePluginShared.h"

#include "engine/plugin/PluginApi.h"

namespace
{

uint32_t g_dependentService = 42;
const char* const g_dependencies[] = {kTestRuntimePluginName};

int32_t OnLoad(const engine::plugin::PluginHostApi* host)
{
    if (!host->GetService(host->HostContext, kTestRuntimeServiceName, 1))
        return -1;
    return host->RegisterService(host->HostContext, kTestDependentServiceName, 1,
                                 &g_dependentService);
}

void OnUnload(const engine::plugin::PluginHostApi* host)
{
    host->UnregisterService(host->HostContext, kTestDependentServiceName);
}

const engine::plugin::PluginDescriptor g_descriptor = {
    sizeof(engine::plugin::PluginDescriptor),
    engine::plugin::kPluginAbiVersion,
    kTestDependentPluginName,
    1,
    0,
    0,
    g_dependencies,
    1,
    &OnLoad,
    &OnUnload,
    nullptr,
};

} // namespace

ENGINE_PLUGIN_ENTRY
{
    return hostAbiVersion == engine::plugin::kPluginAbiVersion ? &g_descriptor : nullptr;
}
