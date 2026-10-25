#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace dbc::studio
{
    namespace panels
    {
        // Project file tree. Replaces ProjectPanel + QFileSystemModel.
        class ProjectPanel
        {
          public:
            using FileHandler = std::function<void(const std::filesystem::path&)>;

            void SetRoot(const std::filesystem::path& Root);
            void SetDbProProject(const std::optional<std::filesystem::path>& Descriptor,
                                 std::vector<std::filesystem::path> OrderedSources, std::filesystem::path AssetRoot);
            void Refresh();
            void Draw();

            void SetOnActivate(FileHandler Handler)
            {
                OnActivate_ = std::move(Handler);
            }

            const std::filesystem::path& Root() const
            {
                return Root_;
            }

          private:
            struct Node
            {
                std::filesystem::path Path;
                std::string Name;
                bool Directory = false;
                std::vector<Node> Children;
                bool Scanned = false;
            };

            static void ScanDirectory(Node& NodeValue);
            void DrawNode(Node& NodeValue);

            std::filesystem::path Root_;
            Node Tree_;
            std::optional<std::filesystem::path> Descriptor_;
            std::vector<std::filesystem::path> OrderedSources_;
            std::filesystem::path AssetRoot_;

            FileHandler OnActivate_;
        };
    } // namespace panels

} // namespace dbc::studio
