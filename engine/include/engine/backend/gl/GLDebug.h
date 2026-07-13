#pragma once

#include <string_view>

namespace engine::gl_debug
{

bool IsAvailable();
void LabelObject(unsigned int identifier, unsigned int object, std::string_view name);

class ScopedGroup
{
  public:
    explicit ScopedGroup(std::string_view name);
    ~ScopedGroup();
    ScopedGroup(const ScopedGroup&) = delete;
    ScopedGroup& operator=(const ScopedGroup&) = delete;

  private:
    bool m_active = false;
};

} // namespace engine::gl_debug
