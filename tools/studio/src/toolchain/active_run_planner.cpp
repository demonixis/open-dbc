#include "active_run_planner.hpp"

#include <utility>

#include "language/language_profile.hpp"

namespace dbc::studio
{
    namespace toolchain
    {
        ActiveRunPlan PlanActiveRun(const std::filesystem::path& ActivePath,
                                    const std::optional<dbc::interpreter::ResolvedProject>& Project,
                                    std::vector<MemorySource> Sources)
        {
            ActiveRunPlan Plan;
            if (ActivePath.empty())
            {
                Plan.Error = "Save the active document as a .dba file before running.";
                return Plan;
            }
            if (!dbc::language::IsLanguageSource(ActivePath))
            {
                Plan.Error = "The active document is not a .dba BASIC source.";
                return Plan;
            }
            if (!Project)
            {
                Plan.Error = "Select DBC or DBPro before running the active document.";
                return Plan;
            }

            dbc::interpreter::ResolvedProject RunProject = *Project;
            RunProject.InputPath = ActivePath;
            RunProject.EntrySource = ActivePath;
            RunProject.OrderedSources = {ActivePath};
            if (!RunProject.DescriptorPath)
            {
                RunProject.InputKind = dbc::interpreter::ProjectInputKind::Source;
                RunProject.ProjectRoot = ActivePath.parent_path();
                RunProject.AssetRoot = ActivePath.parent_path();
            }
            if (RunProject.AssetRoot.empty())
                RunProject.AssetRoot = ActivePath.parent_path();

            RunDocument Document;
            Document.LogicalPath = ActivePath;
            Document.Profile = RunProject.Profile;
            Document.Sources = std::move(Sources);
            Document.Project = std::move(RunProject);

            Plan.AssetRoot = Document.Project->AssetRoot;
            Plan.Blockers = Document.Project->RunBlockers;
            Plan.Document = std::move(Document);
            return Plan;
        }
    } // namespace toolchain

} // namespace dbc::studio
