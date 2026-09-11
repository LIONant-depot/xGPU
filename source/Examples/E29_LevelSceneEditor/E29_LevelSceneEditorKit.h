#ifndef E29_LEVEL_SCENE_EDITOR_KIT_H
#define E29_LEVEL_SCENE_EDITOR_KIT_H
#pragma once

#include "source/xGPU.h"

#include "dependencies/xmath/source/xmath.h"
#include "dependencies/xproperty/source/xcore/my_properties.h"
#include "dependencies/xproperty/source/examples/imgui/xPropertyImGuiInspector.h"
#include "dependencies/xstrtool/source/xstrtool.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <functional>
#include <unordered_set>
#include <cctype>
#include <cstring>

// xECSV2 - real Level/Scene resource types (see dependencies/xECSV2/src/xecs_level*.h,
// xecs_scene*.h) and the entity/component machinery this editor edits. Included BEFORE the
// resource-pipeline/asset-browser headers below so xecs.h's own narrow xresource_pipeline_v2
// includes (descriptor_base/info/factory/version - all #pragma once) are the ones that win; the
// asset-browser's own xresource_pipeline.h umbrella pull of the same four headers becomes a no-op.
#include "dependencies/xECSV2/src/xecs.h"

#define XRESOURCE_PIPELINE_NO_COMPILER
#include "dependencies/xresource_pipeline_v2/source/xresource_pipeline.h"
#include "source/Examples/E10_TextureResourcePipeline/E10_Resources.h"
#include "source/Examples/E10_TextureResourcePipeline/E10_AssetMgr.h"
#include "source/Examples/E10_TextureResourcePipeline/E10_AssetBrowser.h"

//-----------------------------------------------------------------------------------
//
// E29 Level/Scene Editor KIT - everything from the original, single-file E29 example that isn't
// specific to ITS particular demo content (its own `transform` starter component, its own
// window/device bring-up in E29_Example()). Split out so a FUTURE editor wanting the same Level
// tree UI, prefab authoring/instancing, or entity-reference/override inspector wiring can reuse it
// directly instead of re-deriving it - matching this codebase's own existing convention of a
// reusable, header-only "kit" per concern (E10_AssetBrowser.h/E10_asset_browser_virtual_tree_tab.h
// are the model this follows: namespace named after the example that first grew the feature, reused
// by many later examples despite the number in the namespace name).
//
// Layout, in order: error-popup mechanism, the shared `name` component (the tree/prefab machinery
// below needs SOME component to label entities with - promoted here from "just E29's own demo
// content" to "part of the kit" for exactly that reason), resource-picker glue, editor_state,
// id-minting, folder bookkeeping, scene open/close + dependency-cycle guard, SaveEverything, and the
// entity_inspector_bridge (prefab-override + entity-reference inspector callback wiring).
// Two clusters that used to live inline in this same file now live under kit/, pulled in via
// #include at the exact point they used to appear (this file remains the one umbrella header a
// caller includes - see each moved file's own top comment for why it isn't meant to stand alone):
// prefab lookup/override bookkeeping + prefab creation/instancing/deletion/drag-drop
// (E29_PrefabOverrides.h, E29_PrefabAuthoring.h - phase 2 of the kit split), and the three big UI
// panels, Level tree/Entity Properties/System Registry (E29_Panel_*.h - phase 1).
//
//-----------------------------------------------------------------------------------

namespace e29
{
    // Debugger's own console output is invisible from inside the running app - every refusal/failure
    // that only ever went through it (load failures, the circular-dependency-cycle refusal, ...)
    // silently did nothing from the USER's own point of view, direct report: "it fails silently...
    // the user may be confused of why". These two file-static globals plus the modal render block
    // right after Debugger's own definition are a minimal, app-wide fix - every EXISTING Debugger(...)
    // call site benefits for free, not just newly-added ones.
    static std::string g_LastErrorMessage;
    static bool        g_bOpenErrorPopup = false;

    static void Debugger(std::string_view View)
    {
        // Flushed unconditionally - stdout redirected to a file is fully buffered rather than
        // line-buffered, so without this, a crash (or a debug-assert dialog that blocks the process
        // indefinitely) silently loses whatever log lines hadn't been flushed yet - exactly the
        // "can't tell what happened right before the crash" gap that makes these bugs hard to chase.
        printf("%s\n", View.data());
        fflush(stdout);

        // Only the FLAG is set here, not ImGui::OpenPopup itself - Debugger is called from arbitrary,
        // often deeply-nested ID-stack contexts (mid-drag-drop, inside per-row PushID blocks, ...),
        // and OpenPopup(str_id) hashes its id against whatever ID stack is CURRENTLY active - calling
        // it here would give it a different internal id than the BeginPopupModal call below (made
        // from the main loop's own top-level, unnested scope), so the popup would silently never
        // actually open. RenderErrorPopup (called once per frame from that same top-level scope)
        // is the only place that ever calls OpenPopup, one frame later - by which point whatever
        // drag/drop or click triggered this Debugger call has already fully finished processing for
        // its own frame, so a real MODAL (blocks input, dims the background - "very obvious", direct
        // user request after trying the first, input-transparent toast version) can't ever eat an
        // in-flight mouse release.
        g_LastErrorMessage = std::string(View);
        g_bOpenErrorPopup  = true;
    }

