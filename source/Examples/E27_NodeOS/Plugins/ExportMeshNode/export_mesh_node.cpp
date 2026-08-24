// Export Mesh node plugin - the graph's first artifact-producing node: it has zero outputs, and its
// entire purpose is a real side effect outside the graph (writing a plain-text .obj file to disk).
// Wiring a Cube's Mesh output into this node and opening the resulting .obj in any real 3D tool is the
// proof this whole system produces something real, not just an internal simulation.
//
// export_mesh_node IS the property object (via xproperty::base, inherited through xnode_os_node) -
// the host's own real xproperty::inspector draws it directly, including the native file-picker
// "..." button the m_Path member's file_dialog style asks for (see cube_node.cpp's top comment for
// why a real xproperty::type::object crossing the DLL boundary is safe). No ImGui/inspector includes
// needed here even though m_Path has an explicit style: member_ui_base now stores a (Type,Style)
// GUID pair rather than a resolved function pointer, so attaching a style only ever stores two
// integers - the host resolves the real drawer through its own registry at draw time (see
// xproperty's my_property_ui.h/xPropertyImGuiInspector.cpp).
#include "../../SDK/xnode_os_plugin_api.h"
#include "../../SDK/xnode_os_shared_types.h"
#include <cstdio>
#include <fstream>
#include <format>
#include <cassert>

namespace
{
    // Double-null-terminated (description, pattern) pairs, the same Win32 OPENFILENAME filter format
    // xgeom_static_descriptor.h's own mesh_filter_v already uses for this exact file_dialog UI style.
    static constexpr wchar_t s_ExportFilter[] = L"Wavefront OBJ\0*.obj\0All Files\0*.*\0";

    //--------------------------------------------------------------------------------------
    // Writes the incoming mesh as a plain Wavefront .obj - positions only (no normals/UVs, matching
    // xnode_os_mesh_data's own scope), one "v" line per vertex and one "f" line per triangle. OBJ face
    // indices are 1-based, unlike xnode_os_mesh_data's 0-based ones.
    //--------------------------------------------------------------------------------------
    struct export_mesh_node : xnode_os_node
    {
        // std::wstring (not std::string) because member_ui<T>::file_dialog only exists for wstring in
        // this engine's property UI - it's what actually gets the "..." browse button and the native
        // Windows file picker, instead of the plain type-a-path text field the default std::string
        // style draws (which also only commits on Enter, unlike the picker's immediate commit).
        std::wstring m_Path = L"D:/LIONant/xGPU/source/Examples/E27_NodeOS/exported_mesh.obj";
        std::string  m_LastExportResult; // set by Execute() - live debug info, see "Last Export Result" below

        XPROPERTY_VDEF
        ( "export_mesh_node", export_mesh_node
        , obj_member<"Path", &export_mesh_node::m_Path, member_ui<std::wstring>::file_dialog<s_ExportFilter>
            , member_help<"Where the incoming mesh gets written as a Wavefront .obj file, each time this node runs.">>
        , obj_member<"Last Export Result"
            , +[](const export_mesh_node& O, bool bRead, std::string& Value) { assert(bRead); Value = O.m_LastExportResult; }
            , member_flags<xproperty::flags::SHOW_READONLY, xproperty::flags::DONT_SAVE>
            , member_help<"Outcome of the most recent Execute() - success with counts, or why it failed - live debug info, never itself saved. Empty until Execute() runs at least once.">>
        )

        std::span<const xnode_os_port_desc> getInputs() const noexcept override
        {
            static const xnode_os_port_desc s_Inputs[1] = { { "Mesh", "Mesh" } };
            return s_Inputs;
        }

        std::span<const xnode_os_port_desc> getOutputs() const noexcept override
        {
            return {};
        }

        void Execute(void** Inputs, void** /*Outputs*/) noexcept override
        {
            const auto* pMesh = static_cast<const xnode_os_mesh_data*>(Inputs[0]);
            if (!pMesh)          { m_LastExportResult = "Failed: no mesh connected"; return; }
            if (m_Path.empty())  { m_LastExportResult = "Failed: Path is empty"; return; }

            std::ofstream File(m_Path.c_str()); // MSVC's wchar_t* fstream constructor overload
            if (!File.is_open()) { m_LastExportResult = "Failed: could not open the path for writing"; return; }

            File << "# Exported by Node OS - " << pMesh->m_VertexCount << " vertices, " << (pMesh->m_IndexCount / 3) << " triangles\n";
            for (unsigned int i = 0; i < pMesh->m_VertexCount; ++i)
                File << "v " << pMesh->m_pPositions[i * 3 + 0] << ' ' << pMesh->m_pPositions[i * 3 + 1] << ' ' << pMesh->m_pPositions[i * 3 + 2] << '\n';
            for (unsigned int i = 0; i + 2 < pMesh->m_IndexCount; i += 3)
                File << "f " << (pMesh->m_pIndices[i + 0] + 1) << ' ' << (pMesh->m_pIndices[i + 1] + 1) << ' ' << (pMesh->m_pIndices[i + 2] + 1) << '\n';

            m_LastExportResult = std::format("Wrote {} vertices, {} triangles", pMesh->m_VertexCount, pMesh->m_IndexCount / 3);
        }
    };
}
XPROPERTY_VREG(export_mesh_node)

namespace
{
    struct export_mesh_node_factory : xnode_os_node_factory
    {
        XPROPERTY_VDEF("export_mesh_node_factory", export_mesh_node_factory)

        std::string_view getVersion()  const noexcept override { return "1.0"; }
        std::string_view getName()     const noexcept override { return "Export Mesh"; }
        std::string_view getCategory() const noexcept override { return "Output"; }

        xnode_os_node& CreateNodeInstance() override
        {
            auto* pNode = new export_mesh_node();
            pNode->m_pFactory = this;
            return *pNode;
        }

        void DestroyNodeInstance(xnode_os_node& Node) override
        {
            delete static_cast<export_mesh_node*>(&Node);
        }
    };
}
XPROPERTY_VREG(export_mesh_node_factory)

extern "C" XNODE_OS_EXPORT xnode_os_node_factory& NodeOS_CreateFactory(ixnode_os_host& /*Host*/) noexcept
{
    return *new export_mesh_node_factory();
}

extern "C" XNODE_OS_EXPORT void NodeOS_DestroyFactory(xnode_os_node_factory& Factory) noexcept
{
    delete static_cast<export_mesh_node_factory*>(&Factory);
}
