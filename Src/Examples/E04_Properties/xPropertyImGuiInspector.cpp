
#include "xPropertyImGuiInspector.h"
#include "..\..\dependencies\xproperty\source\sprop\property_sprop_getset.h"
#include "..\..\dependencies\xproperty\source\sprop\property_sprop_collector.h"
#include <windows.h>

//-----------------------------------------------------------------------------------
// All the render functions
//-----------------------------------------------------------------------------------
namespace xproperty::ui::details
{
    //-----------------------------------------------------------------------------------
    template< typename T>
    bool ReadOnly( const char* pFmt, T Value )
    {
        std::array<char, 128> Buff;
        snprintf( Buff.data(), Buff.size(), pFmt, Value );
        ImGui::Button( Buff.data(), ImVec2( -1, 0 ) );
        return false;
    }

    //-----------------------------------------------------------------------------------

    template< auto T_IMGUID_DATA_TYPE_V, typename T >
    static void DragRenderNumbers(undo::cmd& Cmd, const T& Value, const xproperty::ui::styles<T>& I, xproperty::flags::type Flags) noexcept
    {
        if (Flags.m_isShowReadOnly) ui::details::ReadOnly(I.m_pFormat, Value);
        else
        {
            T V = Value;
            Cmd.m_isChange = ImGui::SliderScalar("##value", T_IMGUID_DATA_TYPE_V, &V, &I.m_Min, &I.m_Max, I.m_pFormat);
            if (Cmd.m_isChange)
            {
                if (Cmd.m_isEditing == false) Cmd.m_Original.set<T>(Value);
                Cmd.m_isEditing = true;
                Cmd.m_NewValue.set<T>(V);
            }
            if (Cmd.m_isEditing && ImGui::IsItemDeactivatedAfterEdit()) Cmd.m_isEditing = false;
        }
    }

    //-----------------------------------------------------------------------------------
    template< auto T_IMGUID_DATA_TYPE_V, typename T >
    static void SlideRenderNumbers( undo::cmd& Cmd, const T& Value, const xproperty::ui::styles<T>& I, xproperty::flags::type Flags ) noexcept
    {
        if ( Flags.m_isShowReadOnly ) ui::details::ReadOnly( I.m_pFormat, Value );
        else
        {
            T V = Value;
            Cmd.m_isChange = ImGui::DragScalar("##value", T_IMGUID_DATA_TYPE_V, &V, I.m_Speed, &I.m_Min, &I.m_Max, I.m_pFormat );
            if( Cmd.m_isChange )
            {
                if (Cmd.m_isEditing == false) Cmd.m_Original.set<T>(Value);
                Cmd.m_isEditing = true;
                Cmd.m_NewValue.set<T>(V);
            }
            if( Cmd.m_isEditing && ImGui::IsItemDeactivatedAfterEdit() ) Cmd.m_isEditing = false;
        }
    }

    //-----------------------------------------------------------------------------------
    // 64 bits int
    //-----------------------------------------------------------------------------------

    template<>
    void draw<std::int64_t, style::scroll_bar>::Render( undo::cmd& Cmd, const std::int64_t& Value, const xproperty::ui::styles<std::int64_t>& I, xproperty::flags::type Flags ) noexcept
      { DragRenderNumbers<ImGuiDataType_S64>( Cmd, Value, I, Flags ); }

    //-----------------------------------------------------------------------------------

    template<>
    void draw<std::int64_t, style::drag_bar>::Render( undo::cmd& Cmd, const std::int64_t& Value, const xproperty::ui::styles<std::int64_t>& I, xproperty::flags::type Flags ) noexcept
      { SlideRenderNumbers<ImGuiDataType_S64>(Cmd, Value, I, Flags);}

    //-----------------------------------------------------------------------------------

    template<>
    void draw<std::uint64_t, style::scroll_bar>::Render( undo::cmd& Cmd, const std::uint64_t& Value, const xproperty::ui::styles<std::uint64_t>& I, xproperty::flags::type Flags ) noexcept
      { DragRenderNumbers<ImGuiDataType_U64>( Cmd, Value, I, Flags ); }

    //-----------------------------------------------------------------------------------

    template<>
    void draw<std::uint64_t, style::drag_bar>::Render( undo::cmd& Cmd, const std::uint64_t& Value, const xproperty::ui::styles<std::uint64_t>& I, xproperty::flags::type Flags ) noexcept
      { SlideRenderNumbers<ImGuiDataType_U64>(Cmd, Value, I, Flags);}

    //-----------------------------------------------------------------------------------
    // 32 bits int
    //-----------------------------------------------------------------------------------

    template<>
    void draw<std::int32_t, style::scroll_bar>::Render( undo::cmd& Cmd, const std::int32_t& Value, const xproperty::ui::styles<std::int32_t>& I, xproperty::flags::type Flags ) noexcept
      { DragRenderNumbers<ImGuiDataType_S32>( Cmd, Value, I, Flags ); }

    //-----------------------------------------------------------------------------------

    template<>
    void draw<std::int32_t, style::drag_bar>::Render( undo::cmd& Cmd, const std::int32_t& Value, const xproperty::ui::styles<std::int32_t>& I, xproperty::flags::type Flags ) noexcept
      { SlideRenderNumbers<ImGuiDataType_S32>(Cmd, Value, I, Flags);}

    //-----------------------------------------------------------------------------------

    template<>
    void draw<std::uint32_t, style::scroll_bar>::Render( undo::cmd& Cmd, const std::uint32_t& Value, const xproperty::ui::styles<std::uint32_t>& I, xproperty::flags::type Flags ) noexcept
      { DragRenderNumbers<ImGuiDataType_U32>( Cmd, Value, I, Flags ); }

    //-----------------------------------------------------------------------------------

    template<>
    void draw<std::uint32_t, style::drag_bar>::Render( undo::cmd& Cmd, const std::uint32_t& Value, const xproperty::ui::styles<std::uint32_t>& I, xproperty::flags::type Flags ) noexcept
      { SlideRenderNumbers<ImGuiDataType_U32>(Cmd, Value, I, Flags);}

    //-----------------------------------------------------------------------------------
    // 16 bits int
    //-----------------------------------------------------------------------------------