    // Opens/renders the modal popup for the most recent Debugger(...) message, if any - called once
    // per frame from the main loop, right after BeginRendering, from the SAME top-level ID-stack
    // scope every frame (required for BeginPopupModal to ever actually find the popup OpenPopup
    // requested - see Debugger's own comment for why the two calls must share that scope).
    static void RenderErrorPopup() noexcept
    {
        if (g_bOpenErrorPopup)
        {
            ImGui::OpenPopup("Error##E29");
            g_bOpenErrorPopup = false;
        }

        ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Error##E29", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 400.0f);
            ImGui::TextUnformatted(g_LastErrorMessage.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Separator();
            if (ImGui::Button("OK", ImVec2(120.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Escape))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    //---------------------------------------------------------------------------
    // Shared starter component - every entity the Level tree/prefab machinery below labels, names,
    // or searches by relies on THIS component being present (see e.g. ResolveEntityReference,
    // CreatePrefabFromGroupRoot, DetermineGroupRoot's synthetic-root naming, the tree row label
    // logic) - promoted from "just E29's own demo content" into the kit itself for that reason. A
    // future editor built on this kit registers it exactly like any other component
    // (GameMgr.RegisterComponents<e29::name, ...>()).
    //---------------------------------------------------------------------------

    struct name
    {
        constexpr static auto typedef_v = xecs::component::type::data{ .m_pName = "Name" };

        std::string m_Value = "Entity";

        XPROPERTY_DEF
        ( "Name", name
        , obj_member<"Value", &name::m_Value>
        )
    };
    XPROPERTY_REG(name)

    //---------------------------------------------------------------------------
    // Resource-picker wiring - same trio every editor with a resource-ref property carries its own
    // copy of (see E24_AnimPackage_Editor.cpp's identical RemapGUIDToString/RenderResourceWigzmos/
    // ResourceBrowserPopup). Used both by the shared inspectors' delegates and directly by the
    // Scene panel's own "Parent Scenes" row list.
    //---------------------------------------------------------------------------

    void RemapGUIDToString(std::string& Out, const xresource::full_guid& PreFullGuid)
    {
        if (PreFullGuid.empty())
        {
            Out = "(none)";
            return;
        }

        auto FullGuid = xresource::g_Mgr.getFullGuid(PreFullGuid);
        Out.clear();
        e10::g_LibMgr.getNodeInfo(FullGuid, [&](e10::library_db::info_node& Node) { Out = Node.m_Info.m_Name; });
        if (Out.empty()) Out = std::format("{:X}", FullGuid.m_Instance.m_Value);
    }

    void RenderResourceWigzmos(bool& bOpen, const xresource::full_guid& PreFullGuid)
    {
        std::string Name;
        RemapGUIDToString(Name, PreFullGuid);
        bOpen = ImGui::Button(Name.c_str(), ImVec2(-1, 0));
    }

    e10::assert_browser g_AssetBrowserPopup;

    // NOTE: only safe to call with an `Open` that is a genuinely FRESH per-frame local (e.g. declared
    // inside a loop body, or an inspector row's own transient state) - never a persistent member
    // variable. g_AssetBrowserPopup.RenderAsPopup() (called once, early, each frame) already closes
    // the popup and clears its owner id when the user hits its own Close button; if `Open` is a
    // persistent flag that nothing else resets, the very next line below (`if (Open && not
    // isVisible())`) misreads that as a fresh open request and reopens it immediately - an instant,
    // permanent close/reopen loop with no way for the user to actually close it. Call sites that need
    // to track "please open" across frames (e.g. a tree row's own "+" button) should call
    // ShowAsPopup(...) directly on the click itself instead of routing through this function.
    void ResourceBrowserPopup(const void* pUID, bool& Open, xresource::full_guid& Output, std::span<const xresource::type_guid> Filters)
    {
        if (g_AssetBrowserPopup.getCurrentID() != nullptr && g_AssetBrowserPopup.getCurrentID() != pUID)
            return;

        if (Open && not g_AssetBrowserPopup.isVisible())
            g_AssetBrowserPopup.ShowAsPopup(e10::g_LibMgr, pUID, Filters, Output.m_Type);

        if (auto SelectedAsset = g_AssetBrowserPopup.getSelectedAsset(); SelectedAsset.empty() == false)
        {
            for (auto& Type : Filters)
                if (SelectedAsset.m_Type == Type) { Output = SelectedAsset; break; }
        }

        Open = g_AssetBrowserPopup.isVisible();
    }

    // Registers the two stateless resource-picker delegates (m_OnResourceWigzmos/m_OnResourceBrowser)
    // on an entity/component inspector - identical wiring every editor with a resource-ref property
    // needs, extracted here so it's one call instead of re-typing both Register<...> lambdas per
    // editor.
    inline void WireResourcePickerCallbacks(xproperty::inspector& Inspector) noexcept
    {
        Inspector.m_OnResourceWigzmos.Register<[](xproperty::inspector&, const xproperty::type::object&, void*, std::string_view, bool& bOpen, const xresource::full_guid& PreFullGuid)
        {
            e29::RenderResourceWigzmos(bOpen, PreFullGuid);
        }>();
        Inspector.m_OnResourceBrowser.Register<[](xproperty::inspector&, const xproperty::type::object&, void*, std::string_view Path, bool& bOpen, xresource::full_guid& Out, std::span<const xresource::type_guid> Filters)
        {
            const void* pUID = reinterpret_cast<const void*>(std::hash<std::string_view>{}(Path));
            e29::ResourceBrowserPopup(pUID, bOpen, Out, Filters);
        }>();
    }

    //---------------------------------------------------------------------------
    // Editor state - which Level/Scene (if any) is currently open, and which entity (if any) is
    // currently selected for component editing. Level/Scene instances themselves live inside
    // GameMgr.m_LevelMgr/m_SceneMgr - this just remembers which guid to Find() each frame.
    //---------------------------------------------------------------------------

    struct editor_state
    {
        xecs::level::guid   m_CurrentLevel  = {};

        // Every Scene the user has opened stays resident/expanded until removed from the Level -
        // opening one no longer closes any other (previously OpenScene force-closed "the" current
        // scene first; the user explicitly wants all of them open at once if they choose to).
        std::vector<xecs::scene::guid>  m_OpenScenes;

        xecs::scene::permanent_id  m_SelectedEntityId    = xecs::scene::invalid_permanent_id_v;
        xecs::component::entity    m_SelectedEntity      = {};
        xecs::scene::guid          m_SelectedEntityScene = {}; // which OPEN scene m_SelectedEntity belongs to

        bool m_bEntityInspectorDirty = true;

        std::string m_TreeSearchString;

        // Ctrl-click toggle set, separate from the "primary" selection triad above (which still only
        // ever drives the Properties panel - ctrl-clicking never touches it). Only meaningful for
        // "Make Prefab" acting on a group; scoped to ONE scene at a time (a prefab's members must all
        // come from the same live scene to walk children/read components together) - a plain click
        // (no modifier) anywhere clears this set, matching common editor convention.
        std::unordered_set<xecs::scene::permanent_id>  m_MultiSelectedEntityIds;
        // Same membership as m_MultiSelectedEntityIds, but in actual click order (a plain unordered_set
        // has no defined iteration order at all - not even insertion order) - DetermineGroupRoot uses
        // this to find "the first entity the user actually selected", e.g. to inherit ITS folder/parent
        // for a synthetic group root, per direct user request. Kept in lockstep with
        // m_MultiSelectedEntityIds at every mutation site rather than derived from it.
        std::vector<xecs::scene::permanent_id>         m_MultiSelectOrder;
        xecs::scene::guid                              m_MultiSelectScene;

        // Stopped: editing normally, GameMgr.Run() never called. Playing: ticking every frame.
        // Paused: a live play session (world stays exactly as it is, Stop will still revert it) but
        // GameMgr.Run() is NOT called this frame - matches Unity's own Play/Pause/Stop transport,
        // and is also what lets a code-edit reload happen "at rest" mid-session without losing
        // anything (see E29_GamePlugin.h's PollGameReload, which treats Playing and Paused
        // identically - both are "a play session is live").
        enum class play_state : std::uint8_t { Stopped, Playing, Paused };

        // See RenderSystemRegistryPanel's own comment for why this matters beyond just "is the game
        // ticking": it's also what that panel checks to decide whether an enable/reorder edit is a
        // permanent, persisted change or a transient one that GameMgr.Stop()'s own
        // xecs::system::mgr::RestoreFromSnapshot() will discard.
        play_state m_PlayState = play_state::Stopped;
        bool isPlaying() const noexcept { return m_PlayState != play_state::Stopped; }

        // Set by the "Play" button (Stopped -> Playing only - Paused -> Playing is just a resume,
        // no recompile-check needed) and consumed by PollGameReload once the recompile-check it
        // kicks off resolves - Play must never actually start ticking against a DLL that might still
        // be mid-rebuild. See PollGameReload's own comment for the full sequencing.
        bool m_bPlayRequested = false;

        // Set by the "Stop" button; consumed at the same clean top-of-frame point PollGameReload
        // runs from, never at the point of the click itself - see the button's own comment in
        // E29_LevelScene_Editor.cpp for why (StopPlaySession does the same heavy destroy/recreate
        // work a reload does, and the click happens nested inside an active ImGui menu-bar scope).
        bool m_bStopRequested = false;
    };

    // GUID-like rather than sequential (was "Max + 1"): a random id means two branches each creating
    // an unrelated new entity independently essentially never end up minting the same permanent_id, so
    // merging their scene folders afterward doesn't collide two different entities onto one file. Only
    // needs to be unique WITHIN this one scene (checked below) - not globally across all history, so a
    // folded-down 32-bit value is enough entropy for that; xresource::guid_generator::Instance64()
    // already mixes timestamp/thread/machine/random bits, reused here rather than inventing a second
    // id-generation scheme.
    xecs::scene::permanent_id NextFreeEntityId(xecs::scene::instance& Scene) noexcept
    {
        for(;;)
        {
            const auto     Full = xresource::guid_generator::Instance64();
            const auto     Id   = static_cast<xecs::scene::permanent_id>( (Full >> 32) ^ (Full & 0xFFFFFFFFull) );
            if( Id == xecs::scene::invalid_permanent_id_v )                    continue;
            if( Scene.m_LocalToRuntime.contains(Id) )                          continue;
            return Id;
        }
    }

    // Same GUID-like scheme as NextFreeEntityId, checked against the scene's own folder ids instead
    // of its entities - a separate id space, but the same merge-collision reasoning applies.
    xecs::scene::folder_id NextFreeFolderId(xecs::scene::instance& Scene) noexcept
    {
        for(;;)
        {
            const auto Full = xresource::guid_generator::Instance64();
            const auto Id   = static_cast<xecs::scene::folder_id>( (Full >> 32) ^ (Full & 0xFFFFFFFFull) );
            if( Id == xecs::scene::invalid_folder_id_v ) continue;
            if( std::find_if(Scene.m_Folders.begin(), Scene.m_Folders.end(), [&](auto& F) noexcept { return F.m_Id == Id; }) != Scene.m_Folders.end() )
                continue;
            return Id;
        }
    }

    // Forward declarations - FindFolderContaining/PruneEmptyFolderChain are defined below, but
    // ReparentEntityIntoFolder (the one place any entity ever LEAVES a folder) needs both to detect
    // and clean up a folder left empty by that departure.
    xecs::scene::folder_id FindFolderContaining(xecs::scene::instance& Scene, xecs::scene::permanent_id Id) noexcept;
    void PruneEmptyFolderChain(xecs::scene::instance& Scene, xecs::scene::folder_id Id) noexcept;

    // Moves Id into TargetFolder (invalid_folder_id_v = "loose", no folder), removing it from
    // whichever folder currently lists it first - folders own their membership by containment (see
    // xecs_scene.h's folder comment), so "reparent" is just "erase from the old owner, append to the
    // new one" rather than updating any per-entity back-pointer. Whatever folder Id is leaving gets
    // pruned afterward if that departure left it (and, cascading upward, any now-childless ancestor
    // folder) completely empty - direct user report of "fake/empty folders" accumulating, e.g. every
    // time an entity is deleted out of the last-remaining folder that held it.
    void ReparentEntityIntoFolder(xecs::scene::instance& Scene, xecs::scene::permanent_id Id, xecs::scene::folder_id TargetFolder) noexcept
    {
        const auto OldFolder = FindFolderContaining(Scene, Id);

        for( auto& F : Scene.m_Folders )
            std::erase(F.m_Entities, Id);

        if( OldFolder != xecs::scene::invalid_folder_id_v )
            PruneEmptyFolderChain(Scene, OldFolder);

        if( TargetFolder == xecs::scene::invalid_folder_id_v ) return;
        if( auto It = std::find_if(Scene.m_Folders.begin(), Scene.m_Folders.end(), [&](auto& F) noexcept { return F.m_Id == TargetFolder; }); It != Scene.m_Folders.end() )
            It->m_Entities.push_back(Id);
    }

    // Which folder (if any) currently lists Id as a member - invalid_folder_id_v if Id is loose or
    // parented (folders own membership by containment, so this is a linear scan, not a lookup).
    xecs::scene::folder_id FindFolderContaining(xecs::scene::instance& Scene, xecs::scene::permanent_id Id) noexcept
    {
        if( auto It = std::find_if(Scene.m_Folders.begin(), Scene.m_Folders.end(), [&](auto& F) noexcept { return std::find(F.m_Entities.begin(), F.m_Entities.end(), Id) != F.m_Entities.end(); }); It != Scene.m_Folders.end() )
            return It->m_Id;
        return xecs::scene::invalid_folder_id_v;
    }

    // Deletes Id if it's now completely empty (no member entities AND no child folder still parented
    // under it), then repeats for its own parent, walking up until a non-empty/non-childless folder
    // is hit or the root is reached - keeps the tree free of folders left behind purely because
    // whatever used to justify their existence (an entity, a now-pruned child folder) is gone.
    void PruneEmptyFolderChain(xecs::scene::instance& Scene, xecs::scene::folder_id Id) noexcept
    {
        while( Id != xecs::scene::invalid_folder_id_v )
        {
            auto It = std::find_if(Scene.m_Folders.begin(), Scene.m_Folders.end(), [&](auto& F) noexcept { return F.m_Id == Id; });
            if( It == Scene.m_Folders.end() ) return;

            if( It->m_Entities.empty() == false ) return;

            const bool bHasChildFolder = std::any_of(Scene.m_Folders.begin(), Scene.m_Folders.end(), [&](auto& F) noexcept { return F.m_Parent == Id; });
            if( bHasChildFolder ) return;

            const auto ParentId = It->m_Parent;
            Scene.m_Folders.erase(It);
            Id = ParentId;
        }
    }

    // ShowCreateMenuItems moved further down in this file (right after E29_PrefabAuthoring.h's own
    // include) - phase 4's own [[e29_command_undo_system_plan]] routing needs e29::g_pGameMgr/
    // g_pState (E29_PrefabAuthoring.h) and e29::commands::Run (E29_CommandContext.h), neither
    // available yet at this point in the file. Its only 2 callers (kit/E29_Panel_LevelTree.h) are
    // reached much later in the umbrella than either dependency, so moving it is a pure relocation -
    // see its own comment at the new location.

    // EnsureDefaultFolder (the auto-created "Default" bucket every unfoldered entity used to get
    // adopted into) removed entirely - direct user request. Unfoldered entities now render loose at
    // the scene root again (see kit/E29_Panel_LevelTree.h's own "Unfoldered" block), same as before
    // "Default" was introduced.

    // Folders are purely organizational - deleting one must never delete gameplay content. Its
    // member entities and any child folders are promoted up to ITS OWN parent (which may itself be
    // root/invalid) rather than being deleted or left dangling.
    void DeleteFolder(xecs::scene::instance& Scene, xecs::scene::folder_id Id) noexcept
    {
        auto It = std::find_if(Scene.m_Folders.begin(), Scene.m_Folders.end(), [&](auto& F) noexcept { return F.m_Id == Id; });
        if( It == Scene.m_Folders.end() ) return;

        const auto ParentId       = It->m_Parent;
        const auto EntitiesToMove = std::move(It->m_Entities);
        Scene.m_Folders.erase(It);

        for( auto& F : Scene.m_Folders )
            if( F.m_Parent == Id ) F.m_Parent = ParentId;

        if( ParentId != xecs::scene::invalid_folder_id_v )
        {
            if( auto ParentIt = std::find_if(Scene.m_Folders.begin(), Scene.m_Folders.end(), [&](auto& F) noexcept { return F.m_Id == ParentId; }); ParentIt != Scene.m_Folders.end() )
                for( auto EId : EntitiesToMove ) ParentIt->m_Entities.push_back(EId);
        }
        // ParentId == invalid: entities are already "loose" simply by not being listed anywhere.
    }

    // Same icon glyphs the asset browser's own virtual folder tree uses for a folder-with-children
    // vs. an empty one (E10_asset_browser_virtual_tree_tab.h) - reused verbatim rather than picking new
    // ones, per direct user request. Both come from the icon font already loaded once, globally, for
    // the whole app (source/Tools/xgpu_imgui_breach.cpp) - E29 shares that same font atlas already.
    const char* FolderIcon(bool bHasChildren) noexcept
    {
        return bHasChildren ? "\xEE\xA3\x95" : "\xEE\xA2\xB7";
    }

    // Level/Scene row icons - direct user request. Segoe MDL2 Assets "Globe"/"Video" codepoints,
    // distinct from FolderIcon's own pair above and from every icon already used elsewhere in this
    // app (E10_AssetBrowser.h's tab icons) - picked and confirmed via a live screenshot, not inferred
    // from font metadata (see e10_asset_tree_polish_pass2 memory for why metadata alone isn't
    // trustworthy for this specific font).
    constexpr const char* LevelIcon() noexcept { return "\xEE\x9D\xB4"; }
    constexpr const char* SceneIcon() noexcept { return "\xEE\xA4\x9B"; }

    bool ContainsCaseInsensitive(std::string_view Haystack, std::string_view Needle) noexcept
    {
        if (Needle.empty()) return true;
        auto It = std::search(Haystack.begin(), Haystack.end(), Needle.begin(), Needle.end(),
            [](char A, char B) noexcept { return std::tolower(static_cast<unsigned char>(A)) == std::tolower(static_cast<unsigned char>(B)); });
        return It != Haystack.end();
    }

    // Visually matches e10::assert_browser::RenderSearchBar (E10_AssetBrowser.h) - the magnifying-
    // glass placeholder icon, gray "X" to clear, rounded InputText - reimplemented standalone rather
    // than called directly since that method is bound to assert_browser's own m_SearchString member;
    // it's the VISUAL pattern being reused here, backed by E29's own tree-search state instead. Skips
    // the original's leading "▼" sort/filter-type dropdown button - there's no equivalent filter-type
    // concept for the Level tree, just a plain substring search.
    void RenderTreeSearchBar(std::string& SearchString, float AvailWidth) noexcept
    {
        std::array<char, 256> Buffer{};
        strcpy_s(Buffer.data(), Buffer.size(), SearchString.c_str());

        const auto StartX = ImGui::GetCursorPosX();
        if (Buffer[0] != 0)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            if (ImGui::SmallButton("X")) Buffer[0] = 0;
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 0.1f);
        }
        AvailWidth -= ImGui::GetCursorPosX() - StartX;

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f);
        ImGui::PushItemWidth(AvailWidth);
        ImGui::InputText("##TreeSearch", Buffer.data(), Buffer.size());
        const bool bActive  = ImGui::IsItemActive();
        const bool bHasText = (Buffer[0] != 0);
        if (!bActive && !bHasText)
        {
            const ImVec2 InputPos  = ImGui::GetItemRectMin();
            const ImVec2 CursorPos = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2(InputPos.x + 10.0f, InputPos.y + 4.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            ImGui::Text("\xee\x9c\xa1");
            ImGui::PopStyleColor();
            ImGui::SetCursorScreenPos(CursorPos);
        }
        ImGui::PopItemWidth();
        ImGui::PopStyleVar();

        SearchString = std::string_view(Buffer.data());
    }

    void OpenLevel(xecs::game_mgr::instance& GameMgr, editor_state& State, xresource::full_guid LevelGuid)
    {
        const xecs::level::guid Guid{ .m_Instance = LevelGuid.m_Instance };
        if (auto Err = GameMgr.m_LevelMgr.Load(Guid); Err)
        {
            Debugger(std::format("Failed to load Level: {}", Err.getMessage()));
            return;
        }
        State.m_CurrentLevel = Guid;

        // Every scene that's part of a Level is loaded automatically the moment the Level itself
        // opens - direct user request ("Scenes should always be loaded if they are part of the
        // level") - rather than requiring a separate click-to-open per scene. Activate (already in
        // the engine, xecs::level::mgr::Activate - "for now, activating a level just means
        // requesting every scene it owns") does the actual RequestLoad cascade; this just also keeps
        // State.m_OpenScenes in sync so the Level tree's own bIsOpenScene checks reflect it.
        if (auto* pLevel = GameMgr.m_LevelMgr.Find(Guid))
        {
            if (auto Err = GameMgr.m_LevelMgr.Activate(Guid); Err)
            {
                Debugger(std::format("Failed to activate Level (load its scenes): {}", Err.getMessage()));
                return;
            }
            for (auto& SceneGuid : pLevel->m_Scenes)
                if (std::find(State.m_OpenScenes.begin(), State.m_OpenScenes.end(), SceneGuid) == State.m_OpenScenes.end())
                    State.m_OpenScenes.push_back(SceneGuid);
        }
    }

    // Releases one specific scene's residency and removes it from State.m_OpenScenes - unlike the old
    // single-current-scene design, this never gets called implicitly when another scene opens; only
    // when a scene is explicitly removed from the Level (any number of OTHER scenes stay open).
    void CloseScene(xecs::game_mgr::instance& GameMgr, editor_state& State, xecs::scene::guid Guid) noexcept
    {
        auto It = std::find(State.m_OpenScenes.begin(), State.m_OpenScenes.end(), Guid);
        if (It == State.m_OpenScenes.end()) return;

        GameMgr.m_SceneMgr.ReleaseLoad(Guid);
        State.m_OpenScenes.erase(It);

        if (State.m_SelectedEntityScene == Guid)
        {
            State.m_SelectedEntityId    = xecs::scene::invalid_permanent_id_v;
            State.m_SelectedEntity      = {};
            State.m_SelectedEntityScene = {};
        }

        // The primary selection was already scrubbed above, but the MULTI-select set/order (a
        // completely separate pair of fields, see their own comment) never was - every id in it
        // belonged to a scene that just got fully unloaded, so a later Make-Prefab grouping could
        // silently pick up stale, now-nonexistent ids. Whole-scene close means every one of those ids
        // is gone regardless of which entity it was, so this just clears the set outright rather than
        // checking survival one id at a time.
        if (State.m_MultiSelectScene == Guid)
        {
            State.m_MultiSelectedEntityIds.clear();
            State.m_MultiSelectOrder.clear();
        }
    }

    // Would adding "NewDependency" to Candidate's own m_ParentScenes close a cycle in the scene
    // dependency graph? True iff Candidate is already (transitively) reachable FROM NewDependency by
    // walking m_ParentScenes edges - i.e. NewDependency already depends on Candidate, directly or
    // through some chain, so making Candidate ALSO depend on NewDependency would create a loop.
    // The engine's own Scene::EnsureLoaded already DETECTS a cycle at LOAD time ("Scene dependency
    // cycle detected"), but nothing stops one from being AUTHORED in the first place, which is a much
    // worse failure mode (the mistake surfaces later, as a load error, far from whichever
    // drag/assignment actually caused it). Only walks scenes that are CURRENTLY LOADED (via Find()) -
    // an unloaded scene's own m_ParentScenes can't be inspected without loading it, the same
    // pragmatic limit the existing load-time detection itself lives with (it only ever encounters
    // scenes actively in the middle of loading).
    bool WouldCreateDependencyCycle(xecs::game_mgr::instance& GameMgr, xecs::scene::guid Candidate, xecs::scene::guid NewDependency) noexcept
    {
        if (Candidate == NewDependency) return true;

        std::vector<xecs::scene::guid> Visited;
        std::vector<xecs::scene::guid> Stack{ NewDependency };
        while (!Stack.empty())
        {
            const auto Cur = Stack.back();
            Stack.pop_back();
            if (Cur == Candidate) return true;
            if (std::find(Visited.begin(), Visited.end(), Cur) != Visited.end()) continue;
            Visited.push_back(Cur);

            if (auto* pScene = GameMgr.m_SceneMgr.Find(Cur))
                for (auto& Parent : pScene->m_ParentScenes)
                    Stack.push_back(Parent);
        }
        return false;
    }

    // Opens a scene ALONGSIDE whatever is already open - the user explicitly wants every scene they
    // click to stay resident and expanded, not force-close whichever one was open before. A no-op if
    // this scene is already open (matches RequestLoad's own "second request just bumps residency"
    // semantics, so repeatedly clicking an already-open scene's row is harmless).
    void OpenScene(xecs::game_mgr::instance& GameMgr, editor_state& State, xresource::full_guid SceneGuid)
    {
        const xecs::scene::guid Guid{ .m_Instance = SceneGuid.m_Instance };
        if (std::find(State.m_OpenScenes.begin(), State.m_OpenScenes.end(), Guid) != State.m_OpenScenes.end())
            return;

        if (auto Err = GameMgr.m_SceneMgr.RequestLoad(Guid); Err)
        {
            Debugger(std::format("Failed to load Scene: {}", Err.getMessage()));
            return;
        }
        State.m_OpenScenes.push_back(Guid);
    }

    //---------------------------------------------------------------------------
    // Prefab lookup/override bookkeeping and prefab creation/instancing/deletion + the entity->
    // Prefab-asset drag-drop registration moved to standalone files under kit/ - phase 2 of the kit
    // split (same external review, direct user go-ahead to continue phase by phase). Included here,
    // in the same order they used to appear in this file, for the same reason phase 1's panels are:
    // this file remains the one umbrella #include, unchanged from the outside. Mechanical move only
    // - no behavior change; see each file's own top comment.
    //---------------------------------------------------------------------------

} // namespace e29

#include "kit/E29_PrefabOverrides.h"
#include "kit/E29_PrefabAuthoring.h"

// e29::commands::Run/FormatSceneGuid (E29_CommandContext.h, lightweight - no dependency on
// DeleteEntitySubtree itself, but needs e29::g_pGameMgr/g_pState, which E29_PrefabAuthoring.h just
// declared above) needed by ShowCreateMenuItems' own "New Entity" branch, right below - closed/
// reopened around this include for the same ODR-nesting reason E29_Commands_PropertyEdit.h's own
// include comment explains (this file declares its own `namespace e29::commands { ... }` at file
// scope).
#include "source/Examples/E29_LevelSceneEditor/commands/E29_CommandContext.h"

namespace e29
{
    // Shared "New Entity"/"New Folder" menu content, landing directly under TargetFolder (invalid =
    // loose at scene root) - used by
    // BOTH the Scene row's and the Folder row's own right-click context menu. A separate toolbar "+"
    // with a persistent "which row is the target" selection was tried first and dropped per direct
    // user feedback once right-click-in-place existed - it made the "+" redundant.
    // Assumes it's called from inside an already-open popup (BeginPopupContextItem/BeginPopup).
    //
    // "New Entity" routed through the command/undo system ([[e29_command_undo_system_plan]] memory,
    // phase 4 - commands/E29_Commands_EntityLifecycle.h) - create_entity_cmd::Redo does the exact
    // migration this used to do inline, Undo deletes it again. "New Folder" is UNCHANGED (still a
    // direct mutation) - phase 4's own scope is Create/Delete ENTITY only, folders aren't part of it.
    // Moved here (was originally much earlier in this file) since this routing needs both
    // e29::g_pGameMgr/g_pState (just declared, E29_PrefabAuthoring.h above) and e29::commands::Run
    // (just included above) - neither was available at the function's original position.
    void ShowCreateMenuItems(xecs::scene::guid SceneGuid, xecs::scene::instance& Scene, xecs::scene::folder_id TargetFolder, xundo::system& Undo) noexcept
    {
        if (ImGui::MenuItem("New Entity"))
        {
            const auto Id = NextFreeEntityId(Scene);
            e29::commands::Run(Undo, std::format("CreateEntity -Scene {} -Id {} -Folder {:08X}"
                , e29::commands::FormatSceneGuid(SceneGuid)
                , e29::commands::FormatEntityId(Id)
                , static_cast<std::uint32_t>(TargetFolder)
                ));
        }
        if (ImGui::MenuItem("New Folder"))
        {
            xecs::scene::folder NewFolder;
            NewFolder.m_Id     = NextFreeFolderId(Scene);
            NewFolder.m_Parent = TargetFolder;
            NewFolder.m_Name   = "New Folder";
            Scene.m_Folders.push_back(std::move(NewFolder));
        }
    }

