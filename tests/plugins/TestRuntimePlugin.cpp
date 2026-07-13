#include "TestRuntimePluginShared.h"

#include "engine/plugin/PluginApi.h"
#include "engine/runtime/Component.h"

namespace
{

TestRuntimePluginService g_service;

class CounterComponent final : public engine::runtime::Component
{
  public:
    CounterComponent(engine::plugin::EntityId owner, const engine::plugin::PluginHostApi& host)
        : Component(owner, host)
    {
        m_service = static_cast<TestRuntimePluginService*>(
            host.GetService(host.HostContext, kTestRuntimeServiceName, 1));
    }

    void OnStart() override
    {
        if (m_service)
            ++m_service->StartCount;
    }

    void OnActivate() override
    {
        if (m_service)
            ++m_service->ActivateCount;
    }

    void OnDeactivate() override
    {
        if (m_service)
            ++m_service->DeactivateCount;
    }

    void OnUpdate(float deltaSeconds) override
    {
        if (m_service)
        {
            ++m_service->UpdateCount;
            m_service->AccumulatedSeconds += deltaSeconds;
        }
    }

  private:
    TestRuntimePluginService* m_service = nullptr;
};

engine::plugin::PluginComponentType g_counterType =
    engine::runtime::MakeComponentType<CounterComponent>(kTestRuntimeComponentName);

int32_t OnLoad(const engine::plugin::PluginHostApi* host)
{
    if (!host || host->AbiVersion != engine::plugin::kPluginAbiVersion)
        return -1;
    g_service = {};
    ++g_service.LoadCount;
    if (host->RegisterService(host->HostContext, kTestRuntimeServiceName, 1, &g_service) != 0)
        return -2;
    if (host->RegisterComponentType(host->HostContext, &g_counterType) != 0)
        return -3;
    return 0;
}

void OnUnload(const engine::plugin::PluginHostApi* host)
{
    host->UnregisterComponentType(host->HostContext, kTestRuntimeComponentName);
    host->UnregisterService(host->HostContext, kTestRuntimeServiceName);
}

void OnApplicationEvent(const engine::plugin::PluginHostApi*,
                        const engine::plugin::PluginApplicationEvent*)
{
    ++g_service.ApplicationEventCount;
}

const engine::plugin::PluginDescriptor g_descriptor = {
    sizeof(engine::plugin::PluginDescriptor),
    engine::plugin::kPluginAbiVersion,
    kTestRuntimePluginName,
    1,
    0,
    0,
    nullptr,
    0,
    &OnLoad,
    &OnUnload,
    &OnApplicationEvent,
};

} // namespace

ENGINE_PLUGIN_ENTRY
{
    return hostAbiVersion == engine::plugin::kPluginAbiVersion ? &g_descriptor : nullptr;
}