    template<>
    void draw<std::int16_t, style::scroll_bar>::Render( undo::cmd& Cmd, const std::int16_t& Value, const xproperty::ui::styles<std::int16_t>& I, xproperty::flags::type Flags ) noexcept
      { DragRenderNumbers<ImGuiDataType_S16>( Cmd, Value, I, Flags ); }

    //-----------------------------------------------------------------------------------

    template<>
    void draw<std::int16_t, style::drag_bar>::Render( undo::cmd& Cmd, const std::int16_t& Value, const xproperty::ui::styles<std::int16_t>& I, xproperty::flags::type Flags ) noexcept
      { SlideRenderNumbers<ImGuiDataType_S16>(Cmd, Value, I, Flags);}

    //-----------------------------------------------------------------------------------

    template<>
    void draw<std::uint16_t, style::scroll_bar>::Render( undo::cmd& Cmd, const std::uint16_t& Value, const xproperty::ui::styles<std::uint16_t>& I, xproperty::flags::type Flags ) noexcept
      { DragRenderNumbers<ImGuiDataType_U16>( Cmd, Value, I, Flags ); }

    //-----------------------------------------------------------------------------------

    template<>
    void draw<std::uint16_t, style::drag_bar>::Render( undo::cmd& Cmd, const std::uint16_t& Value, const xproperty::ui::styles<std::uint16_t>& I, xproperty::flags::type Flags ) noexcept
      { SlideRenderNumbers<ImGuiDataType_U16>(Cmd, Value, I, Flags);}

    //-----------------------------------------------------------------------------------
    // 8 bits int
    //-----------------------------------------------------------------------------------

    template<>
    void draw<std::int8_t, style::scroll_bar>::Render( undo::cmd& Cmd, const std::int8_t& Value, const xproperty::ui::styles<std::int8_t>& I, xproperty::flags::type Flags ) noexcept
      { DragRenderNumbers<ImGuiDataType_S8>( Cmd, Value, I, Flags ); }

    //-----------------------------------------------------------------------------------

    template<>
    void draw<std::int8_t, style::drag_bar>::Render( undo::cmd& Cmd, const std::int8_t& Value, const xproperty::ui::styles<std::int8_t>& I, xproperty::flags::type Flags ) noexcept
      { SlideRenderNumbers<ImGuiDataType_S8>(Cmd, Value, I, Flags);}

    //-----------------------------------------------------------------------------------

    template<>
    void draw<std::uint8_t, style::scroll_bar>::Render( undo::cmd& Cmd, const std::uint8_t& Value, const xproperty::ui::styles<std::uint8_t>& I, xproperty::flags::type Flags ) noexcept
      { DragRenderNumbers<ImGuiDataType_U8>( Cmd, Value, I, Flags ); }

    //-----------------------------------------------------------------------------------

    template<>
    void draw<std::uint8_t, style::drag_bar>::Render( undo::cmd& Cmd, const std::uint8_t& Value, const xproperty::ui::styles<std::uint8_t>& I, xproperty::flags::type Flags ) noexcept
      { SlideRenderNumbers<ImGuiDataType_U8>(Cmd, Value, I, Flags);}

    //-----------------------------------------------------------------------------------
    // Float
    //-----------------------------------------------------------------------------------

    template<>
    void draw<float, style::scroll_bar>::Render(undo::cmd& Cmd, const float& Value, const xproperty::ui::styles<float>& I, xproperty::flags::type Flags) noexcept
      { DragRenderNumbers<ImGuiDataType_Float>(Cmd, Value, I, Flags);}

    //-----------------------------------------------------------------------------------

    template<>
    void draw<float, style::drag_bar>::Render(undo::cmd& Cmd, const float& Value, const xproperty::ui::styles<float>& I, xproperty::flags::type Flags) noexcept
      { SlideRenderNumbers<ImGuiDataType_Float>(Cmd, Value, I, Flags); }

    //-----------------------------------------------------------------------------------
    // Double
    //-----------------------------------------------------------------------------------

    template<>
    void draw<double, style::scroll_bar>::Render(undo::cmd& Cmd, const double& Value, const xproperty::ui::styles<double>& I, xproperty::flags::type Flags) noexcept
      { DragRenderNumbers<ImGuiDataType_Double>(Cmd, Value, I, Flags);}

    //-----------------------------------------------------------------------------------

    template<>
    void draw<double, style::drag_bar>::Render(undo::cmd& Cmd, const double& Value, const xproperty::ui::styles<double>& I, xproperty::flags::type Flags) noexcept
      { SlideRenderNumbers<ImGuiDataType_Double>(Cmd, Value, I, Flags); }

    //-----------------------------------------------------------------------------------
    // OTHERS!!!
    //-----------------------------------------------------------------------------------

    template<>
    void draw<bool, style::defaulted>::Render(undo::cmd& Cmd, const bool& Value, const xproperty::ui::styles<bool>& I, xproperty::flags::type Flags) noexcept
    {
        bool V = Value;

        if ( Flags.m_isShowReadOnly )
        {
            ImGui::Checkbox("##value", &V);
            V = Value;
        }
        else 
        {
            Cmd.m_isChange = ImGui::Checkbox("##value", &V);
            if ( Cmd.m_isChange )
            {
                if(Cmd.m_isEditing == false) Cmd.m_Original.set<bool>(Value);
                Cmd.m_isEditing = true;
                Cmd.m_NewValue.set<bool>(V);
            } 
            if( Cmd.m_isEditing && ImGui::IsItemDeactivatedAfterEdit() ) Cmd.m_isEditing = false;
        }

        ImGui::SameLine();
        if (V) ImGui::Text(" True");
        else   ImGui::Text(" False");
    }

    //-----------------------------------------------------------------------------------

    template<>
    void draw<std::string, style::defaulted>::Render(undo::cmd& Cmd, const std::string& Value, const xproperty::ui::styles<std::string>& I, xproperty::flags::type Flags) noexcept
    {
        if ( Flags.m_isShowReadOnly ) ImGui::InputText( "##value", (char*)Value.c_str(), Value.length(), ImGuiInputTextFlags_ReadOnly );
        else
        {
            std::array<char, 256> buff;
            Value.copy( buff.data(), Value.length() );
            buff[ Value.length() ] = 0;
            Cmd.m_isChange = ImGui::InputText( "##value", buff.data(), buff.size(), ImGuiInputTextFlags_EnterReturnsTrue );
            if ( Cmd.m_isChange )
            {
                if( Cmd.m_isEditing == false ) Cmd.m_Original.set<std::string>(Value);
                Cmd.m_isEditing = true;
                Cmd.m_NewValue.set<std::string>( buff.data() );
            }
            if( Cmd.m_isEditing && ImGui::IsItemDeactivatedAfterEdit() ) Cmd.m_isEditing = false;
        }
    }