    // The single "Save" action - persists everything currently open (the Level's descriptor, the
    // open Scene's entities + its descriptor) plus the underlying project/library metadata, rather
    // than requiring separate Save Level/Save Scene actions the user has to remember to hit.
    void SaveEverything(xecs::game_mgr::instance& GameMgr, editor_state& State) noexcept
    {
        if (!State.m_CurrentLevel.empty() && GameMgr.m_LevelMgr.Find(State.m_CurrentLevel))
        {
            if (auto Err = GameMgr.m_LevelMgr.Save(State.m_CurrentLevel); Err)
                Debugger(std::format("Failed to save Level: {}", Err.getMessage()));
        }

        for (auto& SceneGuid : State.m_OpenScenes)
        {
            // Plain console log, NOT Debugger() - this is routine save progress (every normal save
            // has SOME pending changes, that's the whole point of saving), not a failure. Routing it
            // through Debugger() before this exact distinction existed meant an ordinary Save popped
            // an "Error" modal every time.
            if (auto* pScene = GameMgr.m_SceneMgr.Find(SceneGuid))
            {
                std::printf("[SaveEverything] scene has %zu pending entity change(s)\n", pScene->m_PendingChanges.size());
                std::fflush(stdout);
            }
            if (auto Err = GameMgr.m_SceneMgr.SaveScene(SceneGuid); Err)
                Debugger(std::format("Failed to save Scene: {}", Err.getMessage()));
        }

        xproperty::settings::context Context;
        e10::g_LibMgr.Save(Context);
    }

} // namespace e29

// Property-edit command (phase 2 of the kit split's own follow-on, [[e29_command_undo_system_plan]]
// memory) included directly here, not relying on E29_LevelScene_Editor.cpp's own later include -
// entity_inspector_bridge, right below, needs it. Same self-sufficiency reasoning as
// kit/E29_Panel_LevelTree.h's own top comment for why. Closed/reopened around this include (rather
// than included mid-namespace like the earlier, WRONG version of this edit was) because
// E29_Commands_PropertyEdit.h declares its own `namespace e29::commands { ... }` at file scope - if
// this #include ran while namespace e29 was already open, that would nest into e29::e29::commands
// instead, exactly the ODR-nesting bug this comment is here to prevent regressing.
#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_PropertyEdit.h"
#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_EntityReference.h"
#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_AssetBrowser.h"
// Needed here (not just from E29_LevelScene_Editor.cpp's own later include) because
// RegisterAssetBrowserCallbacks, just below, now also wires the raw-file hooks and needs
// e29::commands::EncodeAssetPath - include guards make the .cpp's own separate include harmless.
#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_AssetFiles.h"

namespace e29
{

