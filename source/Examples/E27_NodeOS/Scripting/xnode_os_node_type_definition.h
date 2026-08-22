#pragma once
//-----------------------------------------------------------------------------------
// Node type definitions for the Node OS scripting language - see
// ../NODE_SCRIPTING_DESIGN.md for the full design this implements. A node type is a
// plain DATA FILE (Contract #1 from that doc): a fixed pin list, some render hints,
// and ONE codegen template string - no fixed node-category vocabulary at all. Every
// node (If, Loop, End, AddFloat, ...) is the exact same shape; what makes one a loop
// and another a plain statement is entirely what its own template text does with its
// own pins, via the $goto[PinId] substitution directive (see the design doc's
// "flat label/goto" model). Header-only, matching this SDK's existing style
// (xnode_os_plugin_api.h, xnode_os_shared_types.h).
//-----------------------------------------------------------------------------------

#include "dependencies/xtextfile/source/xtextfile.h"
#include "dependencies/xresource_guid/source/xresource_guid.h"
#include <string>
#include <vector>

namespace nodeos::scripting
{
    // Data and exec pins are separate universes (design doc section 3) - an exec edge
    // means "run this next," a data edge means "here is a value."
    enum class pin_kind : int { DataIn, DataOut, ExecIn, ExecOut };

    // One port on a node type. m_TypeGuid is left default (empty) for exec pins - they
    // carry no data type, only control. Reuses this engine's own type_guid (a plain
    // hashed std::uint64_t, see xresource_guid.h) rather than inventing a parallel
    // string-based identity - the same type registry every other type identity in this
    // engine already uses.
    struct pin_desc
    {
        std::string           m_Id;
        pin_kind              m_Kind = pin_kind::DataIn;
        xresource::type_guid  m_TypeGuid;
    };

    struct node_type_definition
    {
        xresource::type_guid   m_Guid;             // e.g. xresource::type_guid{"ForEachArrayFloat"} - constexpr-hashed from a readable name, same convention this engine already uses elsewhere
        std::string            m_DisplayName;
        std::string            m_Category;
        int                    m_SchemaVersion = 1;
        std::uint64_t          m_SemanticHash  = 0; // a hash is a number, not text - no "sha256:..." string
        std::string            m_OwnerExtension;

        std::vector<pin_desc>  m_Pins;

        // Render hints - UI-owned. The compiler never reads these two fields.
        std::string            m_HeaderColor;
        int                    m_Width = 200;

        // The ENTIRE codegen surface, compiler-owned: one template string, using the
        // material graph's own existing $var/$input[N]/_prop[N] substitution grammar
        // plus one addition, $goto[PinId] - emit a jump to whatever node is wired to
        // that exec-out pin. That is the only primitive the compiler needs; there is no
        // Branch/Loop/Sequence/Return category anywhere. A Loop node's own template
        // just happens to contain a couple of labels and a couple of $goto's; an
        // End/Goto node's entire template is literally "$goto[Target]". See the design
        // doc for why this replaced an earlier, more hardcoded lowering_kind enum.
        std::string            m_Template;
    };

    namespace details
    {
        inline const char* PinKindToString(pin_kind K) noexcept
        {
            switch (K)
            {
            case pin_kind::DataIn:   return "DataIn";
            case pin_kind::DataOut:  return "DataOut";
            case pin_kind::ExecIn:   return "ExecIn";
            case pin_kind::ExecOut:  return "ExecOut";
            }
            return "DataIn";
        }
        inline pin_kind PinKindFromString(const std::string& S) noexcept
        {
            if (S == "DataOut") return pin_kind::DataOut;
            if (S == "ExecIn")  return pin_kind::ExecIn;
            if (S == "ExecOut") return pin_kind::ExecOut;
            return pin_kind::DataIn;
        }
    }

