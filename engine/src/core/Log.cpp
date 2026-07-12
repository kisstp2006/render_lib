#include "engine/core/Log.h"

#include <cstdio>

namespace engine::log {

void Write(Level level, const std::string& message)
{
    const char* prefix = "[info] ";
    FILE* stream = stdout;

    switch (level)
    {
        case Level::Info:  prefix = "[info] ";  stream = stdout; break;
        case Level::Warn:  prefix = "[warn] ";  stream = stdout; break;
        case Level::Error: prefix = "[error] "; stream = stderr; break;
    }

    std::fprintf(stream, "%s%s\n", prefix, message.c_str());
}

} // namespace engine::log