    //---------------------------------------------------------------------------
    // Entity Properties inspector wiring - bundles the prefab-override tracking/revert bookkeeping
    // and the entity_reference drag-drop-assign rendering that any xECS editor built on this kit
    // needs the moment it lets a component hold a raw xecs::component::entity field. Constructed once
    // alongside the owning example's own xproperty::inspector; RegisterCallbacks(...) wires all five
    // delegates in one call.
    //
    // Callbacks are stored as std::function MEMBERS (not locals inside RegisterCallbacks) specifically
    // so they outlive that one setup call: xdelegate::Register(T_CLASS&) binds to the callable object
    // BY REFERENCE, so whatever it's given must live as long as the inspector keeps calling it - a
    // local lambda inside RegisterCallbacks would be destroyed the moment that function returns (the
    // same "must be a named local that outlives the registration, not a temporary passed straight into
    // Register(...)" pitfall this codebase's own established convention already warns about elsewhere).
    // A std::function member is a fixed, nameable type a struct CAN hold (unlike the anonymous type of
    // a raw lambda), and since Register binds to ITS address, that address stays valid for exactly as
    // long as this bridge object does.
    //---------------------------------------------------------------------------
    struct entity_inspector_bridge
    {
        // Inspector-to-override pipeline: the inspector's callbacks only give us (type::object&, void*
        // pInstance) - m_ComponentMap closes the gap back to "which xECS component type is this",
        // rebuilt every time the inspector's content is (see RenderEntityPropertiesPanel's own
        // m_bEntityInspectorDirty block). m_bSuppressOverrideTracking guards the one re-entrancy risk:
        // the revert callback's own BeginEdit/CommitEdit bracket (used to write the prefab's base value
        // back into the instance) fires m_OnChangeEvent itself once committed - without the guard, a
        // revert would immediately re-record the very override it just removed.
        std::unordered_map<void*, const xecs::component::type::info*> m_ComponentMap;
        bool                                                           m_bSuppressOverrideTracking = false;