    // Same xtextfile::stream Record/Field pattern E27's own SaveGraph/LoadGraph already
    // use - two records: NodeType (exactly one row) and Pins (one row per pin).
    inline bool SaveNodeTypeDefinition(const std::string& Utf8Path, const node_type_definition& Def) noexcept
    {
        const std::wstring WPath(Utf8Path.begin(), Utf8Path.end()); // ASCII-safe path, matching SaveGraph's own assumption

        xtextfile::stream Stream;
        if (auto Err = Stream.Open(false, WPath, xtextfile::file_type::TEXT); Err)
            return false;

        if (auto Err = Stream.Record("NodeType"
            , [&](std::size_t& C, xerr&) { C = 1; }
            , [&](std::size_t, xerr& Error)
            {
                std::uint64_t Guid          = Def.m_Guid.m_Value;
                std::string   DisplayName   = Def.m_DisplayName;
                std::string   Category      = Def.m_Category;
                int           SchemaVersion = Def.m_SchemaVersion;
                std::uint64_t SemanticHash  = Def.m_SemanticHash;
                std::string   OwnerExtension = Def.m_OwnerExtension;
                std::string   HeaderColor   = Def.m_HeaderColor;
                int           Width         = Def.m_Width;
                std::string   Template      = Def.m_Template;

                0
                || (Error = Stream.Field("Guid",           Guid))
                || (Error = Stream.Field("DisplayName",    DisplayName))
                || (Error = Stream.Field("Category",       Category))
                || (Error = Stream.Field("SchemaVersion",  SchemaVersion))
                || (Error = Stream.Field("SemanticHash",   SemanticHash))
                || (Error = Stream.Field("OwnerExtension", OwnerExtension))
                || (Error = Stream.Field("HeaderColor",    HeaderColor))
                || (Error = Stream.Field("Width",          Width))
                || (Error = Stream.Field("Template",       Template));
            }
        ); Err) return false;

        if (auto Err = Stream.Record("Pins"
            , [&](std::size_t& C, xerr&) { C = Def.m_Pins.size(); }
            , [&](std::size_t i, xerr& Error)
            {
                auto&         P    = Def.m_Pins[i];
                std::string   Id   = P.m_Id;
                std::string   Kind = details::PinKindToString(P.m_Kind);
                std::uint64_t TypeGuid = P.m_TypeGuid.m_Value;

                0
                || (Error = Stream.Field("Id",       Id))
                || (Error = Stream.Field("Kind",     Kind))
                || (Error = Stream.Field("TypeGuid", TypeGuid));
            }
        ); Err) return false;

        return true;
    }

    inline bool LoadNodeTypeDefinition(const std::string& Utf8Path, node_type_definition& OutDef) noexcept
    {
        const std::wstring WPath(Utf8Path.begin(), Utf8Path.end());

        xtextfile::stream Stream;
        if (auto Err = Stream.Open(true, WPath, xtextfile::file_type::TEXT); Err)
            return false;

        if (auto Err = Stream.Record("NodeType"
            , [&](std::size_t&, xerr&) {}
            , [&](std::size_t, xerr& Error)
            {
                std::uint64_t Guid = 0, SemanticHash = 0;
                std::string   DisplayName, Category, OwnerExtension, HeaderColor, Template;
                int           SchemaVersion = 1, Width = 200;

                if (0
                 || (Error = Stream.Field("Guid",           Guid))
                 || (Error = Stream.Field("DisplayName",    DisplayName))
                 || (Error = Stream.Field("Category",       Category))
                 || (Error = Stream.Field("SchemaVersion",  SchemaVersion))
                 || (Error = Stream.Field("SemanticHash",   SemanticHash))
                 || (Error = Stream.Field("OwnerExtension", OwnerExtension))
                 || (Error = Stream.Field("HeaderColor",    HeaderColor))
                 || (Error = Stream.Field("Width",          Width))
                 || (Error = Stream.Field("Template",       Template)))
                    return;

                OutDef.m_Guid           = xresource::type_guid{ Guid };
                OutDef.m_DisplayName    = std::move(DisplayName);
                OutDef.m_Category       = std::move(Category);
                OutDef.m_SchemaVersion  = SchemaVersion;
                OutDef.m_SemanticHash   = SemanticHash;
                OutDef.m_OwnerExtension = std::move(OwnerExtension);
                OutDef.m_HeaderColor    = std::move(HeaderColor);
                OutDef.m_Width          = Width;
                OutDef.m_Template       = std::move(Template);
            }
        ); Err) return false;

        // A node type with zero pins (End/End-Else - pure position markers, see
        // NODE_SCRIPTING_DESIGN.md section 4.2) is legitimate. xtextfile never writes a
        // record's header line at all when its row count is zero (confirmed empirically -
        // the header is only flushed lazily, on the first row write), so there is nothing
        // to read back here in that case. Treat a failure to find "Pins" as "zero pins",
        // not a load failure - the NodeType record above already failed the whole load on
        // any genuinely corrupt/dangling data.
        Stream.Record("Pins"
            , [&](std::size_t& C, xerr&) { OutDef.m_Pins.reserve(C); }
            , [&](std::size_t, xerr& Error)
            {
                std::string   Id, Kind;
                std::uint64_t TypeGuid = 0;
                if (0
                 || (Error = Stream.Field("Id",       Id))
                 || (Error = Stream.Field("Kind",     Kind))
                 || (Error = Stream.Field("TypeGuid", TypeGuid)))
                    return;

                OutDef.m_Pins.push_back(pin_desc{ std::move(Id), details::PinKindFromString(Kind), xresource::type_guid{ TypeGuid } });
            }
        );

        return true;
    }
}