    //-----------------------------------------------------------------------------------

    template<>
    void draw<std::string, style::button>::Render(undo::cmd& Cmd, const std::string& Value, const xproperty::ui::styles<std::string>& I, xproperty::flags::type Flags) noexcept
    {
        if ( Flags.m_isShowReadOnly ) ImGui::Button( Value.c_str(), ImVec2(-1,16) );
        else
        {
            Cmd.m_isChange = ImGui::Button( Value.c_str(), ImVec2(-1,16) );
            if ( Cmd.m_isChange )
            {
                if( Cmd.m_isEditing == false ) Cmd.m_Original.set<std::string>(Value);
                Cmd.m_isEditing = true;
                Cmd.m_NewValue.set<std::string>( Value );
            }
            if( Cmd.m_isEditing && ImGui::IsItemDeactivatedAfterEdit() ) Cmd.m_isEditing = false;
        }        
    }

    //-----------------------------------------------------------------------------------

    void draw_enums( undo::cmd& Cmd, const xproperty::any& AnyValue, xproperty::flags::type Flags) noexcept
    {
     //   assert(AnyValue.isEnum);
        assert(AnyValue.m_pType);
        assert(AnyValue.m_pType->m_RegisteredEnumSpan.size() > 0 );

        //
        // get the current selected item index
        //
        std::size_t current_item = [&]
        {
            // first extract the value of the enum for any type...
            std::uint64_t Value = [&]()->std::uint64_t
            {
                switch (AnyValue.m_pType->m_Size)
                {
                    case 1: return *reinterpret_cast<const std::uint8_t*>(&AnyValue.m_Data);
                    case 2: return *reinterpret_cast<const std::uint16_t*>(&AnyValue.m_Data);
                    case 4: return *reinterpret_cast<const std::uint32_t*>(&AnyValue.m_Data);
                    case 8: return *reinterpret_cast<const std::uint64_t*>(&AnyValue.m_Data);
                }

                assert(false);
                return 0;
            }();

            // then search to find which is the index
            std::size_t current_item=0;
            for (; current_item < AnyValue.m_pType->m_RegisteredEnumSpan.size(); ++current_item)
            {
                if (Value == AnyValue.m_pType->m_RegisteredEnumSpan[current_item].m_Value)
                {
                    break;
                }
            }

            // if we don't find the index... then we have a problem...
            if (current_item == AnyValue.m_pType->m_RegisteredEnumSpan.size())
            {
                // We should have had a value in the list...
                assert(false);
            }

            return current_item;
        }();

        //
        // Handle the UI part...
        //
        if (Flags.m_isShowReadOnly) 
        {
            ImGui::InputText("##value", (char*)AnyValue.m_pType->m_RegisteredEnumSpan[current_item].m_pName, std::strlen(AnyValue.m_pType->m_RegisteredEnumSpan[current_item].m_pName), ImGuiInputTextFlags_ReadOnly);
        }
        else
        {

            Cmd.m_isChange = false;
            if (ImGui::BeginCombo("##combo", AnyValue.m_pType->m_RegisteredEnumSpan[current_item].m_pName)) // The second parameter is the label previewed before opening the combo.
            {
                for (std::size_t n = 0; n < AnyValue.m_pType->m_RegisteredEnumSpan.size(); n++)
                {
                    bool is_selected = (current_item == n); // You can store your selection however you want, outside or inside your objects

                    if (ImGui::Selectable(AnyValue.m_pType->m_RegisteredEnumSpan[n].m_pName, is_selected))
                    {
                        if (Cmd.m_isEditing == false) Cmd.m_Original = AnyValue;

                        //
                        // Set the new value
                        //

                        // First iniailize all the type information...
                        Cmd.m_NewValue = AnyValue;

                        // Now overrite the value in a generic way...
                        switch (AnyValue.m_pType->m_Size)
                        {
                        case 1: *reinterpret_cast<std::uint8_t*>(&Cmd.m_NewValue.m_Data)  = AnyValue.m_pType->m_RegisteredEnumSpan[n].m_Value; break;
                        case 2: *reinterpret_cast<std::uint16_t*>(&Cmd.m_NewValue.m_Data) = AnyValue.m_pType->m_RegisteredEnumSpan[n].m_Value; break;
                        case 4: *reinterpret_cast<std::uint32_t*>(&Cmd.m_NewValue.m_Data) = AnyValue.m_pType->m_RegisteredEnumSpan[n].m_Value; break;
                        case 8: *reinterpret_cast<std::uint64_t*>(&Cmd.m_NewValue.m_Data) = AnyValue.m_pType->m_RegisteredEnumSpan[n].m_Value; break;
                        }
                        Cmd.m_isChange = true;
                    }

                    if (is_selected)
                        ImGui::SetItemDefaultFocus();   // You may set the initial focus when opening the combo (scrolling + for keyboard navigation support)
                }
                ImGui::EndCombo();
            }
        }
    }

    //-----------------------------------------------------------------------------------
    /*
    template<>
    void draw<oobb, style::defaulted>(undo::cmd<oobb>& Cmd, const oobb& Value, const xproperty::ui::style_info<oobb>&, xproperty::flags::type Flags) noexcept
    {
        ImGuiStyle * style   = &ImGui::GetStyle();
        const auto   Width   = (ImGui::GetContentRegionAvail().x - style->ItemInnerSpacing.x ) / 2;
        const auto   Height  = ImGui::GetFrameHeight();
        ImVec2       pos     = ImGui::GetCursorScreenPos();
        oobb         Temp    = Value;

        ImGui::PushItemWidth( Width );

        // Min
        bool bChange      = ImGui::DragFloat( "##value1", &Temp.m_Min, 0.01f, -1000.0f, 1000.0f );
        bool bDoneEditing = ImGui::IsItemDeactivatedAfterEdit();
        ImGui::GetWindowDrawList()->AddRectFilled( pos, ImVec2( pos.x + Width, pos.y + Height ), ImU32( 0x440000ff ) );

        // Max
        ImGui::SameLine( 0, 2 );
        pos = ImGui::GetCursorScreenPos();

        bChange      |= ImGui::DragFloat( "##value2", &Temp.m_Max, 0.01f, -1000.0f, 1000.0f );
        bDoneEditing |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::GetWindowDrawList()->AddRectFilled( pos, ImVec2( pos.x + Width, pos.y + Height ), ImU32( 0x4400ff00 ) );

        // Done
        ImGui::PopItemWidth();

        if( bChange )
        {
            if ( Flags.m_isShowReadOnly ) return;

            if (Cmd.m_isEditing == false) Cmd.m_Original = Value;

            Cmd.m_isEditing     = true;
            Cmd.m_isChange      = true;
            Cmd.m_NewValue      = Temp;
        }
        if( bDoneEditing )
        {
            Cmd.m_isEditing = false;
        }
    }
    */

