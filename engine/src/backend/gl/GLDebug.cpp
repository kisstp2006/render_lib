#include "engine/backend/gl/GLDebug.h"

#include <glad/gl.h>

namespace engine::gl_debug
{

bool IsAvailable()
{
    return glad_glPushDebugGroup != nullptr && glad_glPopDebugGroup != nullptr &&
           glad_glObjectLabel != nullptr;
}

void LabelObject(unsigned int identifier, unsigned int object, std::string_view name)
{
    if (object == 0 || glad_glObjectLabel == nullptr)
        return;
    glObjectLabel(identifier, object, static_cast<int>(name.size()), name.data());
}

ScopedGroup::ScopedGroup(std::string_view name)
{
    if (glad_glPushDebugGroup == nullptr)
        return;
    glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, static_cast<int>(name.size()), name.data());
    m_active = true;
}

ScopedGroup::~ScopedGroup()
{
    if (m_active && glad_glPopDebugGroup != nullptr)
        glPopDebugGroup();
}

} // namespace engine::gl_debug
