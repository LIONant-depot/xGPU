#include "source/xGPU.h"
#include "source/tools/xgpu_imgui_breach.h"
#include <algorithm>

//
// We want to use this version as our core properties
// 
#include "dependencies/xproperty/source/xcore/my_properties.h"

//
// We add this llist needed for the examples
// 
namespace xproperty::settings
{
    template< typename T>
    struct llist
    {
        struct node
        {
            std::unique_ptr<node>  m_pNext;
            T                      m_Data;
        };

        // You are required to implement a simple iterator
        // Which includes the following functions
        struct iterator
        {
            node*       m_pCurrent;
            std::size_t m_Index;

            iterator    operator ++ ()                        noexcept { m_pCurrent = m_pCurrent->m_pNext.get(); ++m_Index; return *this; }
            bool        operator != (const iterator& I) const noexcept { return m_pCurrent != I.m_pCurrent; }
            T&          operator *()                          noexcept { return m_pCurrent->m_Data; }
        };

        // Your container should support the following functions
        // begin, end, and size are require...
        std::size_t     size        ()                  const noexcept { return m_Count;              }
        iterator        begin       ()                  const noexcept { return { m_pHead.get(), 0};  }
        iterator        end         ()                  const noexcept { return { nullptr, m_Count }; }

        // The following functions are not required but are useful
        T*              find        (const T& Key)      const noexcept
        {
            for( auto& E : *this )
                if (E == Key)
                    return &E;
            return nullptr;
        }

        llist() = default;
        llist(llist&& List ) noexcept
            : m_pHead{ std::move(List.m_pHead) }
            , m_Count{ List.m_Count }
        {
            List.m_Count = 0;
        }

        void push_front( T&& Data ) noexcept
        {
            auto pNewNode = std::make_unique<node>();
            pNewNode->m_Data     = std::move(Data);
            pNewNode->m_pNext    = std::move(m_pHead);
            m_pHead = std::move(pNewNode);
            ++m_Count;
        }

        std::unique_ptr<node>   m_pHead = {};
        std::size_t             m_Count = 0;
    };

    //
    // We add the registration/definition of our stupid container here...
    //
    template< typename T >
    struct var_type<llist<T>> : var_list_defaults< "llist", llist<T>, T, typename llist<T>::iterator, T >
    {
        using base           = var_list_defaults< "llist", llist<T>, T, typename llist<T>::iterator, T >;
        using type           = typename base::type;
        using atomic_key     = typename base::atomic_key;
        using specializing_t = typename base::specializing_t;
        using begin_iterator = typename base::begin_iterator;

        constexpr static void IteratorToKey(const type& MemberVar, xproperty::any& Key, const begin_iterator& I, context&) noexcept
        {
            Key.set<atomic_key>( I.m_pCurrent->m_Data );
        }

        constexpr static specializing_t* getObject(type& MemberVar, const any& Key, context&) noexcept
        {
            auto p = MemberVar.find(Key.get<atomic_key>());

            // If we can't find it then we are going to add it!
            if( p == nullptr )
            {
                MemberVar.push_front( T{Key.get<atomic_key>()} );
                p = MemberVar.find(Key.get<atomic_key>());
            }

            return p;
        }
    };
}

//
// Add all the examples 
//
#include "dependencies/xproperty/source/examples/imgui/xPropertyImGuiExample.h"

//
// We add this here forcing it to use our custom properties
//
#include "dependencies/xproperty/source/examples/imgui/xPropertyImGuiInspector.cpp"

namespace e04
{
    //------------------------------------------------------------------------------------------------
    static
    void DebugMessage(std::string_view View)
    {
        printf("%s\n", View.data());
    }

    //------------------------------------------------------------------------------------------------
    // Smoke test for the Milestone 0 surface that survived reconsideration, all in one panel:
    //  - obj_action (sugar over obj_member<"Name", &Class::Method> - previously reflected but never
    //    rendered, the collector just skipped it) for actions that are (or deserve to be) real class
    //    methods, disabled via the SAME member_dynamic_flags<SHOW_READONLY> every other member already
    //    uses - no separate action_state/action_dynamic_state mechanism, which turned out to just
    //    duplicate what member_flags/member_dynamic_flags already did for free (E.m_Flags is resolved
    //    generically for every member kind, function entries included; the button branch just needed
    //    to read it, not grow a parallel enum)
    //  - the OLDER virtual-string-property-styled-as-a-button pattern (member_ui<std::string>::button)
    //    kept as the right tool for a genuinely dynamic/cycling label - a one-off UI action has no
    //    business forcing a class to grow a permanent method just to support a button; the lambda
    //    keeps compute-label-and-act self-contained instead of splitting it across a real method plus
    //    a separate dynamic-label tag
    //  - member_section layout separators
    //  - a real bool member gating an entire obj_scope's visibility two different ways: as a SIBLING
    //    row before the scope (member_dynamic_flags attached to the scope itself - no new mechanism,
    //    obj_scope's T_ARGS pack is filtered into members_t/user_data_t exactly like obj_member's is),
    //    and as the scope's own FIRST CHILD merged into its header row (obj_scope_toggle) - both
    //    valid, genuinely different patterns depending on whether the toggle should visually merge
    //    into the scope it controls or stay a separate, independently-positioned property.
    //------------------------------------------------------------------------------------------------
    struct button_smoke_test
    {
        static constexpr int k_Cap = 5;

        int m_Counter   = 0;
        int m_CycleMode = 0;

        bool        m_bAdvancedEnabledSibling = false;
        float       m_SpeedMultiplierA        = 1.0f;
        int         m_MaxRetriesA             = 3;
        std::string m_DebugTagA               = "none";

        bool        m_bAdvancedEnabledToggle  = false;
        float       m_SpeedMultiplierB        = 1.0f;
        int         m_MaxRetriesB             = 3;
        std::string m_DebugTagB               = "none";

        // Deliberately not noexcept - xproperty's reflected-function specialization only
        // matches plain T_RETURN(T_CLASS::*)(...) pointer-to-member-function types (same as
        // the official union_variant_properties::setValues/CheckValues example); a noexcept
        // method is a distinct type and falls through to the wrong specialization entirely.
        void Increment() { ++m_Counter; }
        void Reset()     { m_Counter = 0; }

        XPROPERTY_DEF
        ( "Button Smoke Test", button_smoke_test
        , obj_member<"Counter", &button_smoke_test::m_Counter, member_section<"obj_action">>
        , obj_action<"Increment", &button_smoke_test::Increment
            , member_dynamic_flags<+[](const button_smoke_test& O) -> xproperty::flags::type
                { xproperty::flags::type F{}; F.m_bShowReadOnly = O.m_Counter >= k_Cap; return F; }>
            , member_help<"obj_action, disabled via the same member_dynamic_flags<SHOW_READONLY> every other property already uses - click adds 1 to Counter above; disables once it hits the cap">>
        , obj_action<"Reset", &button_smoke_test::Reset
            , member_help<"A plain obj_action - real method, real invocation, no fake read/write round-trip">>
        , obj_member<"Cycle Mode"
            , +[](button_smoke_test& O, bool bRead, std::string& Value)
              {
                  static constexpr const char* k_Names[] = { "Mode: A", "Mode: B", "Mode: C" };
                  if (bRead) Value = k_Names[O.m_CycleMode];
                  else       O.m_CycleMode = (O.m_CycleMode + 1) % 3;
              }
            , member_ui<std::string>::button<>
            , member_flags<xproperty::flags::DONT_SAVE>
            , member_section<"the lambda pattern">
            , member_help<"A virtual string property styled as a button - the right tool when the label itself must cycle/reflect live state; keeps compute-label-and-act in one place instead of a real method plus a separate label tag">>
        , obj_member<"Enable Advanced Settings", &button_smoke_test::m_bAdvancedEnabledSibling
            , member_section<"Scope Gating - sibling bool">
            , member_help<"A real, always-visible checkbox - toggling it shows/hides the Advanced Settings scope below">>
        , obj_scope<"Advanced Settings A"
            , obj_member<"Speed Multiplier", &button_smoke_test::m_SpeedMultiplierA>
            , obj_member<"Max Retries",      &button_smoke_test::m_MaxRetriesA>
            , obj_member<"Debug Tag",        &button_smoke_test::m_DebugTagA>
            , member_dynamic_flags<+[](const button_smoke_test& O) -> xproperty::flags::type
                { xproperty::flags::type F{}; F.m_bDontShow = !O.m_bAdvancedEnabledSibling; return F; }>
          >
        , obj_scope<"Advanced Settings B"
            , obj_scope_toggle<"Enabled", &button_smoke_test::m_bAdvancedEnabledToggle>
            , obj_member<"Speed Multiplier", &button_smoke_test::m_SpeedMultiplierB>
            , obj_member<"Max Retries",      &button_smoke_test::m_MaxRetriesB>
            , obj_member<"Debug Tag",        &button_smoke_test::m_DebugTagB>
            , member_section<"Scope Toggle - bool as first child">
          >
        )
    };

