#include "engine/debug/FrameDebugger.h"

#include <algorithm>

namespace engine::debug {

const FrameDebugResource* FindFrameDebugResource(const FrameDebugSnapshot& snapshot,
                                                 uint64_t id)
{
    const auto found = std::find_if(snapshot.Resources.begin(), snapshot.Resources.end(),
        [id](const FrameDebugResource& resource) { return resource.Id == id; });
    return found == snapshot.Resources.end() ? nullptr : &*found;
}

} // namespace engine::debug