    static
    void onRender( xproperty::ui::undo::cmd& Cmd, const xproperty::any& Value, const xproperty::type::members& Entry, xproperty::flags::type Flags ) noexcept
    {
        using                       generic    = void(xproperty::ui::undo::cmd&, const std::uint64_t& Value, const std::uint64_t& I, xproperty::flags::type Flags) noexcept;

        //
        // Enums are handle special... 
        //
        if (Value.m_pType->m_IsEnum)
        {
            draw_enums( Cmd, Value, Flags);
            return;
        }

        //
        // Handle the rest of UI elements
        //
        const xproperty::ui::style_base& StyleBase = [&]() -> const xproperty::ui::style_base&
        {
            const xproperty::settings::member_ui_t* pMemberUI = Entry.getUserData<xproperty::settings::member_ui_t>();

            // Lets see if the user decided to set the style... 
            if (pMemberUI == nullptr)
            {
                static constexpr auto s_DefaultStyleS64     = xproperty::ui::styles<std::int64_t> ::Default();
                static constexpr auto s_DefaultStyleU64     = xproperty::ui::styles<std::uint64_t>::Default();
                static constexpr auto s_DefaultStyleS32     = xproperty::ui::styles<std::int32_t> ::Default();
                static constexpr auto s_DefaultStyleU32     = xproperty::ui::styles<std::uint32_t>::Default();
                static constexpr auto s_DefaultStyleS16     = xproperty::ui::styles<std::int16_t> ::Default();
                static constexpr auto s_DefaultStyleU16     = xproperty::ui::styles<std::uint16_t>::Default();
                static constexpr auto s_DefaultStyleS8      = xproperty::ui::styles<std::int8_t>  ::Default();
                static constexpr auto s_DefaultStyleU8      = xproperty::ui::styles<std::uint8_t> ::Default();
                static constexpr auto s_DefaultStyleChar    = xproperty::ui::styles<char>         ::Default();
                static constexpr auto s_DefaultStyleFloat   = xproperty::ui::styles<float>        ::Default();
                static constexpr auto s_DefaultStyleDouble  = xproperty::ui::styles<double>       ::Default();
                static constexpr auto s_DefaultStyleString  = xproperty::ui::styles<std::string>  ::Default();
                static constexpr auto s_DefaultStyleBool    = xproperty::ui::styles<bool>          ::Default();

                switch (Value.m_pType->m_GUID)
                {
                case xproperty::settings::var_type<std::uint64_t>::guid_v:   return s_DefaultStyleU64;   
                case xproperty::settings::var_type<std::int64_t>::guid_v:    return s_DefaultStyleS64;   
                case xproperty::settings::var_type<std::int32_t>::guid_v:    return s_DefaultStyleS32;   
                case xproperty::settings::var_type<std::uint32_t>::guid_v:   return s_DefaultStyleU32;   
                case xproperty::settings::var_type<std::int16_t>::guid_v:    return s_DefaultStyleS16;   
                case xproperty::settings::var_type<std::uint16_t>::guid_v:   return s_DefaultStyleU16;   
                case xproperty::settings::var_type<std::int8_t>::guid_v:     return s_DefaultStyleS8;    
                case xproperty::settings::var_type<std::uint8_t>::guid_v:    return s_DefaultStyleU8;    
                case xproperty::settings::var_type<float>::guid_v:           return s_DefaultStyleFloat; 
                case xproperty::settings::var_type<double>::guid_v:          return s_DefaultStyleDouble;
                case xproperty::settings::var_type<std::string>::guid_v:     return s_DefaultStyleString;
                case xproperty::settings::var_type<bool>::guid_v:            return s_DefaultStyleBool;  
                default: assert(false); return s_DefaultStyleBool;
                }
            }
            else
            {
                assert(pMemberUI);
                assert(pMemberUI->m_pStyleBase);
                return *pMemberUI->m_pStyleBase;
            }
        }();

        // Check to make sure that the user did not made a mistake...
        // the actual type of the property must match the type of the UI style..
        assert(Value.m_pType->m_GUID == StyleBase.m_TypeGUID );

        // Sanity check make sure that we have a function as well... 
        assert(StyleBase.m_pDrawFn);

        //ImGui::PushID(&Entry);
        reinterpret_cast<generic*>(StyleBase.m_pDrawFn)(Cmd, *reinterpret_cast<const std::uint64_t*>(&Value), *reinterpret_cast<const std::uint64_t*>(&StyleBase), Flags);
        //ImGui::PopID();
    }
}


//-------------------------------------------------------------------------------------------------
// Inspector
//-------------------------------------------------------------------------------------------------

static std::array<ImColor, 20> s_ColorCategories =
{
    ImColor{ 0xffe8c7ae },
    ImColor{ 0xffb4771f },
    ImColor{ 0xff0e7fff },
    ImColor{ 0xff2ca02c },
    ImColor{ 0xff78bbff },
    ImColor{ 0xff8adf98 },
    ImColor{ 0xff2827d6 },
    ImColor{ 0xff9698ff },
    ImColor{ 0xffbd6794 },
    ImColor{ 0xffd5b0c5 },
    ImColor{ 0xff4b568c },
    ImColor{ 0xff949cc4 },
    ImColor{ 0xffc277e3 },
    ImColor{ 0xffd2b6f7 },
    ImColor{ 0xff7f7f7f },
    ImColor{ 0xffc7c7c7 },
    ImColor{ 0xff22bdbc },
    ImColor{ 0xff8ddbdb },
    ImColor{ 0xffcfbe17 },
    ImColor{ 0xffe5da9e }
};

