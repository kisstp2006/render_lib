#pragma once

#include "engine/asset/AssetGuid.h"
#include "engine/concurrency/TaskSystem.h"
#include "engine/resource/CookedResource.h"
#include "engine/resource/RuntimeAssetRegistry.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace engine::resources
{

struct AsyncResourceLoadOptions
{
    concurrency::TaskPriority Priority = concurrency::TaskPriority::Normal;
    concurrency::CancellationToken Cancellation;
};

struct ResourceLoadContext
{
    std::filesystem::path Path;
    CookedResourceHeader Header;
    std::span<const std::byte> Payload;
};

using UntypedResourceLoader = std::function<std::shared_ptr<void>(const ResourceLoadContext &, std::string *)>;
using ResourceReloadListener = std::function<void(assets::AssetGuid)>;
using DevelopmentResourceFallback =
    std::function<std::shared_ptr<void>(assets::AssetGuid, std::type_index, std::string *)>;

class ResourceManager
{
  public:
    explicit ResourceManager(std::filesystem::path cookedRoot = {});

    RuntimeAssetRegistry &Registry()
    {
        return m_registry;
    }
    const RuntimeAssetRegistry &Registry() const
    {
        return m_registry;
    }
    void SetCookedRoot(std::filesystem::path root);
    [[nodiscard]] const std::filesystem::path &CookedRoot() const
    {
        return m_cookedRoot;
    }

    template <typename ResourceType, typename Loader>
    bool RegisterLoader(std::string resourceType, Loader &&loader, std::string *error = nullptr)
    {
        UntypedResourceLoader erased =
            [callback = std::forward<Loader>(loader)](const ResourceLoadContext &context,
                                                      std::string *loadError) -> std::shared_ptr<void> {
            return callback(context, loadError);
        };
        return RegisterLoaderInternal(std::move(resourceType), std::type_index(typeid(ResourceType)), std::move(erased),
                                      error);
    }

    bool UnregisterLoader(std::string_view resourceType);

    template <typename ResourceType>
    std::shared_ptr<ResourceType> Load(assets::AssetHandle<ResourceType> handle, std::string *error = nullptr)
    {
        return std::static_pointer_cast<ResourceType>(
            LoadUntyped(handle.Guid, std::type_index(typeid(ResourceType)), error));
    }

    template <typename ResourceType>
    concurrency::AsyncResult<std::shared_ptr<ResourceType>> LoadAsync(
        assets::AssetHandle<ResourceType> handle, AsyncResourceLoadOptions options = {})
    {
        concurrency::TaskSystem* tasks = m_tasks ? m_tasks : &concurrency::TaskSystem::Global();
        return tasks->SubmitFuture(
            [this, handle](const concurrency::CancellationToken &cancellation) {
                cancellation.ThrowIfCancellationRequested();
                std::string error;
                std::shared_ptr<ResourceType> resource = Load(handle, &error);
                if (!resource)
                    throw std::runtime_error(error.empty() ? "Asynchronous resource load failed" : error);
                cancellation.ThrowIfCancellationRequested();
                return resource;
            },
            options.Priority, std::move(options.Cancellation));
    }

    template <typename ResourceType>
    concurrency::AsyncResult<std::shared_ptr<ResourceType>> LoadAsync(
        assets::AssetGuid guid, AsyncResourceLoadOptions options = {})
    {
        return LoadAsync(assets::AssetHandle<ResourceType>{guid}, std::move(options));
    }

    void SetTaskSystem(concurrency::TaskSystem *tasks)
    {
        m_tasks = tasks;
    }

    template <typename ResourceType>
    std::shared_ptr<ResourceType> Load(assets::AssetGuid guid, std::string *error = nullptr)
    {
        return Load(assets::AssetHandle<ResourceType>{guid}, error);
    }

    void Invalidate(assets::AssetGuid guid);
    void InvalidateAll();
    size_t AddReloadListener(ResourceReloadListener listener);
    bool RemoveReloadListener(size_t token);
    void NotifyResourceChanged(assets::AssetGuid guid);

    void SetDevelopmentFallback(DevelopmentResourceFallback fallback, bool enabled);
    [[nodiscard]] size_t CachedResourceCount() const;

  private:
    struct LoaderEntry
    {
        std::type_index CppType{typeid(void)};
        UntypedResourceLoader Load;
    };

    struct CacheKey
    {
        assets::AssetGuid Guid;
        std::type_index Type{typeid(void)};
        friend bool operator==(const CacheKey &, const CacheKey &) = default;
    };

    struct CacheKeyHash
    {
        size_t operator()(const CacheKey &key) const noexcept;
    };

    bool RegisterLoaderInternal(std::string resourceType, std::type_index cppType, UntypedResourceLoader loader,
                                std::string *error);
    std::shared_ptr<void> LoadUntyped(assets::AssetGuid guid, std::type_index expectedType, std::string *error);

    std::filesystem::path m_cookedRoot;
    RuntimeAssetRegistry m_registry;
    mutable std::shared_mutex m_mutex;
    std::unordered_map<std::string, LoaderEntry> m_loaders;
    std::unordered_map<CacheKey, std::weak_ptr<void>, CacheKeyHash> m_cache;
    std::unordered_map<size_t, ResourceReloadListener> m_reloadListeners;
    DevelopmentResourceFallback m_developmentFallback;
    bool m_developmentFallbackEnabled = false;
    size_t m_nextListenerToken = 1;
    concurrency::TaskSystem *m_tasks = nullptr;
};

} // namespace engine::resources