        // Set by the component-header callback when its "[X]" is clicked - processed once, right
        // after the owning inspector's Show(...) returns for the frame, rather than mutating the
        // entity's archetype WHILE the inspector is still mid-iteration over this same entity's
        // component list.
        const xecs::component::type::info* m_pPendingRemoveComponent = nullptr;

        std::function<void(xproperty::inspector&, const xproperty::ui::undo::cmd&)>                                                      m_OnPropertyChanged;
        std::function<void(xproperty::inspector&, const xproperty::type::object&, void*, std::string_view, const xproperty::any&, bool&)> m_OnOverrideCheck;
        std::function<void(xproperty::inspector&, const xproperty::type::object&, void*, std::string_view)>                              m_OnOverrideReset;
        std::function<void(xproperty::inspector&, const xproperty::type::object&, void*)>                                                m_OnComponentHeaderRender;
        std::function<void(xproperty::inspector&, const xproperty::type::object&, void*, std::string_view, const xproperty::any&, bool&)> m_OnEntityReferenceRender;

        // The "fake pointer, resolved by callback" indirection xproperty's own inspector expects
        // for any component instance whose address isn't a stable, owning member (see
        // E10_TextureResourcePipeline.cpp's identical use for the same reason - a selected asset's
        // descriptor can be reloaded/relocated out from under the inspector). xECS component-pool
        // addresses are exactly this case: an archetype migration, or a full world destroy/recreate
        // (Phase 8's hot reload) invalidates them independent of anything on the inspector's own
        // side. Registered in RegisterCallbacks; re-derives the CURRENT real pointer fresh every
        // time xproperty::inspector::Show() calls it (twice a frame - see xPropertyImGuiInspector's
        // own m_OnGetComponentPointer comment) rather than trusting anything cached from a prior
        // frame. Also the only place m_ComponentMap gets written now (previously written once, at
        // rebuild time, keyed by the same pointer that's now fake) - keyed by the freshly-resolved
        // real pointer, matching what m_OnOverrideCheck/m_OnPropertyChanged/etc. actually receive
        // from xproperty this same frame.
        std::function<void(xproperty::inspector&, const int, void*&, void*)> m_OnGetComponentPointer;

