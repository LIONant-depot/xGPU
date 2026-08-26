#pragma once
// Property-row serialization cluster, extracted from the monolithic E27_NodeOS_Editor.cpp. Moved
// early (header #2, right after NodeOS_Types.h) specifically to eliminate the 8 forward
// declarations DrawGraphCanvas used to need for this cluster's functions (SerializePropertiesToString/
// ReadBoolPropertyFromSnapshot/HasSerializableProperties/SerializeReflectedMembers/
// PushResolvedTypeDebugProperty/PushPinConnectedFlags/ReadEnumAsInt/WriteEnumFromInt) - now they're
// simply defined before DrawGraphCanvas is, so no forward declaration is needed at all.
#include "NodeOS_Common.h"

namespace nodeos
{
    //------------------------------------------------------------------------------------------------
    // Generic, host-owned property serialization - reads/writes a (Name, Kind, Value) row per member,
    // directly against the node's own REAL xproperty::type::object (getProperties(), inherited from
    // xproperty::base - see xnode_os_plugin_api.h's top comment for why a real object crossing the DLL
    // boundary is safe now). xtextfile only ever needs to know about the same fixed 5 atomic kinds
    // (FLOAT/INT/BOOL/STRING/ENUM) the property panel already draws with, and that vocabulary belongs
    // to the host ("the OS"), not to any individual plugin. Values round-trip as text (this engine's
    // text files are already documented as lossy for floats - see xtextfile.h's own top comment - an
    // accepted, existing tradeoff, not a new one). ENUM stores the underlying int value. Only atomic
    // ("var") members are serialized - COMPOUND/LIST members (e.g. Inspect Mesh's nested "Mesh" scope)
    // are skipped, matching this project's current scope; they're still drawn fine by the real
    // inspector, just not round-tripped through save/load or undo snapshots yet.
    //------------------------------------------------------------------------------------------------
    enum class property_kind : int { FLOAT = 0, INT = 1, BOOL = 2, STRING = 3, ENUM = 4 };

    // A single property reduced to its plain-text row shape - Name/Kind/Value, no storage-backend
    // opinion at all. This is the ONLY place that knows how to turn one xproperty member into text and
    // back; every serialization backend (the xtextfile-based graph file below, and the in-memory one
    // used for xundo snapshots) is a thin wrapper around these two functions.
    struct property_row { std::string m_Name; int m_Kind; std::string m_Value; };

    static void NarrowInto(const std::wstring& W, std::string& Out) noexcept
    {
        Out.clear();
        if (W.empty()) return;
        const int Needed = WideCharToMultiByte(CP_UTF8, 0, W.c_str(), (int)W.size(), nullptr, 0, nullptr, nullptr);
        if (Needed <= 0) return;
        Out.resize((std::size_t)Needed);
        WideCharToMultiByte(CP_UTF8, 0, W.c_str(), (int)W.size(), Out.data(), Needed, nullptr, nullptr);
    }
    static std::wstring WidenFromUtf8(const std::string& S) noexcept
    {
        if (S.empty()) return {};
        const int Needed = MultiByteToWideChar(CP_UTF8, 0, S.c_str(), (int)S.size(), nullptr, 0);
        if (Needed <= 0) return {};
        std::wstring W((std::size_t)Needed, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, S.c_str(), (int)S.size(), W.data(), Needed);
        return W;
    }

    static int ReadEnumAsInt(const xproperty::type::members& Member, void* pInstance) noexcept
    {
        xproperty::any Out; xproperty::settings::context Ctx;
        if (!Member.TryRead(pInstance, Out, Ctx) || !Out.m_pType) return 0;
        // xproperty::any stores an enum member as its EXACT declared enum type (atomic_type = T,
        // per xproperty.h's var_type<T>), never converted to a plain int/unsigned/int64/uint64 - so
        // probing tryGet<those>() always misses regardless of the real value, silently falling
        // through to 0 (which then always displays as the enum's first registered item). Read the
        // raw bytes by width instead, exactly like the official xproperty inspector's own generic
        // enum reader does (xPropertyImGuiInspector.cpp's draw_enums) - it doesn't know the concrete
        // enum type either, only its size.
        switch (Out.m_pType->m_Size)
        {
            case 1: return *reinterpret_cast<const std::uint8_t*>(&Out.m_Data);
            case 2: return *reinterpret_cast<const std::uint16_t*>(&Out.m_Data);
            case 4: return *reinterpret_cast<const std::uint32_t*>(&Out.m_Data);
            case 8: return (int)*reinterpret_cast<const std::uint64_t*>(&Out.m_Data);
        }
        return 0;
    }
    static void WriteEnumFromInt(const xproperty::type::members& Member, void* pInstance, int Value) noexcept
    {
        auto* pVar = std::get_if<xproperty::type::members::var>(&Member.m_Variant);
        if (!pVar) return;
        const auto Found = pVar->m_AtomicType.TryFindEnumByValue((std::uint64_t)Value);
        if (!Found) return; // not one of the enum's registered values - reject rather than write garbage
        xproperty::any In{ std::string(Found.value()->m_pName) }; xproperty::settings::context Ctx;
        (void)Member.TryWrite(pInstance, In, Ctx);
    }

