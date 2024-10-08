#ifndef XPROPERTY_IMGUI_INSPECTOR_H
#define XPROPERTY_IMGUI_INSPECTOR_H
#pragma once

#include<functional>

#ifndef IMGUI_API
    #include "imgui.h"
#endif

#ifndef MY_PROPERTIES_H
    #include "my_properties.h"
#endif

// Microsoft and its macros....
#undef max

// Disable this warnings
#pragma warning( push )
#pragma warning( disable : 4201)                    // warning C4201: nonstandard extension used: nameless struct/union

//-----------------------------------------------------------------------------------
// Different Editor Styles to display the properties, easy to access for the user.
//-----------------------------------------------------------------------------------
namespace xproperty
{
    class inspector;

    //-----------------------------------------------------------------------------------
    // Undo command information
    //-----------------------------------------------------------------------------------
    namespace ui::undo
    {
        //-----------------------------------------------------------------------------------
        // Base class of the cmd class
        struct cmd
        {
            std::string                     m_Name;                 // Full name of the property been edited
            const xproperty::type::object*  m_pPropObject;          // Pointer to the property table object
            void*                           m_pClassObject;         // Pointer to the class instance
            xproperty::any                  m_NewValue;             // Whenever a value changes we put it here
            xproperty::any                  m_Original;             // This is the value of the property before we made any changes
            union
            {
                std::uint8_t                m_Flags{ 0 };
                struct
                {
                    bool                    m_isEditing : 1         // Is Item currently been edited
                                          , m_isChange  : 1;
                };
            };

            XPROPERTY_DEF
            ( "UndoCmd", cmd
            , obj_member_ro<"Name",     &cmd::m_Name >
            , obj_member_ro<"NewValue", +[](cmd& O, bool bRead, std::string& OutValue)
                {
                    assert(bRead);
                    if (O.m_NewValue.m_pType)
                    {
                        std::array<char, 256> Buffer;
                        xproperty::settings::AnyToString(Buffer, O.m_NewValue);
                        OutValue = Buffer.data();
                    }
                    else
                    {
                        OutValue = "nullptr";
                    }
                }>
            , obj_member_ro<"Original", +[](cmd& O, bool bRead, std::string& OutValue)
                {
                    assert(bRead);
                    if (O.m_Original.m_pType)
                    {
                        std::array<char, 256> Buffer;
                        xproperty::settings::AnyToString(Buffer, O.m_Original);
                        OutValue = Buffer.data();
                    }
                    else
                    {
                        OutValue = "nullptr";
                    }
                }>
            )
        };
        XPROPERTY_REG(cmd)


        //-----------------------------------------------------------------------------------
        struct system
        {
            std::vector<cmd>  m_lCmds     {};
            int               m_Index     { 0 };
            int               m_MaxSteps  { 15 };

            void clear ( void ) noexcept
            {
                m_Index = 0;
                m_lCmds.clear();
            }

            XPROPERTY_DEF
            ( "System", system
            , obj_member_ro<"Index",    &system::m_Index,    member_ui<int>::drag_bar<0.6f> >
            , obj_member   <"MaxSteps", &system::m_MaxSteps, member_ui<int>::drag_bar<0.6f> >
            , obj_member_ro<"Commands", &system::m_lCmds >
            )
        };
        XPROPERTY_REG(system)
    }

    namespace ui::details
    {
        struct group_render;
    }
}

//-----------------------------------------------------------------------------------
// Inspector to display the properties
//-----------------------------------------------------------------------------------

class xproperty::inspector : public xproperty::base
{
public:

    struct v2 : ImVec2
    {
        XPROPERTY_DEF
        ( "ImVec2", ImVec2, xproperty::settings::vector2_group
        , obj_member<"X", &ImVec2::x, member_ui<float>::scroll_bar<0.0f, 20.0f>, member_help<"X element of a vector"> >
        , obj_member<"Y", &ImVec2::y, member_ui<float>::scroll_bar<0.0f, 20.0f>, member_help<"Y element of a vector"> >
        )
    };

    struct settings
    {
        ImVec2      m_WindowPadding             { 0, 3 };
        ImVec2      m_FramePadding              { 1, 3.5 };
        ImVec2      m_ItemSpacing               { 0.5f, 1.5f };
        float       m_IndentSpacing             { 3.5 };
        ImVec2      m_TableFramePadding         { 2, 6 };

        bool        m_bRenderLeftBackground     { true };
        bool        m_bRenderRightBackground    { true };
        bool        m_bRenderBackgroundDepth    { true };
        float       m_ColorVScalar1             { 0.5f };
        float       m_ColorVScalar2             { 0.4f };
        float       m_ColorSScalar              { 0.4f };

        ImVec2      m_HelpWindowPadding         { 10, 10 };
        int         m_HelpWindowSizeInChars     { 50 };