//-------------------------------------------------------------------------------------------------

void xproperty::inspector::clear(void) noexcept
{
    m_lEntities.clear();
    m_UndoSystem.clear();
}

//-------------------------------------------------------------------------------------------------
void xproperty::inspector::AppendEntity(void) noexcept
{
    m_lEntities.push_back( std::make_unique<entity>() );
}

//-------------------------------------------------------------------------------------------------
void xproperty::inspector::AppendEntityComponent(const xproperty::type::object& Object, void* pBase) noexcept
{
    auto Component = std::make_unique<component>();

    // Cache the information
    Component->m_Base = { &Object, pBase };

    m_lEntities.back()->m_lComponents.push_back(std::move(Component));

}

//-------------------------------------------------------------------------------------------------

void xproperty::inspector::Undo(void) noexcept
{
    if (m_UndoSystem.m_Index == 0 || m_UndoSystem.m_lCmds.size() == 0)
        return;

    auto&               Value = m_UndoSystem.m_lCmds[--m_UndoSystem.m_Index];
    std::string         Error;
    xproperty::sprop::io_property<true>(Error, Value.m_pClassObject, *Value.m_pPropObject, xproperty::sprop::container::prop{ Value.m_Name, Value.m_Original }, m_Context);

    if (Error.empty() == false)
    {
        // Print error...
    }
}

//-------------------------------------------------------------------------------------------------

void xproperty::inspector::Redo(void) noexcept
{
    if (m_UndoSystem.m_Index == static_cast<int>(m_UndoSystem.m_lCmds.size()))
        return;

    auto&               Value = m_UndoSystem.m_lCmds[m_UndoSystem.m_Index++];
    std::string         Error;
    xproperty::sprop::io_property<true>(Error, Value.m_pClassObject, *Value.m_pPropObject, xproperty::sprop::container::prop{ Value.m_Name, Value.m_NewValue }, m_Context);

    if (Error.empty() == false)
    {
        // Print error...
    }
}

//-------------------------------------------------------------------------------------------------
    
void xproperty::inspector::RefreshAllProperties( void ) noexcept
{
    for ( auto& E : m_lEntities )
    {
        for ( auto& C : E->m_lComponents )
        {
            C->m_List.clear();
            int                    iDimensions = -1;
            int                    myDimension = -1;
            xproperty::sprop::collector( C->m_Base.second, *C->m_Base.first, m_Context, [&](const char* pPropertyName, xproperty::any&& Value, const xproperty::type::members& Member, bool isConst)
            {
                std::uint32_t          GUID        = Member.m_GUID;
                xproperty::flags::type Flags;
                Flags.m_isShowReadOnly = isConst;
                Flags.m_isDontSave     = false;
                Flags.m_isDontShow     = false;
                Flags.m_isScope        =    std::holds_alternative<xproperty::type::members::scope>(Member.m_Variant)
                                         || std::holds_alternative<xproperty::type::members::props>(Member.m_Variant);

                if(Flags.m_isScope || std::holds_alternative<xproperty::type::members::var>(Member.m_Variant) )
                {
                    iDimensions = -1;
                    myDimension = -1;
                }

                // Check if we are dealing with atomic types and the size field...
                /*
                if ( Flags.m_isScope == false
                    && ( std::holds_alternative<xproperty::type::members::list_props>(Member.m_Variant)
                         || std::holds_alternative<xproperty::type::members::list_var>(Member.m_Variant))
                    && Value.m_pType->m_GUID == xproperty::settings::var_type<std::size_t>::guid_v )
                    */
                if ( std::holds_alternative<xproperty::type::members::list_props>(Member.m_Variant)
                     || std::holds_alternative<xproperty::type::members::list_var>(Member.m_Variant) )
                {
                    auto i = std::strlen(pPropertyName);
                    if( (pPropertyName[i-1] == ']') && (pPropertyName[i - 2] == '[') )
                    {
                        Flags.m_isScope = true;

                        std::visit([&](auto& List )
                        {
                            if constexpr (std::is_same_v<decltype(List), const xproperty::type::members::list_props&> ||
                                          std::is_same_v<decltype(List), const xproperty::type::members::list_var&>  )
                            {
                                myDimension = 0;
                                iDimensions = static_cast<int>(List.m_Table.size());
                                for (i -= 3; pPropertyName[i] == ']'; --i)
                                {
                                    myDimension++;

                                    // Find the matching closing bracket...
                                    while (pPropertyName[--i] != '[');
                                }
                            }
                            else
                            {
                                assert(false);
                            }

                        }, Member.m_Variant );

                        // We don't deal with zero size arrays...
                        if (0 == Value.get<std::size_t>())
                            return;
                    }
                    else
                    {
                        
                    }
                }

                auto* pHelp = Member.getUserData<xproperty::settings::member_help_t>();

                C->m_List.push_back
                ( std::make_unique<entry>
                    ( xproperty::sprop::container::prop{ pPropertyName, std::move(Value) }
                    , pHelp ? pHelp->m_pHelp : "<<No help>>"
                    , Member.m_pName
                    , Member.m_GUID
                    , Flags.m_isScope ? nullptr : &Member
                    , iDimensions
                    , myDimension
                    , Flags
                    ) 
                );
            }, true );
        }
    }

    int a = 33;
}

//-------------------------------------------------------------------------------------------------

void xproperty::inspector::Show( std::function<void(void)> Callback ) noexcept
{
    if( m_bWindowOpen == false ) return;

    //
    // Key styles 
    //
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding,   m_Settings.m_WindowPadding );
    ImGui::PushStyleVar( ImGuiStyleVar_FramePadding,    m_Settings.m_FramePadding );
    ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing,     m_Settings.m_ItemSpacing );
    ImGui::PushStyleVar( ImGuiStyleVar_IndentSpacing,   m_Settings.m_IndentSpacing );
    ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 0 );

    //
    // Open the window
    //
    ImGui::SetNextWindowSize( ImVec2( static_cast<float>(m_Width), static_cast<float>(m_Height) ), ImGuiCond_FirstUseEver );
    if ( !ImGui::Begin( m_pName, &m_bWindowOpen ) )
    {
        ImGui::PopStyleVar( 5 );
        ImGui::End();
        return;
    }

    //
    // Let the user inject something at the top of the window
    //
    Callback();

    //
    // Display the properties
    //
    ImGui::Columns( 2 );
    ImGui::Separator();

    Show();

    ImGui::Columns( 1 );
    ImGui::Separator();
    ImGui::PopStyleVar( 5 );
    ImGui::End();
}