    // A plain, by-name reflected bool read - e.g. ForEachLoop's own "ReadOnlyElement" checkbox,
    // consulted purely for DISPLAY (decorating the Element port's type label with const/& - see the
    // per-port draw loop) rather than through the general property-snapshot machinery, since this
    // isn't an edit/undo path, just a read.
    static bool ReadBoolProperty(const xnode_os_node* pNode, const char* pName, bool Default) noexcept
    {
        const xproperty::type::object* pObj = pNode->getProperties();
        if (!pObj) return Default;
        for (auto& M : pObj->m_Members)
            if (std::strcmp(M.m_pName, pName) == 0)
            {
                xproperty::any Out; xproperty::settings::context Ctx;
                if (M.TryRead(pNode, Out, Ctx) && Out.is<bool>()) return Out.get<bool>();
            }
        return Default;
    }

    // Mirrors xPropertyImGuiInspector.cpp's own flag-resolution order (dynamic flags win outright if
    // present, else static member_flags, else all-zero) - ReflectedMemberToRow (below) used to
    // ignore both entirely, silently persisting every atomic member regardless of m_bDontSave. That
    // isn't just extra bytes in the save file: a button property's write side (e.g. constant_node.
    // cpp's "Reset") would fire for REAL the moment a saved graph reloads and replays that row
    // through ApplyRowToMember - respecting m_bDontSave here is what makes DontSave an actual
    // guarantee instead of a UI-only hint.
    static xproperty::flags::type GetEffectiveFlags(const xproperty::type::members& Member, const void* pInstance) noexcept
    {
        xproperty::settings::context Ctx;
        if (auto* pDynamic = Member.getUserData<xproperty::settings::member_dynamic_flags_t>())
            return pDynamic->m_pCallback(pInstance, Ctx);
        if (auto* pStatic = Member.getUserData<xproperty::settings::member_flags_t>())
            return pStatic->m_Flags;
        return xproperty::flags::type{ .m_Value = 0 };
    }
    static bool ReflectedMemberToRow(const xproperty::type::members& Member, void* pInstance, property_row& OutRow)
    {
        auto* pVar = std::get_if<xproperty::type::members::var>(&Member.m_Variant);
        if (!pVar) return false; // COMPOUND/LIST - not serialized generically, see comment above
        // A const member (e.g. obj_member_ro) is skipped by the official serializer too
        // (property_sprop_xtextfile_serializer.h's own "Flags.m_bDontSave || isConst || ..." gate) -
        // there's no setter to round-trip through on load, so writing a row for it would just be
        // dead weight at best.
        if (Member.m_bConst) return false;
        if (GetEffectiveFlags(Member, pInstance).m_bDontSave) return false; // see GetEffectiveFlags above

        OutRow.m_Name = Member.m_pName;
        if (pVar->m_AtomicType.m_IsEnum)
        {
            OutRow.m_Kind  = (int)property_kind::ENUM;
            OutRow.m_Value = std::format("{}", ReadEnumAsInt(Member, pInstance));
            return true;
        }

        xproperty::any Out; xproperty::settings::context Ctx;
        if (!Member.TryRead(pInstance, Out, Ctx)) return false;

        if      (Out.is<float>())        { OutRow.m_Kind = (int)property_kind::FLOAT;  OutRow.m_Value = std::format("{}", Out.get<float>()); }
        else if (Out.is<int>())          { OutRow.m_Kind = (int)property_kind::INT;    OutRow.m_Value = std::format("{}", Out.get<int>()); }
        else if (Out.is<bool>())         { OutRow.m_Kind = (int)property_kind::BOOL;   OutRow.m_Value = Out.get<bool>() ? "1" : "0"; }
        else if (Out.is<std::string>())  { OutRow.m_Kind = (int)property_kind::STRING; OutRow.m_Value = Out.get<std::string>(); }
        else if (Out.is<std::wstring>()) { OutRow.m_Kind = (int)property_kind::STRING; NarrowInto(Out.get<std::wstring>(), OutRow.m_Value); }
        else return false; // an atomic type outside the fixed vocabulary - skip, not an error

        return true;
    }

