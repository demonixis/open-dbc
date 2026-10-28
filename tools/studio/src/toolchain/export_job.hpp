#pragma once

#include "artifact_export_service.hpp"

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace dbc::studio
{
    namespace toolchain
    {
        // Runs one export at a time on a worker thread, so Gradle or notarization never blocks the
        // Studio. The UI thread drains the log and the result each frame.
        class ExportJob
        {
          public:
            ExportJob() = default;
            ExportJob(const ExportJob&) = delete;
            ExportJob& operator=(const ExportJob&) = delete;
            ~ExportJob();

            // False when an export is already running.
            bool Start(ArtifactExportRequest Request);
            void Cancel();
            // Cancels a running export and waits for its tools to stop.
            void Shutdown();
            bool Running() const;

            std::vector<std::string> TakeLines();
            // Set once the worker finished; empty while it runs and after it was taken.
            std::optional<ArtifactExportResult> TakeResult();

          private:
            mutable std::mutex Mutex_;
            std::vector<std::string> Lines_;
            std::optional<ArtifactExportResult> Result_;
            std::atomic<bool> Cancel_{false};
            std::atomic<bool> Running_{false};
            std::jthread Worker_;
        };
    } // namespace toolchain

} // namespace dbc::studio