//-------------------------------------------------------------------------------------------------

void xproperty::inspector::Render( component& C, int& GlobalIndex ) noexcept
{
    struct element
    {
        std::uint32_t   m_CRC;
        int             m_iArray;
        std::size_t     m_iStart;
        std::size_t     m_iEnd;
        int             m_OpenAll;
        int             m_MyDimension;
        bool            m_isOpen        : 1
                        , m_isAtomicArray : 1
                        , m_isReadOnly    : 1;
    };

    int                         iDepth   = -1;
    std::array<element,32>      Tree;
    auto                        PushTree = [&]( std::uint32_t UID, const char* pName, std::size_t iStart, std::size_t iEnd, int myDimension, bool isReadOnly, bool bArray = false, bool bAtomic = false )
    {
        bool Open = iDepth<0? true : Tree[ iDepth ].m_isOpen;
        if( Open )
        {
            if ( iDepth >0 && Tree[iDepth-1].m_OpenAll ) ImGui::SetNextItemOpen( Tree[iDepth-1].m_OpenAll > 0 );
            Open = ImGui::TreeNodeEx( pName, ImGuiTreeNodeFlags_DefaultOpen | ( ( iDepth == -1 ) ? ImGuiTreeNodeFlags_Framed : 0 ) );
        }
        auto& L = Tree[ ++iDepth ];

        L.m_CRC             = UID;
        L.m_iArray          = bArray ? 0 : -1;
        L.m_iStart          = iStart;
        L.m_iEnd            = iEnd;
        L.m_OpenAll         = 0;
        L.m_isOpen          = Open;
        L.m_isAtomicArray   = bAtomic;
        L.m_isReadOnly      = isReadOnly || ((iDepth>0)?Tree[ iDepth -1 ].m_isReadOnly : false);
        L.m_MyDimension     = myDimension;

        return Open;
    };
    auto PopTree = [ & ]( std::size_t& iStart, std::size_t& iEnd )
    {
        // Handle muti-dimensional array increment of entries
        if (Tree[iDepth].m_MyDimension >= 0 && iDepth > 1 ) Tree[iDepth - 1].m_iArray++;

        const auto& E = Tree[ iDepth-- ];

        if( E.m_isOpen )
        {
            ImGui::TreePop();
        }

        iStart = E.m_iStart;
        iEnd   = E.m_iEnd;
    };

    std::size_t iStart = 0;
    std::size_t iEnd   = strlen( C.m_Base.first->m_pName );

    //
    // Deal with the top most tree
    //
    {
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, m_Settings.m_TableFramePadding );
        ImGui::AlignTextToFramePadding();

        // If the main tree is Close then forget about it
        PushTree( C.m_Base.first->m_GUID, C.m_Base.first->m_pName, iStart, iEnd, -1, false );

        ImGui::NextColumn();
        ImGui::AlignTextToFramePadding();

        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled( pos, ImVec2( pos.x + ImGui::GetContentRegionAvail().x, pos.y + ImGui::GetFrameHeight() ), ImGui::GetColorU32( ImGuiCol_Header ) );
        ImGui::PopStyleVar();
    }
        
    if( Tree[iDepth].m_isOpen == false )
    {
        PopTree( iStart, iEnd );
        return;
    }

    //
    // Do all properties
    //
    for ( std::size_t iE = 0; iE<C.m_List.size(); ++iE )
    {
        auto& E = *C.m_List[iE];

        //
        // If we have a close tree skip same level entries
        //
        auto CheckSameLevel = [&]
        {
            if (E.m_Property.m_Path.size() < iStart || E.m_Property.m_Path.size() < iEnd) return false;

            // Handle multidimensional arrays...
            if (E.m_Flags.m_isScope 
             && E.m_Dimensions > 1 
             && Tree[iDepth].m_iArray >= 0 
             && Tree[iDepth].m_MyDimension >= E.m_MyDimension
             ) return false;

            return xproperty::settings::strguid({ &E.m_Property.m_Path.c_str()[iStart], static_cast<std::uint32_t>(iEnd - iStart + 1) }) == Tree[iDepth].m_CRC;
        };

        bool bSameLevel = CheckSameLevel();

        if( Tree[iDepth].m_isOpen == false && bSameLevel )
            continue;

        //
        // Do we need to pop scopes?
        //
        while( bSameLevel == false )
        {
            PopTree( iStart, iEnd );
            bSameLevel = CheckSameLevel();
        }

        // Make sure at this point everything is open
        assert( Tree[iDepth].m_isOpen );

        //
        // Render the left column
        //
        ++GlobalIndex;
        ImGui::NextColumn();
        ImGui::AlignTextToFramePadding();
        ImVec2 lpos = ImGui::GetCursorScreenPos();
        if( Tree[iDepth].m_iArray >= 0 ) ImGui::PushID( E.m_GUID + Tree[iDepth].m_iArray + iDepth * 1000 + Tree[iDepth].m_MyDimension * 1000000 );
        else                             ImGui::PushID( E.m_GUID + iDepth * 1000 );
        if ( m_Settings.m_bRenderLeftBackground ) DrawBackground( iDepth, GlobalIndex );

        bool bRenderBlankRight = false;

        // Create a new tree
        if( E.m_Flags.m_isScope ) 
        {
            // Is an array?
            if( E.m_Property.m_Path.back() == ']' )
            {
                if( Tree[iDepth].m_iArray < 0 )
                {
                    std::array<char, 128> Name;
                    snprintf(Name.data(), Name.size(), "%s[]", E.m_pName );
                    PushTree( E.m_GUID, Name.data(), iStart, iEnd, E.m_MyDimension, E.m_Flags.m_isShowReadOnly, true, C.m_List[iE+1]->m_Property.m_Path.back() == ']' );
                    iStart = iEnd + 1;
                    iEnd   = E.m_Property.m_Path.size() - 2;
                }
                else
                {
                    // multi dimensional arrays
                    std::array<char, 128> Name;
                    int stroffset = 0;
                    stroffset += snprintf(&Name[stroffset], Name.size() - stroffset, "%s", E.m_pName);
                    for( int i= E.m_MyDimension-1; i >= 0; --i )
                    {
                        auto Index = Tree[iDepth - i].m_iArray;
                        stroffset += snprintf( &Name[stroffset], Name.size()- stroffset, "[%d]", Index );
                    }

                    stroffset += snprintf(&Name.data()[stroffset], Name.size() - stroffset, "[]");
                    PushTree(E.m_GUID, Name.data(), iStart, iEnd, E.m_MyDimension, E.m_Flags.m_isShowReadOnly, true, C.m_List[iE + 1]->m_Property.m_Path.back() == ']');
                }
            }
            else
            {
                PushTree( E.m_GUID, E.m_pName, iStart, iEnd, E.m_MyDimension, E.m_Flags.m_isShowReadOnly );
                iStart = iEnd + 1;
                iEnd   = E.m_Property.m_Path.size();
            }
        }
        else
        {
            // if it is an array...
            if( Tree[iDepth].m_iArray >= 0 )
            {
                std::array<char, 128> Name;
                snprintf( Name.data(), Name.size(), "[%d]", Tree[iDepth].m_iArray++ );

                // Atomic array
                if ( Tree[iDepth].m_isAtomicArray )
                {
                    ImGui::TreeNodeEx( reinterpret_cast<void*>(static_cast<std::size_t>(E.m_GUID)), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen, "%s", Name.data() );
                }
                else
                {
                    auto NewEnd = iEnd;
                    while( E.m_Property.m_Path[NewEnd] != '/' ) NewEnd++;

                    auto CRC = xproperty::settings::strguid( { &E.m_Property.m_Path.c_str()[ iStart ], static_cast<std::uint32_t>( NewEnd - iStart + 1 ) } );

                    PushTree(CRC, Name.data(), iStart, iEnd, E.m_MyDimension, E.m_Flags.m_isShowReadOnly );
                    iEnd = NewEnd;

                    bRenderBlankRight = true;

                    // We need to redo this entry
                    iE--;
                }
            }
            else
            {
                ImGui::TreeNodeEx( reinterpret_cast<void*>(static_cast<std::size_t>(E.m_GUID)), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen, "%s", E.m_pName );
            }
        }

        if ( E.m_Flags.m_isShowReadOnly || Tree[iDepth].m_isReadOnly )
        {
            ImColor     CC = ImVec4(0.7f, 0.7f, 1.0f, 0.35f);
            ImGui::GetWindowDrawList()->AddRectFilled(lpos, ImVec2(lpos.x + ImGui::GetContentRegionAvail().x, lpos.y + ImGui::GetFrameHeight()), CC);
        }

        // Print the help
        if ( ImGui::IsItemHovered() && bRenderBlankRight == false )
        {
            Help( E );
        }

        //
        // Render the right column
        //
        ImGui::NextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::PushItemWidth( -1 );
        ImVec2 rpos = ImGui::GetCursorScreenPos();

        if( E.m_Flags.m_isScope || bRenderBlankRight )
        {
            if ( m_Settings.m_bRenderRightBackground ) DrawBackground( iDepth-1, GlobalIndex );

            if (E.m_Property.m_Path.back() == ']' )
            {
                ImGui::Text(" Size: %llu  ", E.m_Property.m_Value.get<std::size_t>());
                if(Tree[iDepth].m_isOpen) ImGui::SameLine();
            }

            if( iDepth>0 && Tree[iDepth].m_isAtomicArray == false && Tree[iDepth].m_isOpen )
            {
                if ( ImGui::Button( " O " ) ) Tree[iDepth-1].m_OpenAll = 1;
                HelpMarker( "Open/Expands all entries in the list" );
                ImGui::SameLine();
                if ( ImGui::Button( " C " ) ) Tree[iDepth-1].m_OpenAll = -1;
                HelpMarker( "Closes/Collapses all entries in the list" );
            }

            if (E.m_Flags.m_isShowReadOnly || Tree[iDepth].m_isReadOnly)
            {
                ImColor     CC = ImVec4(0.7f, 0.7f, 1.0f, 0.35f);
                ImGui::GetWindowDrawList()->AddRectFilled(rpos, ImVec2(rpos.x + ImGui::GetContentRegionAvail().x, rpos.y + ImGui::GetFrameHeight()), CC);
            }
        }
        else
        {
            if ( m_Settings.m_bRenderRightBackground ) DrawBackground( iDepth, GlobalIndex );

            if ( E.m_Flags.m_isShowReadOnly || Tree[iDepth].m_isReadOnly )
            {
                E.m_Flags.m_isShowReadOnly = true;

                ImGuiStyle* style = &ImGui::GetStyle();
                ImVec2      pos   = ImGui::GetCursorScreenPos();
                ImColor     CC    = ImVec4( 0.7f, 0.7f, 1.0f, 0.35f );
                ImVec4      CC2f  = style->Colors[ ImGuiCol_Text ];

                CC2f.x *= 1.1f;
                CC2f.y *= 0.8f;
                CC2f.z *= 0.8f;

                ImGui::PushStyleColor( ImGuiCol_Text, CC2f );

                {
                    xproperty::ui::undo::cmd Cmd;
                    xproperty::ui::details::onRender(Cmd, E.m_Property.m_Value, *E.m_pUserData, E.m_Flags);
                    assert(Cmd.m_isChange == false);
                    assert(Cmd.m_isEditing == false);
                }

                ImGui::PopStyleColor();

                ImGui::GetWindowDrawList()->AddRectFilled( pos, ImVec2( pos.x + ImGui::GetContentRegionAvail().x, pos.y + ImGui::GetFrameHeight() ), CC );
            }
            else
            {
                // Determine if we are dealing with the same entry we are editing
                [&]
                {
                    // Check if we are editing an entry already
                    if( m_UndoSystem.m_lCmds.size() > 0 && m_UndoSystem.m_Index < static_cast<int>(m_UndoSystem.m_lCmds.size() ) )
                    {
                        auto& CmdVariant = m_UndoSystem.m_lCmds[m_UndoSystem.m_Index];

                        // Same data type?
                        if( (E.m_Property.m_Value.m_pType && CmdVariant.m_Original.m_pType ) && (E.m_Property.m_Value.m_pType->m_GUID == CmdVariant.m_Original.m_pType->m_GUID) )
                        {
                            auto& UndoCmd = CmdVariant;
                            if( ( UndoCmd.m_isEditing || UndoCmd.m_isChange ) && std::strcmp( UndoCmd.m_Name.c_str(), E.m_Property.m_Path.c_str() ) == 0 )
                            {
                                xproperty::ui::details::onRender( UndoCmd, E.m_Property.m_Value, *E.m_pUserData, E.m_Flags );

                                if (UndoCmd.m_isChange)
                                {
                                    std::string Error;
                                    xproperty::sprop::setProperty( Error, C.m_Base.second, *C.m_Base.first, { E.m_Property.m_Path.c_str(), UndoCmd.m_NewValue }, m_Context );
                                    assert(Error.empty());
                                }
                                

                                if( UndoCmd.m_isEditing == false )
                                {
                                    // Make sure to mark this cmd is done
                                    UndoCmd.m_isChange = false;

                                    // Make it an official undo step
                                    m_UndoSystem.m_Index++;
                                    assert( m_UndoSystem.m_Index == static_cast<int>(m_UndoSystem.m_lCmds.size()) );
                                }
                                return;
                            }
                        }
                    }
                
                    // Any other entry except the editing entry gets handle here
                    xproperty::ui::undo::cmd Cmd;
                    xproperty::ui::details::onRender(Cmd, E.m_Property.m_Value, *E.m_pUserData, E.m_Flags);
                    if( Cmd.m_isEditing || Cmd.m_isChange )
                    {
                        assert( m_UndoSystem.m_Index <= static_cast<int>(m_UndoSystem.m_lCmds.size()) );

                        // Set the property value
                        if (Cmd.m_isChange)
                        {
                            std::string Error;
                            xproperty::sprop::setProperty(Error, C.m_Base.second, *C.m_Base.first, { E.m_Property.m_Path.c_str(), Cmd.m_NewValue }, m_Context);
                            assert(Error.empty());
                        }
                        

                        // Make sure we reset the undo buffer to current entry
                        if( m_UndoSystem.m_Index < static_cast<int>(m_UndoSystem.m_lCmds.size()) )
                            m_UndoSystem.m_lCmds.erase( m_UndoSystem.m_lCmds.begin() + m_UndoSystem.m_Index, m_UndoSystem.m_lCmds.end()  );

                        // Make sure we don't have more entries than we should
                        if( m_UndoSystem.m_Index > m_UndoSystem.m_MaxSteps )
                        {
                            m_UndoSystem.m_lCmds.erase(m_UndoSystem.m_lCmds.begin());
                            m_UndoSystem.m_Index--;
                        }

                        // Insert the cmd into the list
                        Cmd.m_Name.assign( E.m_Property.m_Path.c_str() );
                        Cmd.m_pPropObject  = C.m_Base.first;
                        Cmd.m_pClassObject = C.m_Base.second;

                        m_UndoSystem.m_lCmds.push_back( std::move(Cmd) );
                    }
                }();
            }
        }

        ImGui::PopItemWidth();
        ImGui::PopID();
    }

    //
    // Pop any scope
    //
    while( iDepth >= 0 ) 
        PopTree( iStart, iEnd );
}