    // Looks the member up BY NAME (not by the row's original index) so a property added, removed, or
    // reordered on the plugin's struct since the row was produced doesn't silently misassign a value.
    static void ApplyRowToMember(const xproperty::type::object* pObj, void* pInstance, const property_row& Row)
    {
        const xproperty::type::members* pFound = nullptr;
        for (auto& M : pObj->m_Members) if (Row.m_Name == M.m_pName) { pFound = &M; break; }
        if (!pFound) return; // property no longer exists on this type - skip, don't fail the whole caller

        xproperty::settings::context Ctx;
        switch (static_cast<property_kind>(Row.m_Kind))
        {
            case property_kind::ENUM:  WriteEnumFromInt(*pFound, pInstance, std::stoi(Row.m_Value)); return;
            case property_kind::FLOAT: { xproperty::any In{ std::stof(Row.m_Value) };  (void)pFound->TryWrite(pInstance, In, Ctx); return; }
            case property_kind::INT:   { xproperty::any In{ std::stoi(Row.m_Value) };  (void)pFound->TryWrite(pInstance, In, Ctx); return; }
            case property_kind::BOOL:  { xproperty::any In{ Row.m_Value == "1" };      (void)pFound->TryWrite(pInstance, In, Ctx); return; }
            case property_kind::STRING:
            {
                auto* pVar = std::get_if<xproperty::type::members::var>(&pFound->m_Variant);
                if (pVar && pVar->m_AtomicType.m_GUID == xproperty::type::atomic_v<std::wstring>.m_GUID)
                {
                    xproperty::any In{ WidenFromUtf8(Row.m_Value) }; (void)pFound->TryWrite(pInstance, In, Ctx);
                }
                else
                {
                    xproperty::any In{ Row.m_Value }; (void)pFound->TryWrite(pInstance, In, Ctx);
                }
                return;
            }
        }
    }

    // Only atomic ("var") members count here - used to predict, BEFORE writing the "Nodes" record's
    // own HasProperties flag, whether a node's own "xProperties" record will be non-empty. This is
    // narrower than what the real serializer (SerializeReflectedMembers, below) can actually write -
    // it walks scopes/lists/nested compound objects too - so a future Node OS plugin whose ONLY
    // reflected data lives inside an obj_scope or a list would have HasProperties come back false
    // here and its (real, serializable) properties would silently never get written. Not an active
    // bug - no plugin uses scope/list today - but worth fixing THIS function (not the real
    // serializer) the day one does, by asking sprop's own collector rather than this narrower walk.
    static std::vector<std::uint32_t> SerializableMemberIndices(const xproperty::type::object* pObj)
    {
        std::vector<std::uint32_t> Out;
        for (std::uint32_t i = 0; i < pObj->m_Members.size(); ++i)
            if (std::get_if<xproperty::type::members::var>(&pObj->m_Members[i].m_Variant)) Out.push_back(i);
        return Out;
    }

    static bool HasSerializableProperties(xnode_os_node* pNode)
    {
        return pNode && !SerializableMemberIndices(pNode->getProperties()).empty();
    }

