#pragma once
#include "synchrorep/engine.hpp"
#include <chrono>

namespace synchrorep {
struct MirrorMonitorSettings {
 std::chrono::milliseconds poll_interval{500};
 std::chrono::seconds reconcile_interval{60};
};
// Runs in the calling thread, until cancellation. Never background-detaches.
// Fast filesystem metadata checks trigger complete SHA-256 reconciliation.
// Periodic reconciliation also detects equal-size/mtime changes and remote drift.
class MirrorMonitor {
public:
 explicit MirrorMonitor(MirrorMonitorSettings settings={}):settings_(settings){}
 Result run(Endpoint& source,Endpoint& target,Engine& engine,
            const fs::path& local_directory,const RunCallbacks& callbacks={})const;
private:
 MirrorMonitorSettings settings_;
};
}
