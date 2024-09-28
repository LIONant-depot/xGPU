
#include "xPropertyImGuiInspector.h"

/*
//-----------------------------------------------------------------------------------

using example_entry = std::pair<const property::table&, void*>;

//-----------------------------------------------------------------------------------

template< typename T >
std::pair<const char*, example_entry> CreateInstance()
{
    static T Instance;
    Instance.DefaultValues();
    return { property::getTable(Instance).m_pName, { property::getTable(Instance), &Instance } };
}

//-----------------------------------------------------------------------------------

template< typename... T_ARGS >
struct examples
{
    examples(T_ARGS&&... args)
        : m_Tables{ args.second... }
        , m_Names{ args.first ... }
    {}

    std::array<example_entry, sizeof...(T_ARGS) >   m_Tables;
    std::array<const char*, sizeof...(T_ARGS) >     m_Names;
};

//-----------------------------------------------------------------------------------

std::array<property::inspector, 2>   Inspector{ "Examples", "Settings" };
examples                            Examples
{
      CreateInstance<example0>()
    , CreateInstance<example1>()
    , CreateInstance<example2>()
    , CreateInstance<example3>()
    , CreateInstance<example4>()
    , CreateInstance<example5>()
    , CreateInstance<example6>()
    , CreateInstance<example7>()
    , CreateInstance<example8>()
    , CreateInstance<example9>()
    , CreateInstance<example10>()
    , CreateInstance<example0_custom_lists>()
    , CreateInstance<example1_custom_lists>()
    , CreateInstance<example2_custom_lists>()
    , CreateInstance<example3_custom_lists>()
    , CreateInstance<example4_custom_lists>()
};
*/

std::array<xproperty::inspector, 1>   Inspector{ "Settings" }; //"Examples", 

//-----------------------------------------------------------------------------------

void DrawPropertyWindow()
{
    static int iSelection = -1;

    /*
    // Show properties
    Inspector[0].Show([&]
        {
            if (ImGui::Combo("Select Example", &iSelection, Examples.m_Names.data(), static_cast<int>(Examples.m_Names.size())))
            {
                Inspector[0].clear();
                Inspector[0].AppendEntity();
                Inspector[0].AppendEntityComponent(Examples.m_Tables[iSelection].first, Examples.m_Tables[iSelection].second);
            }

            if (ImGui::Button("  Undo  ")) Inspector[0].Undo();
            ImGui::SameLine(80);
            if (ImGui::Button("  Redo  ")) Inspector[0].Redo();
        });
*/
    // Settings
    {
        static bool Init = false;
        if (Init == false)
        {
            Init = true;
            Inspector[0].clear();
            Inspector[0].AppendEntity();
            Inspector[0].AppendEntityComponent( *xproperty::getObject(Inspector[0]), &Inspector[0] );
        }
        Inspector[0].Show([]
            {
                if (ImGui::Button("  Undo  ")) Inspector[0].Undo();
                ImGui::SameLine(80);
                if (ImGui::Button("  Redo  ")) Inspector[0].Redo();
            });
    }
}

