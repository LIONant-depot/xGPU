#include "source/xGPU.h"

#include "dependencies/xmath/source/xmath.h"
#include "dependencies/xproperty/source/xcore/my_properties.h"
#include "dependencies/xproperty/source/examples/imgui/xPropertyImGuiInspector.h"
#include "dependencies/xstrtool/source/xstrtool.h"

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
// E29 - Level + Scene editor.
//
// First real consumer of xECSV2's scene system (dependency-ordered scene load/unload, permanent-ID
// entity serialization - see dependencies/xECSV2/doc/xecs_scene*.md) as actual, resource-pipeline-
// integrated Level/Scene resource types: browse/create Levels (a list of member Scenes) and Scenes
// (dependency edges to parent scenes + the entities that live in them), create/delete entities,
// edit their components through the same xproperty inspector every other editor uses. Raw entities
// only this pass - no prefab placement/instancing UI, no 3D viewport (this is a pure data editor).
//
//-----------------------------------------------------------------------------------

namespace e29
{
    static void Debugger(std::string_view View)
    {
        // Flushed unconditionally - stdout redirected to a file is fully buffered rather than
        // line-buffered, so without this, a crash (or a debug-assert dialog that blocks the process
        // indefinitely) silently loses whatever log lines hadn't been flushed yet - exactly the
        // "can't tell what happened right before the crash" gap that makes these bugs hard to chase.
        printf("%s\n", View.data());
        fflush(stdout);
    }

    //---------------------------------------------------------------------------
    // Starter components - just enough for entities to have something to create/edit. A real game
    // would register its own gameplay components the same way (GameMgr.RegisterComponents<...>()).
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

    struct transform
    {
        constexpr static auto typedef_v = xecs::component::type::data{ .m_pName = "Transform" };

        // Position/Rotation/Scale via xmath::fvec3 - same shape Unity/Unreal/Godot's own Transform
        // uses, and the same convention this codebase's own descriptors already follow (see
        // xskeleton_desc::transform in xskeleton_descriptor.h: Scale/Rotation/Translation as
        // xmath::fvec3, Rotation stored in radians as Euler angles).
        xmath::fvec3 m_Position = xmath::fvec3::fromZero();
        xmath::fvec3 m_Rotation = xmath::fvec3::fromZero(); // radians
        xmath::fvec3 m_Scale    = xmath::fvec3::fromOne();

        XPROPERTY_DEF
        ( "Transform", transform
        , obj_member<"Position", &transform::m_Position>
        , obj_member<"Rotation", &transform::m_Rotation>
        , obj_member<"Scale",    &transform::m_Scale>
        )
    };
    XPROPERTY_REG(transform)

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

    // Moves Id into TargetFolder (invalid_folder_id_v = "loose", no folder), removing it from
    // whichever folder currently lists it first - folders own their membership by containment (see
    // xecs_scene.h's folder comment), so "reparent" is just "erase from the old owner, append to the
    // new one" rather than updating any per-entity back-pointer.
    void ReparentEntityIntoFolder(xecs::scene::instance& Scene, xecs::scene::permanent_id Id, xecs::scene::folder_id TargetFolder) noexcept
    {
        for( auto& F : Scene.m_Folders )
            std::erase(F.m_Entities, Id);

        if( TargetFolder == xecs::scene::invalid_folder_id_v ) return;
        if( auto It = std::find_if(Scene.m_Folders.begin(), Scene.m_Folders.end(), [&](auto& F) noexcept { return F.m_Id == TargetFolder; }); It != Scene.m_Folders.end() )
            It->m_Entities.push_back(Id);
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
        if (Details.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::editor::prefab_instance>.m_BitID) == false)
            return nullptr;
        return &Details.m_pPool->getComponent<xecs::editor::prefab_instance>(Details.m_PoolIndex);
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

