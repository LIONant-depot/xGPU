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
}
XPROPERTY_REG(e04::button_smoke_test)

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
            static xproperty::inspector          ButtonInspector{ "Button Smoke Test" };
            static bool                          Init = false;
            xproperty::settings::context         Context;

            if (Init == false)
            {
                Init = true;
                ButtonInspector.clear();
                ButtonInspector.AppendEntity();
                ButtonInspector.AppendEntityComponent(*xproperty::getObject(ButtonSmokeTest), &ButtonSmokeTest);
            }

            ImGui::SetNextWindowPos(ImVec2(990, 10), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_FirstUseEver);
            ButtonInspector.Show(Context, []{});
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

