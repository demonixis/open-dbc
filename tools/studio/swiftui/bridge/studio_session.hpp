#pragma once

// The state behind DBCStudio: what the Dear ImGui Studio keeps in its App, without its panels.

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "core/asset_library.hpp"
#include "core/project_settings.hpp"
#include "core/recovery.hpp"
#include "core/reference_archive.hpp"
#include "core/settings.hpp"
#include "core/snippet_library.hpp"
#include "core/workspace.hpp"
#include "dba/command_database.hpp"
#include "dba/reference.hpp"
#include "dba/source_symbols.hpp"
#include "interpreter/project_resolver.hpp"
#include "panels/scene_editor.hpp"
#include "studio_forms.hpp"
#include "toolchain/execution_adapter.hpp"
#include "toolchain/icon_generator.hpp"

#import <Foundation/Foundation.h>

@class DBCStudio;

namespace dbc::studio::swiftui
{
    inline NSString* ToNs(std::string_view Text)
    {
        NSString* Value = [[NSString alloc] initWithBytes:Text.data() length:Text.size() encoding:NSUTF8StringEncoding];
        return Value != nil ? Value : @"";
    }

    inline NSString* ToNs(const std::string& Text)
    {
        return ToNs(std::string_view(Text));
    }

    inline NSString* ToNs(const char* Text)
    {
        return ToNs(std::string_view(Text != nullptr ? Text : ""));
    }

    inline NSString* ToNs(const std::filesystem::path& Path)
    {
        return ToNs(std::string_view(Path.native()));
    }

    inline std::string FromNs(NSString* Text)
    {
        return Text != nil ? std::string(Text.UTF8String) : std::string();
    }

    inline std::filesystem::path PathFromNs(NSString* Text)
    {
        return std::filesystem::path(FromNs(Text));
    }

    struct Session
    {
        core::Settings Settings;
        core::ProjectSettings ProjectSettings;
        core::Workspace Workspace;

        std::filesystem::path Root;
        std::optional<dbc::interpreter::ResolvedProject> Resolved;
        dbc::interpreter::ProjectResolveResult Pending;
        std::filesystem::path PendingTarget;
        std::optional<dbc::language::LanguageProfile> PendingProfile;
        bool DialectLocked = false;

        toolchain::ExecutionAdapter Execution;
        toolchain::RunOptions RunOptions;
        toolchain::IconGenerator IconGenerator;
        bool Busy = false;
        // The window a running program draws in, over the interface.
        SDL_Window* RunWindow = nullptr;
        bool InBackground = false;

        core::Recovery Recovery;
        std::vector<core::RecoveryCopy> RecoveryCopies;
        std::string RecoveryError;
        Uint64 NextRecovery = 0;

        dba::Reference Reference;
        dba::CommandDatabase Commands;
        // The names of the program in the current tab, for completion.
        dba::SourceSymbols Symbols;
        bool HasSymbols = false;
        core::ReferenceArchive Documentation;
        std::vector<core::Snippet> Snippets;
        std::vector<core::LibraryTexture> Library;

        // The Scene mode. The window is never shown: its Metal view sits in the interface.
        SDL_Window* ViewWindow = nullptr;
        SDL_GPUDevice* Gpu = nullptr;
        bool ImGuiReady = false;
        panels::SceneEditor Scene;
        bool SceneMode = false;
        bool SystemLight = false;
        // The forms as they were last built, which is what a field number names.
        std::vector<Field> Forms[4];
        // A value is being dragged in a form: the edit is not an undo step yet.
        bool FormEditing = false;

        std::string Status;

        dbc::language::LanguageProfile Profile() const
        {
            return Resolved ? Resolved->Profile : dbc::language::LanguageProfile::Dbc;
        }
    };

    // The one session, owned by DBCStudio.
    Session& TheSession();
    DBCStudio* TheStudio();

    // Shared by DBCStudio.mm and DBCScene.mm.
    void AppendOutput(const std::string& Line, int Severity);
    void SetStatus(const std::string& Status);
    void NotifySceneChanged();
    std::optional<std::string> ReadSource(const std::filesystem::path& Path);
    bool WriteSource(const std::filesystem::path& Path, const std::string& Text, bool Generated);
    void OpenFile(const std::filesystem::path& File);
    // Why the Scene mode cannot open: an English source text, or empty.
    std::string SceneBlocker();
    void UpdateSceneProject();
    // Creates the GPU device and the ImGui context the viewport draws with, on first use.
    bool EnsurePresentation();
} // namespace dbc::studio::swiftui