    //------------------------------------------------------------------------------------------------
    // Smoke test for m_OnOverrideCheck/m_OnOverrideReset - the inspector never has a built-in notion
    // of "overridden", it just calls out with the real (type::object&, instance) pair, the full
    // canonical property path (a complete opaque key - array indices live inside it, e.g.
    // "m_lTextures[G:2]", no parsing needed by the consumer), and the already-resolved current value.
    // This demo's own check/reset logic (below, registered on the SAME inspector as button_smoke_test
    // rather than a separate window) compares against a plain default-constructed instance - E20's
    // real prefab/material-instance case would instead compare against (or fetch from) a real base
    // object, using the exact same callback shape.
    //------------------------------------------------------------------------------------------------
    struct override_demo_test
    {
        float       m_Speed = 5.0f; // edit this away from 5.0 to see the override indicator + revert button appear
        std::string m_Tag   = "default";

        XPROPERTY_DEF
        ( "Override Demo", override_demo_test
        , obj_member<"Speed", &override_demo_test::m_Speed
            , member_override_check<+[](xproperty::inspector&, const xproperty::type::object&, void*, std::string_view, const xproperty::any& Value, bool& bOut) noexcept
                {
                    static const override_demo_test k_Base{};
                    bOut = Value.get<float>() != k_Base.m_Speed;
                }>
            , member_override_reset<+[](xproperty::inspector& Inspector, const xproperty::type::object& Obj, void* pInstance, std::string_view Path) noexcept
                {
                    static const override_demo_test k_Base{};
                    std::string                   Error;
                    xproperty::settings::context  Context;
                    xproperty::any                A; A.set<float>(k_Base.m_Speed);
                    Inspector.BeginEdit(Obj, pInstance, "Reset Override");
                    xproperty::sprop::setProperty(Error, pInstance, Obj, xproperty::sprop::container::prop{ std::string(Path), A }, Context);
                    Inspector.CommitEdit(Context);
                }>
            , member_section<"m_OnOverrideCheck / m_OnOverrideReset">
            , member_help<"Edit away from its base value (5.0) to see the override indicator and revert button appear - declares its own override check/reset directly via member_override_check/member_override_reset">>
        , obj_member<"Tag", &override_demo_test::m_Tag
            , member_override_check<+[](xproperty::inspector&, const xproperty::type::object&, void*, std::string_view, const xproperty::any& Value, bool& bOut) noexcept
                {
                    static const override_demo_test k_Base{};
                    bOut = Value.get<std::string>() != k_Base.m_Tag;
                }>
            , member_override_reset<+[](xproperty::inspector& Inspector, const xproperty::type::object& Obj, void* pInstance, std::string_view Path) noexcept
                {
                    static const override_demo_test k_Base{};
                    std::string                   Error;
                    xproperty::settings::context  Context;
                    xproperty::any                A; A.set<std::string>(k_Base.m_Tag);
                    Inspector.BeginEdit(Obj, pInstance, "Reset Override");
                    xproperty::sprop::setProperty(Error, pInstance, Obj, xproperty::sprop::container::prop{ std::string(Path), A }, Context);
                    Inspector.CommitEdit(Context);
                }>>
        )
    };

    //------------------------------------------------------------------------------------------------
    // A small reflected struct used as the ELEMENT type of array_ops_smoke_test's m_ObjectList below -
    // proves the per-element drag/insert/delete controls work for object (list_props) elements too,
    // not just atomic/scalar ones, via the real list_table::TrySwap/TrySetSize this session added
    // (a whole object doesn't fit in one xproperty::any the way a scalar does, so that path needs the
    // real container pointer instead of the atomic branch's setProperty-by-path calls).
    //------------------------------------------------------------------------------------------------
    struct array_item
    {
        std::string m_Name  = "Item";
        int         m_Value = 0;

        XPROPERTY_DEF
        ( "Array Item", array_item
        , obj_member<"Name",  &array_item::m_Name>
        , obj_member<"Value", &array_item::m_Value>
        )
    };

    // Forward declared so custom_render_smoke_test's own XPROPERTY_DEF (below) can attach it directly
    // via member_custom_render_block - the property declares its own rendering right where it's
    // declared, instead of a separate shared registration lambda having to Path.ends_with()-match it
    // out of every other property in the inspector. A DECLARATION is enough here (a function call
    // only needs one) - the full DEFINITION, which needs custom_render_smoke_test's complete type to
    // read m_Keyframes, stays below it, after the struct it's defined in terms of.
    struct custom_render_smoke_test;
    static void DrawCurveEditor(xproperty::inspector& Inspector, const xproperty::type::object& Obj, void* pInstance, ImColor RowColor);

