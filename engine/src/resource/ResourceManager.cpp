#include "engine/resource/ResourceManager.h"

#include <algorithm>

namespace engine::resources
{

ResourceManager::ResourceManager(std::filesystem::path cookedRoot) : m_cookedRoot(std::move(cookedRoot))
{
}

void ResourceManager::SetCookedRoot(std::filesystem::path root)
{
    std::unique_lock lock(m_mutex);
    m_cookedRoot = std::move(root);
    m_cache.clear();
}

size_t ResourceManager::CacheKeyHash::operator()(const CacheKey &key) const noexcept
{
    const size_t guidHash = assets::AssetGuidHash{}(key.Guid);
    return guidHash ^ (key.Type.hash_code() + 0x9e3779b9u + (guidHash << 6u) + (guidHash >> 2u));
}

bool ResourceManager::RegisterLoaderInternal(std::string resourceType, std::type_index cppType,
                                             UntypedResourceLoader loader, std::string *error)
{
    if (resourceType.empty() || !loader)
    {
        if (error)
            *error = "Resource loader registration is incomplete";
        return false;
    }
    std::unique_lock lock(m_mutex);
    if (m_loaders.contains(resourceType))
    {
        if (error)
            *error = "Resource loader is already registered for type '" + resourceType + "'";
        return false;
    }
    m_loaders.emplace(std::move(resourceType), LoaderEntry{cppType, std::move(loader)});
    return true;
}

bool ResourceManager::UnregisterLoader(std::string_view resourceType)
{
    std::unique_lock lock(m_mutex);
    const auto found = m_loaders.find(std::string(resourceType));
    if (found == m_loaders.end())
        return false;
    const std::type_index type = found->second.CppType;
    m_loaders.erase(found);
    for (auto iterator = m_cache.begin(); iterator != m_cache.end();)
    {
        if (iterator->first.Type == type)
            iterator = m_cache.erase(iterator);
        else
            ++iterator;
    }
    return true;
}

std::shared_ptr<void> ResourceManager::LoadUntyped(assets::AssetGuid guid, std::type_index expectedType,
                                                   std::string *error)
{
    if (!guid.IsValid())
    {
        if (error)
            *error = "Cannot load an invalid asset handle";
        return {};
    }
    const CacheKey cacheKey{guid, expectedType};
    {
        std::shared_lock lock(m_mutex);
        const auto cached = m_cache.find(cacheKey);
        if (cached != m_cache.end())
            if (auto resource = cached->second.lock())
                return resource;
    }

    const std::optional<RuntimeAssetEntry> location = m_registry.Resolve(guid);
    if (!location)
    {
        DevelopmentResourceFallback fallback;
        {
            std::shared_lock lock(m_mutex);
            if (m_developmentFallbackEnabled)
                fallback = m_developmentFallback;
        }
        if (fallback)
            return fallback(guid, expectedType, error);
        if (error)
            *error = "Asset GUID is not present in the runtime registry: " + guid.ToString();
        return {};
    }

    LoaderEntry loader;
    {
        std::shared_lock lock(m_mutex);
        const auto found = m_loaders.find(location->ResourceType);
        if (found == m_loaders.end())
        {
            if (error)
                *error = "No runtime loader registered for resource type '" + location->ResourceType + "' (asset " +
                         guid.ToString() + ')';
            return {};
        }
        if (found->second.CppType != expectedType)
        {
            if (error)
                *error = "Asset " + guid.ToString() + " has runtime type '" + location->ResourceType +
                         "', but a different C++ type was requested";
            return {};
        }
        loader = found->second;
    }

    const std::filesystem::path path =
        location->CookedPath.is_absolute() ? location->CookedPath : m_cookedRoot / location->CookedPath;
    CookedResourceData cooked;
    std::string loadError;
    if (!ReadCookedResource(path, cooked, &loadError))
    {
        if (error)
            *error = "Failed to load asset " + guid.ToString() + " from '" + path.generic_string() + "': " + loadError;
        return {};
    }
    if (cooked.Header.Asset != guid || cooked.Header.AssetType != location->AssetType ||
        cooked.Header.ResourceType != location->ResourceType ||
        cooked.Header.ResourceVersion != location->ResourceVersion)
    {
        if (error)
            *error = "Cooked resource header does not match runtime registry entry for asset " + guid.ToString();
        return {};
    }

    ResourceLoadContext context{path, cooked.Header, cooked.Payload};
    std::shared_ptr<void> resource = loader.Load(context, &loadError);
    if (!resource)
    {
        if (error)
            *error = "Runtime loader for '" + location->ResourceType + "' failed for asset " + guid.ToString() + ": " +
                     loadError;
        return {};
    }
    {
        std::unique_lock lock(m_mutex);
        const auto existing = m_cache.find(cacheKey);
        if (existing != m_cache.end())
            if (auto concurrent = existing->second.lock())
                return concurrent;
        m_cache[cacheKey] = resource;
    }
    return resource;
}

void ResourceManager::Invalidate(assets::AssetGuid guid)
{
    std::unique_lock lock(m_mutex);
    for (auto iterator = m_cache.begin(); iterator != m_cache.end();)
    {
        if (iterator->first.Guid == guid)
            iterator = m_cache.erase(iterator);
        else
            ++iterator;
    }
}

void ResourceManager::InvalidateAll()
{
    std::unique_lock lock(m_mutex);
    m_cache.clear();
}

size_t ResourceManager::AddReloadListener(ResourceReloadListener listener)
{
    if (!listener)
        return 0;
    std::unique_lock lock(m_mutex);
    const size_t token = m_nextListenerToken++;
    m_reloadListeners.emplace(token, std::move(listener));
    return token;
}

bool ResourceManager::RemoveReloadListener(size_t token)
{
    std::unique_lock lock(m_mutex);
    return m_reloadListeners.erase(token) != 0;
}

void ResourceManager::NotifyResourceChanged(assets::AssetGuid guid)
{
    std::vector<ResourceReloadListener> listeners;
    {
        std::unique_lock lock(m_mutex);
        for (auto iterator = m_cache.begin(); iterator != m_cache.end();)
        {
            if (iterator->first.Guid == guid)
                iterator = m_cache.erase(iterator);
            else
                ++iterator;
        }
        listeners.reserve(m_reloadListeners.size());
        for (const auto &[token, listener] : m_reloadListeners)
        {
            (void)token;
            listeners.push_back(listener);
        }
    }
    for (const auto &listener : listeners)
        listener(guid);
}

void ResourceManager::SetDevelopmentFallback(DevelopmentResourceFallback fallback, bool enabled)
{
    std::unique_lock lock(m_mutex);
    m_developmentFallback = std::move(fallback);
    m_developmentFallbackEnabled = enabled;
}

size_t ResourceManager::CachedResourceCount() const
{
    std::shared_lock lock(m_mutex);
    return static_cast<size_t>(
        std::count_if(m_cache.begin(), m_cache.end(), [](const auto &item) { return !item.second.expired(); }));
}

} // namespace engine::resources
