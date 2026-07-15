#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace engine::log {

enum class Level { Info, Warn, Error };

struct Record
{
    Level Severity = Level::Info;
    std::string Category;
    std::string Message;
    std::chrono::system_clock::time_point Timestamp;
    std::thread::id Thread;
    uint64_t Sequence = 0;
};

using Sink = std::function<void(const Record&)>;
using SinkId = uint64_t;

SinkId AddSink(Sink sink);
bool RemoveSink(SinkId sink);

void Write(Level level, const std::string& message);
void Write(Level level, std::string_view category, std::string message);

inline void Info(const std::string& message) { Write(Level::Info, message); }
inline void Warn(const std::string& message) { Write(Level::Warn, message); }
inline void Error(const std::string& message) { Write(Level::Error, message); }
inline void Info(std::string_view category, std::string message)
{
    Write(Level::Info, category, std::move(message));
}
inline void Warn(std::string_view category, std::string message)
{
    Write(Level::Warn, category, std::move(message));
}
inline void Error(std::string_view category, std::string message)
{
    Write(Level::Error, category, std::move(message));
}

} // namespace engine::log