        XPROPERTY_DEF
        ( "Settings", settings
        , obj_member<"WindowPadding", &settings::m_WindowPadding,  member_help<"Blank Border for the property window"> >
        , obj_member<"FramePadding",  &settings::m_FramePadding,   member_help<"Main/Top Property Border size"> >
        , obj_member<"ItemSpacing",   &settings::m_ItemSpacing,    member_help<"Main/Top Property Border size"> >
        , obj_member<"IndentSpacing", &settings::m_IndentSpacing,  member_help<"Main/Top Property Border size"> >
        , obj_scope 
            < "Background"
            , obj_member<"RenderLeft",      &settings::m_bRenderLeftBackground, member_help<"Disable the rendering of the background on the left"> >
            , obj_member<"RenderRight",     &settings::m_bRenderRightBackground, member_help<"Disable the rendering of the background on the right"> >
            , obj_member<"Depth",           &settings::m_bRenderBackgroundDepth, member_help<"Disable the rendering of multiple color background"> >
            , obj_member<"ColorVScalar1",   &settings::m_ColorVScalar1, member_ui<float>::scroll_bar<0.0f, 2.0f>, member_help<"Changes the Luminosity of one of the alternate colors for the background"> >
            , obj_member<"ColorVScalar2",   &settings::m_ColorVScalar2, member_ui<float>::scroll_bar<0.0f, 2.0f>, member_help<"Changes the Luminosity of one of the alternate colors for the background"> >
            , obj_member<"ColorSScalar",    &settings::m_ColorSScalar,  member_ui<float>::scroll_bar<0.0f, 10.0f>, member_help<"Changes the Saturation for all the colors in the background"> >
            >
        , obj_scope
            < "Help Popup"
            , obj_member<"HelpWindowPadding",       &settings::m_HelpWindowPadding,     member_help<"Border size"> >
            , obj_member<"HelpWindowSizeInChars",   &settings::m_HelpWindowSizeInChars, member_ui<int>::scroll_bar<1, 200>, member_help<"Max Size of the help window popup when it opens"> >
            >
        )
    };

public:

    inline                  inspector               ( const char* pName="Inspector", bool isOpen = true)    noexcept : m_pName{pName}, m_bWindowOpen{isOpen} {}
    virtual                ~inspector               ( void )                                                noexcept = default;
                void        clear                   ( void )                                                noexcept;
                void        AppendEntity            ( void )                                                noexcept;
                void        AppendEntityComponent   ( const xproperty::type::object& PropObject, void* pBase ) noexcept;
                void        Undo                    ( void )                                                noexcept;
                void        Redo                    ( void )                                                noexcept;
                void        Show                    ( std::function<void(void)> Callback )                  noexcept;
    inline      bool        isValid                 ( void )                                        const   noexcept { return m_lEntities.empty() == false; }
    inline      void        setupWindowSize         ( int Width, int Height )                               noexcept { m_Width = Width; m_Height = Height; }
    inline      void        setOpenWindow           ( bool b )                                              noexcept { m_bWindowOpen = b; }
    constexpr   bool        isWindowOpen            ( void )                                        const   noexcept { return m_bWindowOpen; }


    settings                                            m_Settings {};

protected:

    struct entry
    {
        xproperty::sprop::container::prop               m_Property;
        const char*                                     m_pHelp;
        const char*                                     m_pName;
        std::uint32_t                                   m_GUID;
        std::uint32_t                                   m_GroupGUID;
        const xproperty::type::members*                 m_pUserData;
        int                                             m_Dimensions;
        int                                             m_MyDimension;
        xproperty::flags::type                          m_Flags;
    };

    struct component
    {
        std::pair<const xproperty::type::object*, void*>    m_Base { nullptr,nullptr };
        std::vector<std::unique_ptr<entry>>                 m_List {};
    };

    struct entity
    {
        std::vector<std::unique_ptr<component>>     m_lComponents {};
    };

protected:

    void        RefreshAllProperties                ( void )                                        noexcept;
    void        Render                              ( component& C, int& GlobalIndex )              noexcept;
    void        Show                                ( void )                                        noexcept;
    void        DrawBackground                      ( int Depth, int GlobalIndex )          const   noexcept;
    void        HelpMarker                          ( const char* desc )                    const   noexcept;
    void        Help                                ( const entry& Entry )                  const   noexcept;

protected:

    const char*                                 m_pName         {nullptr};
    std::vector<std::unique_ptr<entity>>        m_lEntities     {};
    int                                         m_Width         {430};
    int                                         m_Height        {450};
    bool                                        m_bWindowOpen   { true };
    xproperty::ui::undo::system                 m_UndoSystem    {};
    xproperty::settings::context                m_Context       {};

    friend struct ui::details::group_render;

    XPROPERTY_VDEF
    ( "Inspector", inspector
    , obj_member<"Settings",   &inspector::m_Settings >
    , obj_member<"UndoSystem", &inspector::m_UndoSystem >
    )
};

XPROPERTY_VREG2(inspect_props,  xproperty::inspector)
XPROPERTY_REG2(v2_props,        xproperty::inspector::v2)
XPROPERTY_REG2(settings_props,  xproperty::inspector::settings)

#pragma warning( pop ) 
#endif