        void RegisterCallbacks(xproperty::inspector& Inspector, xecs::game_mgr::instance& GameMgr, editor_state& State, xundo::system& Undo) noexcept
        {
            // xdelegate::Register(...) unconditionally push_back's - it has no dedup and no
            // Unregister at all (confirmed reading dependencies/xdelegate/source/xdelegate.h
            // directly). This method is called MORE than once on the SAME Inspector across this
            // session's lifetime (once at startup, again after Phase 8's ReloadGame recreates
            // GameMgr) - without clearing first, every callback below silently accumulates a
            // second, third, ... registration and fires that many times per event, which is
            // exactly the "properties/rows rendering doubled" bug a reload produced (confirmed live
            // - stacked "X"/duplicate rows in the Entity Properties panel after one reload).
            // E10_TextureResourcePipeline.cpp already established this exact idiom
            // (`Inspectors[0].m_OnGetComponentPointer.m_Delegates.clear();` before its own
            // re-Register) for the same reason - mirrored here for all six delegates this bridge
            // owns, not just the one E10 happened to need it for.
            Inspector.m_OnChangeEvent.m_Delegates.clear();
            Inspector.m_OnOverrideCheck.m_Delegates.clear();
            Inspector.m_OnOverrideReset.m_Delegates.clear();
            Inspector.m_OnComponentHeaderRender.m_Delegates.clear();
            Inspector.m_OnCustomRenderReplaceValue.m_Delegates.clear();
            Inspector.m_OnGetComponentPointer.m_Delegates.clear();

            // xdelegate::Register(T_CLASS&) binds to the lambda OBJECT itself (by reference) rather
            // than copying/erasing it into a std::function - so each callback must be a named local
            // that outlives the registration; here that "local" is the std::function MEMBER itself
            // (see this struct's own comment), assigned below and then registered.
            // Routed through the command/undo system ([[e29_command_undo_system_plan]] memory, phase
            // 2 - commands/E29_Commands_PropertyEdit.h) instead of applying the value/recording the
            // override directly here - this is the ORDINARY per-row commit path (Cmd.m_Name is a real
            // property path, Cmd.m_NewValue/m_Original real scalar values), never the whole-component
            // BeginEdit/CommitEdit snapshot bracket the Revert Override action below uses (that one's
            // own m_OnChangeEvent notification carries a bracket label and a multi-line blob instead -
            // m_bSuppressOverrideTracking is what keeps THIS callback from misinterpreting that case).
            // set_property_cmd's own Redo()/Undo() do what this callback used to do inline (mark
            // dirty, FindOrCreateOverrideEntry) - the difference, and the whole point of this move, is
            // that Undo() now runs that SAME logic with the BEFORE value, so undoing an edit correctly
            // reverts the override bookkeeping too, not just the live property (direct user caution:
            // "careful with resetting the overrides").
            m_OnPropertyChanged = [this, &Undo](xproperty::inspector&, const xproperty::ui::undo::cmd& Cmd)
            {
                if (m_bSuppressOverrideTracking) return;

                auto It = m_ComponentMap.find(Cmd.m_pClassObject);
                if (It == m_ComponentMap.end()) return;
                if (!e29::g_pState || !e29::g_pGameMgr) return;
                auto& State = *e29::g_pState;

                std::array<char, 256> BeforeBuffer{}, AfterBuffer{};
                const auto BeforeLen = e29::commands::FormatPropertyValue(BeforeBuffer, Cmd.m_Original);
                const auto AfterLen  = e29::commands::FormatPropertyValue(AfterBuffer, Cmd.m_NewValue);
                const std::string Before(BeforeBuffer.data(), BeforeLen > 0 ? static_cast<std::size_t>(BeforeLen) : 0);
                const std::string After(AfterBuffer.data(), AfterLen > 0 ? static_cast<std::size_t>(AfterLen) : 0);
                const std::uint32_t TypeGuid = Cmd.m_NewValue.m_pType ? Cmd.m_NewValue.m_pType->m_GUID : 0;

                e29::commands::Run(Undo, std::format("SetProperty -Scene {} -Id {} -Component {:016X} -Path {} -TypeGuid {:08X} -Before {} -After {}"
                    , e29::commands::FormatSceneGuid(State.m_SelectedEntityScene)
                    , e29::commands::FormatEntityId(State.m_SelectedEntityId)
                    , It->second->m_Guid.m_Value
                    , e29::commands::Base64Encode(Cmd.m_Name)
                    , TypeGuid
                    , e29::commands::Base64Encode(Before)
                    , e29::commands::Base64Encode(After)
                    ));
            };
            Inspector.m_OnChangeEvent.Register(m_OnPropertyChanged);

            m_OnOverrideCheck = [this, &GameMgr, &State](xproperty::inspector&, const xproperty::type::object&, void* pInstance, std::string_view Path, const xproperty::any&, bool& bOut)
            {
                bOut = false;

                auto It = m_ComponentMap.find(pInstance);
                if (It == m_ComponentMap.end()) return;

                auto Ctx = e29::FindContainingPrefabInstance(GameMgr, State.m_SelectedEntity);
                if (Ctx.m_pPI == nullptr) return;

                for (auto& C : Ctx.m_pPI->m_lComponents)
                {
                    if (C.m_ComponentTypeGuid != It->second->m_Guid.m_Value) continue;
                    if (std::ranges::equal(C.m_MemberPath, Ctx.m_MemberPath) == false) continue;
                    for (auto& O : C.m_PropertyOverrides)
                        if (O.m_PropertyName == Path) { bOut = true; return; }
                }
            };
            Inspector.m_OnOverrideCheck.Register(m_OnOverrideCheck);

            m_OnOverrideReset = [this, &GameMgr, &State](xproperty::inspector& Inspector, const xproperty::type::object& Obj, void* pInstance, std::string_view Path)
            {
                auto It = m_ComponentMap.find(pInstance);
                if (It == m_ComponentMap.end()) return;

                auto Ctx = e29::FindContainingPrefabInstance(GameMgr, State.m_SelectedEntity);
                if (Ctx.m_pPI == nullptr) return;

                if (auto Err = GameMgr.m_PrefabMgr.EnsureLoaded(Ctx.m_pPI->m_PrefabInstance); Err)
                {
                    e29::Debugger(std::format("Failed to load source prefab for revert: {}", Err.getMessage()));
                    return;
                }

                auto RootIt = GameMgr.m_PrefabMgr.m_PrefabList.find(Ctx.m_pPI->m_PrefabInstance.m_Instance.m_Value);
                if (RootIt == GameMgr.m_PrefabMgr.m_PrefabList.end()) return;

                // Same MemberPath, walked from the PREFAB's own root instead of the placed instance's
                // root - reaches the corresponding source member (see
                // prefab_component_override::m_MemberPath).
                const auto BaseEntity = xecs::persist::details::ResolveMemberPath(GameMgr, RootIt->second, Ctx.m_MemberPath);
                if (BaseEntity.isValid() == false) return;

                auto& RootDetails = GameMgr.m_ComponentMgr.getEntityDetails(BaseEntity);
                const auto iType  = RootDetails.m_pPool->findIndexComponentFromInfo(*It->second);
                if (iType < 0) return;
                auto* pRootData = &RootDetails.m_pPool->m_pComponent[iType][RootDetails.m_PoolIndex.m_Value * It->second->m_Size];

                xproperty::settings::context Context;
                xproperty::any               BaseValue;
                bool                         bFoundValue = false;
                xproperty::sprop::collector(pRootData, Obj, Context, [&](const char* pPropertyName, xproperty::any&& Data, const xproperty::type::members&, bool, const void*) noexcept
                {
                    if (Path == pPropertyName) { BaseValue = std::move(Data); bFoundValue = true; }
                });
                if (bFoundValue == false) return;

                std::string SetError;
                m_bSuppressOverrideTracking = true;
                Inspector.BeginEdit(Obj, pInstance, "Revert Override");
                xproperty::sprop::setProperty(SetError, pInstance, Obj, xproperty::sprop::container::prop{ std::string(Path), BaseValue }, Context);
                Inspector.CommitEdit(Context);
                m_bSuppressOverrideTracking = false;

                for (auto& C : Ctx.m_pPI->m_lComponents)
                {
                    if (C.m_ComponentTypeGuid != It->second->m_Guid.m_Value) continue;
                    if (std::ranges::equal(C.m_MemberPath, Ctx.m_MemberPath) == false) continue;
                    std::erase_if(C.m_PropertyOverrides, [&](auto& O) noexcept { return O.m_PropertyName == Path; });
                    if (C.m_PropertyOverrides.empty())
                    {
                        auto& MemberPath = Ctx.m_MemberPath;
                        std::erase_if(Ctx.m_pPI->m_lComponents, [&](auto& CC) noexcept { return CC.m_ComponentTypeGuid == It->second->m_Guid.m_Value && std::ranges::equal(CC.m_MemberPath, MemberPath); });
                    }
                    break;
                }

                // Two entities just changed and must both be (re)saved: the edited member itself (its
                // live data just went back to the prefab's base value - CommitEdit above ran with
                // m_bSuppressOverrideTracking held, so the property-changed callback never fired for
                // it) and Ctx.m_RootEntity, whose m_lComponents bookkeeping the erase_if above just
                // mutated (a DIFFERENT entity than the edited one whenever m_MemberPath is non-empty).
                // Missing either one would silently leave the revert un-persisted on next save.
                GameMgr.m_SceneMgr.MarkEntityDirty(State.m_SelectedEntityScene, State.m_SelectedEntityId);
                if (Ctx.m_RootEntity.m_Value != State.m_SelectedEntity.m_Value)
                {
                    if (auto* pScene = GameMgr.m_SceneMgr.Find(State.m_SelectedEntityScene))
                    {
                        if (auto RootIt2 = pScene->m_RuntimeToLocal.find(Ctx.m_RootEntity.m_Value); RootIt2 != pScene->m_RuntimeToLocal.end())
                            GameMgr.m_SceneMgr.MarkEntityDirty(State.m_SelectedEntityScene, RootIt2->second);
                    }
                }
            };
            Inspector.m_OnOverrideReset.Register(m_OnOverrideReset);

            // "[X]" on a component's own header row - resolves back to which xECS component this is via
            // the SAME m_ComponentMap the property callbacks above already use, so it stays in sync
            // with whatever's currently appended. Excludes the same components the "Remove Component"
            // combo already excludes (internal bookkeeping components, and Name - every entity stays
            // nameable) - one shared exclusion list, not two independently maintained ones. Only
            // records the request (m_pPendingRemoveComponent); the actual AddOrRemoveComponents call
            // happens after the inspector's Show(...) returns for this frame.
            m_OnComponentHeaderRender = [this](xproperty::inspector&, const xproperty::type::object&, void* pInstance)
            {
                auto It = m_ComponentMap.find(pInstance);
                if (It == m_ComponentMap.end()) return;
                auto* pInfo = It->second;
                if (e29::IsInternalComponent(pInfo)) return;
                // Name is a regular, removable component like any other now - an entity with none of
                // its own components at all (not even Name) is a legitimate state.

                // NOT ImGui::SameLine() here - SameLine(x) positions using CursorPosPrevLine.y (the Y
                // of whichever line a real widget last finished on), not the actual current cursor.
                // Nothing real draws between NextColumn() and this callback firing, so for every
                // component AFTER the first, CursorPosPrevLine.y is still stale from the PREVIOUS
                // component's last property row - visible live as this button rendering on top of
                // whatever row happened to be last, not its own header. GetCursorScreenPos() (the
                // real, current cursor - already correctly placed by the caller right before this
                // fires) has no such staleness, so compute the absolute position from that instead.
                const ImVec2 RowPos = ImGui::GetCursorScreenPos();
                const float  AvailW = ImGui::GetContentRegionAvail().x;
                ImGui::SetCursorScreenPos(ImVec2(RowPos.x + AvailW - 20.0f, RowPos.y));
                // Borderless/transparent-at-rest, only picking up a background on hover - matches
                // Unity's own small inline toolbar icon buttons (direct user comparison screenshot:
                // a bordered gray box vs Unity's flat "?"/drag-handle/"..." icons that only highlight
                // on hover). ButtonHovered/ButtonActive are left as the theme's own values so the
                // hover feedback itself still reads as a real button, just not a boxed one at rest.
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
                if (ImGui::SmallButton("X")) m_pPendingRemoveComponent = pInfo;
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove this component from the entity");
            };
            Inspector.m_OnComponentHeaderRender.Register(m_OnComponentHeaderRender);

            // Custom render for ANY xecs::component::entity-valued property (today, only
            // xecs::component::entity_reference::m_Target - but this is a value-type check, not a
            // per-property tag, so it applies automatically to any FUTURE component with an
            // entity-reference field too) - the shared inspector has no default draw style registered
            // for the raw 'entity' atomic type at all, so this is not optional polish, it's what makes
            // entity_reference safe to add to an entity in the first place. Drag a row from the Level
            // tree (E29_ENTITY_DRAG, the same shared payload reparenting/prefab-creation already use)
            // onto this property to assign it; "X" clears it. Shows "<unresolved>" rather than
            // crashing when the target is valid but its owning scene isn't currently open
            // (ResolveEntityReference can't search a scene nobody loaded) - the underlying
            // value/reference is untouched either way, this is purely a display limitation.
            m_OnEntityReferenceRender = [this, &GameMgr, &State, &Undo](xproperty::inspector& Inspector, const xproperty::type::object& Obj, void* pInstance, std::string_view Path, const xproperty::any& Value, bool& bHandled)
            {
                if (Value.m_pType == nullptr || Value.m_pType->m_GUID != xproperty::settings::var_type<xecs::component::entity>::guid_v) return;
                bHandled = true;

                const auto CurrentValue = Value.get<xecs::component::entity>();
                std::string     Label;
                xecs::scene::guid TargetScene;
                const bool bResolved = e29::ResolveEntityReference(GameMgr, State, CurrentValue, Label, TargetScene);
                if (!bResolved) Label = CurrentValue.isValid() ? "<unresolved>" : "None";

                // A plain Text/TextUnformatted's own "last item" rect is only as wide as its glyphs -
                // dropping anywhere else in this (usually much wider) property cell would silently miss
                // BeginDragDropTarget's hover check entirely. Selectable with an explicit size fills the
                // REST of the cell with a real, hoverable rect.
                const float AvailWidth  = ImGui::GetContentRegionAvail().x;
                const bool  bShowClear  = CurrentValue.isValid();
                ImGui::Selectable(Label.c_str(), false, ImGuiSelectableFlags_None, ImVec2(bShowClear ? AvailWidth - 24.0f : AvailWidth, 0.0f));

                // Attached to the Selectable specifically, immediately after it and BEFORE the "X"
                // button below (which would otherwise become the new "last item" and steal the drop
                // target down to its own tiny rect the moment a reference is already assigned).
                const bool bIsDropTarget = ImGui::BeginDragDropTarget();
                if (bIsDropTarget)
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("E29_ENTITY_DRAG"))
                    {
                        IM_ASSERT(payload->DataSize == sizeof(e29::entity_drag_payload_t));
                        auto& Dropped = *reinterpret_cast<const e29::entity_drag_payload_t*>(payload->Data);
                        if (auto* pDropScene = GameMgr.m_SceneMgr.Find(Dropped.m_SceneGuid))
                        {
                            if (auto It = pDropScene->m_LocalToRuntime.find(Dropped.m_Id); It != pDropScene->m_LocalToRuntime.end())
                            {
                                // A reference pointing outside the owning entity's own scene needs a
                                // Dependencies entry (pOwningScene->m_ParentScenes) to ever resolve
                                // again on save/reload - added automatically here rather than leaving
                                // the reference dangling until the user separately remembers to add it
                                // by hand. Checked (and, if it would close a cycle, REFUSED) before the
                                // property assignment itself runs, not after - creating the assignment
                                // AND then skipping just the dependency edge would leave a cross-scene
                                // reference with nothing to make it resolvable, an even more confusing
                                // state than refusing outright.
                                bool bRefused = false;
                                if (Dropped.m_SceneGuid != State.m_SelectedEntityScene)
                                {
                                    if (auto* pOwningScene = GameMgr.m_SceneMgr.Find(State.m_SelectedEntityScene))
                                    {
                                        const bool bAlreadyDependency = std::find(pOwningScene->m_ParentScenes.begin(), pOwningScene->m_ParentScenes.end(), Dropped.m_SceneGuid) != pOwningScene->m_ParentScenes.end();
                                        if (!bAlreadyDependency && e29::WouldCreateDependencyCycle(GameMgr, State.m_SelectedEntityScene, Dropped.m_SceneGuid))
                                        {
                                            e29::Debugger("Can't assign that reference: its scene already depends on this one (would create a circular scene dependency)");
                                            bRefused = true;
                                        }
                                        else if (!bAlreadyDependency)
                                        {
                                            pOwningScene->m_ParentScenes.push_back(Dropped.m_SceneGuid);
                                        }
                                    }
                                }

                                // Routed through the command system (gap #3, [[e29_command_undo_known_gaps]])
                                // instead of BeginEdit/setProperty/CommitEdit directly - see this file's
                                // own top comment (E29_Commands_EntityReference.h) for why AfterScene/
                                // AfterId (not the raw runtime handle It->second) are what actually cross
                                // into the command string.
                                if (!bRefused)
                                {
                                    auto CompIt = m_ComponentMap.find(pInstance);
                                    if (CompIt != m_ComponentMap.end())
                                    {
                                        e29::commands::Run(Undo, std::format("SetEntityReference -Scene {} -Id {} -Component {:016X} -Path {} -AfterScene {} -AfterId {}"
                                            , e29::commands::FormatSceneGuid(State.m_SelectedEntityScene)
                                            , e29::commands::FormatEntityId(State.m_SelectedEntityId)
                                            , CompIt->second->m_Guid.m_Value
                                            , e29::commands::Base64Encode(std::string(Path))
                                            , e29::commands::FormatSceneGuid(Dropped.m_SceneGuid)
                                            , e29::commands::FormatEntityId(Dropped.m_Id)));
                                    }
                                }
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                if (bShowClear)
                {
                    ImGui::SameLine();
                    // Same borderless/hover-only treatment as the component-header "X" above - this is
                    // the entity-reference "Target" field's own clear button, visible in the same panel.
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
                    const bool bClearClicked = ImGui::SmallButton("X");
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor();
                    if (bClearClicked)
                    {
                        // AfterScene/AfterId 0/0 is SetEntityReference's own "clear" sentinel - same
                        // routing/reasoning as the assign path just above.
                        auto CompIt = m_ComponentMap.find(pInstance);
                        if (CompIt != m_ComponentMap.end())
                        {
                            e29::commands::Run(Undo, std::format("SetEntityReference -Scene {} -Id {} -Component {:016X} -Path {} -AfterScene {} -AfterId {}"
                                , e29::commands::FormatSceneGuid(State.m_SelectedEntityScene)
                                , e29::commands::FormatEntityId(State.m_SelectedEntityId)
                                , CompIt->second->m_Guid.m_Value
                                , e29::commands::Base64Encode(std::string(Path))
                                , e29::commands::FormatSceneGuid(xecs::scene::guid{})
                                , e29::commands::FormatEntityId(xecs::scene::invalid_permanent_id_v)));
                        }
                    }
                }
            };
            Inspector.m_OnCustomRenderReplaceValue.Register(m_OnEntityReferenceRender);

            // See this member's own declaration comment for why this exists at all. pUserData is
            // the pInfo passed to AppendEntityComponent's own pUserData argument (RenderEntity
            // PropertiesPanel's dirty-rebuild block) - re-derive the CURRENT pool address for that
            // exact component type on the CURRENTLY selected entity, the same lookup that block
            // itself uses, just re-run fresh instead of cached.
            m_OnGetComponentPointer = [this, &GameMgr, &State](xproperty::inspector&, const int, void*& pObject, void* pUserData) noexcept
            {
                pObject = nullptr;
                if (State.m_SelectedEntity.isValid() == false) return;

                auto* pInfo = static_cast<const xecs::component::type::info*>(pUserData);
                auto& Details = GameMgr.m_ComponentMgr.getEntityDetails(State.m_SelectedEntity);
                if (Details.m_pPool == nullptr) return;

                const auto iType = Details.m_pPool->findIndexComponentFromInfo(*pInfo);
                if (iType < 0) return;

                auto* pData = &Details.m_pPool->m_pComponent[iType][Details.m_PoolIndex.m_Value * pInfo->m_Size];
                pObject = pData;

                // Keyed by the freshly-resolved real pointer, matching what m_OnOverrideCheck/
                // m_OnPropertyChanged/m_OnComponentHeaderRender actually receive from xproperty this
                // same frame (xproperty temporarily overwrites the fake pointer with exactly this
                // value for the duration of its own Render pass - see xPropertyImGuiInspector.cpp's
                // own Show()).
                m_ComponentMap[pData] = pInfo;
            };
            Inspector.m_OnGetComponentPointer.Register(m_OnGetComponentPointer);
        }
    };

    // Wires the Asset Browser's optional mutation hooks (e10::assert_browser::m_OnRenameAsset/
    // m_OnMoveAsset/m_OnDeleteAsset/m_OnRestoreAsset/m_OnCreateAsset, E10_AssetBrowser.h) to E29's own
    // command/undo system, so a real click in the browser panel - not just a CLI/AI call - becomes an
    // undo-routed E29_Commands_AssetBrowser.h command. Same additive, opt-in pattern as
    // entity_inspector_bridge::RegisterCallbacks just above; every other example that embeds the same
    // browser leaves these hooks unset and is byte-for-byte unaffected. Plain free function (not a
    // whole bridge struct) since these hooks need no persistent per-frame render state, unlike the
    // property inspector's own m_ComponentMap.
    inline void RegisterAssetBrowserCallbacks(e10::assert_browser& Browser, xundo::system& Undo, xgpu::window& MainWindow) noexcept
    {
        // OS-level (Explorer) drag-out (E10_AssetOleDrag.h) needs to know the real Win32 rect of the
        // main window to tell "has this drag left our own app" apart from an ordinary in-app drag -
        // see m_OnGetMainWindowHandle's own comment in E10_AssetBrowser.h for the multi-viewport/
        // undocked-panel scope limit this deliberately accepts.
        Browser.m_OnGetMainWindowHandle = [&MainWindow](void) -> std::size_t
        {
            return MainWindow.getSystemWindowHandle();
        };

        Browser.m_OnRenameAsset = [&Undo](e10::library::guid LibraryGuid, xresource::full_guid Asset, std::string_view NewName)
        {
            e29::commands::Run(Undo, std::format("RenameAsset -Library {} -Asset {} -Name {}"
                , e29::commands::FormatLibraryGuid(LibraryGuid), e29::commands::FormatAssetGuid(Asset), e29::commands::Base64Encode(std::string(NewName))));
        };

        Browser.m_OnMoveAsset = [&Undo](e10::library::guid LibraryGuid, xresource::full_guid Asset, xresource::full_guid OldParent, xresource::full_guid NewParent)
        {
            e29::commands::Run(Undo, std::format("MoveAsset -Library {} -Asset {} -OldParent {} -NewParent {}"
                , e29::commands::FormatLibraryGuid(LibraryGuid), e29::commands::FormatAssetGuid(Asset), e29::commands::FormatAssetGuid(OldParent), e29::commands::FormatAssetGuid(NewParent)));
        };

        Browser.m_OnDeleteAsset = [&Undo](e10::library::guid LibraryGuid, xresource::full_guid Asset)
        {
            e29::commands::Run(Undo, std::format("DeleteAsset -Library {} -Asset {}"
                , e29::commands::FormatLibraryGuid(LibraryGuid), e29::commands::FormatAssetGuid(Asset)));
        };

        Browser.m_OnRestoreAsset = [&Undo](e10::library::guid LibraryGuid, xresource::full_guid Asset, xresource::full_guid NewParent)
        {
            e29::commands::Run(Undo, std::format("RestoreAsset -Library {} -Asset {} -Parent {}"
                , e29::commands::FormatLibraryGuid(LibraryGuid), e29::commands::FormatAssetGuid(Asset), e29::commands::FormatAssetGuid(NewParent)));
        };

        // Needs the resulting guid back synchronously (the browser immediately selects it) - mints a
        // fresh instance guid itself, same call NewAsset's own auto-generate path uses internally
        // (xresource::instance_guid::GenerateGUID), so the command string always names an explicit id
        // rather than relying on CreateAsset's Redo to invent one (it deliberately never does - see
        // that command's own top comment on why Redo must stay deterministic/re-runnable).
        Browser.m_OnCreateAsset = [&Undo](e10::library::guid LibraryGuid, xresource::type_guid Type, xresource::full_guid Parent, std::string_view Name) -> xresource::full_guid
        {
            xresource::instance_guid NewInstance{};
            NewInstance.GenerateGUID();
            const xresource::full_guid NewAsset{ .m_Instance = NewInstance, .m_Type = Type };

            e29::commands::Run(Undo, std::format("CreateAsset -Library {} -Type {:016X} -Asset {} -Parent {} -Name {}"
                , e29::commands::FormatLibraryGuid(LibraryGuid), Type.m_Value, e29::commands::FormatAssetGuid(NewAsset)
                , e29::commands::FormatAssetGuid(Parent), e29::commands::Base64Encode(std::string(Name))));
            return NewAsset;
        };

        // Raw Assets-folder file hooks (Phase 5 of the window-split plan) - route files_tab's own
        // Rename/Move/Cut-Paste/Delete/Copy UI actions through the SAME MoveAssetFile/CopyAssetFile
        // xundo commands Phase 4 already proved via CLI (E29_Commands_AssetFiles.h), rather than a
        // second, competing call path into library_mgr.
        //
        // -Force 1 is passed HERE unconditionally: these hooks only ever fire from files_tab's own
        // StageOrExecute, which already ran the SAME CountDependents check and (if anything was
        // affected) already got the user's explicit "Continue" on its own confirmation modal before
        // calling this hook at all - re-running the command's own dependent-count gate here would just
        // reject a change the user already approved. The command-level gate exists for the OTHER path
        // into these commands - a human or AI issuing them directly via the Command Console/CLI, which
        // has no modal to click and must use its own -Force 1 deliberately instead.
        //
        // Batched - files_tab hands the WHOLE multi-item gesture here in one call; RunGroup turns it
        // into ONE undo/redo step for every item, not N separate ones (direct user correction: "a
        // 5-file delete should be 1 undo/redo step... the operation should be grouped" - xundo::system
        // already supports this via its own grouped Execute(), this was just never wired through it).
        Browser.m_OnMoveAssetFileBatch = [&Undo](e10::library::guid LibraryGuid, const std::vector<std::pair<std::wstring, std::wstring>>& Items) -> bool
        {
            std::vector<std::string> Cmds;
            Cmds.reserve(Items.size());
            for (auto& [OldRelPath, NewRelPath] : Items)
                Cmds.push_back(std::format("MoveAssetFile -Library {} -OldPath {} -NewPath {} -Force 1"
                    , e29::commands::FormatLibraryGuid(LibraryGuid), e29::commands::EncodeAssetPath(OldRelPath), e29::commands::EncodeAssetPath(NewRelPath)));
            return e29::commands::RunGroup(Undo, "MoveAssetFile (multiple)", Cmds);
        };

        // -TrashPath must be pre-minted by the CALLER (ComputeTrashPath is a pure query, not something
        // Redo() can compute itself - see E29_Commands_AssetFiles.h's own top comment) - this hook is
        // exactly the call site that comment said didn't exist yet.
        Browser.m_OnDeleteAssetFileToTrashBatch = [&Undo](e10::library::guid LibraryGuid, const std::vector<std::wstring>& RelPaths) -> bool
        {
            std::vector<std::string> Cmds;
            Cmds.reserve(RelPaths.size());
            for (auto& RelPath : RelPaths)
            {
                const std::wstring TrashPath = e10::g_LibMgr.ComputeTrashPath(LibraryGuid, RelPath);
                Cmds.push_back(std::format("DeleteAssetFileToTrash -Library {} -Path {} -TrashPath {} -Force 1"
                    , e29::commands::FormatLibraryGuid(LibraryGuid), e29::commands::EncodeAssetPath(RelPath), e29::commands::EncodeAssetPath(TrashPath)));
            }
            return e29::commands::RunGroup(Undo, "DeleteAssetFileToTrash (multiple)", Cmds);
        };

        Browser.m_OnRestoreAssetFileFromTrash = [&Undo](e10::library::guid LibraryGuid, const std::wstring& TrashRelPath, const std::wstring& OriginalRelPath)
        {
            e29::commands::Run(Undo, std::format("RestoreAssetFileFromTrash -Library {} -TrashPath {} -OriginalPath {}"
                , e29::commands::FormatLibraryGuid(LibraryGuid), e29::commands::EncodeAssetPath(TrashRelPath), e29::commands::EncodeAssetPath(OriginalRelPath)));
        };

        Browser.m_OnCopyAssetFile = [&Undo](e10::library::guid LibraryGuid, const std::wstring& SourceRelPath, const std::wstring& NewRelPath)
        {
            e29::commands::Run(Undo, std::format("CopyAssetFile -Library {} -SourcePath {} -NewPath {}"
                , e29::commands::FormatLibraryGuid(LibraryGuid), e29::commands::EncodeAssetPath(SourceRelPath), e29::commands::EncodeAssetPath(NewRelPath)));
        };
    }

    //---------------------------------------------------------------------------
    // The three UI panels below moved to standalone files under kit/ - phase 1 of the kit split
    // (direct user request, following an external review's proposed module boundaries). Included
    // here, in the same order they used to appear in this file, rather than left for the caller to
    // include separately - this file remains the one umbrella #include (E29_LevelScene_Editor.cpp
    // still just does #include "E29_LevelSceneEditorKit.h"), unchanged from the outside. Mechanical
    // move only - no behavior change; see each file's own top comment.
    //---------------------------------------------------------------------------

} // namespace e29

#include "kit/E29_Panel_LevelTree.h"
#include "kit/E29_Panel_EntityProperties.h"
#include "kit/E29_Panel_SystemRegistry.h"
#include "kit/E29_Panel_CommandConsole.h"

#endif // E29_LEVEL_SCENE_EDITOR_KIT_H
