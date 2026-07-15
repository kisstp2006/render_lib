#include "engine/core/Log.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace engine::log {

namespace
{

std::mutex g_logMutex;
std::condition_variable g_sinkIdle;
struct SinkEntry
{
    SinkId Id = 0;
    Sink Callback;
    size_t ActiveCallbacks = 0;
    bool Removed = false;
};
std::vector<std::shared_ptr<SinkEntry>> g_sinks;
std::atomic<uint64_t> g_nextSink{1};
std::atomic<uint64_t> g_sequence{1};
thread_local bool g_dispatchingSink = false;
thread_local SinkId g_currentSink = 0;

} // namespace

SinkId AddSink(Sink sink)
{
    if (!sink)
        return 0;
    const SinkId id = g_nextSink.fetch_add(1, std::memory_order_relaxed);
    std::scoped_lock lock(g_logMutex);
    g_sinks.push_back(std::make_shared<SinkEntry>(SinkEntry{id, std::move(sink)}));
    return id;
}

bool RemoveSink(SinkId sink)
{
    if (sink == 0)
        return false;
    std::unique_lock lock(g_logMutex);
    const auto found = std::find_if(g_sinks.begin(), g_sinks.end(),
        [sink](const std::shared_ptr<SinkEntry>& entry) {
            return entry->Id == sink;
        });
    if (found == g_sinks.end())
        return false;
    const std::shared_ptr<SinkEntry> entry = *found;
    entry->Removed = true;
    g_sinks.erase(found);
    // Removing a sink from inside its own callback is valid and cannot wait
    // for itself. All external removals are a synchronization point: after
    // return, no worker thread can still call into the receiver.
    if (g_currentSink != sink)
        g_sinkIdle.wait(lock, [&] { return entry->ActiveCallbacks == 0; });
    return true;
}

void Write(Level level, const std::string& message)
{
    Write(level, "Engine", message);
}

void Write(Level level, std::string_view category, std::string message)
{
    Record record;
    record.Severity = level;
    record.Category = category.empty() ? "Engine" : std::string(category);
    record.Message = std::move(message);
    record.Timestamp = std::chrono::system_clock::now();
    record.Thread = std::this_thread::get_id();
    record.Sequence = g_sequence.fetch_add(1, std::memory_order_relaxed);

    std::vector<std::shared_ptr<SinkEntry>> sinks;
    {
        std::scoped_lock lock(g_logMutex);
        sinks.reserve(g_sinks.size());
        sinks = g_sinks;
    }

    const char* prefix = "[info] ";
    FILE* stream = stdout;

    switch (level)
    {
        case Level::Info:  prefix = "[info] ";  stream = stdout; break;
        case Level::Warn:  prefix = "[warn] ";  stream = stdout; break;
        case Level::Error: prefix = "[error] "; stream = stderr; break;
    }

    {
        std::scoped_lock lock(g_logMutex);
        if (record.Category == "Engine")
            std::fprintf(stream, "%s%s\n", prefix, record.Message.c_str());
        else
            std::fprintf(stream, "%s[%s] %s\n", prefix, record.Category.c_str(),
                         record.Message.c_str());
        std::fflush(stream);
    }

    if (g_dispatchingSink)
        return;
    g_dispatchingSink = true;
    for (const std::shared_ptr<SinkEntry>& sink : sinks)
    {
        {
            std::scoped_lock lock(g_logMutex);
            if (sink->Removed)
                continue;
            ++sink->ActiveCallbacks;
        }
        g_currentSink = sink->Id;
        try
        {
            sink->Callback(record);
        }
        catch (...)
        {
            // A console or telemetry sink must not break failure logging.
        }
        g_currentSink = 0;
        {
            std::scoped_lock lock(g_logMutex);
            --sink->ActiveCallbacks;
        }
        g_sinkIdle.notify_all();
    }
    g_dispatchingSink = false;
}

} // namespace engine::log