    // Named (not a +[] lambda at each declaration site) specifically so the top-level Block Start/End
    // properties AND their "Indent Test" nested copies can share one implementation via
    // member_custom_render_block<DrawBlockStartContent>/<DrawBlockEndContent> at both declaration
    // sites, instead of duplicating this body. Matches member_custom_render_block_t::callback's
    // signature exactly, so it converts to the tag's stored function pointer with no wrapper needed.
    static void DrawBlockStartContent(xproperty::inspector&, const xproperty::type::object&, void*, std::string_view, const xproperty::any&, std::uint32_t RowColorU32, bool bDryRun, bool& bIsBlockContent) noexcept
    {
        bIsBlockContent = true;
        if (bDryRun) return;
        const ImColor RowColor = ImColor(RowColorU32);
        // A block nested inside a scope picks up a subtle light border "for free" at its top/bottom
        // edges - from the scope's own framed-content styling, not anything this panel draws - while a
        // top-level block (no enclosing scope) has no such border, confirmed live as a real, visible
        // inconsistency between the two. Explicit border here, using ImGui's own ImGuiCol_Border
        // (matching what the framed case already looks like rather than inventing a different color)
        // makes the treatment identical regardless of nesting, instead of relying on incidental styling
        // that only shows up sometimes.
        const ImU32 BorderColor = ImGui::GetColorU32(ImGuiCol_Border);
        // Every formula tried for this fill's height (GetFrameHeight(), then
        // GetTextLineHeightWithSpacing(), then + ItemSpacing.y, then +1px) matched the top-level copy
        // of this block but not one nested inside a scope - confirmed live via debug logging that the
        // header TEXT ITEM ITSELF starts ~3.5px later (matching this style's FramePadding.y exactly)
        // when nested than its own captured start cursor (Min.y) says it should. That's some
        // framework/ImGui baseline-offset interaction with being the first item after a scope opens,
        // not anything controllable from here - no formula written up front can account for it. Fixed
        // properly by not guessing at all: split the draw list into two channels, draw the REAL content
        // first on the content channel, MEASURE the actual resulting cursor advance, then paint the
        // background on the earlier-merged channel using that real measurement - the same deferred-
        // background technique the framework itself already uses for rows whose height isn't known
        // until their content has actually drawn (see m_RowExtraHeightCache and its own leaf-branch
        // call site in xPropertyImGuiInspector.cpp).
        ImDrawList* pDrawList = ImGui::GetWindowDrawList();
        const ImVec2 Min   = ImGui::GetCursorScreenPos();
        const float  Width = ImGui::GetContentRegionAvail().x; // captured early - a late re-measure drifts once content has drawn, see the framework's own "late width measurement" fix for why
        pDrawList->ChannelsSplit(2);
        pDrawList->ChannelsSetCurrent(1);
        ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "  Custom Block (Odin-style area) - see the real curve editor further down for the full version of this");
        const float RealBottom = ImGui::GetCursorScreenPos().y;
        pDrawList->ChannelsSetCurrent(0);
        pDrawList->AddRectFilled(Min, ImVec2(Min.x + Width, RealBottom), RowColor);
        pDrawList->AddLine(Min, ImVec2(Min.x + Width, Min.y), BorderColor, 1.0f);
        pDrawList->ChannelsMerge();
    }

    static void DrawBlockEndContent(xproperty::inspector&, const xproperty::type::object&, void*, std::string_view, const xproperty::any&, std::uint32_t, bool bDryRun, bool& bIsBlockContent) noexcept
    {
        bIsBlockContent = true;
        if (bDryRun) return;
        // Just the closing border line, no reserved height/fill - a half-height footer rect was here
        // before, but it only ever drew to the draw list (no real widget call), so the cursor never
        // advanced past it and it silently overlapped whatever the NEXT property drew (invisible while
        // framework backgrounds painted over both in the same shade, confirmed live as a real overlap
        // once those backgrounds were turned off). Rather than fix that by making it consume real space
        // (which just turned the invisible overlap into a visible, unlabeled empty-looking strip - "why
        // do we need that, makes zero sense"), drop the fill and the reserved space entirely: draw the
        // one line the block actually needs to mark its own end, at zero cursor cost.
        const ImVec2 Min = ImGui::GetCursorScreenPos();
        // Plain black, not the theme's ImGuiCol_Border gray - that gray blends differently against the
        // top-level block's lighter background vs a nested block's darker one, so it never read as a
        // consistent "black line" on both.
        ImGui::GetForegroundDrawList()->AddLine(Min, ImVec2(Min.x + ImGui::GetContentRegionAvail().x, Min.y), IM_COL32(0, 0, 0, 255), 1.0f);
    }

    // Named for the same reason as DrawBlockStartContent/DrawBlockEndContent above - shared between
    // the top-level "Full Row Replaced (level 3)" property and its "Indent Test" nested copy via
    // member_custom_render_replace_value<DrawFullRowReplaceValue>/member_custom_render_replace_row<
    // DrawFullRowReplaceRow> at both declaration sites, instead of duplicating the body.
    static void DrawFullRowReplaceValue(xproperty::inspector& Inspector, const xproperty::type::object& Obj, void* pInstance, std::string_view Path, const xproperty::any& Value, bool& bHandled) noexcept
    {
        // Right half of this level-3 smoke test - the sibling member_custom_render_replace_row tag
        // draws the left column; the automatic NextColumn() between the two levels means this one
        // still has to separately handle the right column itself.
        bHandled = true;
        if (ImGui::Button(std::format("Roll (currently {})", Value.get<int>()).c_str(), ImVec2(-1, 0)))
        {
            std::string                   Error;
            xproperty::settings::context  Context;
            xproperty::any                NewValue; NewValue.set<int>((Value.get<int>() % 6) + 1);
            Inspector.BeginEdit(Obj, pInstance, "Roll Dice");
            xproperty::sprop::setProperty(Error, pInstance, Obj, xproperty::sprop::container::prop{ std::string(Path), NewValue }, Context);
            Inspector.CommitEdit(Context);
        }
    }

    static void DrawFullRowReplaceRow(xproperty::inspector&, const xproperty::type::object&, void*, std::string_view, const xproperty::any&, bool& bHandled) noexcept
    {
        bHandled = true;
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.9f, 1.0f), "Dice");
    }

    //------------------------------------------------------------------------------------------------
    // Element type for custom_render_smoke_test's curve editor (see m_Keyframes below) - reflected the
    // same way array_item is above, so xproperty::ui::undo::SnapshotToString/ApplySnapshotFromString
    // (walked via the same sprop::collector traversal RefreshAllProperties itself uses) can genuinely
    // see and restore every field, not just whichever one happens to have its own visible row.
    //------------------------------------------------------------------------------------------------
    struct curve_keyframe
    {
        float m_Time          = 0.0f;
        float m_Value          = 0.0f;
        float m_InTangent      = 0.0f;
        float m_OutTangent     = 0.0f;
        // Purely cosmetic - cubic Hermite interpolation (see DrawCurveEditor) only ever reads the
        // slope above, never this. Stored anyway (rather than a fixed constant) so the out-tangent
        // handle's on-screen distance from its point is draggable and WYSIWYG - resting render and
        // live drag both read this same field, so there's nothing to snap back to on release.
        float m_OutHandleLen  = 28.0f;

        XPROPERTY_DEF
        ( "Curve Keyframe", curve_keyframe
        , obj_member<"Time",        &curve_keyframe::m_Time>
        , obj_member<"Value",       &curve_keyframe::m_Value>
        , obj_member<"In Tangent",  &curve_keyframe::m_InTangent>
        , obj_member<"Out Tangent", &curve_keyframe::m_OutTangent>
        , obj_member<"Out Handle Length", &curve_keyframe::m_OutHandleLen>
        )
    };

    //------------------------------------------------------------------------------------------------
    // Smoke test for this session's list-primitive additions: list_table::getCapacity (a physical slot
    // ceiling, separate from getSize's live count) and the always-on generic Swap (both new), plus
    // member_overwrite_list_size - which already existed in core xproperty.h but had zero real
    // consumers anywhere in this codebase until now. It's a single bRead callback (same idiom as the
    // lambda-styled-as-a-button pattern above) that lets a FIXED-capacity container become resizable-
    // within-capacity by delegating its live count to a sibling field, leaving the unused tail slots
    // just sitting there rather than deallocated.
    //
    // Also exercises a real, previously-latent bug fix: the Size: field's read-only gate used to check
    // only for an (unused-anywhere) member_ui_list_size_t STYLE tag, never the list's actual
    // m_bHasRealSetSize - so every array's Size field, vector or not, rendered permanently disabled.
    // Both members below have no style tag attached (matching how every real array in the codebase is
    // declared today) and should now show a genuinely editable Size: field regardless.
    //------------------------------------------------------------------------------------------------
    struct array_ops_smoke_test
    {
        std::vector<int> m_DynamicList = { 1, 2, 3 };

        std::vector<array_item> m_ObjectList = { { "Alpha", 1 }, { "Beta", 2 }, { "Gamma", 3 } };

        static constexpr int             k_FixedCapacity = 8;
        std::array<int, k_FixedCapacity> m_FixedSlots = { 10, 20, 30 };
        std::uint8_t                     m_UsedSlots  = 3; // how many of m_FixedSlots[] are "live" - the rest just sit there unused

        std::array<int, 4> m_TrulyFixed = { 100, 200, 300, 400 }; // no override at all - genuinely non-resizable

        XPROPERTY_DEF
        ( "Array Ops Smoke Test", array_ops_smoke_test
        , obj_member<"Dynamic List (std::vector)", &array_ops_smoke_test::m_DynamicList
            , member_section<"Real resize - no UI style tag attached">
            , member_help<"A real std::vector, no member_ui_list_size style tag attached - Size: should now be genuinely editable since the gate reads list_table::m_bHasRealSetSize instead of the tag's (previously always-absent) presence">>
        , obj_member<"Object List (std::vector<array_item>)", &array_ops_smoke_test::m_ObjectList
            , member_ui_open<true>
            , member_section<"Object elements - same controls via list_table::TrySwap">
            , member_help<"std::vector<array_item> - each element is its own reflected struct (Name/Value), not a single scalar. Drag/insert-above/insert-below/delete should work the same as the scalar arrays above, just via real Swap on the raw objects instead of setProperty-by-path">>
        , obj_member<"Fixed Slots (std::array, capacity 8)", &array_ops_smoke_test::m_FixedSlots
            , member_overwrite_list_size<+[](array_ops_smoke_test& O, bool bRead, std::size_t& Size)
                {
                    if (bRead) Size = O.m_UsedSlots;
                    else       O.m_UsedSlots = static_cast<std::uint8_t>(Size > static_cast<std::size_t>(array_ops_smoke_test::k_FixedCapacity) ? array_ops_smoke_test::k_FixedCapacity : Size);
                }>
            , member_section<"Fixed capacity + sibling live count">
            , member_help<"std::array<int,8> - normally fixed/non-resizable. member_overwrite_list_size delegates the live count to the sibling Used Slots field below, unlocking Size: within the 8-slot capacity; slots past the live count just sit there unused, not deallocated">>
        , obj_member<"Used Slots (sibling count, read-only mirror)", &array_ops_smoke_test::m_UsedSlots
            , member_flags<xproperty::flags::SHOW_READONLY>
            , member_help<"Backs Fixed Slots' live count above - shown here read-only just to make the override visible; drive it via Fixed Slots' own Size: field, not directly">>
        , obj_member<"Truly Fixed (std::array, no override)", &array_ops_smoke_test::m_TrulyFixed
            , member_section<"Non-resizable - controls should be hidden entirely">
            , member_help<"No member_overwrite_list_size at all - genuinely non-resizable. Size: should be read-only, and none of the per-element drag/insert/delete controls should appear on any of its rows">>
        )
    };

    //------------------------------------------------------------------------------------------------
    // Small, dedicated test bed for the custom-rendering hooks (m_OnCustomRenderAppend and whatever
    // follows it) - kept separate from button_smoke_test/array_ops_smoke_test on purpose, since a
    // sprawling multi-purpose panel makes it hard to tell at a glance which row is even relevant (lost
    // a debug marker in a wall of unrelated rows once already this session). Two cases specifically:
    // a WIDE field (numeric, fills the whole value column via -1 width - the case that turned out to
    // matter, since there's zero leftover space for anything appended via SameLine()) and a NARROW one
    // (checkbox - already had visible room).
    //------------------------------------------------------------------------------------------------
    struct custom_render_smoke_test
    {
        int  m_WideNumber    = 42;
        bool m_NarrowBool    = true;
        int  m_ReplacedField = 7;
        int  m_FullRow       = 3;
        int  m_BlockStart    = 1;
        int  m_BlockEnd      = 3;
        int  m_AfterBlock    = 99;
        int  m_Seed          = 5;

        // Dedicated fields for a SECOND copy of the block, nested one scope deep - their own
        // obj_member declarations attach the SAME named tag functions (DrawBlockStartContent/
        // DrawBlockEndContent/DrawFullRowReplaceValue/DrawFullRowReplaceRow) as the top-level ones
        // below, proving the nested case still works. Separate fields rather than reusing
        // m_BlockStart/etc. directly, since reflecting the same C++ member at two different paths
        // would confuse xproperty's per-property GUID/path resolution.
        int  m_NestedBlockStart  = 1;
        int  m_NestedBlockEnd    = 3;
        int  m_NestedAfterBlock  = 44;
        int  m_NestedFullRow     = 3;

        // Backing data for Block Start's curve editor (see m_OnCustomRenderBlock/DrawCurveEditor
        // below) - the stress test for xproperty::inspector::BeginEdit/CommitEdit: a variable-length
        // reflected array (not a scalar), edited via direct field writes on a live pointer (not
        // setProperty-by-path), where "one drag = one undo step" and structural inserts/deletes both
        // need to work. Genuinely reflected (unlike the sparkline it replaces) so the whole-instance
        // snapshot BeginEdit/CommitEdit takes can actually see and restore it; m_OnCustomRenderBlock
        // claims its entire subtree (size marker + every element/member) so it never ALSO renders as
        // a normal array section underneath the canvas.
        std::vector<curve_keyframe> m_Keyframes =
        { { 0.00f, 0.20f,  0.0f,  1.2f }
        , { 0.25f, 0.80f,  1.0f, -0.6f }
        , { 0.50f, 0.35f, -0.8f,  0.8f }
        , { 0.75f, 0.90f,  0.6f, -0.3f }
        , { 1.00f, 0.55f, -0.4f,  0.0f }
        };

        XPROPERTY_DEF
        ( "Custom Render Test", custom_render_smoke_test
        , obj_member<"Wide Number (fills the column)", &custom_render_smoke_test::m_WideNumber
            , member_flags<xproperty::flags::APPEND_NEW_LINE>
            , member_custom_render_append<+[](xproperty::inspector&, const xproperty::type::object&, void*, std::string_view, const xproperty::any&) noexcept
                {
                    // Diagnostic for the APPEND_NEW_LINE background-height fix: X/Y/Z are real
                    // frame-height buttons (unlike the inspector's own default append-marker text),
                    // so their top/bottom margin against the row's background rect shows directly
                    // whether the appended line is vertically centered in the space DrawBackground
                    // reserved for it, or just flush against one edge.
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "<- appended");
                    ImGui::SameLine();
                    ImGui::Button("X", ImVec2(24, 0));
                    ImGui::SameLine();
                    ImGui::Button("Y", ImVec2(24, 0));
                    ImGui::SameLine();
                    ImGui::Button("Z", ImVec2(24, 0));
                }>
            , member_help<"Fills the value column via -1 width, no leftover space for a same-line append - opts into APPEND_NEW_LINE so the framework starts a new line before invoking its own member_custom_render_append tag instead">>
        , obj_member<"Seed (Odin-style inline button)", &custom_render_smoke_test::m_Seed
            , member_dynamic_item_width<+[](const custom_render_smoke_test&) noexcept -> float
                { return -(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x); }>
            , member_custom_render_append<+[](xproperty::inspector& Inspector, const xproperty::type::object& Obj, void* pInstance, std::string_view Path, const xproperty::any& Value) noexcept
                {
                    // Odin-style [InlineButton]: this property's own member_dynamic_item_width above
                    // already reserved exactly this much room, so the button sits flush against the
                    // field with no gap and no overlap regardless of font/DPI - nothing here needs to
                    // know or guess a pixel number itself.
                    const float Sz = ImGui::GetFrameHeight();
                    ImGui::SameLine();
                    if (ImGui::Button("\xEE\x9C\xAC", ImVec2(Sz, Sz))) // Segoe MDL2 Assets Refresh (U+E72C)
                    {
                        std::string                   Error;
                        xproperty::settings::context  Context;
                        xproperty::any                NewValue; NewValue.set<int>((Value.get<int>() * 1103515245 + 12345) & 0x7fff);
                        Inspector.BeginEdit(Obj, pInstance, "Randomize Seed");
                        xproperty::sprop::setProperty(Error, pInstance, Obj, xproperty::sprop::container::prop{ std::string(Path), NewValue }, Context);
                        Inspector.CommitEdit(Context);
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Randomize");
                }>
            , member_help<"Reserves exactly one square icon button's worth of space (GetFrameHeight() + ItemSpacing.x), computed rather than a guessed pixel number - same idea as Odin's [InlineButton] attribute in Unity. The field itself stays fully usable; the Refresh button sits right after it">>
        , obj_member<"Narrow Bool (checkbox)",         &custom_render_smoke_test::m_NarrowBool
            , member_help<"No APPEND_NEW_LINE flag - default same-line append, which already has visible room after a narrow checkbox">>
        , obj_member<"Replaced Field (level 2)",       &custom_render_smoke_test::m_ReplacedField
            , member_custom_render_replace_value<+[](xproperty::inspector& Inspector, const xproperty::type::object& Obj, void* pInstance, std::string_view Path, const xproperty::any& Value, bool& bHandled) noexcept
                {
                    bHandled = true;
                    constexpr int Cap = 10;
                    const int Val = Value.get<int>();
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.6f, 0.1f, 0.6f, 1.0f));
                    ImGui::ProgressBar(static_cast<float>(Val) / Cap, ImVec2(-1, 0), std::format("{} / {} (click to +1)", Val, Cap).c_str());
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemClicked())
                    {
                        std::string                   Error;
                        xproperty::settings::context  Context;
                        xproperty::any                NewValue; NewValue.set<int>((Val + 1) % (Cap + 1));
                        Inspector.BeginEdit(Obj, pInstance, "Increment Replaced Field");
                        xproperty::sprop::setProperty(Error, pInstance, Obj, xproperty::sprop::container::prop{ std::string(Path), NewValue }, Context);
                        Inspector.CommitEdit(Context);
                    }
                }>
            , member_help<"member_custom_render_replace_value draws a custom button here instead of the normal numeric widget entirely - clicking it writes through sprop::setProperty bracketed by BeginEdit/CommitEdit, same as every other commit in this file, so it's undoable via the Undo button above">>
        , obj_member<"Full Row Replaced (level 3)",    &custom_render_smoke_test::m_FullRow
            , member_custom_render_replace_value<DrawFullRowReplaceValue>
            , member_custom_render_replace_row<DrawFullRowReplaceRow>
            , member_help<"member_custom_render_replace_row takes over BOTH the label and value columns (the sibling member_custom_render_replace_value tag handles the value side) - the normal 'Full Row Replaced (level 3)' name never even renders">>
        , obj_member<"Block Start (level 4)",          &custom_render_smoke_test::m_BlockStart
            , member_custom_render_block<DrawBlockStartContent>
            , member_help<"Level 4 test: does 'replace multiple rows until resume' actually need new framework code, or does level 3 already provide it via consumer-side state? Declares its own block rendering directly via member_custom_render_block - a block is one property, not one property per visual element">>
        , obj_member<"Block End (level 4)",            &custom_render_smoke_test::m_BlockEnd
            , member_custom_render_block<DrawBlockEndContent>
            , member_help<"A second property still inside the same block - proves bInPersistentBlock's multi-property merge (no gap between Start's content and this one) without needing a third, purely-fallback-caught property in between. Its own tag closes the custom block and hands control back to normal rendering">>
        , obj_member<"After Block (normal)",           &custom_render_smoke_test::m_AfterBlock
            , member_help<"No custom-render registration matches this one at all - should render completely normally, proving resume genuinely works">>
        , obj_member<"Keyframes (curve editor data)",  &custom_render_smoke_test::m_Keyframes
            , member_custom_render_block<+[](xproperty::inspector& Inspector, const xproperty::type::object& Obj, void* pInstance, std::string_view Path, const xproperty::any&, std::uint32_t RowColorU32, bool bDryRun, bool& bIsBlockContent) noexcept
                {
                    // Claims this whole array's subtree - its own size-marker entry (".../Keyframes
                    // (curve editor data)[]", where DrawCurveEditor actually draws) plus every
                    // element/member entry under it - claimed but drawn as nothing, same idiom
                    // on_custom_render_block's own comment describes for a multi-property block.
                    bIsBlockContent = true;
                    if (bDryRun) return;
                    if (Path.ends_with("[]")) DrawCurveEditor(Inspector, Obj, pInstance, ImColor(RowColorU32));
                }>
            , member_help<"Backing data for the curve editor drawn as part of Block Start above - reflected so BeginEdit/CommitEdit's whole-instance snapshot can see it. Declares its own rendering directly via member_custom_render_block, right at this property's own declaration, instead of a shared inspector-wide delegate having to Path-match it out of every other property">>
        , obj_scope<"Indent Test (nested block)"
            , obj_member<"Block Start (level 4)",       &custom_render_smoke_test::m_NestedBlockStart
                , member_custom_render_block<DrawBlockStartContent>>
            , obj_member<"Block End (level 4)",         &custom_render_smoke_test::m_NestedBlockEnd
                , member_custom_render_block<DrawBlockEndContent>>
            , obj_member<"After Block (normal)",        &custom_render_smoke_test::m_NestedAfterBlock>
            , obj_member<"Full Row Replaced (level 3)", &custom_render_smoke_test::m_NestedFullRow
                , member_custom_render_replace_value<DrawFullRowReplaceValue>
                , member_custom_render_replace_row<DrawFullRowReplaceRow>
                , member_help<"Same level-3 row-replace ('Dice'), one scope deeper - draws with a plain ImGui::TextColored at whatever the current cursor position is, same as any normal label; if it lines up flush with the window edge instead of with this scope's other nested siblings, the row-replace hook is bypassing the ambient indent somehow">>
            , member_help<"Same block, one scope deeper - the framework's own Columns(1) escape should NOT reset indentation, since ImGui's indent stack (from this scope's own real TreePush) is independent of column state. If the block text lines up flush with the window edge instead of with 'Block Start' sibling's indent, that claim was wrong">>
        )
    };

    //------------------------------------------------------------------------------------------------
    // Full curve editor for custom_render_smoke_test::m_Keyframes, drawn full-width via the
    // m_OnCustomRenderBlock escape - the stress test for xproperty::inspector::BeginEdit/CommitEdit
    // (see xPropertyImGuiInspector.h/.cpp): a real drag (one drag = one undo step, not one per frame),
    // structural inserts/deletes on top of that, and a tangent handle that changes a property with no
    // row of its own - all bracketed the same way ("mutate the live pointer directly, commit once").
    // Each ImGui::IsItem*() check below refers to whichever InvisibleButton call immediately precedes
    // it - no manual "which index is being dragged" bookkeeping needed, same idiom the framework's own
    // per-row scalar widgets already use (Cmd.m_isEditing via IsItemActive/IsItemDeactivatedAfterEdit).
    //------------------------------------------------------------------------------------------------
    static void DrawCurveEditor(xproperty::inspector& Inspector, const xproperty::type::object& Obj, void* pInstance, ImColor RowColor)
    {
        auto&                          Keys    = static_cast<custom_render_smoke_test*>(pInstance)->m_Keyframes;
        xproperty::settings::context   Context; // stateless per-call helper, same ad hoc construction every other one-off setProperty call in this file already uses

        constexpr float CanvasHeight = 160.0f;
        const ImVec2    Min   = ImGui::GetCursorScreenPos();
        const float     Width = ImGui::GetContentRegionAvail().x;
        const ImVec2    Max   = ImVec2(Min.x + Width, Min.y + CanvasHeight);
        ImDrawList*     pDraw = ImGui::GetWindowDrawList();

        pDraw->AddRectFilled(Min, Max, RowColor);
        pDraw->AddRect(Min, Max, ImGui::GetColorU32(ImGuiCol_Border));
        ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "  Curve Editor - drag a point, drag its blue tangent handle, double-click empty space to insert, double-click or right-click a point to delete, scroll to zoom, right-drag to pan");
        ImGui::SameLine();
        // View (zoom/pan center) is pure UI state, not curve data - a function-local static, same
        // "single demo instance" idiom already used by the various Init flags elsewhere in this file,
        // not reflected/undoable and not part of curve_keyframe.
        static float  s_ViewScale  = 1.0f;
        static ImVec2 s_ViewCenter { 0.5f, 0.5f };
        if (ImGui::SmallButton("Reset View")) { s_ViewScale = 1.0f; s_ViewCenter = { 0.5f, 0.5f }; }

        // Time/Value both map through a zoomable [center-halfSpan, center+halfSpan] window instead of
        // a fixed [0,1] - halfSpan shrinks as s_ViewScale grows, i.e. scrolling "in" narrows the
        // visible range. No domain clamp any more either (dragging/inserting a point can now go
        // outside the original 0..1 box - reasonable once the view itself can pan/zoom past it).
        // HalfSpan is recomputed INSIDE each lambda (not hoisted into an outer const) specifically so
        // the zoom-to-cursor code below - which calls ToCurve once, mutates s_ViewScale, then calls
        // ToCurve again - sees the NEW scale on its second call instead of a value baked in from before
        // the mutation, which would silently turn "zoom to cursor" into "zoom to center" instead.
        const auto ToScreen = [&](float T, float V) noexcept
        {
            const float HalfSpanTime  = 0.5f / s_ViewScale;
            const float HalfSpanValue = 0.5f / s_ViewScale;
            const float NormT = (T - (s_ViewCenter.x - HalfSpanTime))  / (2.0f * HalfSpanTime);
            const float NormV = (V - (s_ViewCenter.y - HalfSpanValue)) / (2.0f * HalfSpanValue);
            return ImVec2(Min.x + NormT * Width, Max.y - NormV * CanvasHeight);
        };
        const auto ToCurve = [&](ImVec2 Pt) noexcept
        {
            const float HalfSpanTime  = 0.5f / s_ViewScale;
            const float HalfSpanValue = 0.5f / s_ViewScale;
            const float NormT = (Pt.x - Min.x) / Width;
            const float NormV = (Max.y - Pt.y) / CanvasHeight;
            return ImVec2( (s_ViewCenter.x - HalfSpanTime)  + NormT * (2.0f * HalfSpanTime)
                         , (s_ViewCenter.y - HalfSpanValue) + NormV * (2.0f * HalfSpanValue) );
        };

        // The catch-all "##CurveCanvas" button (below, AFTER the keyframe loop) is submitted LAST on
        // purpose: ImGui resolves overlapping items by submission order - the FIRST item submitted at
        // a given pixel permanently owns hover there for the rest of the frame (confirmed against this
        // exact vendored ImGui's own ItemHoverable(), imgui.cpp ~4942-4972 - SetNextItemAllowOverlap()
        // on a LATER small item does NOT let it steal hover from an EARLIER plain InvisibleButton; it
        // only matters between two items that both opt in, which isn't this shape). Submitting the
        // small per-keyframe buttons first, and the big background button last, means the small ones
        // simply claim their pixels before the big one ever gets a chance to - no AllowOverlap needed,
        // and it also gives the right BEHAVIOR for free: canvas correctly reads as NOT hovered directly
        // on top of a keyframe, so double-click-to-insert can't fire exactly where a point already is.
        const ImVec2 Mouse = ImGui::GetIO().MousePos;

        // Now that the view can zoom/pan, the curve and its keyframes can genuinely land outside the
        // canvas box (zoomed out, or panned) - without an explicit clip they'd bleed into whatever
        // property row sits above/below this one. PushClipRect scopes ONLY the drawing (it doesn't
        // affect widget hit-testing - InvisibleButton placement/interaction below is unaffected),
        // popped right after the keyframe loop, before the catch-all canvas button and the header
        // text/button above (which should stay visible regardless of the current zoom/pan).
        pDraw->PushClipRect(Min, Max, true);

        // Hermite curve through consecutive (already time-sorted) keyframes, using each one's own
        // in/out tangent - same interpolation Unity's AnimationCurve/InspectorCurveEditor uses.
        if (Keys.size() >= 2)
        {
            constexpr int Steps = 24;
            for (std::size_t i = 0; i + 1 < Keys.size(); ++i)
            {
                const auto& K0 = Keys[i];
                const auto& K1 = Keys[i + 1];
                const float dt = K1.m_Time - K0.m_Time;
                ImVec2 Prev = ToScreen(K0.m_Time, K0.m_Value);
                for (int s = 1; s <= Steps; ++s)
                {
                    const float t  = s / float(Steps);
                    const float t2 = t * t, t3 = t2 * t;
                    const float V  = (2*t3 - 3*t2 + 1) * K0.m_Value + (t3 - 2*t2 + t) * dt * K0.m_OutTangent
                                    + (-2*t3 + 3*t2)    * K1.m_Value + (t3 - t2)      * dt * K1.m_InTangent;
                    const ImVec2 Cur = ToScreen(K0.m_Time + t * dt, V);
                    pDraw->AddLine(Prev, Cur, IM_COL32(240, 180, 80, 255), 2.0f);
                    Prev = Cur;
                }
            }
        }

        for (std::size_t i = 0; i < Keys.size(); ++i)
        {
            auto&      K = Keys[i];
            const ImVec2 P = ToScreen(K.m_Time, K.m_Value);

            ImGui::PushID(static_cast<int>(i));

            // Tangent handle - m_OutTangent is a single scalar slope (rise/run); the handle's on-screen
            // DISTANCE from its point is a separate, purely cosmetic field (m_OutHandleLen - Hermite
            // interpolation below never reads it) precisely so it can be dragged shorter or longer,
            // not pinned to one fixed length. SlopeAndLenToHandle is the ONE formula used for both the
            // nominal (not being dragged) position AND the live drag preview, both derived the same
            // way from whatever K.m_OutTangent/m_OutHandleLen currently hold - using two DIFFERENT
            // formulas (raw mouse position while dragging, a fixed assumed length at rest) was the
            // earlier bug: any drag ending at a different distance than the fixed assumption left the
            // resting position somewhere else entirely, reading as an unexplained "snap."
            const auto SlopeAndLenToHandle = [&](float Slope, float Len) noexcept -> ImVec2
            {
                // Slope is in logical (Value/Time) units; converting to screen-space rise/run first
                // (via the canvas's own Time->X / Value->Y scale) keeps a steep LOGICAL slope reading
                // as a steep SCREEN angle too, rather than being stretched/squashed by Width vs
                // CanvasHeight being different sizes.
                const float ScreenSlope = -Slope * (CanvasHeight / Width);
                const float InvNorm     = 1.0f / std::sqrt(1.0f + ScreenSlope * ScreenSlope);
                return ImVec2(P.x + Len * InvNorm, P.y + Len * ScreenSlope * InvNorm);
            };

            ImVec2 TangentP = SlopeAndLenToHandle(K.m_OutTangent, K.m_OutHandleLen);
            ImGui::SetCursorScreenPos(ImVec2(TangentP.x - 5, TangentP.y - 5));
            ImGui::InvisibleButton("##Tangent", ImVec2(10, 10));
            const bool bTangentActive = ImGui::IsItemActive();
            const bool bTangentHot    = ImGui::IsItemHovered() || bTangentActive;
            if (ImGui::IsItemActivated())                                        Inspector.BeginEdit(Obj, pInstance, "Edit Keyframe Tangent");
            if (bTangentActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                const float Dx = Mouse.x - P.x;
                const float Dy = Mouse.y - P.y;
                const float DistSqr = Dx * Dx + Dy * Dy;
                if (DistSqr > 4.0f) // ignore right on top of P - angle AND length are undefined there
                {
                    const float ScreenSlope = Dy / (std::abs(Dx) > 0.01f ? Dx : (Dx < 0.0f ? -0.01f : 0.01f));
                    K.m_OutTangent   = std::clamp(-ScreenSlope * (Width / CanvasHeight), -5.0f, 5.0f);
                    K.m_OutHandleLen = std::clamp(std::sqrt(DistSqr), 10.0f, 90.0f);
                }
                TangentP = SlopeAndLenToHandle(K.m_OutTangent, K.m_OutHandleLen); // re-derive from this frame's update, not last frame's, so the preview tracks the live drag
            }
            if (ImGui::IsItemDeactivated())                                      Inspector.CommitEdit(Context);
            pDraw->AddLine(P, TangentP, IM_COL32(120, 200, 255, 255), 1.5f);
            pDraw->AddCircleFilled(TangentP, bTangentHot ? 5.0f : 3.5f, IM_COL32(120, 200, 255, 255));

            // Point handle - drags Time+Value directly on the live keyframe (no setProperty-by-path;
            // this IS the live pointer), committed as one undo step on release regardless of how many
            // frames the drag itself spanned.
            ImGui::SetCursorScreenPos(ImVec2(P.x - 6, P.y - 6));
            ImGui::InvisibleButton("##Key", ImVec2(12, 12));
            const bool bKeyHovered = ImGui::IsItemHovered();
            const bool bKeyActive  = ImGui::IsItemActive();
            if (ImGui::IsItemActivated())                                        Inspector.BeginEdit(Obj, pInstance, "Move Keyframe");
            if (bKeyActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                const ImVec2 C = ToCurve(Mouse);
                K.m_Time  = C.x;
                K.m_Value = C.y;
            }
            if (ImGui::IsItemDeactivated())                                      Inspector.CommitEdit(Context);
            pDraw->AddCircleFilled(P, (bKeyHovered || bKeyActive) ? 6.0f : 4.5f, IM_COL32(255, 220, 120, 255));
            pDraw->AddCircle(P, (bKeyHovered || bKeyActive) ? 6.0f : 4.5f, IM_COL32(80, 50, 10, 255), 12, 1.5f);

            // Double-click OR right-click a point to delete it (kept above the 2-keyframe floor - an
            // empty/single-point curve has nothing left to interpolate). Single immediate
            // BeginEdit/mutate/CommitEdit, no multi-frame bracket needed for a one-click structural
            // edit. Double-click on a point is handled HERE, not by the canvas's own double-click-to-
            // insert further down - "##Key" is submitted first and wins the pixel (same submission-
            // order rule this whole widget already relies on), so the canvas never even sees it as
            // hovered there and would never fire its own insert logic for this same click anyway.
            if (bKeyHovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) && Keys.size() > 2)
            {
                Inspector.BeginEdit(Obj, pInstance, "Delete Keyframe");
                Keys.erase(Keys.begin() + static_cast<std::ptrdiff_t>(i));
                Inspector.CommitEdit(Context);
                ImGui::PopID();
                break; // Keys/indices are stale after the erase - stop this frame's loop
            }

            ImGui::PopID();
        }

        pDraw->PopClipRect();

        // Submitted LAST, after every per-keyframe button above - see this function's own opening
        // comment for why submission order (not AllowOverlap) is what makes a keyframe correctly win
        // hover/drag over this catch-all background button instead of the reverse.
        ImGui::SetCursorScreenPos(Min);
        // Right button enabled too (left is the InvisibleButton default) - needed for right-drag-to-
        // pan below; this does NOT conflict with a point's own right-click-to-delete above, since
        // that fires on THAT point's own "##Key" button (submitted earlier, wins the pixel), never on
        // this canvas button underneath it.
        ImGui::InvisibleButton("##CurveCanvas", ImVec2(Width, CanvasHeight), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const bool bCanvasHovered = ImGui::IsItemHovered();
        const bool bCanvasActive  = ImGui::IsItemActive();

        // Zoom to cursor: keep whatever logical (Time,Value) point is currently under the mouse fixed
        // on screen across the scale change, rather than always zooming toward the view's center -
        // the same trick most graphics/map apps use so zooming doesn't wander you away from what you
        // were actually looking at. Takes effect next frame (ToScreen/ToCurve above already captured
        // this frame's pre-zoom values), which is imperceptible at frame rate.
        if (bCanvasHovered && ImGui::GetIO().MouseWheel != 0.0f)
        {
            const ImVec2 LogicalBefore = ToCurve(Mouse);
            s_ViewScale = std::clamp(s_ViewScale * (1.0f + ImGui::GetIO().MouseWheel * 0.15f), 0.2f, 20.0f);
            const ImVec2 LogicalAfter  = ToCurve(Mouse); // same Mouse, new scale - center hasn't moved yet, so this drifted
            s_ViewCenter.x += (LogicalBefore.x - LogicalAfter.x);
            s_ViewCenter.y += (LogicalBefore.y - LogicalAfter.y);
        }

        // Right-drag to pan - gated on ACTIVE (not just hovered) so a fast drag that momentarily
        // leaves the canvas rect doesn't drop the pan mid-gesture, same as any normal ImGui drag
        // widget. Pure view state, not curve data - no BeginEdit/CommitEdit bracket, nothing here is
        // undoable or reflected.
        if (bCanvasActive && ImGui::IsMouseDragging(ImGuiMouseButton_Right))
        {
            const ImVec2 Delta        = ImGui::GetIO().MouseDelta;
            const float  HalfSpanTime  = 0.5f / s_ViewScale;
            const float  HalfSpanValue = 0.5f / s_ViewScale;
            s_ViewCenter.x -= Delta.x / Width        * (2.0f * HalfSpanTime);
            s_ViewCenter.y += Delta.y / CanvasHeight * (2.0f * HalfSpanValue);
        }

        if (bCanvasHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            const ImVec2 C = ToCurve(Mouse);
            Inspector.BeginEdit(Obj, pInstance, "Insert Keyframe");
            curve_keyframe NewKey{ C.x, C.y, 0.0f, 0.0f };
            Keys.insert(std::lower_bound(Keys.begin(), Keys.end(), NewKey, [](const curve_keyframe& A, const curve_keyframe& B) noexcept { return A.m_Time < B.m_Time; }), NewKey);
            Inspector.CommitEdit(Context);
        }

        ImGui::SetCursorScreenPos(ImVec2(Min.x, Max.y + ImGui::GetStyle().ItemSpacing.y));
    }
}
XPROPERTY_REG(e04::button_smoke_test)
XPROPERTY_REG(e04::override_demo_test)
XPROPERTY_REG(e04::array_item)
XPROPERTY_REG(e04::curve_keyframe)
XPROPERTY_REG(e04::array_ops_smoke_test)
XPROPERTY_REG(e04::custom_render_smoke_test)