    // Unlike HasSerializableProperties (var-only, for the in-memory undo row-list which genuinely
    // can't walk anything else), this asks the only question that matters for (a) whether to attempt
    // writing/reading a node's real "xProperties" file record and (b) whether the side panel's real
    // xproperty::inspector has anything to show.
    //
    // This must count only members the real serializer will actually SAVE - not "has any reflected
    // member at all". xproperty::sprop::serializer::Stream's write path (property_sprop_xtextfile_
    // serializer.h) builds its row list through xproperty::sprop::collector, which skips any member
    // whose flags (static member_flags, or the live result of member_dynamic_flags) have m_bDontSave
    // set. When a node's ENTIRE member list is such (e.g. print_node's sole member, "Last Printed",
    // is SHOW_READONLY+DONT_SAVE), that row list ends up with zero entries - and, verified directly in
    // xtextfile's own WriteRecord/WriteLine, a zero-row Record() call writes NOTHING at all, not even
    // an empty "[ xProperties : 0 ]" header (the header is only flushed lazily, from inside WriteLine,
    // which a zero-iteration per-row loop never calls). The original version of this function counted
    // "any member at all", which said true for print_node - so SaveGraph wrote HasProperties=1 into
    // the Nodes record while the real serializer silently wrote zero bytes for it, and LoadGraph then
    // tried to read an "xProperties" record that was never there, silently consuming the NEXT node's
    // block instead and cascading a misalignment through the rest of the file (root-caused by reading
    // the vendored serializer source directly, matching the user's own "if it has 0 entries I believe
    // it does nothing" observation from earlier in this same investigation).
    //
    // Recomputing this fresh from live node state (rather than persisting a save-time answer) is safe
    // here because every current Node OS plugin either has zero dynamically-flagged members (Print/
    // ExportMeshNode: always false; everything else: always true) or has at least one ALWAYS-saveable
    // member alongside its dynamic ones (Compare/MathExpression's Operator, alongside dynamic A/B) -
    // so the answer this function gives is identical whether asked right before a save (live wiring
    // state) or right after a fresh load (before any per-frame PushPinConnectedFlags has run). A
    // future plugin whose ENTIRE member list is dynamically-flagged would need this reasoned through
    // again.
    static bool HasAnyProperties(xnode_os_node* pNode)
    {
        if (!pNode || !pNode->getProperties()) return false;
        xproperty::settings::context Context;
        for (auto& M : pNode->getProperties()->m_Members)
        {
            xproperty::flags::type Flags{};
            if (auto* pDynamicFlags = M.getUserData<xproperty::settings::member_dynamic_flags_t>(); pDynamicFlags)
                Flags = pDynamicFlags->m_pCallback(pNode, Context);
            else if (auto* pStaticFlags = M.getUserData<xproperty::settings::member_flags_t>(); pStaticFlags)
                Flags = pStaticFlags->m_Flags;
            if (!Flags.m_bDontSave) return true;
        }
        return false;
    }

    // Pushes a live, host-computed value straight into a node's own reflected debug property, if it
    // declares one named "Resolved Type" (silently does nothing otherwise - most node types don't).
    // This is how Compare/Math Expression/ForEachLoop's own effective type (see
    // ResolveNodeWildcardType - resolved purely from wiring, something the plugin itself has no way
    // to know) becomes inspectable directly on the node, without needing graph context to re-derive
    // it. A direct TryWrite, not routed through commands::MakeSetProperties - this is the host
    // reflecting its OWN computed state into the node for display, every frame, not a user edit, so
    // it has no business being undoable or triggering a dirty/save-changed flag.
    static void PushResolvedTypeDebugProperty(const xnode_os_node* pNode, const char* pTypeName)
    {
        if (!pNode) return;
        // TryWrite needs a non-const instance pointer - the object itself is never actually const
        // here (this whole call exists specifically to mutate its "Resolved Type" field), it's only
        // DescOf's declared return type that's const; same pattern already used elsewhere in this
        // file for writing through a property obtained via a const-typed descriptor.
        auto* pMutableNode = const_cast<xnode_os_node*>(pNode);
        const xproperty::type::object* pObj = pNode->getProperties();
        for (auto& M : pObj->m_Members)
            if (std::strcmp(M.m_pName, "Resolved Type") == 0)
            {
                xproperty::any In{ std::string(pTypeName) }; xproperty::settings::context Ctx;
                (void)M.TryWrite(pMutableNode, In, Ctx);
                return;
            }
    }

    // Pushes "is this input pin currently wired" into a node's own reflected "<PinName> Connected"
    // bool property, if it declares one (silently does nothing otherwise) - always DONT_SHOW itself
    // (host-internal bookkeeping, never meant to be seen), but a real reflected member so this by-
    // name TryWrite can reach it. This is what lets a same-named literal property (Compare/Math
    // Expression's "A"/"B" - see FindMemberByName) hide itself and stop being saved the moment a
    // wire attaches, via its own member_dynamic_flags reading this flag - same host-computed-state-
    // into-a-property idea as PushResolvedTypeDebugProperty, just per-pin connectivity instead of a
    // resolved type string.
    static void PushPinConnectedFlags(const xnode_os_node* pNode, std::uint64_t NodeId, const std::vector<link_instance>& Links)
    {
        if (!pNode) return;
        auto* pMutableNode = const_cast<xnode_os_node*>(pNode);
        const auto Inputs = pNode->getInputs();
        const xproperty::type::object* pObj = pNode->getProperties();
        for (int i = 0; i < (int)Inputs.size(); ++i)
        {
            const std::string FlagName = std::string(Inputs[i].m_pName) + " Connected";
            for (auto& M : pObj->m_Members)
                if (FlagName == M.m_pName)
                {
                    bool bConnected = false;
                    for (auto& L : Links) if (L.m_TargetNode == NodeId && L.m_TargetInput == i) { bConnected = true; break; }
                    xproperty::any In{ bConnected }; xproperty::settings::context Ctx;
                    (void)M.TryWrite(pMutableNode, In, Ctx);
                    break;
                }
        }
    }