//-------------------------------------------------------------------------------------------------

void xproperty::inspector::Show( void ) noexcept
{
    // Anything to render?
    if( m_lEntities.size() == 0 ) 
        return;

    //
    // Refresh all the properties
    //
    RefreshAllProperties();

    //
    // If we have multiple Entities refactor components
    //

    //
    // Render each of the components
    //
    for ( auto& E : m_lEntities )
    {
        int GlobalIndex = 0;
        for ( auto& C : E->m_lComponents )
        {
            ImGui::Columns( 2 );
            Render( *C, GlobalIndex );
            ImGui::Columns( 1 );
        }
    }
}

//-------------------------------------------------------------------------------------------------

void xproperty::inspector::DrawBackground( int Depth, int GlobalIndex ) const noexcept
{
    if( m_Settings.m_bRenderBackgroundDepth == false ) 
        Depth = 0;

    ImVec2 pos = ImGui::GetCursorScreenPos();
    auto Color = s_ColorCategories[Depth];

    float h, s, v;
    ImVec4 C = Color;
    ImGui::ColorConvertRGBtoHSV( C.x, C.y, C.z, h, s, v );

    if(GlobalIndex&1)
    {
        Color.SetHSV( h, s*m_Settings.m_ColorSScalar, v*m_Settings.m_ColorVScalar1 );
    }
    else
    {
        Color.SetHSV( h, s*m_Settings.m_ColorSScalar, v*m_Settings.m_ColorVScalar2 );
    }


    ImGui::GetWindowDrawList()->AddRectFilled(
        pos
        , ImVec2( pos.x + ImGui::GetContentRegionAvail().x
                , pos.y + ImGui::GetFrameHeight() )
        , Color );
}

