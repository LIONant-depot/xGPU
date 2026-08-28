#include "source/xGPU.h"
#include "source/tools/xgpu_imgui_breach.h"

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
        , obj_member<"Speed", &override_demo_test::m_Speed, member_section<"m_OnOverrideCheck / m_OnOverrideReset">
            , member_help<"Edit away from its base value (5.0) to see the override indicator and revert button appear">>
        , obj_member<"Tag", &override_demo_test::m_Tag>
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

        // Dedicated fields for a SECOND copy of the block, nested one scope deep - same suffix names
        // as the top-level ones above, so the existing m_OnCustomRenderBlock registration (which
        // matches via Path.ends_with(), suffix-only) picks these up for free. Separate fields rather
        // than reusing m_BlockStart/etc. directly, since reflecting the same C++ member at two
        // different paths would confuse xproperty's per-property GUID/path resolution.
        int  m_NestedBlockStart  = 1;
        int  m_NestedBlockEnd    = 3;
        int  m_NestedAfterBlock  = 44;
        int  m_NestedFullRow     = 3;

        // Backing data for Block Start's sparkline (see m_OnCustomRenderBlock below) - not itself a
        // reflected property, just what the block's own custom widget plots. Not a real waveform,
        // just enough shape variation to look like actual data rather than a flat placeholder line.
        std::array<float, 24> m_Sparkline =
        { 0.2f, 0.5f, 0.35f, 0.8f, 0.65f, 0.9f, 0.7f, 0.4f
        , 0.3f, 0.55f, 0.75f, 0.6f, 0.45f, 0.85f, 0.95f, 0.6f
        , 0.3f, 0.2f, 0.4f, 0.65f, 0.5f, 0.3f, 0.15f, 0.35f };

        XPROPERTY_DEF
        ( "Custom Render Test", custom_render_smoke_test
        , obj_member<"Wide Number (fills the column)", &custom_render_smoke_test::m_WideNumber
            , member_flags<xproperty::flags::APPEND_NEW_LINE>
            , member_help<"Fills the value column via -1 width, no leftover space for a same-line append - opts into APPEND_NEW_LINE so the framework starts a new line before invoking m_OnCustomRenderAppend instead">>
        , obj_member<"Seed (Odin-style inline button)", &custom_render_smoke_test::m_Seed
            , member_dynamic_item_width<+[](const custom_render_smoke_test&) noexcept -> float
                { return -(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x); }>
            , member_help<"Reserves exactly one square icon button's worth of space (GetFrameHeight() + ItemSpacing.x), computed rather than a guessed pixel number - same idea as Odin's [InlineButton] attribute in Unity. The field itself stays fully usable; the Refresh button sits right after it">>
        , obj_member<"Narrow Bool (checkbox)",         &custom_render_smoke_test::m_NarrowBool
            , member_help<"No APPEND_NEW_LINE flag - default same-line append, which already has visible room after a narrow checkbox">>
        , obj_member<"Replaced Field (level 2)",       &custom_render_smoke_test::m_ReplacedField
            , member_help<"m_OnCustomRenderReplaceValue draws a custom button here instead of the normal numeric widget entirely - clicking it still writes through sprop::setProperty like any other commit in this file">>
        , obj_member<"Full Row Replaced (level 3)",    &custom_render_smoke_test::m_FullRow
            , member_help<"m_OnCustomRenderReplaceRow takes over BOTH the label and value columns - the normal 'Full Row Replaced (level 3)' name never even renders">>
        , obj_member<"Block Start (level 4)",          &custom_render_smoke_test::m_BlockStart
            , member_help<"Level 4 test: does 'replace multiple rows until resume' actually need new framework code, or does level 3 already provide it via consumer-side state? This one opens a custom block and draws its own full content (header + graph) in one shot - a block is one property, not one property per visual element">>
        , obj_member<"Block End (level 4)",            &custom_render_smoke_test::m_BlockEnd
            , member_help<"A second property still inside the same block - proves bInPersistentBlock's multi-property merge (no gap between Start's content and this one) without needing a third, purely-fallback-caught property in between. Closes the custom block and hands control back to normal rendering">>
        , obj_member<"After Block (normal)",           &custom_render_smoke_test::m_AfterBlock
            , member_help<"No custom-render registration matches this one at all - should render completely normally, proving resume genuinely works">>
        , obj_scope<"Indent Test (nested block)"
            , obj_member<"Block Start (level 4)",       &custom_render_smoke_test::m_NestedBlockStart>
            , obj_member<"Block End (level 4)",         &custom_render_smoke_test::m_NestedBlockEnd>
            , obj_member<"After Block (normal)",        &custom_render_smoke_test::m_NestedAfterBlock>
            , obj_member<"Full Row Replaced (level 3)", &custom_render_smoke_test::m_NestedFullRow
                , member_help<"Same level-3 row-replace ('Dice'), one scope deeper - m_OnCustomRenderReplaceRow draws with a plain ImGui::TextColored at whatever the current cursor position is, same as any normal label; if it lines up flush with the window edge instead of with this scope's other nested siblings, the row-replace hook is bypassing the ambient indent somehow">>
            , member_help<"Same block, one scope deeper - the framework's own Columns(1) escape should NOT reset indentation, since ImGui's indent stack (from this scope's own real TreePush) is independent of column state. If the block text lines up flush with the window edge instead of with 'Block Start' sibling's indent, that claim was wrong">>
        )
    };

    // Shared between the m_OnCustomRenderReplaceRow and m_OnCustomRenderAppend registrations below -
    // both are captureless +[] lambdas (required for .Register<+[]...>()'s function-pointer
    // conversion), so this can't be a local static inside just one of them; needs namespace scope to
    // be visible to both. Without this, level 1's append (which has no Path filter of its own, by
    // design - see its own registration comment) fires even for a level-4-suppressed "middle of block"
    // row, which is correct/expected given the two levels are orthogonal, but makes for a less clean
    // demo of "fully suppressed until resume" than checking this here too.
    static bool g_bInCustomBlock = false;
}
XPROPERTY_REG(e04::button_smoke_test)
XPROPERTY_REG(e04::override_demo_test)
XPROPERTY_REG(e04::array_item)
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

                // Registered once, on this shared inspector - only reacts to paths belonging to
                // OverrideDemo's own two members; every other row (button_smoke_test's own
                // properties) just gets bIsOverridden left at its default false. A real consumer
                // (E20's material-instance case) would instead compare against/fetch from a real
                // base object rather than a plain default-constructed instance.
                ButtonInspector.m_OnOverrideCheck.Register<+[](xproperty::inspector&, const xproperty::type::object&, void* pInstance, std::string_view Path, const xproperty::any& Value, bool& bOut)
                {
                    static const e04::override_demo_test k_Base{};
                    if      (Path.ends_with("/Speed")) bOut = Value.get<float>()       != k_Base.m_Speed;
                    else if (Path.ends_with("/Tag"))   bOut = Value.get<std::string>() != k_Base.m_Tag;
                }>();
                ButtonInspector.m_OnOverrideReset.Register<+[](xproperty::inspector&, const xproperty::type::object& Obj, void* pInstance, std::string_view Path)
                {
                    static const e04::override_demo_test k_Base{};
                    std::string                   Error;
                    xproperty::settings::context  Context;
                    if (Path.ends_with("/Speed"))
                    {
                        xproperty::any A; A.set<float>(k_Base.m_Speed);
                        xproperty::sprop::setProperty(Error, pInstance, Obj, xproperty::sprop::container::prop{ std::string(Path), A }, Context);
                    }
                    else if (Path.ends_with("/Tag"))
                    {
                        xproperty::any A; A.set<std::string>(k_Base.m_Tag);
                        xproperty::sprop::setProperty(Error, pInstance, Obj, xproperty::sprop::container::prop{ std::string(Path), A }, Context);
                    }
                }>();
            }

            ImGui::SetNextWindowPos(ImVec2(990, 10), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(400, 560), ImGuiCond_FirstUseEver);
            ButtonInspector.Show(Context, []{});
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
            static bool                           Init = false;
            xproperty::settings::context          Context;

            if (Init == false)
            {
                Init = true;
                CustomRenderInspector.clear();
                CustomRenderInspector.AppendEntity();
                CustomRenderInspector.AppendEntityComponent(*xproperty::getObject(CustomRenderDemo), &CustomRenderDemo);

                // Fired unconditionally for every property on this inspector, same idiom as
                // m_OnOverrideCheck - the consumer checks Path to decide whether to draw anything.
                // Appends after BOTH properties, to compare same-line (Narrow Bool, default) vs new-line
                // (Wide Number, via APPEND_NEW_LINE) layout - the framework already positioned the
                // cursor correctly before calling this, no ImGui::SameLine() needed here.
                CustomRenderInspector.m_OnCustomRenderAppend.Register<+[](xproperty::inspector&, const xproperty::type::object& Obj, void* pInstance, std::string_view Path, const xproperty::any& Value)
                {
                    // Odin-style [InlineButton]: Seed's own member_dynamic_item_width already reserved
                    // exactly this much room, so the button sits flush against the field with no gap
                    // and no overlap regardless of font/DPI - nothing here needs to know or guess a
                    // pixel number itself.
                    if (Path.ends_with("/Seed (Odin-style inline button)"))
                    {
                        const float Sz = ImGui::GetFrameHeight();
                        ImGui::SameLine();
                        if (ImGui::Button("\xEE\x9C\xAC", ImVec2(Sz, Sz))) // Segoe MDL2 Assets Refresh (U+E72C)
                        {
                            std::string                   Error;
                            xproperty::settings::context  Context;
                            xproperty::any                NewValue; NewValue.set<int>((Value.get<int>() * 1103515245 + 12345) & 0x7fff);
                            xproperty::sprop::setProperty(Error, pInstance, Obj, xproperty::sprop::container::prop{ std::string(Path), NewValue }, Context);
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Randomize");
                        return;
                    }

                    // Diagnostic for the APPEND_NEW_LINE background-height fix: X/Y/Z are real
                    // frame-height buttons (unlike the plain text below), so their top/bottom margin
                    // against the row's background rect shows directly whether the appended line is
                    // vertically centered in the space DrawBackground reserved for it, or just
                    // flush against one edge.
                    if (Path.ends_with("/Wide Number (fills the column)"))
                    {
                        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "<- appended");
                        ImGui::SameLine();
                        ImGui::Button("X", ImVec2(24, 0));
                        ImGui::SameLine();
                        ImGui::Button("Y", ImVec2(24, 0));
                        ImGui::SameLine();
                        ImGui::Button("Z", ImVec2(24, 0));
                        return;
                    }

                    // Block Start/Middle/End now escape the grid entirely via m_OnCustomRenderBlock
                    // (registered below) - Render() never reaches this hook for them at all anymore,
                    // so no g_bInCustomBlock check is needed here any more.
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "<- appended");
                }>();

                // Smoke test for level 2: replace the value column's default widget entirely. A plain
                // repainted button (same shape as a normal button, different color) doesn't actually
                // demonstrate why custom rendering matters - an Odin-style [ProgressBar] does, since
                // ImGui has no built-in "clickable progress bar" widget at all. ImGui::ProgressBar()
                // itself isn't interactive, so IsItemClicked() right after it is what turns it into
                // one - still commits through sprop::setProperty on click, same write path every other
                // commit in this file already uses.
                CustomRenderInspector.m_OnCustomRenderReplaceValue.Register<+[](xproperty::inspector&, const xproperty::type::object& Obj, void* pInstance, std::string_view Path, const xproperty::any& Value, bool& bHandled)
                {
                    if (Path.ends_with("/Replaced Field (level 2)"))
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
                            xproperty::sprop::setProperty(Error, pInstance, Obj, xproperty::sprop::container::prop{ std::string(Path), NewValue }, Context);
                        }
                    }
                    // Right half of the level 3 smoke test below - m_OnCustomRenderReplaceRow draws the
                    // left column; the automatic NextColumn() between the two delegates means this one
                    // still has to separately handle the right column itself.
                    else if (Path.ends_with("/Full Row Replaced (level 3)"))
                    {
                        bHandled = true;
                        if (ImGui::Button(std::format("Roll (currently {})", Value.get<int>()).c_str(), ImVec2(-1, 0)))
                        {
                            std::string                   Error;
                            xproperty::settings::context  Context;
                            xproperty::any                NewValue; NewValue.set<int>((Value.get<int>() % 6) + 1);
                            xproperty::sprop::setProperty(Error, pInstance, Obj, xproperty::sprop::container::prop{ std::string(Path), NewValue }, Context);
                        }
                    }
                }>();

                // Smoke test for level 3 (Full Row Replaced): replace the LEFT column (label) -
                // composes with the level 2 registration above, which handles the SAME property's
                // right column, to fully replace the row.
                CustomRenderInspector.m_OnCustomRenderReplaceRow.Register<+[](xproperty::inspector&, const xproperty::type::object&, void*, std::string_view Path, const xproperty::any&, bool& bHandled)
                {
                    if (Path.ends_with("/Full Row Replaced (level 3)"))
                    {
                        bHandled = true;
                        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.9f, 1.0f), "Dice");
                        return;
                    }
                }>();

                // Smoke test for the "block" mechanism (Block Start/End): genuinely leaves the 2-column
                // grid, unlike level 4's old approach (still inside the grid, just suppressing both
                // columns) - full window width, no override-revert button, no help tooltip, no
                // background stripe from the framework. Block Start opens g_bInCustomBlock and draws
                // its ENTIRE visual content (header text + sparkline graph) in one shot - a block is
                // one property with one callback, not one property per visual element making up the
                // block. An earlier version split the header and the graph into two separate backing
                // properties (Start drawing only the text, a since-removed "Block Middle" drawing only
                // the graph) purely to prove multi-property merging worked - but that made a single
                // logical block look, and BE, two stacked rows, which is real UX confusion for anyone
                // wanting one cohesive block (a curve editor doesn't want its title as a separate row
                // from the curve). Block End is still its own, second property - still proving
                // bInPersistentBlock's multi-property merge (no gap between Start's content and End's
                // footer) without forcing a single logical block to fragment into more rows than it has
                // reason to.
                //
                // Filling the background with RowColor - the same shade the grid's own striping would
                // have used - turns out to be the common case, not an edge case: a block is still part
                // of the property list visually, so it should still carry the SAME row-striping look
                // the rest of the panel has, just spanning the full width instead of one column.
                //
                // The graph is an actual ImGui::PlotLines sparkline instead of leaving the block body
                // blank - a real widget that GENUINELY needs the full width a curve editor would (this
                // is the exact motivation the block feature exists for at all), not another button or
                // checkbox in disguise. pInstance is cast to read the smoke test's own backing array
                // directly, same as any other consumer would reach its own live data.
                //
                // Fires twice per property (bDryRun true then false, see on_custom_render_block's own
                // comment) - only the actual drawing is skipped when bDryRun is true.
                CustomRenderInspector.m_OnCustomRenderBlock.Register<+[](xproperty::inspector&, const xproperty::type::object&, void* pInstance, std::string_view Path, const xproperty::any&, ImColor RowColor, bool bDryRun, bool& bIsBlockContent)
                {
                    const auto FillRow = [&](float Height)
                    {
                        const ImVec2 Min = ImGui::GetCursorScreenPos();
                        const ImVec2 Max = ImVec2(Min.x + ImGui::GetContentRegionAvail().x, Min.y + Height);
                        ImGui::GetWindowDrawList()->AddRectFilled(Min, Max, RowColor);
                        return Min;
                    };
                    // A block nested inside a scope picks up a subtle light border "for free" at its
                    // top/bottom edges - from the scope's own framed-content styling, not anything this
                    // panel draws - while a top-level block (no enclosing scope) has no such border,
                    // confirmed live as a real, visible inconsistency between the two. Explicit borders
                    // here, using ImGui's own ImGuiCol_Border (matching what the framed case already
                    // looks like rather than inventing a different color) make the treatment identical
                    // regardless of nesting, instead of relying on incidental styling that only shows
                    // up sometimes.
                    const ImU32 BorderColor = ImGui::GetColorU32(ImGuiCol_Border);

                    if (Path.ends_with("/Block Start (level 4)"))
                    {
                        e04::g_bInCustomBlock = true;
                        bIsBlockContent = true;
                        if (bDryRun) return;
                        constexpr float PlotHeight = 70.0f;
                        // Every formula tried for this fill's height (GetFrameHeight(), then
                        // GetTextLineHeightWithSpacing() + PlotHeight, then + ItemSpacing.y, then +1px)
                        // matched the top-level copy of this block but not the nested one - confirmed
                        // live via debug logging that the header TEXT ITEM ITSELF starts ~3.5px later
                        // (matching this style's FramePadding.y exactly) when nested than its OWN
                        // captured start cursor (Min.y) says it should, while PlotLines' own advance
                        // was identical in both cases. That's some framework/ImGui baseline-offset
                        // interaction with being the first item after a scope opens, not anything
                        // controllable from here - no formula written up front can account for it.
                        // Fixed properly by not guessing at all: split the draw list into two channels,
                        // draw the REAL content (text + plot) first on the content channel, MEASURE
                        // the actual resulting cursor advance, then paint the background on the
                        // earlier-merged channel using that real measurement - the same deferred-
                        // background technique the framework itself already uses for rows whose height
                        // isn't known until their content has actually drawn (see m_RowExtraHeightCache
                        // and its own leaf-branch call site in xPropertyImGuiInspector.cpp).
                        ImDrawList* pDrawList = ImGui::GetWindowDrawList();
                        const ImVec2 Min = ImGui::GetCursorScreenPos();
                        const float  Width = ImGui::GetContentRegionAvail().x; // captured early - a late re-measure drifts once content has drawn, see the framework's own "late width measurement" fix for why
                        pDrawList->ChannelsSplit(2);
                        pDrawList->ChannelsSetCurrent(1);
                        ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "  Custom Block (Odin-style [Curve]/[ProgressBar] area)");
                        // PlotLines draws its own ImGuiCol_FrameBg rectangle first, which would sit on
                        // TOP of (and hide) the manually-filled RowColor rect painted below - pushing
                        // FrameBg to RowColor instead gives the plot the same row-striping shade for
                        // free, no separate fill needed. Drawn right here, in the SAME property's
                        // callback as the header text above, so the header and the graph it labels are
                        // genuinely one row - not two properties standing in for one logical block.
                        auto& Data = static_cast<e04::custom_render_smoke_test*>(pInstance)->m_Sparkline;
                        ImGui::PushStyleColor(ImGuiCol_FrameBg, static_cast<ImVec4>(RowColor));
                        ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.9f, 0.7f, 0.3f, 1.0f));
                        ImGui::PlotLines("##Sparkline", Data.data(), static_cast<int>(Data.size()), 0, nullptr, 0.0f, 1.0f, ImVec2(-1, PlotHeight));
                        ImGui::PopStyleColor(2);
                        const float RealBottom = ImGui::GetCursorScreenPos().y;
                        pDrawList->ChannelsSetCurrent(0);
                        pDrawList->AddRectFilled(Min, ImVec2(Min.x + Width, RealBottom), RowColor);
                        pDrawList->AddLine(Min, ImVec2(Min.x + Width, Min.y), BorderColor, 1.0f);
                        pDrawList->ChannelsMerge();
                        return;
                    }
                    if (Path.ends_with("/Block End (level 4)"))
                    {
                        e04::g_bInCustomBlock = false; // resume normal grid rendering starting with the NEXT property
                        bIsBlockContent = true;
                        if (bDryRun) return;
                        // Just the closing border line, no reserved height/fill - a half-height
                        // footer rect was here before, but it only ever drew to the draw list (no
                        // real widget call), so the cursor never advanced past it and it silently
                        // overlapped whatever the NEXT property drew (invisible while framework
                        // backgrounds painted over both in the same shade, confirmed live as a real
                        // overlap once those backgrounds were turned off). Rather than fix that by
                        // making it consume real space (which just turned the invisible overlap into
                        // a visible, unlabeled empty-looking strip - "why do we need that, makes zero
                        // sense"), drop the fill and the reserved space entirely: draw the one line the
                        // block actually needs to mark its own end, at zero cursor cost.
                        const ImVec2 Min = ImGui::GetCursorScreenPos();
                        // Plain black, not the theme's ImGuiCol_Border gray - that gray blends
                        // differently against the top-level block's lighter background vs the nested
                        // block's darker one, so it never read as a consistent "black line" on both.
                        ImGui::GetForegroundDrawList()->AddLine(Min, ImVec2(Min.x + ImGui::GetContentRegionAvail().x, Min.y), IM_COL32(0, 0, 0, 255), 1.0f);
                        return;
                    }
                }>();
            }

            ImGui::SetNextWindowPos(ImVec2(990, 580), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(400, 160), ImGuiCond_FirstUseEver);
            CustomRenderInspector.Show(Context, []{});

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