    // Thin wrapper delegating straight to the OFFICIAL xproperty::sprop::serializer::Stream - every
    // other editor in this codebase (E10/E20/E21/E23/E24/E25) already saves its own descriptors this
    // way. It walks the real getProperties() object directly (nested scopes/lists included, which
    // the row-conversion functions above never supported), and it already knows about m_bDontSave/
    // dynamic-flags/const-ness - the same three things ReflectedMemberToRow had to be patched to
    // respect by hand. Node OS's own row-based conversion (above) is kept ONLY for the in-memory
    // undo-snapshot format (SerializePropertiesToString, below) - the one job the official
    // serializer genuinely can't do, since xtextfile::stream is fopen-backed with no in-memory mode.
    static xerr SerializeReflectedMembers(xtextfile::stream& Stream, xnode_os_node* pNode)
    {
        xproperty::settings::context Context;
        return xproperty::sprop::serializer::Stream(Stream, pNode, *pNode->getProperties(), Context);
    }

    // In-memory, xtextfile-free snapshot of a whole properties block - for xundo command payloads,
    // where a plain string is exactly what's wanted (xundo commands ARE strings) and pulling in a
    // file-stream abstraction for something that never touches disk would be the wrong tool. One line
    // per member, tab-separated (property names/values here are plain identifiers/numbers/paths, never
    // tabs or newlines, so no escaping is needed - unlike the general text-file format).
    static std::string SerializePropertiesToString(xnode_os_node* pNode)
    {
        const xproperty::type::object* pObj = pNode->getProperties();
        std::string Out;
        for (auto Idx : SerializableMemberIndices(pObj))
        {
            property_row Row;
            if (!ReflectedMemberToRow(pObj->m_Members[Idx], pNode, Row)) continue;
            Out += std::format("{}\t{}\t{}\n", Row.m_Name, Row.m_Kind, Row.m_Value);
        }
        return Out;
    }

    static void ApplyPropertiesFromString(xnode_os_node* pNode, const std::string& Snapshot)
    {
        const xproperty::type::object* pObj = pNode->getProperties();
        std::size_t Pos = 0;
        while (Pos < Snapshot.size())
        {
            const std::size_t LineEnd = Snapshot.find('\n', Pos);
            const std::string Line    = Snapshot.substr(Pos, (LineEnd == std::string::npos ? Snapshot.size() : LineEnd) - Pos);
            Pos = (LineEnd == std::string::npos) ? Snapshot.size() : LineEnd + 1;
            if (Line.empty()) continue;

            const std::size_t Tab1 = Line.find('\t');
            const std::size_t Tab2 = Line.find('\t', Tab1 == std::string::npos ? std::string::npos : Tab1 + 1);
            if (Tab1 == std::string::npos || Tab2 == std::string::npos) continue; // malformed row - skip

            ApplyRowToMember(pObj, pNode, property_row
            { .m_Name  = Line.substr(0, Tab1)
            , .m_Kind  = std::stoi(Line.substr(Tab1 + 1, Tab2 - Tab1 - 1))
            , .m_Value = Line.substr(Tab2 + 1)
            });
        }
    }

    // A bool property serializes as "1"/"0" (see ReflectedMemberToRow) - a plain, generic scrape
    // over the same Name\tKind\tValue\n rows SerializePropertiesToString already produces, so this
    // needs no knowledge of any specific node type's own C++ layout (the host never casts a plugin's
    // xnode_os_node* to its own concrete type - see xnode_os_plugin_api.h's cross-DLL note).
    static bool ReadBoolPropertyFromSnapshot(const std::string& Snapshot, std::string_view Name) noexcept
    {
        std::size_t Pos = 0;
        while (Pos < Snapshot.size())
        {
            const std::size_t LineEnd = Snapshot.find('\n', Pos);
            const std::string Line = Snapshot.substr(Pos, (LineEnd == std::string::npos ? Snapshot.size() : LineEnd) - Pos);
            Pos = (LineEnd == std::string::npos) ? Snapshot.size() : LineEnd + 1;
            const std::size_t Tab1 = Line.find('\t');
            if (Tab1 != std::string::npos && std::string_view(Line).substr(0, Tab1) == Name)
            {
                const std::size_t Tab2 = Line.find('\t', Tab1 + 1);
                if (Tab2 != std::string::npos) return Line.substr(Tab2 + 1) == "1";
            }
        }
        return false;
    }
}