//-----------------------------------------------------------------------------------

void xproperty::inspector::HelpMarker( const char* desc ) const noexcept
{
    if ( ImGui::IsItemHovered() )
    {
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, m_Settings.m_HelpWindowPadding );
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos( ImGui::GetFontSize() * m_Settings.m_HelpWindowSizeInChars );
        ImGui::TextUnformatted( desc );
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
        ImGui::PopStyleVar();
    }
}

//-----------------------------------------------------------------------------------

void xproperty::inspector::Help( const entry& Entry ) const noexcept
{
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, m_Settings.m_HelpWindowPadding );
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos( ImGui::GetFontSize() * m_Settings.m_HelpWindowSizeInChars );

    ImGui::TextDisabled( "FullName: ");
    ImGui::SameLine();
    ImGui::Text( "%s", Entry.m_Property.m_Path.c_str() );

    ImGui::TextDisabled( "Name:     " );
    ImGui::SameLine();
    ImGui::Text( "%s", Entry.m_pName );
       
    ImGui::TextDisabled( "Hash:     " );
    ImGui::SameLine();
    ImGui::Text( "0x%x", Entry.m_GUID );

    if( Entry.m_pHelp )
    {
        ImGui::Separator();
        ImGui::TextUnformatted( Entry.m_pHelp );
    }
    else
    {
        ImGui::TextDisabled( "Help:     " );
        ImGui::SameLine();
        ImGui::Text( "none provided" );
    }

    ImGui::EndTooltip();
    ImGui::PopStyleVar();
}