//------------------------------------------------------------------------------------------------

int E04_Example()
{
    xgpu::instance Instance;
    if (auto Err = xgpu::CreateInstance(Instance, { .m_bDebugMode = false, .m_bEnableRenderDoc = false, .m_pLogErrorFunc = e04::DebugMessage, .m_pLogWarning = e04::DebugMessage }); Err)
        return xgpu::getErrorInt(Err);

    xgpu::device Device;
    if (auto Err = Instance.Create(Device); Err)
        return xgpu::getErrorInt(Err);

    xgpu::window MainWindow;
    if (auto Err = Device.Create(MainWindow, {}); Err)
        return xgpu::getErrorInt(Err);

    //
    // Setup ImGui
    //
    xgpu::tools::imgui::CreateInstance(MainWindow);

    //
    // Main loop
    //
    while (Instance.ProcessInputEvents())
    {
        if (xgpu::tools::imgui::BeginRendering())
            continue;

        //
        // Show ImGui demo
        // 
        static bool show_demo_window = true;
        ImGui::ShowDemoWindow(&show_demo_window);

        DrawPropertyWindow();

        //
        // Smoke test: reflected member functions (obj_member<"Name", &Class::Method>) rendering
        // as real inspector buttons.
        //
        {
            static e04::button_smoke_test       ButtonSmokeTest;
            static e04::override_demo_test      OverrideDemo;
            static e04::array_ops_smoke_test    ArrayOpsDemo;
            static xproperty::inspector          ButtonInspector{ "Button Smoke Test" };
            static xproperty::ui::undo::system   ButtonUndo;
            static bool                          Init = false;
            xproperty::settings::context         Context;

            if (Init == false)
            {
                Init = true;
                ButtonInspector.clear();
                ButtonInspector.AppendEntity();
                ButtonInspector.AppendEntityComponent(*xproperty::getObject(ButtonSmokeTest), &ButtonSmokeTest);
                ButtonInspector.AppendEntityComponent(*xproperty::getObject(OverrideDemo), &OverrideDemo);
                ButtonInspector.AppendEntityComponent(*xproperty::getObject(ArrayOpsDemo), &ArrayOpsDemo);

                // OverrideDemo's own Speed/Tag now declare their own override check/reset directly via
                // member_override_check/member_override_reset (see override_demo_test's XPROPERTY_DEF)
                // - no registration needed here; ButtonInspector.m_OnOverrideCheck/m_OnOverrideReset
                // have no registrations left on this inspector at all.

                // Wired the same way as Custom Render Test's own undo below - lets Undo/Redo here
                // actually be exercised against ArrayOpsDemo's insert/delete/reorder buttons, which is
                // what the array-controls undo fix (xPropertyImGuiInspector.cpp's Commit lambda) needs
                // a real UI to verify against.
                ButtonInspector.m_OnChangeEvent.Register< [&](xproperty::inspector&, const xproperty::ui::undo::cmd& Cmd)
                {
                    ButtonUndo.Add(Cmd);
                }>();
            }

            ImGui::SetNextWindowPos(ImVec2(990, 10), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(400, 560), ImGuiCond_FirstUseEver);
            ButtonInspector.Show(Context, [&]
            {
                if (ImGui::Button("  Undo  ")) ButtonUndo.Undo(Context);
                ImGui::SameLine(80);
                if (ImGui::Button("  Redo  ")) ButtonUndo.Redo(Context);
            });
        }

        //
        // Smoke test: level 1 of the 4 planned custom-rendering levels (append-after-value). Own small
        // dedicated panel/struct rather than bolted onto Button Smoke Test - a sprawling multi-purpose
        // panel makes it hard to tell which row is even relevant (a debug marker got lost in a wall of
        // unrelated rows once already this session, purely from the panel being too busy to scan).
        //
        {
            static e04::custom_render_smoke_test CustomRenderDemo;
            static xproperty::inspector           CustomRenderInspector{ "Custom Render Test" };
            static xproperty::ui::undo::system    CustomRenderUndo;
            static bool                           Init = false;
            xproperty::settings::context          Context;

            if (Init == false)
            {
                Init = true;
                CustomRenderInspector.clear();
                CustomRenderInspector.AppendEntity();
                CustomRenderInspector.AppendEntityComponent(*xproperty::getObject(CustomRenderDemo), &CustomRenderDemo);

                // Wires this panel's own Undo/Redo, same idiom xPropertyImGuiExample.h's bundled demo
                // already uses (I.m_OnChangeEvent.Register<[&]{ UndoSystem.Add(Cmd); }>()) - the only
                // way to actually exercise the curve editor's BeginEdit/CommitEdit (and the array-
                // controls undo fix, via Button Smoke Test's own ArrayOpsDemo above) end to end.
                CustomRenderInspector.m_OnChangeEvent.Register< [&](xproperty::inspector&, const xproperty::ui::undo::cmd& Cmd)
                {
                    CustomRenderUndo.Add(Cmd);
                }>();

                // The only registration left on this delegate: a fallback marker for whatever property
                // has no member_custom_render_append tag of its own (Seed and Wide Number both declare
                // their own now - see custom_render_smoke_test's XPROPERTY_DEF). This one genuinely IS
                // an inspector-wide default policy, not a specific property's own behavior, so it stays
                // on the broadcast delegate rather than becoming a tag itself - there's no single
                // property to attach "applies to everything else" to.
                CustomRenderInspector.m_OnCustomRenderAppend.Register<+[](xproperty::inspector&, const xproperty::type::object&, void*, std::string_view, const xproperty::any&)
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "<- appended");
                }>();

                // m_OnCustomRenderReplaceValue/m_OnCustomRenderReplaceRow have no registrations left on
                // this inspector at all - Replaced Field and Full Row Replaced both declare their own
                // rendering directly via member_custom_render_replace_value/member_custom_render_replace_row
                // now (see custom_render_smoke_test's XPROPERTY_DEF).

                // m_OnCustomRenderBlock itself has no registrations left on this inspector - Block
                // Start/Block End and the curve editor's Keyframes property all declare their own
                // block rendering directly via member_custom_render_block now (see custom_render_
                // smoke_test's XPROPERTY_DEF), so this shared, inspector-wide dispatcher no longer
                // needs to know any of those specific properties exist at all. The delegate itself
                // stays available for a genuinely one-off case that doesn't warrant its own tag.
            }

            ImGui::SetNextWindowPos(ImVec2(990, 580), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(400, 160), ImGuiCond_FirstUseEver);
            CustomRenderInspector.Show(Context, [&]
            {
                if (ImGui::Button("  Undo  ")) CustomRenderUndo.Undo(Context);
                ImGui::SameLine(80);
                if (ImGui::Button("  Redo  ")) CustomRenderUndo.Redo(Context);
            });

            // A REAL xproperty::inspector targeting CustomRenderInspector's own m_Settings crashed live
            // - 'Assertion failed: type::get_obj_info<key_t> != nullptr'. Root cause found and fixed in
            // xPropertyImGuiInspector.h itself: xproperty::inspector::v2's own registration actually
            // populates get_obj_info<ImVec2> (not get_obj_info<v2> - v2's XPROPERTY_DEF uses ImVec2 as
            // its object-type argument to resolve the inherited &ImVec2::x/&ImVec2::y), so a field
            // declared AS v2 directly (like settings::m_WindowPadding used to be) hit the
            // never-populated get_obj_info<v2>. Fixed by declaring those fields as plain ImVec2 instead
            // (the intended usage per the reflected_type<ImVec2> redirect's own comment) and moving
            // that redirect's specialization to BEFORE settings uses it (an explicit specialization
            // must be visible before its template's first implicit instantiation - declaring it after,
            // as the original code did, silently used the wrong primary template until something
            // actually needed it, at which point moving the specialization later became a hard
            // compile error instead). A real inspector now works here, using the reflection registry
            // for real instead of a hand-rolled text dump.
            static xproperty::inspector MetaInspector{ "Inspector Settings" };
            static bool                 MetaInit = false;
            if (MetaInit == false)
            {
                MetaInit = true;
                MetaInspector.clear();
                MetaInspector.AppendEntity();
                MetaInspector.AppendEntityComponent(*xproperty::getObject(CustomRenderInspector.m_Settings), &CustomRenderInspector.m_Settings);
            }
            ImGui::SetNextWindowPos(ImVec2(60, 60), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
            MetaInspector.Show(Context, []{});
        }

        //
        // Render
        //
        xgpu::tools::imgui::Render();

        //
        // Pageflip the windows
        //
        MainWindow.PageFlip();
    }

    return 0;
}

