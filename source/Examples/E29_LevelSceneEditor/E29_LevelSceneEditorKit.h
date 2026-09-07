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
// id-minting, folder bookkeeping, scene open/close + dependency-cycle guard, prefab-instance
// override context, entity-reference resolution, prefab creation/instancing/deletion, the
// drag-payload + drop registration that turns a Level-tree entity into a Prefab asset, SaveEverything,
// and the entity_inspector_bridge (prefab-override + entity-reference inspector callback wiring).
// The three UI panels (Level tree, Entity Properties, System Registry) that used to live at the end
// of this same file now live under kit/ - see this file's own tail for why they're still pulled in
// here rather than left for the caller to include separately.
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
    // Exempts the special auto-created "Default" bucket (EnsureDefaultFolder) - it's meant to always
    // be there as a landing zone for loose entities, and gets recreated on demand anyway if ever
    // pruned, so leaving it out just avoids the visible "Default (0)" row flickering away and back.
    void PruneEmptyFolderChain(xecs::scene::instance& Scene, xecs::scene::folder_id Id) noexcept
    {
        while( Id != xecs::scene::invalid_folder_id_v )
        {
            auto It = std::find_if(Scene.m_Folders.begin(), Scene.m_Folders.end(), [&](auto& F) noexcept { return F.m_Id == Id; });
            if( It == Scene.m_Folders.end() ) return;

            if( It->m_Parent == xecs::scene::invalid_folder_id_v && It->m_Name == "Default" ) return;
            if( It->m_Entities.empty() == false ) return;

            const bool bHasChildFolder = std::any_of(Scene.m_Folders.begin(), Scene.m_Folders.end(), [&](auto& F) noexcept { return F.m_Parent == Id; });
            if( bHasChildFolder ) return;

            const auto ParentId = It->m_Parent;
            Scene.m_Folders.erase(It);
            Id = ParentId;
        }
    }

    // Shared "New Entity"/"New Folder" menu content, landing directly under TargetFolder (invalid =
    // scene root, adopted into "Default" the next render pass - see EnsureDefaultFolder) - used by
    // BOTH the Scene row's and the Folder row's own right-click context menu. A separate toolbar "+"
    // with a persistent "which row is the target" selection was tried first and dropped per direct
    // user feedback once right-click-in-place existed - it made the "+" redundant.
    // Assumes it's called from inside an already-open popup (BeginPopupContextItem/BeginPopup).
    void ShowCreateMenuItems(xecs::game_mgr::instance& GameMgr, xecs::scene::guid SceneGuid, xecs::scene::instance& Scene, xecs::scene::folder_id TargetFolder) noexcept
    {
        if (ImGui::MenuItem("New Entity"))
        {
            auto& Archetype = GameMgr.getOrCreateArchetype<>();
            auto  Entity    = Archetype.CreateEntity(xecs::tools::empty_lambda{});
            const auto Id   = NextFreeEntityId(Scene);
            Scene.m_LocalToRuntime[Id]              = Entity;
            Scene.m_RuntimeToLocal[Entity.m_Value]  = Id;
            GameMgr.m_SceneMgr.MarkEntityNew(SceneGuid, Id);
            if (TargetFolder != xecs::scene::invalid_folder_id_v)
                ReparentEntityIntoFolder(Scene, Id, TargetFolder);
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

    // Every scene always has somewhere for an entity to live - there's no more "loose at scene root"
    // concept (direct user request: unfoldered entities belong in an auto-created "Default" folder
    // instead). Idempotent: returns the existing one if a root-level folder already named "Default"
    // exists, only creates it the first time it's actually needed. A REAL entry in Scene.m_Folders
    // (unlike the synthesized "Dependencies" one - Default genuinely owns persisted entity membership)
    // but rendered/treated as a special, locked folder just like Dependencies: no delete, no manual
    // New Entity/New Folder inside it, no drag-drop INTO it - purely a temporary holding area for
    // entities the user hasn't organized yet, populated only by this function.
    xecs::scene::folder_id EnsureDefaultFolder(xecs::scene::instance& Scene) noexcept
    {
        for (auto& F : Scene.m_Folders)
            if (F.m_Parent == xecs::scene::invalid_folder_id_v && F.m_Name == "Default")
                return F.m_Id;

        xecs::scene::folder NewFolder;
        NewFolder.m_Id     = NextFreeFolderId(Scene);
        NewFolder.m_Parent = xecs::scene::invalid_folder_id_v;
        NewFolder.m_Name   = "Default";
        Scene.m_Folders.push_back(std::move(NewFolder));
        return Scene.m_Folders.back().m_Id;
    }

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

    // Returns the entity's live prefab_instance component, or nullptr if it isn't a prefab instance
    // (a plain entity never has this component).
    xecs::editor::prefab_instance* FindPrefabInstance(xecs::game_mgr::instance& GameMgr, xecs::component::entity Entity) noexcept
    {
        if (Entity.isValid() == false) return nullptr;
        auto& Details = GameMgr.m_ComponentMgr.getEntityDetails(Entity);
        if (Details.m_pPool == nullptr) return nullptr;
        // findIndexComponentFromInfo, not getComponentBits().getBit() - see
        // [[xecs_getbit_vs_findindexcomponentfrominfo]] (a runtime-assigned component bit checked this
        // way can read as absent/invalid even when the component is genuinely present).
        if (Details.m_pPool->findIndexComponentFromInfo(xecs::component::type::info_v<xecs::editor::prefab_instance>) < 0)
            return nullptr;
        return &Details.m_pPool->getComponent<xecs::editor::prefab_instance>(Details.m_PoolIndex);
    }

    // Result of walking UP from some entity to find the prefab instance it's structurally part of -
    // itself if it carries prefab_instance directly, else the nearest ancestor (via parent) that
    // does, recording the child-index path down from that ancestor to the original entity along the
    // way (see xecs::editor::prefab_component_override::m_MemberPath's own comment for why a path,
    // not a stored id). Stops at the first prefab_instance found walking up - never crosses further
    // out past a nested instance's own root, matching this session's existing nested-override scope.
    struct prefab_instance_context
    {
        xecs::editor::prefab_instance* m_pPI = nullptr;
        xecs::component::entity        m_RootEntity{};
        std::vector<std::uint32_t>     m_MemberPath;
    };

    prefab_instance_context FindContainingPrefabInstance(xecs::game_mgr::instance& GameMgr, xecs::component::entity Entity) noexcept
    {
        prefab_instance_context Ctx;
        if (Entity.isValid() == false) return Ctx;

        std::vector<std::uint32_t> ReversePath;
        auto Cur = Entity;
        for(;;)
        {
            if (auto* pPI = FindPrefabInstance(GameMgr, Cur))
            {
                Ctx.m_pPI       = pPI;
                Ctx.m_RootEntity = Cur;
                Ctx.m_MemberPath.assign(ReversePath.rbegin(), ReversePath.rend());
                return Ctx;
            }

            auto& Details = GameMgr.m_ComponentMgr.getEntityDetails(Cur);
            if (Details.m_pPool == nullptr) return {};
            const auto iParentType = Details.m_pPool->findIndexComponentFromInfo(xecs::component::type::info_v<xecs::component::parent>);
            if (iParentType < 0) return {};   // no parent, and not a PI itself - not part of any instance

            const auto ParentEntity = Details.m_pPool->getComponent<xecs::component::parent>(Details.m_PoolIndex).m_Value;
            if (ParentEntity.isValid() == false) return {};

            auto& PDetails = GameMgr.m_ComponentMgr.getEntityDetails(ParentEntity);
            if (PDetails.m_pPool == nullptr) return {};
            const auto iChildrenType = PDetails.m_pPool->findIndexComponentFromInfo(xecs::component::type::info_v<xecs::component::children>);
            if (iChildrenType < 0) return {};

            auto& List = PDetails.m_pPool->getComponent<xecs::component::children>(PDetails.m_PoolIndex).m_List;
            auto  It    = std::find_if(List.begin(), List.end(), [&](auto& E) noexcept { return E.m_Value == Cur.m_Value; });
            if (It == List.end()) return {};

            ReversePath.push_back(static_cast<std::uint32_t>(std::distance(List.begin(), It)));
            Cur = ParentEntity;
        }
    }

    // Resolves a live entity handle of UNKNOWN owning scene (all the inspector ever has for an
    // xecs::component::entity_reference field) into a friendly label + which open scene owns it, by
    // scanning every currently-open scene's m_RuntimeToLocal - same label logic the Level tree's own
    // entity rows already use (Name component if present, else "Entity #Id"), just without a SceneGuid
    // known up front the way a tree row already has one. Only scenes the user has actually opened are
    // searched - a reference into a scene nobody opened this session simply can't be resolved to a
    // live handle yet (matches how the reference itself only round-trips through save/load, not
    // through any live lookup that would need every scene loaded just to inspect one entity).
    bool ResolveEntityReference(xecs::game_mgr::instance& GameMgr, editor_state& State, xecs::component::entity Entity, std::string& OutLabel, xecs::scene::guid& OutSceneGuid) noexcept
    {
        if (Entity.isValid() == false) return false;

        for (auto& SceneGuid : State.m_OpenScenes)
        {
            auto* pScene = GameMgr.m_SceneMgr.Find(SceneGuid);
            if (pScene == nullptr) continue;

            auto It = pScene->m_RuntimeToLocal.find(Entity.m_Value);
            if (It == pScene->m_RuntimeToLocal.end()) continue;

            const auto Id = It->second;
            OutLabel = std::format("Entity #{}", Id);
            if (auto& Details = GameMgr.m_ComponentMgr.getEntityDetails(Entity); Details.m_pPool)
            {
                auto Bits = Details.m_pPool->m_pArchetype->getComponentBits();
                if (Bits.getBit(xecs::component::type::info_v<e29::name>.m_BitID))
                    OutLabel = Details.m_pPool->getComponent<e29::name>(Details.m_PoolIndex).m_Value;
            }
            std::string SceneLabel;
            RemapGUIDToString(SceneLabel, xresource::full_guid{ SceneGuid.m_Instance, SceneGuid.m_Type });
            OutLabel += std::format(" ({})", SceneLabel);
            OutSceneGuid = SceneGuid;
            return true;
        }
        return false;
    }

    // Components that xECS itself attaches/manages internally (never meaningful to add/remove/edit
    // by hand): entity is the identity itself; parent/children carry raw xecs::component::entity
    // references, which the shared xproperty inspector has no rendering style for at all (asserts -
    // "UNHANDLED ATOMIC STYLE: TypeName='entity'" - the moment one is appended); ref_count/
    // share_filter/share_as_data_exclusive_tag are share-component bookkeeping; prefab::tag/root only
    // ever live on a prefab's own root entity (in mgr::m_PrefabList), never on a scene entity; and
    // editor::prefab_instance is this editor's own override-tracking bookkeeping, edited only through
    // the dedicated prefab-override UI. Centralized here so Add/Remove Component and the inspector
    // rebuild loop can't independently drift out of sync on this list.
    bool IsInternalComponent(const xecs::component::type::info* pInfo) noexcept
    {
        return pInfo == &xecs::component::type::info_v<xecs::component::entity>
            || pInfo == &xecs::component::type::info_v<xecs::component::parent>
            || pInfo == &xecs::component::type::info_v<xecs::component::children>
            || pInfo == &xecs::component::type::info_v<xecs::component::ref_count>
            || pInfo == &xecs::component::type::info_v<xecs::component::share_filter>
            || pInfo == &xecs::component::type::info_v<xecs::component::share_as_data_exclusive_tag>
            || pInfo == &xecs::component::type::info_v<xecs::prefab::tag>
            || pInfo == &xecs::component::type::info_v<xecs::prefab::root>
            || pInfo == &xecs::component::type::info_v<xecs::editor::prefab_instance>;
    }

    // Finds the override-tracking entry for a given (component type, group member) pair on a prefab
    // instance, creating one (as OVERRIDES) if none exists yet - fixes the old, never-finished
    // design's bug of always appending a new entry even when one already exists. MemberPath empty
    // means the prefab_instance-carrying entity itself (the only case that existed before
    // multi-entity groups); non-empty addresses a plain child/nested-instance-root member instead -
    // see prefab_component_override::m_MemberPath's own comment.
    xecs::editor::prefab_component_override& FindOrCreateOverrideEntry(xecs::editor::prefab_instance& PI, std::uint64_t ComponentTypeGuidValue, std::span<const std::uint32_t> MemberPath) noexcept
    {
        for (auto& C : PI.m_lComponents)
            if (C.m_ComponentTypeGuid == ComponentTypeGuidValue && std::ranges::equal(C.m_MemberPath, MemberPath)) return C;

        PI.m_lComponents.push_back(xecs::editor::prefab_component_override
        { .m_ComponentTypeGuid = ComponentTypeGuidValue
        , .m_MemberPath        = std::vector<std::uint32_t>(MemberPath.begin(), MemberPath.end())
        , .m_PropertyOverrides = {}
        });
        return PI.m_lComponents.back();
    }

    // Attaches a fresh (no overrides yet) prefab_instance component pointed at PrefabGuid onto
    // Entity, and re-registers the (possibly archetype-migrated - AddOrRemoveComponents returns a new
    // entity handle) result into Scene's local/runtime maps under Id. The common tail end of both
    // "instantiate a prefab into a scene" (Entity is brand new, Id not yet in the maps - the erase
    // below is just a harmless no-op) and "the entity just dragged out becomes an instance of the
    // prefab created from it" (Entity/Id already exist in the maps under the same Id).
    //
    // pState (nullable - InstantiatePrefabIntoScene's brand-new entity can never already be selected,
    // so it passes nullptr) matters for the OTHER caller, entity_to_prefab_drop::OnDrop: if the
    // dragged-out entity happened to be the one currently shown in the Entity Properties panel, this
    // migration invalidates State.m_SelectedEntity (a stale handle) AND every pool-memory address the
    // xproperty inspector cached for it (entity_inspector_bridge::m_ComponentMap, populated by the
    // m_bEntityInspectorDirty rebuild block in RenderEntityPropertiesPanel) - exactly like the entity
    // handle "Add Component"/"Remove Component" migrate, except NEITHER of those refreshed State nor
    // set the dirty flag afterward for THIS migration, since this function used to have no idea a
    // selection even existed. Without this fix, editing a property afterward through the still-
    // displayed, now-stale inspector hands OnPropertyChanged a dangling/reused pool address via
    // Cmd.m_pClassObject - a plausible root cause for "override a property, then Save -> invalidated
    // vector iterator" style corruption that only manifests through real UI interaction, never
    // through headless, data-only testing (which never drives State/the component map at all).
    void AttachPrefabInstanceComponent(xecs::game_mgr::instance& GameMgr, xecs::scene::instance& Scene, xecs::scene::permanent_id Id, xecs::component::entity Entity, xecs::prefab::guid PrefabGuid, editor_state* pState) noexcept
    {
        const bool bWasSelected = pState != nullptr && pState->m_SelectedEntity.m_Value == Entity.m_Value;

        xecs::component::entity NewEntity;

        // If Entity already carries editor::prefab_instance, it's the root of a NESTED prefab
        // instance (this prefab's own root wraps a DIFFERENT prefab - the "variant" case): the
        // engine's own instantiation already gave it correct live DATA (re-derived from the INNER
        // prefab's current state, inner-relative overrides applied), but its PI still identifies as
        // an instance of the INNER prefab, not the OUTER one the user actually just placed - without
        // stamping over it here, the scene entity is silently tracked under the wrong prefab guid
        // (asset-browser "reveal", future re-instantiation-of-this-prefab bookkeeping, etc. would all
        // point at the inner prefab instead of what was dragged in). No AddOrRemoveComponents needed
        // (the bit is already set, no archetype migration) - just overwrite the existing component's
        // fields directly. m_lComponents/m_ComponentDiffs are reset to empty rather than left as-is:
        // they were computed relative to the INNER prefab and would misleadingly describe "overrides"
        // relative to the wrong base; a save recomputes them fresh anyway
        // (RefreshPrefabInstanceOverlayRecord), so this loses no data - it only avoids a stale,
        // wrongly-labeled "differs from prefab" indicator in the Properties panel between placement
        // and the next save. Checked via findIndexComponentFromInfo (matches the per-component lookup
        // SaveGroupMember/LoadGroupMember already use), not getComponentBits().getBit() - see
        // [[xecs_getbit_vs_findindexcomponentfrominfo]].
        auto& ExistingDetails = GameMgr.m_ComponentMgr.getEntityDetails(Entity);
        if( ExistingDetails.m_pPool && ExistingDetails.m_pPool->findIndexComponentFromInfo(xecs::component::type::info_v<xecs::editor::prefab_instance>) >= 0 )
        {
            auto& PI = ExistingDetails.m_pPool->getComponent<xecs::editor::prefab_instance>(ExistingDetails.m_PoolIndex);
            PI.m_PrefabInstance = PrefabGuid;
            PI.m_lComponents.clear();
            PI.m_ComponentDiffs.clear();
            NewEntity = Entity;
        }
        else
        {
            std::array Add{ &xecs::component::type::info_v<xecs::editor::prefab_instance> };
            NewEntity = GameMgr.AddOrRemoveComponents(Entity, Add, {});
            auto& NewDetails = GameMgr.m_ComponentMgr.getEntityDetails(NewEntity);
            NewDetails.m_pPool->getComponent<xecs::editor::prefab_instance>(NewDetails.m_PoolIndex).m_PrefabInstance = PrefabGuid;
        }

        Scene.m_RuntimeToLocal.erase(Entity.m_Value);
        Scene.m_LocalToRuntime[Id]               = NewEntity;
        Scene.m_RuntimeToLocal[NewEntity.m_Value] = Id;

        if (bWasSelected)
        {
            pState->m_SelectedEntity        = NewEntity;
            pState->m_bEntityInspectorDirty = true;
        }
    }

    // Recursively registers every entity in a freshly-instantiated prefab subtree (Entity itself,
    // plus - if it has children - every descendant) into Scene's bookkeeping under a freshly minted
    // permanent_id each, marking each new. Shared by InstantiatePrefabIntoScene (the whole returned
    // group needs registering) and CreatePrefabFromGroupRoot (only the NEW group's children need fresh
    // ids - its root keeps a preserved one, registered separately by the caller).
    void RegisterInstantiatedSubtree(xecs::game_mgr::instance& GameMgr, xecs::scene::instance& Scene, xecs::scene::guid SceneGuid, xecs::component::entity Entity) noexcept
    {
        const auto Id = NextFreeEntityId(Scene);
        Scene.m_LocalToRuntime[Id]              = Entity;
        Scene.m_RuntimeToLocal[Entity.m_Value]  = Id;
        GameMgr.m_SceneMgr.MarkEntityNew(SceneGuid, Id);

        auto& Details = GameMgr.m_ComponentMgr.getEntityDetails(Entity);
        if (Details.m_pPool == nullptr) return;
        if (Details.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::component::children>.m_BitID) == false) return;

        // Snapshot - registering a child only ever touches Scene's own maps, never this entity's OWN
        // children list, so a plain copy is enough (no in-place-mutation hazard to guard against here).
        auto ChildEntities = Details.m_pPool->getComponent<xecs::component::children>(Details.m_PoolIndex).m_List;
        for (auto Child : ChildEntities)
            RegisterInstantiatedSubtree(GameMgr, Scene, SceneGuid, Child);
    }

    // Loads PrefabGuid (if not already resident) and instantiates it into Scene under a fresh
    // permanent_id - the shared tail of both the drag-a-prefab-onto-the-scene-tree flow and (until it
    // existed) the old "+ Instantiate Prefab" button. TargetFolder (invalid = loose, auto-adopted into
    // "Default" the next render pass - see EnsureDefaultFolder) lets a drop directly onto a specific
    // folder row land the new instance there instead of always defaulting away from wherever the user
    // actually dropped it.
    void InstantiatePrefabIntoScene(xecs::game_mgr::instance& GameMgr, xecs::scene::instance& Scene, xecs::prefab::guid PrefabGuid, xecs::scene::folder_id TargetFolder = xecs::scene::invalid_folder_id_v) noexcept
    {
        if (auto Err = GameMgr.m_PrefabMgr.EnsureLoaded(PrefabGuid); Err)
        {
            Debugger(std::format("Failed to load Prefab: {}", Err.getMessage()));
            return;
        }

        auto RootIt = GameMgr.m_PrefabMgr.m_PrefabList.find(PrefabGuid.m_Instance.m_Value);
        if (RootIt == GameMgr.m_PrefabMgr.m_PrefabList.end()) return;

        // bRemoveRoot=false - a multi-entity ("Scene-Prefab") root must survive instancing so it comes
        // back as a real, independent entity here; bRemoveRoot=true (the default) is for splicing a
        // prefab's CHILDREN directly onto a caller-supplied existing entity, discarding the prefab's
        // own root - not what this wants (this needs one standalone instantiated group, root included).
        auto NewRoot = GameMgr.m_PrefabMgr.CreatePrefabInstance(1, RootIt->second, xecs::tools::empty_lambda{}, /*bRemoveRoot=*/false);

        // Registers the whole group (root + every descendant, each under a freshly minted id).
        RegisterInstantiatedSubtree(GameMgr, Scene, Scene.m_Guid, NewRoot);

        const auto RootId = Scene.m_RuntimeToLocal.at(NewRoot.m_Value);
        if (TargetFolder != xecs::scene::invalid_folder_id_v)
            ReparentEntityIntoFolder(Scene, RootId, TargetFolder);
        AttachPrefabInstanceComponent(GameMgr, Scene, RootId, NewRoot, PrefabGuid, nullptr); // brand-new entity, can't already be selected
    }

    // Recursively deletes Entity and (if it has children) its whole live descendant subtree, scrubbing
    // scene bookkeeping/folder membership for each - the "whole group" analog of a single-entity
    // delete action.
    void DeleteEntitySubtree(xecs::game_mgr::instance& GameMgr, xecs::scene::instance& Scene, xecs::scene::guid SceneGuid, xecs::component::entity Entity) noexcept
    {
        auto& Details = GameMgr.m_ComponentMgr.getEntityDetails(Entity);

        // Scrub Entity out of its own parent's children list, if it has one - otherwise the parent
        // keeps holding a dangling handle to an entity that's about to stop existing, rendering as a
        // broken/empty expandable row. Only meaningful on the TOP-LEVEL call (the row actually
        // clicked) - a recursive call's own parent is itself being deleted this same pass, so
        // scrubbing it is harmless but moot; doing it unconditionally here is simpler than threading a
        // "is this the top call" flag through the recursion.
        if (Details.m_pPool && Details.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::component::parent>.m_BitID))
        {
            const auto ParentEntity = Details.m_pPool->getComponent<xecs::component::parent>(Details.m_PoolIndex).m_Value;
            if (ParentEntity.isValid())
            {
                auto& PDetails = GameMgr.m_ComponentMgr.getEntityDetails(ParentEntity);
                if (PDetails.m_pPool && PDetails.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::component::children>.m_BitID))
                {
                    auto& List = PDetails.m_pPool->getComponent<xecs::component::children>(PDetails.m_PoolIndex).m_List;
                    std::erase_if(List, [&](auto& E) noexcept { return E.m_Value == Entity.m_Value; });
                    if (auto ParentIt = Scene.m_RuntimeToLocal.find(ParentEntity.m_Value); ParentIt != Scene.m_RuntimeToLocal.end())
                        GameMgr.m_SceneMgr.MarkEntityDirty(SceneGuid, ParentIt->second);
                }
            }
        }

        if (Details.m_pPool && Details.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::component::children>.m_BitID))
        {
            auto ChildEntities = Details.m_pPool->getComponent<xecs::component::children>(Details.m_PoolIndex).m_List;
            for (auto Child : ChildEntities)
                DeleteEntitySubtree(GameMgr, Scene, SceneGuid, Child);
        }

        if (auto It = Scene.m_RuntimeToLocal.find(Entity.m_Value); It != Scene.m_RuntimeToLocal.end())
        {
            const auto Id = It->second;
            Scene.m_RuntimeToLocal.erase(It);
            Scene.m_LocalToRuntime.erase(Id);
            GameMgr.m_SceneMgr.MarkEntityDeleted(SceneGuid, Id);
            ReparentEntityIntoFolder(Scene, Id, xecs::scene::invalid_folder_id_v);
        }

        auto E = Entity;
        GameMgr.DeleteEntity(E);
    }

    // The Level tree's "Make Prefab" action - the multi-entity-aware counterpart of dragging a single
    // entity onto the asset browser (entity_to_prefab_drop, below). ClickedId is the row the context
    // menu/drag was started on; if it's part of a live multi-selection (2+ entities, all in this same
    // scene), the WHOLE selection becomes the group, otherwise just ClickedId alone.
    //
    // Root selection: a single selected entity becomes the root directly (covers "no children" and
    // "already has children" alike - CreatePrefabFromEntity/CloneEntityIntoPrefabGroup pulls in
    // children automatically). Multiple selected entities compute their "top-level" subset (those
    // whose parent, if any, isn't ALSO selected): exactly one top-level entity means the user
    // multi-selected an existing subtree - use it as the real root directly; otherwise (multiple
    // disjoint top-level entities) a synthetic root (Name + Children only) is created and every
    // top-level entity is reparented under it.
    xecs::component::entity DetermineGroupRoot(xecs::game_mgr::instance& GameMgr, xecs::scene::instance& Scene, xecs::scene::guid SceneGuid, editor_state& State, xecs::scene::permanent_id ClickedId) noexcept
    {
        std::vector<xecs::scene::permanent_id> SelectedIds;
        if (State.m_MultiSelectScene == SceneGuid && State.m_MultiSelectedEntityIds.size() > 1 && State.m_MultiSelectedEntityIds.contains(ClickedId))
            SelectedIds = State.m_MultiSelectOrder; // click order, not m_MultiSelectedEntityIds' own unordered iteration
        else
            SelectedIds.push_back(ClickedId);

        std::vector<xecs::component::entity> SelectedEntities;
        for (auto Id : SelectedIds)
            if (auto It = Scene.m_LocalToRuntime.find(Id); It != Scene.m_LocalToRuntime.end())
                SelectedEntities.push_back(It->second);
        std::printf("[MakePrefab] DetermineGroupRoot: %zu selected id(s), %zu resolved live entity(ies)\n", SelectedIds.size(), SelectedEntities.size());
        std::fflush(stdout);
        if (SelectedEntities.empty()) return {};

        if (SelectedEntities.size() == 1)
            return SelectedEntities.front();

        auto IsSelected = [&](xecs::component::entity E) noexcept
        {
            return std::find_if(SelectedEntities.begin(), SelectedEntities.end(), [&](auto& S) noexcept { return S.m_Value == E.m_Value; }) != SelectedEntities.end();
        };

        std::vector<xecs::component::entity> TopLevel;
        for (auto E : SelectedEntities)
        {
            auto& Details = GameMgr.m_ComponentMgr.getEntityDetails(E);
            bool bParentSelected = false;
            if (Details.m_pPool && Details.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::component::parent>.m_BitID))
                bParentSelected = IsSelected(Details.m_pPool->getComponent<xecs::component::parent>(Details.m_PoolIndex).m_Value);
            if (!bParentSelected) TopLevel.push_back(E);
        }
        std::printf("[MakePrefab] DetermineGroupRoot: %zu top-level entity(ies) among the selection\n", TopLevel.size());
        std::fflush(stdout);

        if (TopLevel.size() == 1)
            return TopLevel.front();

        // The synthetic root is brand new, so it has no history of its own to fall back on - without
        // this, it always starts loose and gets auto-adopted into "Default" the next render, even when
        // the entities it's about to wrap all came from the SAME real folder or the SAME real
        // scene-hierarchy parent. A real PARENT wins over folder membership - matching how "an entity
        // with a parent is never ALSO in a folder" already works everywhere else in this tree - falling
        // back to whichever folder (if any) the FIRST top-level entity was in when there's no external
        // parent, and using ITS choice when several top-level entities disagree.
        xecs::component::entity InheritedParent;
        if (auto& FirstDetails = GameMgr.m_ComponentMgr.getEntityDetails(TopLevel.front()); FirstDetails.m_pPool && FirstDetails.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::component::parent>.m_BitID))
            InheritedParent = FirstDetails.m_pPool->getComponent<xecs::component::parent>(FirstDetails.m_PoolIndex).m_Value;
        const auto InheritedFolderId = InheritedParent.isValid() ? xecs::scene::invalid_folder_id_v : FindFolderContaining(Scene, Scene.m_RuntimeToLocal.at(TopLevel.front().m_Value));

        auto& RootArchetype = GameMgr.getOrCreateArchetype<e29::name, xecs::component::children>();
        auto  Root          = RootArchetype.CreateEntity([&](e29::name& Name) noexcept { Name.m_Value = "Prefab Root"; });

        if (InheritedParent.isValid())
        {
            std::array Add{ &xecs::component::type::info_v<xecs::component::parent> };
            Root = GameMgr.AddOrRemoveComponents(Root, Add, {});
            auto& RootPDetails = GameMgr.m_ComponentMgr.getEntityDetails(Root);
            RootPDetails.m_pPool->getComponent<xecs::component::parent>(RootPDetails.m_PoolIndex).m_Value = InheritedParent;

            // Splice Root into whatever position the FIRST top-level entity held in ITS parent's own
            // children list, replacing it - the parent's list otherwise keeps pointing at that entity's
            // stale handle (about to be swapped for a fresh one below) instead of the new wrapper root.
            // Every OTHER top-level entity that ALSO happened to share this same external parent
            // (multi-selecting 2+ disjoint entities that are siblings under one real parent) must be
            // ERASED from this list entirely, not merely left alone - Root already represents the
            // whole group in the one spliced slot, and each of those other entities is ALSO about to
            // be migrated to a new handle by the reparent loop below, so leaving its OLD entry here
            // would be both a duplicate membership (appears under InheritedParent AND under Root) and
            // a dangling one (pointing at a handle the migration is about to invalidate).
            auto& ExtParentDetails = GameMgr.m_ComponentMgr.getEntityDetails(InheritedParent);
            if (ExtParentDetails.m_pPool && ExtParentDetails.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::component::children>.m_BitID))
            {
                auto& ExtChildren = ExtParentDetails.m_pPool->getComponent<xecs::component::children>(ExtParentDetails.m_PoolIndex).m_List;
                bool bSplicedRoot = false;
                std::erase_if(ExtChildren, [&](auto& C) noexcept
                {
                    const bool bIsTopLevelMember = std::find_if(TopLevel.begin(), TopLevel.end(), [&](auto& T) noexcept { return T.m_Value == C.m_Value; }) != TopLevel.end();
                    if (!bIsTopLevelMember) return false;
                    if (!bSplicedRoot) { C = Root; bSplicedRoot = true; return false; }
                    return true;
                });
            }
        }

        const auto RootId = NextFreeEntityId(Scene);
        Scene.m_LocalToRuntime[RootId]        = Root;
        Scene.m_RuntimeToLocal[Root.m_Value]  = RootId;
        GameMgr.m_SceneMgr.MarkEntityNew(SceneGuid, RootId);
        if (InheritedFolderId != xecs::scene::invalid_folder_id_v)
            ReparentEntityIntoFolder(Scene, RootId, InheritedFolderId);

        for (auto E : TopLevel)
        {
            const auto OldId = Scene.m_RuntimeToLocal.at(E.m_Value);

            std::array Add{ &xecs::component::type::info_v<xecs::component::parent> };
            auto NewE = GameMgr.AddOrRemoveComponents(E, Add, {});
            auto& NewDetails = GameMgr.m_ComponentMgr.getEntityDetails(NewE);
            NewDetails.m_pPool->getComponent<xecs::component::parent>(NewDetails.m_PoolIndex).m_Value = Root;

            Scene.m_RuntimeToLocal.erase(E.m_Value);
            Scene.m_LocalToRuntime[OldId]         = NewE;
            Scene.m_RuntimeToLocal[NewE.m_Value]  = OldId;
            GameMgr.m_SceneMgr.MarkEntityDirty(SceneGuid, OldId);

            auto& RootDetails = GameMgr.m_ComponentMgr.getEntityDetails(Root);
            RootDetails.m_pPool->getComponent<xecs::component::children>(RootDetails.m_PoolIndex).m_List.push_back(NewE);

            if (State.m_SelectedEntityId == OldId)
            {
                State.m_SelectedEntity        = NewE;
                State.m_bEntityInspectorDirty = true;
            }

            // Entities with a parent are excluded from folder membership entirely (rendered via their
            // parent's own row instead) - scrub whatever folder this entity was in.
            ReparentEntityIntoFolder(Scene, OldId, xecs::scene::invalid_folder_id_v);
        }

        return Root;
    }

    // Step 2: given a resolved group root (a real, live entity - either the single dragged/clicked
    // entity, an existing subtree's own root, or DetermineGroupRoot's synthetic one), creates the
    // Prefab asset (at LibraryGUID/ParentGUID - the caller's own drop target) and converts the
    // original live group into an instance of it, generalizing the single-entity "drag out becomes an
    // instance" behavior.
    xresource::full_guid CreatePrefabFromGroupRoot(xecs::game_mgr::instance& GameMgr, xecs::scene::instance& Scene, xecs::scene::guid SceneGuid, editor_state* pState, e10::library_mgr& AssetMgr, e10::library::guid LibraryGUID, xresource::full_guid ParentGUID, xecs::component::entity Root) noexcept
    {
        // If Root already had a parent in the live scene (e.g. a single child entity that's part of
        // some OTHER, unrelated hierarchy, or a whole existing subtree being grouped), that positional
        // link is NOT part of what gets persisted (a prefab root never carries its own parent) -
        // captured here so the freshly-instantiated root can be spliced back into the exact same
        // position afterward, rather than unexpectedly falling out to scene-root.
        xecs::component::entity OriginalParent;
        if (auto& RD = GameMgr.m_ComponentMgr.getEntityDetails(Root); RD.m_pPool && RD.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::component::parent>.m_BitID))
            OriginalParent = RD.m_pPool->getComponent<xecs::component::parent>(RD.m_PoolIndex).m_Value;

        const auto RootId           = Scene.m_RuntimeToLocal.at(Root.m_Value);
        const bool bRootWasSelected = pState && (pState->m_SelectedEntityId == RootId);
        const auto StaleRootValue   = Root.m_Value; // Root's own OLD live handle - about to be deleted; only ever compared, never dereferenced, below

        // Folder membership is keyed by RootId (a permanent_id, preserved across this whole
        // conversion) rather than by live entity handle, so in principle it wouldn't need capturing -
        // except DeleteEntitySubtree (below) explicitly scrubs it as part of deleting the OLD live
        // root (ReparentEntityIntoFolder(..., invalid_folder_id_v)), since from ITS point of view the
        // entity is simply being removed. Without capturing and restoring it here, RootId ends up in
        // no folder at all after re-registration, and the very next render's Default-folder auto-adopt
        // pass silently sweeps it into "Default".
        const auto OriginalFolderId = FindFolderContaining(Scene, RootId);

        std::string Name = "Prefab";
        if (auto& D = GameMgr.m_ComponentMgr.getEntityDetails(Root); D.m_pPool && D.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<e29::name>.m_BitID))
            Name = D.m_pPool->getComponent<e29::name>(D.m_PoolIndex).m_Value;

        const xresource::full_guid NewGuid   = AssetMgr.NewAsset(LibraryGUID, xresource::full_guid{ {}, xecs::prefab::type_guid_v }, ParentGUID, Name);
        const xecs::prefab::guid   PrefabGuid = NewGuid;

        std::printf("[MakePrefab] CreatePrefabFromGroupRoot: RootId=%u Name='%s' - cloning into prefab\n", RootId, Name.c_str());
        std::fflush(stdout);

        GameMgr.m_PrefabMgr.CreatePrefabFromEntity(Root, PrefabGuid);
        if (auto Err = GameMgr.m_PrefabMgr.Save(PrefabGuid); Err)
        {
            Debugger(std::format("Failed to save new Prefab: {}", Err.getMessage()));
            return {};
        }

        // Convert the original live group into an instance of the new prefab: delete the original
        // root+descendants, instantiate a fresh copy, splice it back into whatever OriginalParent
        // held, then register it under RootId's preserved permanent_id (so scene bookkeeping/
        // selection keep referencing "the same" entity) - every child gets a freshly minted id
        // instead (they're new scene entities, never existed as "an instance" before).
        DeleteEntitySubtree(GameMgr, Scene, SceneGuid, Root);

        auto NewRoot = GameMgr.m_PrefabMgr.CreatePrefabInstance(1, GameMgr.m_PrefabMgr.m_PrefabList.at(PrefabGuid.m_Instance.m_Value), xecs::tools::empty_lambda{}, /*bRemoveRoot=*/false);
        std::printf("[MakePrefab] CreatePrefabFromGroupRoot: instantiated fresh copy, NewRoot.isValid=%d NewRoot.isZombie=%d\n", NewRoot.isValid(), NewRoot.isZombie());
        std::fflush(stdout);

        if (OriginalParent.isValid())
        {
            std::array Add{ &xecs::component::type::info_v<xecs::component::parent> };
            NewRoot = GameMgr.AddOrRemoveComponents(NewRoot, Add, {});
            auto& NRDetails = GameMgr.m_ComponentMgr.getEntityDetails(NewRoot);
            NRDetails.m_pPool->getComponent<xecs::component::parent>(NRDetails.m_PoolIndex).m_Value = OriginalParent;

            auto& OPDetails = GameMgr.m_ComponentMgr.getEntityDetails(OriginalParent);
            if (OPDetails.m_pPool && OPDetails.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::component::children>.m_BitID))
            {
                auto& OPChildren = OPDetails.m_pPool->getComponent<xecs::component::children>(OPDetails.m_PoolIndex).m_List;
                for (auto& C : OPChildren)
                    if (C.m_Value == StaleRootValue) { C = NewRoot; break; }
            }
        }

        auto& NewChildDetails = GameMgr.m_ComponentMgr.getEntityDetails(NewRoot);
        if (NewChildDetails.m_pPool && NewChildDetails.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::component::children>.m_BitID))
        {
            auto ChildEntities = NewChildDetails.m_pPool->getComponent<xecs::component::children>(NewChildDetails.m_PoolIndex).m_List;
            std::printf("[MakePrefab] CreatePrefabFromGroupRoot: NewRoot has %zu child(ren) to register\n", ChildEntities.size());
            std::fflush(stdout);
            for (auto Child : ChildEntities)
                RegisterInstantiatedSubtree(GameMgr, Scene, SceneGuid, Child);
        }

        Scene.m_LocalToRuntime[RootId]           = NewRoot;
        Scene.m_RuntimeToLocal[NewRoot.m_Value]  = RootId;

        // Restore RootId's folder membership, scrubbed by DeleteEntitySubtree above - but only when
        // the root did NOT get a parent restored (an entity with a parent is never ALSO placed via
        // folder membership - the parent becomes the folder, per this whole tree's own convention).
        if (false == OriginalParent.isValid())
            ReparentEntityIntoFolder(Scene, RootId, OriginalFolderId);

        AttachPrefabInstanceComponent(GameMgr, Scene, RootId, NewRoot, PrefabGuid, pState);
        GameMgr.m_SceneMgr.MarkEntityDirty(SceneGuid, RootId);

        if (pState)
        {
            pState->m_MultiSelectedEntityIds.clear();
            pState->m_MultiSelectOrder.clear();
            if (bRootWasSelected)
            {
                if (auto It = Scene.m_LocalToRuntime.find(RootId); It != Scene.m_LocalToRuntime.end())
                {
                    pState->m_SelectedEntity        = It->second;
                    pState->m_SelectedEntityScene   = SceneGuid;
                    pState->m_bEntityInspectorDirty = true;
                }
            }
            else if (pState->m_SelectedEntityScene == SceneGuid && pState->m_SelectedEntityId != xecs::scene::invalid_permanent_id_v)
            {
                // The previously-selected entity might have been one of the OTHER group members (a
                // non-root one). Non-root members get deleted and replaced with a FRESH entity under
                // a FRESH id (RegisterInstantiatedSubtree), so there's no principled "same identity"
                // to preserve for them the way the root's own preserved RootId gives one - if the
                // id/handle pairing no longer matches what's actually live, the safe move is to clear
                // the selection rather than leave pState->m_SelectedEntity holding a stale handle into
                // an entity that DeleteEntitySubtree already destroyed (a stale handle used later
                // trips xECS's own generation/validation assert).
                auto It = Scene.m_LocalToRuntime.find(pState->m_SelectedEntityId);
                if (It == Scene.m_LocalToRuntime.end() || It->second.m_Value != pState->m_SelectedEntity.m_Value)
                {
                    pState->m_SelectedEntityId      = xecs::scene::invalid_permanent_id_v;
                    pState->m_SelectedEntity        = {};
                    pState->m_SelectedEntityScene   = {};
                    pState->m_bEntityInspectorDirty = true;
                }
            }
        }

        std::printf("[MakePrefab] CreatePrefabFromGroupRoot: done, RootId=%u still resident=%d\n", RootId, Scene.m_LocalToRuntime.contains(RootId));
        std::fflush(stdout);

        return NewGuid;
    }

    // Payload for dragging a scene entity onto an asset-browser folder to create a Prefab from it -
    // registered against e10::external_drop_registration_base (see E10_AssetBrowser.h) so the browser
    // can accept it without knowing anything about xECS/scenes. Carries the scene guid + the entity's
    // scene-local permanent_id rather than a live xecs::component::entity handle, since the handle
    // itself is only guaranteed valid for the frame it was captured in - re-resolving it through the
    // scene's own maps at drop time is what makes this safe across the drag's lifetime.
    struct entity_drag_payload_t
    {
        xecs::scene::guid          m_SceneGuid;
        xecs::scene::permanent_id  m_Id;
    };

    // Set once, near the top of the owning example's setup, so entity_to_prefab_drop::OnDrop (a
    // static, globally-registered object constructed long before GameMgr/State exist) can reach the
    // live editor state at drop time. Matches this codebase's existing convention for singleton editor
    // state (e10::g_LibMgr, xresource::g_Mgr, e29::g_AssetBrowserPopup) - an example built on this kit
    // only ever runs one instance of itself, so this isn't introducing a new kind of assumption.
    inline xecs::game_mgr::instance* g_pGameMgr = nullptr;
    inline editor_state*             g_pState   = nullptr;

    // Unity's own "Prefab Variant" fast path: dragging a SINGLE existing prefab instance (no other
    // entity in the active selection) into the asset browser creates a variant WITHOUT touching the
    // scene object's own live identity - Unity re-points that same GameObject's prefab connection at
    // the new variant rather than deleting and recreating it. Deliberately narrower than
    // CreatePrefabFromGroupRoot (which always deletes+recreates): a multi-select group has no single
    // existing identity to preserve in the first place (a brand-new synthetic root is minted either
    // way), and a PLAIN entity (never instanced) has no existing prefab connection to re-point - both
    // of those keep going through the general path unchanged.
    xresource::full_guid CreatePrefabVariantFromInstance(xecs::game_mgr::instance& GameMgr, xecs::scene::instance& Scene, xecs::scene::permanent_id Id, xecs::component::entity Entity, e10::library_mgr& AssetMgr, e10::library::guid LibraryGUID, xresource::full_guid ParentGUID) noexcept
    {
        std::string Name = "Prefab";
        if (auto& D = GameMgr.m_ComponentMgr.getEntityDetails(Entity); D.m_pPool && D.m_pPool->findIndexComponentFromInfo(xecs::component::type::info_v<e29::name>) >= 0)
            Name = D.m_pPool->getComponent<e29::name>(D.m_PoolIndex).m_Value;

        const xresource::full_guid NewGuid   = AssetMgr.NewAsset(LibraryGUID, xresource::full_guid{ {}, xecs::prefab::type_guid_v }, ParentGUID, Name);
        const xecs::prefab::guid   PrefabGuid = NewGuid;

        std::printf("[MakePrefab] CreatePrefabVariantFromInstance: Id=%u Name='%s' - capturing into a variant, live entity untouched\n", Id, Name.c_str());
        std::fflush(stdout);

        GameMgr.m_PrefabMgr.CreatePrefabFromEntity(Entity, PrefabGuid);
        if (auto Err = GameMgr.m_PrefabMgr.Save(PrefabGuid); Err)
        {
            Debugger(std::format("Failed to save new Prefab: {}", Err.getMessage()));
            return {};
        }

        // Re-point the SAME live entity's own bookkeeping at the new variant - no deletion, no fresh
        // instantiation needed: this entity's current data IS already exactly what a fresh instance of
        // the new variant looks like, since it's what the variant was just captured FROM. Overrides are
        // cleared (matching AttachPrefabInstanceComponent's own reasoning) since they were computed
        // relative to whatever this entity pointed at BEFORE - a save recomputes them fresh regardless.
        auto& Details = GameMgr.m_ComponentMgr.getEntityDetails(Entity);
        auto& PI = Details.m_pPool->getComponent<xecs::editor::prefab_instance>(Details.m_PoolIndex);
        PI.m_PrefabInstance = PrefabGuid;
        PI.m_lComponents.clear();
        PI.m_ComponentDiffs.clear();
        GameMgr.m_SceneMgr.MarkEntityDirty(Scene.m_Guid, Id);

        return NewGuid;
    }

    struct entity_to_prefab_drop final : e10::external_drop_registration_base
    {
        entity_to_prefab_drop() noexcept : e10::external_drop_registration_base{ "E29_ENTITY_DRAG" } {}

        xresource::full_guid OnDrop(e10::library_mgr& AssetMgr, e10::library::guid LibraryGUID, xresource::full_guid ParentGUID, const void* pData, std::size_t Size) const noexcept override
        {
            if (Size != sizeof(entity_drag_payload_t) || g_pGameMgr == nullptr) return {};
            auto& Payload = *reinterpret_cast<const entity_drag_payload_t*>(pData);

            auto* pScene = g_pGameMgr->m_SceneMgr.Find(Payload.m_SceneGuid);
            if (pScene == nullptr) return {};

            auto SourceIt = pScene->m_LocalToRuntime.find(Payload.m_Id);
            if (SourceIt == pScene->m_LocalToRuntime.end()) return {};

            // Single-instance Prefab Variant fast path - see CreatePrefabVariantFromInstance's own
            // comment. Only when NOT part of a real (2+) active multi-selection, and only when the
            // dragged entity already carries editor::prefab_instance.
            const bool bIsMultiSelect = g_pState && g_pState->m_MultiSelectScene == Payload.m_SceneGuid && g_pState->m_MultiSelectedEntityIds.size() > 1 && g_pState->m_MultiSelectedEntityIds.contains(Payload.m_Id);
            if (!bIsMultiSelect)
            {
                auto& SourceDetails = g_pGameMgr->m_ComponentMgr.getEntityDetails(SourceIt->second);
                if (SourceDetails.m_pPool && SourceDetails.m_pPool->findIndexComponentFromInfo(xecs::component::type::info_v<xecs::editor::prefab_instance>) >= 0)
                    return CreatePrefabVariantFromInstance(*g_pGameMgr, *pScene, Payload.m_Id, SourceIt->second, AssetMgr, LibraryGUID, ParentGUID);
            }

            // If the dragged entity is part of an active multi-selection (2+, ctrl-clicked in the
            // Level tree, this same scene), the WHOLE selection becomes the prefab's group - this is
            // the primary way to make a multi-entity prefab (drag-and-drop, exactly like the existing
            // single-entity flow, just generalized): ctrl-click to build a selection, then drag any
            // one of the selected rows onto the asset browser, same as before. A single dragged entity
            // with no active multi-selection behaves exactly as it always has.
            auto Root = g_pState ? DetermineGroupRoot(*g_pGameMgr, *pScene, Payload.m_SceneGuid, *g_pState, Payload.m_Id)
                                 : pScene->m_LocalToRuntime.find(Payload.m_Id)->second;
            if (Root.isValid() == false) return {};

            return CreatePrefabFromGroupRoot(*g_pGameMgr, *pScene, Payload.m_SceneGuid, g_pState, AssetMgr, LibraryGUID, ParentGUID, Root);
        }
    };
    inline static entity_to_prefab_drop g_EntityToPrefabDrop{};

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

        void RegisterCallbacks(xproperty::inspector& Inspector, xecs::game_mgr::instance& GameMgr, editor_state& State) noexcept
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
            m_OnPropertyChanged = [this, &GameMgr, &State](xproperty::inspector&, const xproperty::ui::undo::cmd& Cmd)
            {
                if (m_bSuppressOverrideTracking) return;

                auto It = m_ComponentMap.find(Cmd.m_pClassObject);
                if (It == m_ComponentMap.end()) return;

                // Every commit through the inspector changes this entity's live data, whether or not
                // it's a prefab instance/override - the rest of this callback only maintains
                // prefab-override bookkeeping, which is a separate concern from "does this entity need
                // (re)saving at all".
                GameMgr.m_SceneMgr.MarkEntityDirty(State.m_SelectedEntityScene, State.m_SelectedEntityId);

                auto Ctx = e29::FindContainingPrefabInstance(GameMgr, State.m_SelectedEntity);
                if (Ctx.m_pPI == nullptr) return;

                // The override entry just below is written into Ctx.m_pPI, which lives on
                // Ctx.m_RootEntity - a DIFFERENT entity than the one just edited whenever m_MemberPath
                // is non-empty (a plain member's own property changed, tracked on the containing
                // instance's root). Without this, only the edited member's own file gets re-saved; the
                // root's own "Components[]" override list - the ONLY on-disk record of the override at
                // all - silently never gets written, since nothing ever marked that entity dirty.
                if (Ctx.m_RootEntity.m_Value != State.m_SelectedEntity.m_Value)
                {
                    if (auto* pScene = GameMgr.m_SceneMgr.Find(State.m_SelectedEntityScene))
                    {
                        if (auto RootIt = pScene->m_RuntimeToLocal.find(Ctx.m_RootEntity.m_Value); RootIt != pScene->m_RuntimeToLocal.end())
                            GameMgr.m_SceneMgr.MarkEntityDirty(State.m_SelectedEntityScene, RootIt->second);
                    }
                }

                auto& CompOverride = e29::FindOrCreateOverrideEntry(*Ctx.m_pPI, It->second->m_Guid.m_Value, Ctx.m_MemberPath);

                // m_PropertyValueAsString is a read-only echo for humans/tools (see its own comment) -
                // the ECS itself never reads it back, but since it CAN go stale (the same property
                // edited a second time to a different value), every commit refreshes it rather than
                // only setting it once at creation and leaving later edits unreflected.
                std::array<char, 256> ValueBuffer{};
                const auto             ValueLen = xproperty::settings::AnyToString(ValueBuffer, Cmd.m_NewValue);
                const std::string       ValueStr(ValueBuffer.data(), ValueLen > 0 ? static_cast<std::size_t>(ValueLen) : 0);

                for (auto& O : CompOverride.m_PropertyOverrides)
                {
                    if (O.m_PropertyName == Cmd.m_Name)
                    {
                        O.m_PropertyValueAsString = ValueStr;
                        return;
                    }
                }

                CompOverride.m_PropertyOverrides.push_back(xecs::editor::prefab_property_override{ .m_PropertyName = Cmd.m_Name, .m_PropertyValueAsString = ValueStr });
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
                if (ImGui::SmallButton("X")) m_pPendingRemoveComponent = pInfo;
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
            m_OnEntityReferenceRender = [&GameMgr, &State](xproperty::inspector& Inspector, const xproperty::type::object& Obj, void* pInstance, std::string_view Path, const xproperty::any& Value, bool& bHandled)
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

                                if (!bRefused)
                                {
                                    xproperty::settings::context Context;
                                    xproperty::any               NewValue;
                                    NewValue.set<xecs::component::entity>(It->second);
                                    std::string SetError;
                                    Inspector.BeginEdit(Obj, pInstance, "Assign Entity Reference");
                                    xproperty::sprop::setProperty(SetError, pInstance, Obj, xproperty::sprop::container::prop{ std::string(Path), NewValue }, Context);
                                    Inspector.CommitEdit(Context);
                                }
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                if (bShowClear)
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("X"))
                    {
                        xproperty::settings::context Context;
                        xproperty::any               Cleared;
                        Cleared.set<xecs::component::entity>({});
                        std::string SetError;
                        Inspector.BeginEdit(Obj, pInstance, "Clear Entity Reference");
                        xproperty::sprop::setProperty(SetError, pInstance, Obj, xproperty::sprop::container::prop{ std::string(Path), Cleared }, Context);
                        Inspector.CommitEdit(Context);
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

#endif // E29_LEVEL_SCENE_EDITOR_KIT_H