    // Finds the override-tracking entry for a given component type on a prefab instance, creating one
    // (as OVERRIDES) if none exists yet - fixes the old, never-finished design's bug of always
    // appending a new entry even when one already exists for that component type.
    xecs::editor::prefab_component_override& FindOrCreateOverrideEntry(xecs::editor::prefab_instance& PI, std::uint64_t ComponentTypeGuidValue) noexcept
    {
        for (auto& C : PI.m_lComponents)
            if (C.m_ComponentTypeGuid == ComponentTypeGuidValue) return C;

        PI.m_lComponents.push_back(xecs::editor::prefab_component_override
        { .m_ComponentTypeGuid = ComponentTypeGuidValue
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
    // xproperty inspector cached for it (EntityInspectorComponentMap, populated by the
    // m_bEntityInspectorDirty rebuild block in E29_Example) - exactly like the entity handle
    // "Add Component"/"Remove Component" migrate, except NEITHER of those refreshed State nor set the
    // dirty flag afterward for THIS migration, since this function used to have no idea a selection
    // even existed. Without this fix, editing a property afterward through the still-displayed, now-
    // stale inspector hands OnPropertyChanged a dangling/reused pool address via Cmd.m_pClassObject -
    // a plausible root cause for "override a property, then Save -> invalidated vector iterator"
    // style corruption that only manifests through real UI interaction, never through headless,
    // data-only testing (which never drives State/EntityInspectorComponentMap at all).
    void AttachPrefabInstanceComponent(xecs::game_mgr::instance& GameMgr, xecs::scene::instance& Scene, xecs::scene::permanent_id Id, xecs::component::entity Entity, xecs::prefab::guid PrefabGuid, editor_state* pState) noexcept
    {
        const bool bWasSelected = pState != nullptr && pState->m_SelectedEntity.m_Value == Entity.m_Value;

        std::array Add{ &xecs::component::type::info_v<xecs::editor::prefab_instance> };
        auto NewEntity = GameMgr.AddOrRemoveComponents(Entity, Add, {});
        auto& NewDetails = GameMgr.m_ComponentMgr.getEntityDetails(NewEntity);
        NewDetails.m_pPool->getComponent<xecs::editor::prefab_instance>(NewDetails.m_PoolIndex).m_PrefabInstance = PrefabGuid;

        Scene.m_RuntimeToLocal.erase(Entity.m_Value);
        Scene.m_LocalToRuntime[Id]               = NewEntity;
        Scene.m_RuntimeToLocal[NewEntity.m_Value] = Id;

        if (bWasSelected)
        {
            pState->m_SelectedEntity        = NewEntity;
            pState->m_bEntityInspectorDirty = true;
        }
    }

    // Loads PrefabGuid (if not already resident) and instantiates it into Scene under a fresh
    // permanent_id - the shared tail of both the drag-a-prefab-onto-the-scene-tree flow and (until it
    // existed) the old "+ Instantiate Prefab" button.
    void InstantiatePrefabIntoScene(xecs::game_mgr::instance& GameMgr, xecs::scene::instance& Scene, xecs::prefab::guid PrefabGuid) noexcept
    {
        if (auto Err = GameMgr.m_PrefabMgr.EnsureLoaded(PrefabGuid); Err)
        {
            Debugger(std::format("Failed to load Prefab: {}", Err.getMessage()));
            return;
        }

        auto RootIt = GameMgr.m_PrefabMgr.m_PrefabList.find(PrefabGuid.m_Instance.m_Value);
        if (RootIt == GameMgr.m_PrefabMgr.m_PrefabList.end()) return;

        auto NewEntity = GameMgr.m_PrefabMgr.CreatePrefabInstance(1, RootIt->second, xecs::tools::empty_lambda{});
        const auto Id  = NextFreeEntityId(Scene);

        // Register under Id first (AttachPrefabInstanceComponent's erase-old/insert-new dance expects
        // Entity to already be reachable via Entity.m_Value for the "convert existing entity" case;
        // for a brand-new instance neither map has an entry yet, so this insert is what makes the
        // erase-then-reinsert inside it a no-op-then-real-insert instead of losing the registration).
        Scene.m_LocalToRuntime[Id]              = NewEntity;
        Scene.m_RuntimeToLocal[NewEntity.m_Value] = Id;
        GameMgr.m_SceneMgr.MarkEntityNew(Scene.m_Guid, Id);
        AttachPrefabInstanceComponent(GameMgr, Scene, Id, NewEntity, PrefabGuid, nullptr); // brand-new entity, can't already be selected
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

    // Set once, near the top of E29_Example(), so entity_to_prefab_drop::OnDrop (a static, globally-
    // registered object constructed long before GameMgr/State exist) can reach the live editor state
    // at drop time. Matches this codebase's existing convention for singleton editor state
    // (e10::g_LibMgr, xresource::g_Mgr, e29::g_AssetBrowserPopup) - E29 only ever runs one instance of
    // itself, so this isn't introducing a new kind of assumption.
    inline xecs::game_mgr::instance* g_pGameMgr = nullptr;
    inline editor_state*             g_pState   = nullptr;

    struct entity_to_prefab_drop final : e10::external_drop_registration_base
    {
        entity_to_prefab_drop() noexcept : e10::external_drop_registration_base{ "E29_ENTITY_DRAG" } {}

        xresource::full_guid OnDrop(e10::library_mgr& AssetMgr, e10::library::guid LibraryGUID, xresource::full_guid ParentGUID, const void* pData, std::size_t Size) const noexcept override
        {
            if (Size != sizeof(entity_drag_payload_t) || g_pGameMgr == nullptr) return {};
            auto& Payload = *reinterpret_cast<const entity_drag_payload_t*>(pData);

            auto* pScene = g_pGameMgr->m_SceneMgr.Find(Payload.m_SceneGuid);
            if (pScene == nullptr) return {};

            auto It = pScene->m_LocalToRuntime.find(Payload.m_Id);
            if (It == pScene->m_LocalToRuntime.end()) return {};
            auto Entity = It->second;

            // Name the new asset after the entity's own Name component when it has one.
            std::string Name = "Prefab";
            if (auto& Details = g_pGameMgr->m_ComponentMgr.getEntityDetails(Entity); Details.m_pPool
                && Details.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<e29::name>.m_BitID))
                Name = Details.m_pPool->getComponent<e29::name>(Details.m_PoolIndex).m_Value;

            const xresource::full_guid NewGuid = AssetMgr.NewAsset(LibraryGUID, xresource::full_guid{ {}, xecs::prefab::type_guid_v }, ParentGUID, Name);
            const xecs::prefab::guid   PrefabGuid = NewGuid;

            g_pGameMgr->m_PrefabMgr.CreatePrefabFromEntity(Entity, PrefabGuid);
            if (auto Err = g_pGameMgr->m_PrefabMgr.Save(PrefabGuid); Err)
            {
                Debugger(std::format("Failed to save new Prefab: {}", Err.getMessage()));
                return {};
            }

            // Unity-style: the entity that was dragged out becomes an instance of the prefab it just
            // spawned, rather than being left behind as an untouched raw entity. Passing g_pState lets
            // this refresh the selection/inspector if Entity was the one currently shown - see
            // AttachPrefabInstanceComponent's own comment for why that matters here specifically.
            AttachPrefabInstanceComponent(*g_pGameMgr, *pScene, Payload.m_Id, Entity, PrefabGuid, g_pState);
            g_pGameMgr->m_SceneMgr.MarkEntityDirty(Payload.m_SceneGuid, Payload.m_Id); // pre-existing entity, its data just changed in place

            return NewGuid;
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
            if (auto* pScene = GameMgr.m_SceneMgr.Find(SceneGuid))
                Debugger(std::format("SaveEverything: scene has {} pending entity change(s)", pScene->m_PendingChanges.size()));
            if (auto Err = GameMgr.m_SceneMgr.SaveScene(SceneGuid); Err)
                Debugger(std::format("Failed to save Scene: {}", Err.getMessage()));
        }

        xproperty::settings::context Context;
        e10::g_LibMgr.Save(Context);
    }
}

//-----------------------------------------------------------------------------------

int E29_Example()
{
    xgpu::instance Instance;
    if (auto Err = xgpu::CreateInstance(Instance, { .m_bDebugMode = true, .m_pLogErrorFunc = e29::Debugger, .m_pLogWarning = e29::Debugger }); Err)
        return xgpu::getErrorInt(Err);

    xgpu::device Device;
    if (auto Err = Instance.Create(Device); Err)
        return xgpu::getErrorInt(Err);

    xgpu::window MainWindow;
    if (auto Err = Device.Create(MainWindow, {}); Err)
        return xgpu::getErrorInt(Err);

    xresource::g_Mgr.Initiallize(20000);

    //
    // Setup Imgui interface
    //
    xgpu::tools::imgui::CreateInstance(MainWindow);
    ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = 0.85f;

    //
    // ECS setup - first xGPU example to own an xecs::game_mgr::instance.
    //
    xecs::game_mgr::instance GameMgr;
    GameMgr.RegisterComponents<e29::name, e29::transform, xecs::editor::prefab_instance, xecs::component::entity_reference>();
    GameMgr.RegisterSystems<>(); // locks component bit IDs - required even with zero systems

    //
    // Project path (same lookup every editor example uses)
    //
    {
        TCHAR szFileName[MAX_PATH];
        GetModuleFileName(NULL, szFileName, MAX_PATH);

        if (auto I = xstrtool::findI(std::wstring{ szFileName }, { L"xGPU" }); I != std::string::npos)
        {
            I += 4; // Skip the xGPU part
            szFileName[I] = 0;

            TCHAR LIONantProject[] = L"\\example.lionprj";
            for (int i = 0; szFileName[I++] = LIONantProject[i]; ++i);

            if (auto Err = e10::g_LibMgr.OpenProject(szFileName); Err)
            {
                e29::Debugger(Err.getMessage());
                return 1;
            }

            ImGuiIO& io = ImGui::GetIO();
            static std::string IniSave = std::format("{}/Assets/imgui_e29.ini", xstrtool::To(szFileName));
            io.IniFilename = IniSave.c_str();

            GameMgr.m_SceneMgr.m_ProjectPath  = e10::g_LibMgr.m_ProjectPath;
            GameMgr.m_LevelMgr.m_ProjectPath  = e10::g_LibMgr.m_ProjectPath;
            GameMgr.m_PrefabMgr.m_ProjectPath = e10::g_LibMgr.m_ProjectPath;
        }
    }

    //
    // Asset browser + editor state
    //
    e10::assert_browser  AsserBrowser;
    e29::editor_state    State;

    // Lets entity_to_prefab_drop::OnDrop (a static, globally-registered object) reach the live
    // GameMgr/State at drop time - see their own declaration comment for why this is safe here.
    e29::g_pGameMgr = &GameMgr;
    e29::g_pState   = &State;

    // Visible from the start and never closable - browsing/creating Levels and Scenes is this
    // editor's primary activity (not an occasional lookup), so it's a permanent, dockable part of the
    // layout rather than a modal picker: DOCKABLE drops the bottom Close button and lets it dock like
    // Level Editor/Entity Properties instead of floating as an undockable overlay. (The "+" pickers
    // elsewhere in this file use a separate e10::assert_browser instance, g_AssetBrowserPopup, which
    // stays at the POPUP default.)
    AsserBrowser.setDisplayMode(e10::assert_browser::display_mode::DOCKABLE);
    AsserBrowser.Show(true);

    //
    // Entity component inspector - the currently-selected entity's components.
    //
    xproperty::inspector EntityInspector("Entity Properties");
    EntityInspector.m_OnResourceWigzmos.Register<[](xproperty::inspector&, const xproperty::type::object&, void*, std::string_view, bool& bOpen, const xresource::full_guid& PreFullGuid)
    {
        e29::RenderResourceWigzmos(bOpen, PreFullGuid);
    }>();
    EntityInspector.m_OnResourceBrowser.Register<[](xproperty::inspector&, const xproperty::type::object&, void*, std::string_view Path, bool& bOpen, xresource::full_guid& Out, std::span<const xresource::type_guid> Filters)
    {
        const void* pUID = reinterpret_cast<const void*>(std::hash<std::string_view>{}(Path));
        e29::ResourceBrowserPopup(pUID, bOpen, Out, Filters);
    }>();

    // Inspector-to-override pipeline: the inspector's callbacks only give us (type::object&, void*
    // pInstance) - EntityInspectorComponentMap closes the gap back to "which xECS component type is
    // this", rebuilt every time the inspector's content is (see the m_bEntityInspectorDirty block
    // below). bSuppressOverrideTracking guards the one re-entrancy risk: m_OnOverrideReset's own
    // BeginEdit/CommitEdit bracket (used to write the prefab's base value back into the instance)
    // fires m_OnChangeEvent itself once committed - without the guard, a revert would immediately
    // re-record the very override it just removed.
    std::unordered_map<void*, const xecs::component::type::info*> EntityInspectorComponentMap;
    bool                                                            bSuppressOverrideTracking = false;

    // Set by OnComponentHeaderRender (below) when its "[X]" is clicked - processed once, right after
    // EntityInspector.Show(...) returns for the frame, rather than mutating the entity's archetype
    // WHILE the inspector is still mid-iteration over this same entity's component list (the same
    // reason the entity-tree's own delete "X" button breaks out of its loop immediately after
    // mutating, rather than continuing to iterate a now-stale range).
    const xecs::component::type::info* pPendingRemoveComponent = nullptr;

    // xdelegate::Register(T_CLASS&) binds to the lambda OBJECT itself (by reference) rather than
    // copying/erasing it into a std::function - so each callback must be a named local that outlives
    // the registration (matching E20_Material_Instance_Editor.cpp/E21_StaticGeom_Editor.cpp's own
    // CallbackWhenPropsChanges pattern), not a temporary passed straight into Register(...).
    auto OnPropertyChanged = [&](xproperty::inspector&, const xproperty::ui::undo::cmd& Cmd)
    {
        if (bSuppressOverrideTracking) return;

        auto It = EntityInspectorComponentMap.find(Cmd.m_pClassObject);
        if (It == EntityInspectorComponentMap.end()) return;

        // Every commit through the inspector changes this entity's live data, whether or not it's a
        // prefab instance/override - the rest of this callback only maintains prefab-override
        // bookkeeping, which is a separate concern from "does this entity need (re)saving at all".
        GameMgr.m_SceneMgr.MarkEntityDirty(State.m_SelectedEntityScene, State.m_SelectedEntityId);

        auto* pPI = e29::FindPrefabInstance(GameMgr, State.m_SelectedEntity);
        if (pPI == nullptr) return;

        auto& CompOverride = e29::FindOrCreateOverrideEntry(*pPI, It->second->m_Guid.m_Value);

        // m_PropertyValueAsString is a read-only echo for humans/tools (see its own comment) - the
        // ECS itself never reads it back, but since it CAN go stale (the same property edited a
        // second time to a different value), every commit refreshes it rather than only setting it
        // once at creation and leaving later edits unreflected.
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
    EntityInspector.m_OnChangeEvent.Register(OnPropertyChanged);

    auto OnOverrideCheck = [&](xproperty::inspector&, const xproperty::type::object&, void* pInstance, std::string_view Path, const xproperty::any&, bool& bOut)
    {
        bOut = false;

        auto It = EntityInspectorComponentMap.find(pInstance);
        if (It == EntityInspectorComponentMap.end()) return;

        auto* pPI = e29::FindPrefabInstance(GameMgr, State.m_SelectedEntity);
        if (pPI == nullptr) return;

        for (auto& C : pPI->m_lComponents)
        {
            if (C.m_ComponentTypeGuid != It->second->m_Guid.m_Value) continue;
            for (auto& O : C.m_PropertyOverrides)
                if (O.m_PropertyName == Path) { bOut = true; return; }
        }
    };
    EntityInspector.m_OnOverrideCheck.Register(OnOverrideCheck);

    auto OnOverrideReset = [&](xproperty::inspector& Inspector, const xproperty::type::object& Obj, void* pInstance, std::string_view Path)
    {
        auto It = EntityInspectorComponentMap.find(pInstance);
        if (It == EntityInspectorComponentMap.end()) return;

        auto* pPI = e29::FindPrefabInstance(GameMgr, State.m_SelectedEntity);
        if (pPI == nullptr) return;

        if (auto Err = GameMgr.m_PrefabMgr.EnsureLoaded(pPI->m_PrefabInstance); Err)
        {
            e29::Debugger(std::format("Failed to load source prefab for revert: {}", Err.getMessage()));
            return;
        }

        auto RootIt = GameMgr.m_PrefabMgr.m_PrefabList.find(pPI->m_PrefabInstance.m_Instance.m_Value);
        if (RootIt == GameMgr.m_PrefabMgr.m_PrefabList.end()) return;

        auto& RootDetails = GameMgr.m_ComponentMgr.getEntityDetails(RootIt->second);
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
        bSuppressOverrideTracking = true;
        Inspector.BeginEdit(Obj, pInstance, "Revert Override");
        xproperty::sprop::setProperty(SetError, pInstance, Obj, xproperty::sprop::container::prop{ std::string(Path), BaseValue }, Context);
        Inspector.CommitEdit(Context);
        bSuppressOverrideTracking = false;

        for (auto& C : pPI->m_lComponents)
        {
            if (C.m_ComponentTypeGuid != It->second->m_Guid.m_Value) continue;
            std::erase_if(C.m_PropertyOverrides, [&](auto& O) noexcept { return O.m_PropertyName == Path; });
            if (C.m_PropertyOverrides.empty())
                std::erase_if(pPI->m_lComponents, [&](auto& CC) noexcept { return CC.m_ComponentTypeGuid == It->second->m_Guid.m_Value; });
            break;
        }
    };
    EntityInspector.m_OnOverrideReset.Register(OnOverrideReset);

    // "[X]" on a component's own header row - resolves back to which xECS component this is via the
    // SAME EntityInspectorComponentMap the property callbacks above already use, so it stays in sync
    // with whatever's currently appended (rebuilt every m_bEntityInspectorDirty pass). Excludes the
    // same components the "Remove Component" combo already excludes (internal bookkeeping components,
    // and Name - every entity stays nameable) - one shared exclusion list, not two independently
    // maintained ones. Only records the request (pPendingRemoveComponent); the actual
    // AddOrRemoveComponents call happens after EntityInspector.Show(...) returns for this frame.
    auto OnComponentHeaderRender = [&](xproperty::inspector&, const xproperty::type::object&, void* pInstance)
    {
        auto It = EntityInspectorComponentMap.find(pInstance);
        if (It == EntityInspectorComponentMap.end()) return;
        auto* pInfo = It->second;
        if (e29::IsInternalComponent(pInfo)) return;
        // Name is a regular, removable component like any other now - an entity with none of its own
        // components at all (not even Name) is a legitimate state (see NextFreeEntityId's caller and
        // the Scene tree's own RuntimeID fallback label for the other half of this).

        // NOT ImGui::SameLine() here - SameLine(x) positions using CursorPosPrevLine.y (the Y of
        // whichever line a real widget last finished on), not the actual current cursor. Nothing real
        // (the header's own background fill is a draw-list rect, not an ImGui item) draws between
        // NextColumn() and this callback firing, so for every component AFTER the first,
        // CursorPosPrevLine.y is still stale from the PREVIOUS component's last property row - visible
        // live as this button rendering on top of whatever row happened to be last, not its own
        // header. GetCursorScreenPos() (the real, current cursor - already correctly placed by the
        // caller right before this fires) has no such staleness, so compute the absolute position from
        // that instead - same "stop trusting ambient ImGui state, use an explicit known-good value"
        // fix as the other two SameLine/IsItemHovered bugs already found in this exact inspector.
        const ImVec2 RowPos = ImGui::GetCursorScreenPos();
        const float  AvailW = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorScreenPos(ImVec2(RowPos.x + AvailW - 20.0f, RowPos.y));
        if (ImGui::SmallButton("X")) pPendingRemoveComponent = pInfo;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove this component from the entity");
    };
    EntityInspector.m_OnComponentHeaderRender.Register(OnComponentHeaderRender);

    // Custom render for ANY xecs::component::entity-valued property (today, only
    // xecs::component::entity_reference::m_Target - but this is a value-type check, not a per-
    // property tag, so it applies automatically to any FUTURE component with an entity-reference field
    // too) - the shared inspector has no default draw style registered for the raw 'entity' atomic
    // type at all (would assert in onRender's ResolveDrawFn/pDrawFn dispatch the moment this property
    // tried to draw its default widget), so this is not optional polish, it's what makes
    // entity_reference safe to add to an entity in the first place. Drag a row from the Level Editor
    // tree (E29_ENTITY_DRAG, the same shared payload reparenting/prefab-creation already use - see
    // the drag source's own comment for why it's one shared name, not one per purpose) onto this
    // property to assign it; "X" clears it. Shows "<unresolved>" rather than crashing
    // when the target is valid but its owning scene isn't currently open (ResolveEntityReference can't
    // search a scene nobody loaded) - the underlying value/reference is untouched either way, this is
    // purely a display limitation.
    auto OnEntityReferenceRender = [&](xproperty::inspector& Inspector, const xproperty::type::object& Obj, void* pInstance, std::string_view Path, const xproperty::any& Value, bool& bHandled)
    {
        if (Value.m_pType == nullptr || Value.m_pType->m_GUID != xproperty::settings::var_type<xecs::component::entity>::guid_v) return;
        bHandled = true;

        const auto CurrentValue = Value.get<xecs::component::entity>();
        std::string     Label;
        xecs::scene::guid TargetScene;
        const bool bResolved = e29::ResolveEntityReference(GameMgr, State, CurrentValue, Label, TargetScene);
        if (!bResolved) Label = CurrentValue.isValid() ? "<unresolved>" : "None";

        // A plain Text/TextUnformatted's own "last item" rect is only as wide as its glyphs - dropping
        // anywhere else in this (usually much wider) property cell would silently miss
        // BeginDragDropTarget's hover check entirely (confirmed live: dragging worked, dropping never
        // registered). Selectable with an explicit size fills the REST of the cell with a real,
        // hoverable rect - the same "make the hit-test span more than the glyphs" fix this file's own
        // component-header/other rows already needed for unrelated reasons, just via a different tool.
        const float AvailWidth  = ImGui::GetContentRegionAvail().x;
        const bool  bShowClear  = CurrentValue.isValid();
        ImGui::Selectable(Label.c_str(), false, ImGuiSelectableFlags_None, ImVec2(bShowClear ? AvailWidth - 24.0f : AvailWidth, 0.0f));

        // Persistent diagnostic logging, gated on GetDragDropPayload()!=nullptr so it only fires for however many
        // frames an actual drag is in flight (not every frame the app runs) - two prior fix attempts
        // (widening the hover rect, fixing draw order) didn't resolve the user's report, so guessing a
        // third time isn't warranted; this pins down exactly which link in the chain (does this
        // callback even see the drag at all, does BeginDragDropTarget's hover check succeed, does the
        // payload NAME match) is actually failing.
        if (ImGui::GetDragDropPayload() != nullptr)
        {
            std::printf("[EntityRef] render Path='%.*s' AvailWidth=%.1f CurrentValue.isValid=%d\n", static_cast<int>(Path.size()), Path.data(), AvailWidth, CurrentValue.isValid());
            std::fflush(stdout);
        }

        // Attached to the Selectable specifically, immediately after it and BEFORE the "X" button
        // below (which would otherwise become the new "last item" and steal the drop target down to
        // its own tiny rect the moment a reference is already assigned - the exact same bug this whole
        // fix is for, just reintroduced one widget later).
        const bool bIsDropTarget = ImGui::BeginDragDropTarget();
        if (ImGui::GetDragDropPayload() != nullptr)
        {
            std::printf("[EntityRef] BeginDragDropTarget=%d\n", bIsDropTarget);
            std::fflush(stdout);
        }
        if (bIsDropTarget)
        {
            const ImGuiPayload* pActivePayload = ImGui::GetDragDropPayload();
            std::printf("[EntityRef] active payload DataType='%s' looking for 'E29_ENTITY_DRAG'\n", pActivePayload ? pActivePayload->DataType : "<none>");
            std::fflush(stdout);
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("E29_ENTITY_DRAG"))
            {
                std::printf("[EntityRef] AcceptDragDropPayload HIT, DataSize=%d (expected %zu)\n", payload->DataSize, sizeof(e29::entity_drag_payload_t));
                std::fflush(stdout);
                IM_ASSERT(payload->DataSize == sizeof(e29::entity_drag_payload_t));
                auto& Dropped = *reinterpret_cast<const e29::entity_drag_payload_t*>(payload->Data);
                if (auto* pDropScene = GameMgr.m_SceneMgr.Find(Dropped.m_SceneGuid))
                {
                    if (auto It = pDropScene->m_LocalToRuntime.find(Dropped.m_Id); It != pDropScene->m_LocalToRuntime.end())
                    {
                        xproperty::settings::context Context;
                        xproperty::any               NewValue;
                        NewValue.set<xecs::component::entity>(It->second);
                        std::string SetError;
                        Inspector.BeginEdit(Obj, pInstance, "Assign Entity Reference");
                        xproperty::sprop::setProperty(SetError, pInstance, Obj, xproperty::sprop::container::prop{ std::string(Path), NewValue }, Context);
                        Inspector.CommitEdit(Context);

                        // A reference pointing outside the owning entity's own scene is exactly what
                        // the Dependencies folder (pScene->m_ParentScenes) is for - this is the other
                        // way (besides dragging a Scene asset onto that folder directly) a dependency
                        // can be created, and it should happen automatically here rather than leaving
                        // the reference dangling on save/reload until the user separately remembers to
                        // add the dependency by hand. Same dedup/self-check as the Dependencies folder's
                        // own drop target.
                        if (Dropped.m_SceneGuid != State.m_SelectedEntityScene)
                        {
                            if (auto* pOwningScene = GameMgr.m_SceneMgr.Find(State.m_SelectedEntityScene))
                            {
                                if (std::find(pOwningScene->m_ParentScenes.begin(), pOwningScene->m_ParentScenes.end(), Dropped.m_SceneGuid) == pOwningScene->m_ParentScenes.end())
                                    pOwningScene->m_ParentScenes.push_back(Dropped.m_SceneGuid);
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
    EntityInspector.m_OnCustomRenderReplaceValue.Register(OnEntityReferenceRender);

    //
    // Main Loop
    //
    while (Instance.ProcessInputEvents())
    {
        if (xgpu::tools::imgui::BeginRendering(true)) continue;

        //
        // Main menu bar - same "File > Asset Browser..."/"Save Project" pattern every other editor
        // example uses (see E24_AnimPackage_Editor.cpp's identical menu). AsserBrowser.Render() is a
        // no-op until Show(true) is called at least once - every other example gates that behind this
        // exact menu item, not an always-on window.
        //
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Asset Browser..."))
                    AsserBrowser.Show(true);

                ImGui::Separator();
                if (ImGui::MenuItem("Save", "Ctrl+S"))
                    e29::SaveEverything(GameMgr, State);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        AsserBrowser.Render(e10::g_LibMgr, xresource::g_Mgr);
        e29::g_AssetBrowserPopup.RenderAsPopup(e10::g_LibMgr, xresource::g_Mgr);

        if (auto NewAsset = AsserBrowser.getNewAsset(); NewAsset.empty() == false)
        {
            if (NewAsset.m_Type == xecs::level::type_guid_v) e29::OpenLevel(GameMgr, State, NewAsset);
            else if (NewAsset.m_Type == xecs::scene::type_guid_v) e29::OpenScene(GameMgr, State, NewAsset);
        }
        else if (auto SelAsset = AsserBrowser.getSelectedAsset(); SelAsset.empty() == false)
        {
            if (SelAsset.m_Type == xecs::level::type_guid_v) e29::OpenLevel(GameMgr, State, SelAsset);
            else if (SelAsset.m_Type == xecs::scene::type_guid_v) e29::OpenScene(GameMgr, State, SelAsset);
        }

        //
        // Level Editor panel - one tree: Level -> Scenes -> Folders -> Entities. Clicking a Scene's
        // label opens it (loads its entities) WITHOUT closing any other already-open scene - any
        // number of scenes can be open/expanded at once, each independently. Clicking an Entity
        // selects it for the Properties panel. A scene's dependency edges (a different relationship
        // than level ownership) render as a fixed "Dependencies" folder right under that scene's own
        // row, not a separate section.
        //
        ImGui::SetNextWindowPos(ImVec2(915, 18), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(360, 680), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Level Editor"))
        {
            if (State.m_CurrentLevel.empty())
            {
                ImGui::TextDisabled("Create or open a Level from the asset browser.");
            }
            else if (auto* pLevel = GameMgr.m_LevelMgr.Find(State.m_CurrentLevel))
            {
                std::string LevelLabel;
                e29::RemapGUIDToString(LevelLabel, xresource::full_guid{ State.m_CurrentLevel.m_Instance, State.m_CurrentLevel.m_Type });

                // Search box, visually matching the asset browser's own (RenderTreeSearchBar's own
                // comment). Adding entities/folders is right-click-in-place on a Scene/Folder row now
                // (BeginPopupContextItem, below) - a separate toolbar "+" turned out to be redundant
                // once that existed, per direct user feedback, so it's gone rather than kept alongside.
                e29::RenderTreeSearchBar(State.m_TreeSearchString, ImGui::GetContentRegionAvail().x);

                // The whole Level -> Scene -> Folder -> Entity hierarchy lives in one real
                // ImGui::BeginTable now (ImGui's own documented "tree inside a table" shape -
                // ImGuiTreeNodeFlags_SpanFullWidth on every tree row so hover/selection spans the
                // Name column), replacing the old flat TreeNodeEx-plus-manual-SameLine layout: that
                // exact SameLine(GetContentRegionAvail().x - N) pattern is what caused a real,
                // user-reported bug elsewhere in this file (a button landing on the wrong row because
                // SameLine anchors to whichever real item last finished a LINE, not the current
                // cursor) - a real table column has no such ambiguity. Column 1 ("Actions") is
                // intentionally minimal today (just each row's own remove/delete button) - the second
                // column exists so future per-row content (type badges, visibility toggles, etc.) has
                // somewhere to go without another rewrite. Sized to fill the rest of the window's
                // height (ImGuiTableFlags_ScrollY so it scrolls internally instead of pushing the
                // window's own edge) rather than only as tall as its content.
                if (ImGui::BeginTable("LevelTree", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersV | ImGuiTableFlags_ScrollY, ImVec2(0.0f, ImGui::GetContentRegionAvail().y)))
                {
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 64.0f);

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    const bool bLevelOpen = ImGui::TreeNodeEx(LevelLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth);

                    // Drag a Scene asset from the asset browser onto the Level's own row to add it -
                    // replaces the old "+ Add Scene" button/picker. Same "DESCRIPTOR_GUID" payload the
                    // Scene row already decodes for prefab-instantiation, just checked against the
                    // Scene type instead. Skips an already-present scene rather than adding a duplicate.
                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DESCRIPTOR_GUID"))
                        {
                            IM_ASSERT(payload->DataSize == sizeof(e10::drag_and_drop_folder_payload_t));
                            auto& Dropped = *reinterpret_cast<const e10::drag_and_drop_folder_payload_t*>(payload->Data);
                            if (Dropped.m_Source.m_Type == xecs::scene::type_guid_v)
                            {
                                const xecs::scene::guid NewSceneGuid{ .m_Instance = Dropped.m_Source.m_Instance };
                                if (std::find(pLevel->m_Scenes.begin(), pLevel->m_Scenes.end(), NewSceneGuid) == pLevel->m_Scenes.end())
                                    pLevel->m_Scenes.push_back(NewSceneGuid);
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    if (bLevelOpen)
                    {
                        for (std::size_t iScene = 0; iScene < pLevel->m_Scenes.size(); ++iScene)
                        {
                            ImGui::PushID(static_cast<int>(iScene));
                            const auto SceneGuid = pLevel->m_Scenes[iScene];

                            std::string SceneLabel;
                            e29::RemapGUIDToString(SceneLabel, xresource::full_guid{ SceneGuid.m_Instance, SceneGuid.m_Type });

                            const bool bIsOpenScene = std::find(State.m_OpenScenes.begin(), State.m_OpenScenes.end(), SceneGuid) != State.m_OpenScenes.end();

                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            const bool bSceneExpanded = ImGui::TreeNodeEx(SceneLabel.c_str(), ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth | (bIsOpenScene ? ImGuiTreeNodeFlags_Selected : 0));
                            if (ImGui::IsItemClicked())
                                e29::OpenScene(GameMgr, State, xresource::full_guid{ SceneGuid.m_Instance, SceneGuid.m_Type });

                            // Right-click: same "New Entity"/"New Folder" the Folder row's own menu
                            // offers (landing at this scene's root), plus removing the scene itself.
                            if (ImGui::BeginPopupContextItem())
                            {
                                if (auto* pMenuScene = GameMgr.m_SceneMgr.Find(SceneGuid))
                                    e29::ShowCreateMenuItems(GameMgr, SceneGuid, *pMenuScene, xecs::scene::invalid_folder_id_v);
                                ImGui::Separator();
                                if (ImGui::MenuItem("Remove Scene"))
                                {
                                    pLevel->m_Scenes.erase(pLevel->m_Scenes.begin() + iScene);
                                    e29::CloseScene(GameMgr, State, SceneGuid);
                                    ImGui::EndPopup();
                                    if (bSceneExpanded) ImGui::TreePop();
                                    ImGui::PopID();
                                    break; // pLevel->m_Scenes was just mutated mid-iteration
                                }
                                ImGui::EndPopup();
                            }

                            // Drop a Prefab asset from the asset browser here to instantiate it -
                            // decodes the SAME "DESCRIPTOR_GUID" payload the browser's own asset icons
                            // already drag (see e10::drag_and_drop_folder_payload_t). ALSO accepts an
                            // entity dragged out of a folder back to loose/root (E29_ENTITY_DRAG,
                            // reusing the same payload struct the prefab-creation drag already uses -
                            // it already carries exactly {SceneGuid, Id}).
                            if (bIsOpenScene && ImGui::BeginDragDropTarget())
                            {
                                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DESCRIPTOR_GUID"))
                                {
                                    IM_ASSERT(payload->DataSize == sizeof(e10::drag_and_drop_folder_payload_t));
                                    auto& Dropped = *reinterpret_cast<const e10::drag_and_drop_folder_payload_t*>(payload->Data);
                                    if (Dropped.m_Source.m_Type == xecs::prefab::type_guid_v)
                                    {
                                        if (auto* pDropScene = GameMgr.m_SceneMgr.Find(SceneGuid))
                                            e29::InstantiatePrefabIntoScene(GameMgr, *pDropScene, xecs::prefab::guid{ Dropped.m_Source.m_Instance, Dropped.m_Source.m_Type });
                                    }
                                }
                                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("E29_ENTITY_DRAG"))
                                {
                                    IM_ASSERT(payload->DataSize == sizeof(e29::entity_drag_payload_t));
                                    auto& Dropped = *reinterpret_cast<const e29::entity_drag_payload_t*>(payload->Data);
                                    if (auto* pDropScene = GameMgr.m_SceneMgr.Find(Dropped.m_SceneGuid))
                                        e29::ReparentEntityIntoFolder(*pDropScene, Dropped.m_Id, xecs::scene::invalid_folder_id_v);
                                }
                                ImGui::EndDragDropTarget();
                            }

                            ImGui::TableSetColumnIndex(1);
                            if (ImGui::SmallButton("Remove"))
                            {
                                pLevel->m_Scenes.erase(pLevel->m_Scenes.begin() + iScene);
                                e29::CloseScene(GameMgr, State, SceneGuid);
                                if (bSceneExpanded) ImGui::TreePop();
                                ImGui::PopID();
                                break; // pLevel->m_Scenes was just mutated mid-iteration
                            }

                            if (bSceneExpanded)
                            {
                                if (bIsOpenScene)
                                {
                                    if (auto* pScene = GameMgr.m_SceneMgr.Find(SceneGuid))
                                    {
                                        // One entity row, used both for folder members and loose
                                        // (unfoldered) entities below - returns true if it just mutated
                                        // pScene->m_LocalToRuntime (deleted), telling the caller's own
                                        // loop over a SNAPSHOT (never the live map/vector directly - see
                                        // every call site below) that this id is now stale.
                                        auto RenderEntityRow = [&](xecs::scene::permanent_id Id, xecs::component::entity Entity) -> bool
                                        {
                                            std::string EntityLabel = std::format("Entity #{}", Id);
                                            if (auto& Details = GameMgr.m_ComponentMgr.getEntityDetails(Entity); Details.m_pPool)
                                            {
                                                auto Bits = Details.m_pPool->m_pArchetype->getComponentBits();
                                                if (Bits.getBit(xecs::component::type::info_v<e29::name>.m_BitID))
                                                    EntityLabel = Details.m_pPool->getComponent<e29::name>(Details.m_PoolIndex).m_Value;
                                            }
                                            auto* pPI = e29::FindPrefabInstance(GameMgr, Entity);
                                            if (pPI)
                                            {
                                                std::string PrefabName;
                                                e29::RemapGUIDToString(PrefabName, pPI->m_PrefabInstance);
                                                EntityLabel += std::format(" (Prefab: {})", PrefabName);
                                            }

                                            // Search filters entities only (folders always stay visible
                                            // so a match nested inside one is still reachable - see the
                                            // toolbar's own comment for why this pass keeps that simple).
                                            if (!State.m_TreeSearchString.empty() && !e29::ContainsCaseInsensitive(EntityLabel, State.m_TreeSearchString))
                                                return false;

                                            ImGui::PushID(static_cast<int>(Id));
                                            ImGui::TableNextRow();
                                            ImGui::TableSetColumnIndex(0);
                                            const bool bEntitySelected = (State.m_SelectedEntityId == Id);
                                            // Prefab instances render in blue, matching Unity's own
                                            // Hierarchy convention (the reference screenshot) - real
                                            // GameObjects/entities stay the default text color.
                                            if (pPI) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 170, 255, 255));
                                            ImGui::TreeNodeEx(EntityLabel.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Bullet | ImGuiTreeNodeFlags_SpanFullWidth | (bEntitySelected ? ImGuiTreeNodeFlags_Selected : 0));
                                            if (pPI) ImGui::PopStyleColor();

                                            // Select on mouse-UP, not mouse-DOWN (ImGui::IsItemClicked
                                            // fires on press) - direct user report: selecting on press
                                            // reassigned State.m_SelectedEntity (rebuilding the WHOLE
                                            // Entity Properties inspector, m_bEntityInspectorDirty)
                                            // before a drag onto one of its OWN property rows (e.g. an
                                            // EntityReference's drop target) could ever get going,
                                            // exactly the "select on press" pitfall most drag-capable
                                            // list/tree widgets (Windows Explorer included) avoid.
                                            // GetMouseDragDelta (not IsMouseDragging, which needs the
                                            // button still held to report anything - already false by
                                            // the time a release is detected) is the one ImGui query
                                            // documented to still reflect the drag distance on the
                                            // exact release frame.
                                            // Persistent diagnostic logging - gated on the mouse button
                                            // actually being held/just-released while over THIS row, so
                                            // it only fires during a real press-hold-release sequence
                                            // (a handful of frames), not every frame the app runs. Two
                                            // reported symptoms need tracing here: dragging stopped
                                            // working after the first successful drag, and the
                                            // EntityReference target saw nothing on a later attempt -
                                            // this pins down whether BeginDragDropSource is even being
                                            // reached/returning true on later attempts, or whether the
                                            // selection logic right above it is somehow interfering.
                                            const bool bMouseDownHere = ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left);
                                            const bool bMouseUpHere   = ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left);
                                            if (bMouseDownHere || bMouseUpHere)
                                            {
                                                const ImVec2 Drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                                                std::printf("[EntityDrag] Id=%u hovered, MouseDown=%d MouseReleased=%d Drag=(%.1f,%.1f) IsDragDropActivePayload=%s\n",
                                                    Id, ImGui::IsMouseDown(ImGuiMouseButton_Left), ImGui::IsMouseReleased(ImGuiMouseButton_Left), Drag.x, Drag.y,
                                                    ImGui::GetDragDropPayload() ? ImGui::GetDragDropPayload()->DataType : "<none>");
                                                std::fflush(stdout);
                                            }

                                            if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                                            {
                                                const ImVec2 Drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                                                if (Drag.x == 0.0f && Drag.y == 0.0f) // released without ever dragging past the threshold
                                                {
                                                    std::printf("[EntityDrag] Id=%u -> SELECTED (plain click, no drag)\n", Id);
                                                    std::fflush(stdout);
                                                    State.m_SelectedEntityId      = Id;
                                                    State.m_SelectedEntity        = Entity;
                                                    State.m_SelectedEntityScene   = SceneGuid;
                                                    State.m_bEntityInspectorDirty = true;
                                                }
                                            }

                                            // Three independent drag intents from the same row, picked
                                            // apart by the consumer at drop time (which target it landed
                                            // on), NOT by payload type name: dropping on the asset
                                            // browser creates a Prefab (e29::entity_to_prefab_drop);
                                            // dropping on a folder/scene-root row here reparents it;
                                            // dropping on an xecs::component::entity-typed property row
                                            // in the Entity Properties inspector assigns a reference (see
                                            // the m_OnCustomRenderReplaceValue registration for
                                            // entity-reference fields). ImGui::SetDragDropPayload writes
                                            // into ONE global payload slot - calling it multiple times
                                            // with different type-name strings does NOT register
                                            // multiple simultaneous payloads, each call just overwrites
                                            // the last, so only one name can ever be shared here (same
                                            // pattern as this codebase's own "DESCRIPTOR_GUID" reuse).
                                            const bool bBeganDragSource = ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID);
                                            if (bMouseDownHere)
                                            {
                                                std::printf("[EntityDrag] Id=%u BeginDragDropSource=%d\n", Id, bBeganDragSource);
                                                std::fflush(stdout);
                                            }
                                            if (bBeganDragSource)
                                            {
                                                e29::entity_drag_payload_t Payload{ SceneGuid, Id };
                                                ImGui::SetDragDropPayload("E29_ENTITY_DRAG", &Payload, sizeof(Payload));
                                                ImGui::Text("%s", EntityLabel.c_str());
                                                ImGui::EndDragDropSource();
                                            }

                                            bool bDeleted = false;
                                            auto DoDeleteEntity = [&]() noexcept
                                            {
                                                auto E = Entity;
                                                GameMgr.DeleteEntity(E);
                                                pScene->m_RuntimeToLocal.erase(Entity.m_Value);
                                                pScene->m_LocalToRuntime.erase(Id);
                                                GameMgr.m_SceneMgr.MarkEntityDeleted(SceneGuid, Id);
                                                e29::ReparentEntityIntoFolder(*pScene, Id, xecs::scene::invalid_folder_id_v); // scrub any dangling folder membership
                                                if (State.m_SelectedEntityId == Id)
                                                {
                                                    State.m_SelectedEntityId    = xecs::scene::invalid_permanent_id_v;
                                                    State.m_SelectedEntity      = {};
                                                    State.m_SelectedEntityScene = {};
                                                }
                                                bDeleted = true;
                                            };

                                            if (ImGui::BeginPopupContextItem())
                                            {
                                                if (ImGui::MenuItem("Delete Entity")) DoDeleteEntity();
                                                ImGui::EndPopup();
                                            }

                                            ImGui::TableSetColumnIndex(1);
                                            if (!bDeleted && ImGui::SmallButton("X")) DoDeleteEntity();

                                            ImGui::PopID();
                                            return bDeleted;
                                        };

                                        // Recursive folder walk. Snapshots ids (never a live reference
                                        // into pScene->m_Folders) before recursing/rendering, then
                                        // re-finds each by id fresh right before use - the tree is
                                        // mutated in-place by "+ New Folder"/delete/reparent actions
                                        // fired from arbitrary depths in this same recursion, which
                                        // would invalidate any iterator/pointer held across those calls.
                                        std::function<void(xecs::scene::folder_id)> RenderFolderChildren = [&](xecs::scene::folder_id ParentId)
                                        {
                                            std::vector<xecs::scene::folder_id> ChildIds;
                                            for (auto& F : pScene->m_Folders)
                                            {
                                                if (F.m_Parent != ParentId) continue;
                                                // "Default" is rendered separately as a special, locked
                                                // folder (see its own comment) - excluded here so it
                                                // never ALSO gets the generic New Folder/Delete/drag-drop
                                                // treatment every other root-level folder gets.
                                                if (ParentId == xecs::scene::invalid_folder_id_v && F.m_Name == "Default") continue;
                                                ChildIds.push_back(F.m_Id);
                                            }

                                            for (auto FolderId : ChildIds)
                                            {
                                                auto It = std::find_if(pScene->m_Folders.begin(), pScene->m_Folders.end(), [&](auto& F) noexcept { return F.m_Id == FolderId; });
                                                if (It == pScene->m_Folders.end()) continue; // deleted earlier this same frame

                                                ImGui::PushID(static_cast<int>(FolderId));
                                                ImGui::TableNextRow();
                                                ImGui::TableSetColumnIndex(0);
                                                const bool bFolderHasChildren = !It->m_Entities.empty() || std::any_of(pScene->m_Folders.begin(), pScene->m_Folders.end(), [&](auto& F) noexcept { return F.m_Parent == FolderId; });
                                                const std::string FolderLabel = std::format("{} {}", e29::FolderIcon(bFolderHasChildren), It->m_Name);
                                                const bool bFolderOpen = ImGui::TreeNodeEx(FolderLabel.c_str(), ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth);

                                                bool bFolderDeleted = false;
                                                if (ImGui::BeginPopupContextItem())
                                                {
                                                    e29::ShowCreateMenuItems(GameMgr, SceneGuid, *pScene, FolderId);
                                                    ImGui::Separator();
                                                    if (ImGui::MenuItem("Delete Folder"))
                                                    {
                                                        e29::DeleteFolder(*pScene, FolderId);
                                                        bFolderDeleted = true;
                                                    }
                                                    ImGui::EndPopup();
                                                }
                                                if (bFolderDeleted)
                                                {
                                                    if (bFolderOpen) ImGui::TreePop();
                                                    ImGui::PopID();
                                                    continue; // It/this folder no longer exists - nothing left to render for it
                                                }

                                                if (ImGui::BeginDragDropTarget())
                                                {
                                                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("E29_ENTITY_DRAG"))
                                                    {
                                                        IM_ASSERT(payload->DataSize == sizeof(e29::entity_drag_payload_t));
                                                        auto& Dropped = *reinterpret_cast<const e29::entity_drag_payload_t*>(payload->Data);
                                                        if (Dropped.m_SceneGuid == SceneGuid)
                                                            e29::ReparentEntityIntoFolder(*pScene, Dropped.m_Id, FolderId);
                                                    }
                                                    ImGui::EndDragDropTarget();
                                                }

                                                ImGui::TableSetColumnIndex(1);
                                                if (ImGui::SmallButton("X"))
                                                {
                                                    e29::DeleteFolder(*pScene, FolderId);
                                                    if (bFolderOpen) ImGui::TreePop();
                                                    ImGui::PopID();
                                                    continue; // It/this folder no longer exists - nothing left to render for it
                                                }

                                                if (bFolderOpen)
                                                {
                                                    RenderFolderChildren(FolderId);

                                                    // Snapshot this folder's own member ids too - a
                                                    // nested delete/reparent could otherwise mutate
                                                    // It->m_Entities while this exact loop walks it.
                                                    std::vector<xecs::scene::permanent_id> MemberIds;
                                                    if (auto FreshIt = std::find_if(pScene->m_Folders.begin(), pScene->m_Folders.end(), [&](auto& F) noexcept { return F.m_Id == FolderId; }); FreshIt != pScene->m_Folders.end())
                                                        MemberIds = FreshIt->m_Entities;
                                                    for (auto EId : MemberIds)
                                                        if (auto EIt = pScene->m_LocalToRuntime.find(EId); EIt != pScene->m_LocalToRuntime.end())
                                                            RenderEntityRow(EId, EIt->second);

                                                    ImGui::TreePop();
                                                }
                                                ImGui::PopID();
                                            }
                                        };

                                        // Special, fixed folder for this scene's dependencies (what
                                        // used to be the separate "Parent Scenes" section below the
                                        // whole tree) - NOT a real entry in pScene->m_Folders (so it can
                                        // never be confused with a user folder), synthesized here from
                                        // pScene->m_ParentScenes directly. Always the first child of the
                                        // scene's own row, per direct user request: "there are no parent
                                        // scenes in reality, Scenes have dependencies" - and the folder
                                        // itself "can not be moved or touched", so no delete/drag-source
                                        // on the folder row, only a drop target (drag a Scene asset onto
                                        // it to add a dependency) and a per-entry delete button below.
                                        {
                                            ImGui::PushID("Dependencies");
                                            ImGui::TableNextRow();
                                            ImGui::TableSetColumnIndex(0);
                                            const std::string DepLabel = std::format("{} Dependencies", e29::FolderIcon(!pScene->m_ParentScenes.empty()));
                                            const bool bDepOpen = ImGui::TreeNodeEx(DepLabel.c_str(), ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth);

                                            if (ImGui::BeginDragDropTarget())
                                            {
                                                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DESCRIPTOR_GUID"))
                                                {
                                                    IM_ASSERT(payload->DataSize == sizeof(e10::drag_and_drop_folder_payload_t));
                                                    auto& Dropped = *reinterpret_cast<const e10::drag_and_drop_folder_payload_t*>(payload->Data);
                                                    if (Dropped.m_Source.m_Type == xecs::scene::type_guid_v && Dropped.m_Source.m_Instance != SceneGuid.m_Instance)
                                                    {
                                                        const xecs::scene::guid NewParent{ .m_Instance = Dropped.m_Source.m_Instance };
                                                        if (std::find(pScene->m_ParentScenes.begin(), pScene->m_ParentScenes.end(), NewParent) == pScene->m_ParentScenes.end())
                                                            pScene->m_ParentScenes.push_back(NewParent);
                                                    }
                                                }
                                                ImGui::EndDragDropTarget();
                                            }

                                            if (bDepOpen)
                                            {
                                                for (std::size_t iDep = 0; iDep < pScene->m_ParentScenes.size(); ++iDep)
                                                {
                                                    ImGui::PushID(static_cast<int>(iDep));
                                                    std::string DepName;
                                                    e29::RemapGUIDToString(DepName, xresource::full_guid{ pScene->m_ParentScenes[iDep].m_Instance, pScene->m_ParentScenes[iDep].m_Type });

                                                    ImGui::TableNextRow();
                                                    ImGui::TableSetColumnIndex(0);
                                                    ImGui::TreeNodeEx(DepName.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Bullet | ImGuiTreeNodeFlags_SpanFullWidth);

                                                    bool bDepRemoved = false;
                                                    if (ImGui::BeginPopupContextItem())
                                                    {
                                                        if (ImGui::MenuItem("Remove Dependency")) bDepRemoved = true;
                                                        ImGui::EndPopup();
                                                    }

                                                    ImGui::TableSetColumnIndex(1);
                                                    if (bDepRemoved || ImGui::SmallButton("X"))
                                                    {
                                                        pScene->m_ParentScenes.erase(pScene->m_ParentScenes.begin() + iDep);
                                                        ImGui::PopID();
                                                        break; // pScene->m_ParentScenes was just mutated mid-iteration
                                                    }
                                                    ImGui::PopID();
                                                }
                                                ImGui::TreePop();
                                            }
                                            ImGui::PopID();
                                        }

                                        // Any entity not currently in any folder gets adopted into
                                        // "Default" (auto-created the first time it's actually needed)
                                        // - there's no more "loose at scene root" state at all, per
                                        // direct user request. Runs before the root-folder render below
                                        // so a freshly-created Default folder (or one that just gained a
                                        // new member) renders correctly the same frame.
                                        {
                                            std::unordered_set<xecs::scene::permanent_id> FolderedEntities;
                                            for (auto& F : pScene->m_Folders)
                                                for (auto EId : F.m_Entities) FolderedEntities.insert(EId);

                                            std::vector<xecs::scene::permanent_id> Unfoldered;
                                            for (auto& Pair : pScene->m_LocalToRuntime)
                                                if (FolderedEntities.contains(Pair.first) == false) Unfoldered.push_back(Pair.first);

                                            if (!Unfoldered.empty())
                                            {
                                                const auto DefaultId = e29::EnsureDefaultFolder(*pScene);
                                                for (auto Id : Unfoldered) e29::ReparentEntityIntoFolder(*pScene, Id, DefaultId);
                                            }
                                        }

                                        // "Default" is a special, locked folder - same treatment as
                                        // Dependencies (direct user request: "treat that folder similar
                                        // to the dependencies folder"): no delete, no New Entity/New
                                        // Folder menu (nothing can be created directly inside it, and no
                                        // subfolders of it either), no manual drag-drop INTO it (only the
                                        // automatic adoption pass above ever populates it) - it's a
                                        // temporary holding area, not a real destination. The entities
                                        // inside it are perfectly ordinary rows, freely draggable OUT to
                                        // a real folder - that's the whole point. Rendered here, once,
                                        // separately from the generic recursive walk below (which
                                        // excludes it by name at the root level for exactly this reason).
                                        if (auto It = std::find_if(pScene->m_Folders.begin(), pScene->m_Folders.end(), [](auto& F) noexcept { return F.m_Parent == xecs::scene::invalid_folder_id_v && F.m_Name == "Default"; }); It != pScene->m_Folders.end())
                                        {
                                            const auto DefaultId = It->m_Id;
                                            ImGui::PushID("Default");
                                            ImGui::TableNextRow();
                                            ImGui::TableSetColumnIndex(0);
                                            const std::string DefaultLabel = std::format("{} Default", e29::FolderIcon(!It->m_Entities.empty()));
                                            const bool bDefaultOpen = ImGui::TreeNodeEx(DefaultLabel.c_str(), ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth);
                                            if (bDefaultOpen)
                                            {
                                                std::vector<xecs::scene::permanent_id> MemberIds;
                                                if (auto FreshIt = std::find_if(pScene->m_Folders.begin(), pScene->m_Folders.end(), [&](auto& F) noexcept { return F.m_Id == DefaultId; }); FreshIt != pScene->m_Folders.end())
                                                    MemberIds = FreshIt->m_Entities;
                                                for (auto EId : MemberIds)
                                                    if (auto EIt = pScene->m_LocalToRuntime.find(EId); EIt != pScene->m_LocalToRuntime.end())
                                                        RenderEntityRow(EId, EIt->second);
                                                ImGui::TreePop();
                                            }
                                            ImGui::PopID();
                                        }

                                        RenderFolderChildren(xecs::scene::invalid_folder_id_v); // root-level user folders (Default excluded - rendered specially above)

                                        // Prefabs are still instantiated by dragging one from the asset
                                        // browser onto this scene's own row in the tree (see the drop
                                        // target attached to it above) - Unity-style, no button.
                                    }
                                }
                                else
                                {
                                    ImGui::TableNextRow();
                                    ImGui::TableSetColumnIndex(0);
                                    ImGui::TextDisabled("(click to open)");
                                }
                                ImGui::TreePop();
                            }
                            ImGui::PopID();
                        }

                        // Adding a Scene to this Level is now drag-and-drop onto the Level's own row
                        // (see the drop target attached to it above) - Unity-style, no button.

                        ImGui::TreePop();
                    }
                    ImGui::EndTable();
                }
                // Scene dependencies now render as a fixed "Dependencies" folder directly under each
                // Scene's own row inside the tree above, not a separate section here - see its own
                // comment for why ("there are no parent scenes in reality, Scenes have dependencies").
            }
        }
        ImGui::End();

        //
        // Entity properties panel - the selected entity's components, plus Add/Remove Component.
        //
        ImGui::SetNextWindowPos(ImVec2(18, 18), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(480, 500), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Entity Properties"))
        {
            if (State.m_SelectedEntity.isValid() == false || State.m_SelectedEntityScene.empty())
            {
                ImGui::TextDisabled("Select an entity in the Level Editor panel.");
            }
            else if (auto* pScene = GameMgr.m_SceneMgr.Find(State.m_SelectedEntityScene))
            {
                // Pointers (not references) so RefreshEntityView() below can rebind them after
                // AddOrRemoveComponents migrates State.m_SelectedEntity to a new handle - without this,
                // adding/removing a component and rebuilding the inspector in the SAME frame would
                // still walk the OLD archetype's DataSpan (captured before the migration), so the
                // just-added component silently wouldn't appear until some later, unrelated dirty flag
                // flip (e.g. reselecting the entity) rebuilt it with fresh data.
                auto* pDetails   = &GameMgr.m_ComponentMgr.getEntityDetails(State.m_SelectedEntity);
                auto* pArchetype = pDetails->m_pPool->m_pArchetype;
                auto  DataSpan   = pArchetype->getDataComponentInfos();

                auto RefreshEntityView = [&]() noexcept
                {
                    pDetails   = &GameMgr.m_ComponentMgr.getEntityDetails(State.m_SelectedEntity);
                    pArchetype = pDetails->m_pPool->m_pArchetype;
                    DataSpan   = pArchetype->getDataComponentInfos();
                };

                if (ImGui::BeginCombo("###AddComponent", "Add Component"))
                {
                    for (auto& Pair : xecs::component::mgr::m_ComponentInfoMap)
                    {
                        auto* pInfo = Pair.second;
                        if (pInfo->m_TypeID != xecs::component::type::id::DATA) continue;
                        if (e29::IsInternalComponent(pInfo)) continue;
                        if (pArchetype->getComponentBits().getBit(pInfo->m_BitID)) continue; // already present

                        if (ImGui::Selectable(pInfo->m_pName))
                        {
                            std::array Add{ pInfo };
                            auto NewEntity = GameMgr.AddOrRemoveComponents(State.m_SelectedEntity, Add, {});
                            pScene->m_RuntimeToLocal.erase(State.m_SelectedEntity.m_Value);
                            pScene->m_LocalToRuntime[State.m_SelectedEntityId]     = NewEntity;
                            pScene->m_RuntimeToLocal[NewEntity.m_Value]           = State.m_SelectedEntityId;
                            GameMgr.m_SceneMgr.MarkEntityDirty(State.m_SelectedEntityScene, State.m_SelectedEntityId);
                            State.m_SelectedEntity        = NewEntity;
                            State.m_bEntityInspectorDirty = true;
                            RefreshEntityView();
                        }
                    }
                    ImGui::EndCombo();
                }

                // Prefabs are created by dragging an entity from the Level Editor tree onto a folder
                // in the asset browser (see e29::entity_to_prefab_drop) - Unity-style, no button.

                ImGui::Separator();

                if (State.m_bEntityInspectorDirty)
                {
                    std::printf("[EntityDrag] Entity Properties inspector REBUILDING (m_bEntityInspectorDirty) for SelectedEntityId=%u\n", State.m_SelectedEntityId);
                    std::fflush(stdout);
                    EntityInspector.clear();
                    EntityInspectorComponentMap.clear();
                    EntityInspector.AppendEntity();
                    for (auto pInfo : DataSpan)
                    {
                        if (e29::IsInternalComponent(pInfo)) continue;
                        if (pInfo->m_pPropertyTable == nullptr) continue;

                        const auto iType = pDetails->m_pPool->findIndexComponentFromInfo(*pInfo);
                        if (iType < 0) continue;
                        auto* pData = &pDetails->m_pPool->m_pComponent[iType][pDetails->m_PoolIndex.m_Value * pInfo->m_Size];
                        EntityInspector.AppendEntityComponent(*pInfo->m_pPropertyTable, pData);
                        EntityInspectorComponentMap[pData] = pInfo;
                    }
                    State.m_bEntityInspectorDirty = false;
                }

                xproperty::settings::context Context;
                EntityInspector.Show(Context, []{});

                // A component header's "[X]" (OnComponentHeaderRender, registered above) only ever
                // records the request while Show() is mid-iteration over this same component list -
                // now that it's returned, it's safe to actually mutate the archetype, same call the
                // "Remove Component" combo below makes for the same action.
                if (pPendingRemoveComponent)
                {
                    std::array Sub{ pPendingRemoveComponent };
                    auto NewEntity = GameMgr.AddOrRemoveComponents(State.m_SelectedEntity, {}, Sub);
                    pScene->m_RuntimeToLocal.erase(State.m_SelectedEntity.m_Value);
                    pScene->m_LocalToRuntime[State.m_SelectedEntityId] = NewEntity;
                    pScene->m_RuntimeToLocal[NewEntity.m_Value]       = State.m_SelectedEntityId;
                    GameMgr.m_SceneMgr.MarkEntityDirty(State.m_SelectedEntityScene, State.m_SelectedEntityId);
                    State.m_SelectedEntity        = NewEntity;
                    State.m_bEntityInspectorDirty = true;
                    pPendingRemoveComponent        = nullptr;
                    RefreshEntityView();
                }
            }
        }
        ImGui::End();

        xgpu::tools::imgui::Render();
        MainWindow.PageFlip();
        xresource::g_Mgr.OnEndFrameDelegate();
    }

    xgpu::tools::imgui::Shutdown();
    return 0;
}
