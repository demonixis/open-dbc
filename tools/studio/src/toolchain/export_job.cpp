#include "export_job.hpp"

#include <utility>

namespace dbc::studio
{
    namespace toolchain
    {
        ExportJob::~ExportJob()
        {
            Shutdown();
        }

        bool ExportJob::Start(ArtifactExportRequest Request)
        {
            if (Running_.exchange(true))
                return false;
            // The previous worker has posted its result, so joining it does not wait.
            if (Worker_.joinable())
                Worker_.join();
            Cancel_.store(false);
            {
                const std::lock_guard Lock(Mutex_);
                Lines_.clear();
                Result_.reset();
            }
            Worker_ = std::jthread(
                [this, Request = std::move(Request)]
                {
                    ArtifactExportProgress Progress;
                    Progress.Cancel = &Cancel_;
                    Progress.Log = [this](std::string_view Line)
                    {
                        const std::lock_guard Lock(Mutex_);
                        Lines_.emplace_back(Line);
                    };
                    ArtifactExportResult Result = ArtifactExportService().Export(Request, Progress);
                    const std::lock_guard Lock(Mutex_);
                    Result_ = std::move(Result);
                    Running_.store(false);
                });
            return true;
        }

        void ExportJob::Cancel()
        {
            if (Running_.load())
                Cancel_.store(true);
        }

        void ExportJob::Shutdown()
        {
            Cancel();
            if (Worker_.joinable())
                Worker_.join();
        }

        bool ExportJob::Running() const
        {
            return Running_.load();
        }

        std::vector<std::string> ExportJob::TakeLines()
        {
            const std::lock_guard Lock(Mutex_);
            return std::exchange(Lines_, {});
        }

        std::optional<ArtifactExportResult> ExportJob::TakeResult()
        {
            const std::lock_guard Lock(Mutex_);
            return std::exchange(Result_, std::nullopt);
        }
    } // namespace toolchain

} // namespace dbc::studio
