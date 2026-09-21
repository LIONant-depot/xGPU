#pragma once

// e29::app: wiring between the Asset Browser and the editor.
namespace e29
{
    // Everything the Asset Browser needs from the editor: command-routed mutations, source-control badges and locks,
    // open-asset routing (Level / Texture), and the extra Project Settings sections.
    inline void app::WireAssetBrowser()
    {
        e10::RegisterAssetBrowserCallbacks(AsserBrowser, E29Undo, CmdContext.m_Undo, MainWindow);
        e10::RegisterSourceControlCallbacks(AsserBrowser, E29Undo);



        // "Scripting" section in the merged Plugins/Project Settings tab (e10::plugin_tab,

        // E10_asset_browser_plugin_tab.h) - the project's Script-Module build-membership list

        // (Project.config\Script.config.txt, e29::g_ScriptConfig.m_ModuleRefs), rendered as a normal

        // xproperty::inspector array field (WireResourcePickerCallbacks already gives every full_guid

        // element a working click-to-browse picker for free, and the array gets the standard Unity-style

        // insert/delete/drag controls - see xproperty_array_element_controls). NOT the Dependencies-node

        // drag-drop pattern an earlier pass here used - that assumed the Resources tab could be docked

        // and visible AT THE SAME TIME as this one, which is false: "Project Settings" and "Resources"

        // are tabs in the SAME tab strip, mutually exclusive on screen, so a drag source and this drop

        // target could never both be visible. The inspector's own click-to-open-popup picker has no such

        // docking assumption at all - direct user correction.

        //

        // No m_OnPropertyChanged hook wired here (unlike EntityInspector's own edits, which route through

        // InspectorBridge into the undo system) - a snapshot/diff around the render call instead: simple,

        // catches every mutation kind the array control can make (insert/delete/reorder/reassign)

        // uniformly, and doesn't require raw edits here to go through xundo the way every other project-

        // level-settings edit in this codebase already doesn't either (Library.config.txt's own

        // ParentLibraries has no raw-inspector-edit path at all, only command-driven Add/Remove).

        //

        // Reuses plugin_tab's OWN inherited xproperty::inspector (passed in by RightPanel()) rather than

        // carrying a second, redundant instance - direct user correction: "you have one inspector

        // working with the plugin... why did you reinvent the wheel?". WireResourcePickerCallbacks is

        // registered lazily, once, the first time this section is actually selected - E29 has no way to

        // reach plugin_tab's own instance ahead of time (it's created generically inside assert_browser's

        // own tab list), so "wire on first use" is the only hook point available, not a startup call.

        AsserBrowser.m_ExtraPluginTabSections.push_back(

        {

            "Scripting",

            [](xproperty::inspector& Inspector)

            {

                static bool bWired = false;

                if (!bWired) { e10::WireResourcePickerCallbacks(Inspector); bWired = true; }



                // REAL BUG FOUND LIVE (2026-09-19): rebuilding (clear/AppendEntity/AppendEntityComponent)

                // on EVERY frame - not just when the data actually changed - makes every widget's ImGui id

                // unstable, so a tree node's own open/closed state can never persist between frames; the

                // ModuleRefs array node fought itself and flickered continuously the instant it was

                // expanded. Same documented failure mode as xproperty_inspector_must_persist_across_frames/

                // xgpu_imgui_per_frame_rebuild_activeid_bug - rebuild ONLY when s_BuiltWith says the

                // structure is stale (first render, or the data changed since the frame that built it,

                // whether from this same UI or an external CLI command), never unconditionally.

                static std::vector<xresource::full_guid> s_BuiltWith;

                if (e29::g_ScriptConfig.m_ModuleRefs != s_BuiltWith)

                {

                    Inspector.clear();

                    Inspector.AppendEntity();

                    Inspector.AppendEntityComponent(*xproperty::getObjectByType<e29::script_config>(), &e29::g_ScriptConfig);

                }



                // Separate, frame-local snapshot - did THIS ShowEmbedded call itself edit the array (an

                // insert/delete/reassign via the array's own controls)? Distinct from the staleness check

                // above, which would otherwise misfire a save on the very first render of an already-

                // populated list (stale-vs-s_BuiltWith is true then too, but nothing was actually edited).

                const auto BeforeThisRender = e29::g_ScriptConfig.m_ModuleRefs;



                xproperty::settings::context Context;

                Inspector.ShowEmbedded(Context);



                if (e29::g_ScriptConfig.m_ModuleRefs != BeforeThisRender)

                {

                    if (auto Err = e29::SaveScriptConfig(e10::g_LibMgr.m_ProjectPath, e29::g_ScriptConfig); Err)

                        xeditor::NotifyError(std::format("Failed to save Script.config.txt: {}", Err.getMessage()));

                    e29::RegenerateGameModuleSources();

                }

                s_BuiltWith = e29::g_ScriptConfig.m_ModuleRefs;

            }

        });



        // Editor Framework: double-click a Texture resource opens the standalone Texture editor

        // (Plugins/xtexture.plugin/source/Editor/xtexture_editor.h) - direct type check for now rather

        // than the generic xeditor::registry lookup (that registry's CreateDocument/CreateUI factories

        // aren't wired up yet - real follow-up work, not done under today's time budget). Every other

        // resource type's double-click behavior is unchanged (today's inert setSelection-only default).

        AsserBrowser.m_OnOpenAsset = [this](e10::library::guid LibraryGuid, xresource::full_guid AssetGuid)
            {
                if (AssetGuid.m_Type == xecs::level::type_guid_v)
                {
                    if (e29::RequestOpenLevel(*pGameMgr, State, CmdContext.m_Undo, AssetGuid, /*bStartGameReload*/ true))
                        State.m_bPendingStartGameReloadAfterOpen = true;
                    return;
                }
                if (AssetGuid.m_Type != xrsc::texture_type_guid_v) return;
                for (auto& S : e29::g_OpenTextureEditors)
                    if (S && S->m_Document.getGuid() == AssetGuid) { S->Focus(); return; }
                e29::g_OpenTextureEditors.push_back(std::make_unique<xtexture_editor::session>(AssetGuid, LibraryGuid, e29::g_pTextureEditorDevice));
            };
    }
}
