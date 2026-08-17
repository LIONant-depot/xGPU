#ifndef XNODE_OS_PROPERTY_ADAPTER_H
#define XNODE_OS_PROPERTY_ADAPTER_H
#pragma once

// Plugin-side ONLY - never included by the host. Wraps any ordinary XPROPERTY_DEF'd struct into an
// ixnode_os_reflected_object implementation automatically, so a plugin author writes normal xproperty
// code (exactly like every other descriptor in this engine - see e.g.
// example.lionprj/Cache/Plugins/xgeom_static.plugin/source/xgeom_static_descriptor.h for the
// established XPROPERTY_DEF/XPROPERTY_REG/obj_member style) and gets ABI-safe exposure for free.
//
// Real xproperty types (xproperty::any, xproperty::type::object) are used freely in this file - safe,
// because it is compiled entirely inside the plugin's own binary and none of that ever crosses the
// ABI boundary; only the primitive-typed ixnode_os_reflected_object calls do.
//
// The core implementation (reflected_object_impl, below) is deliberately NOT templated: xproperty's
// own member-recursion is already type-erased at the level this file operates on (a compound member's
// props::m_pCast returns a plain (void* instance, const type::object* schema) pair, no concrete C++
// type in sight), so nesting into a struct-within-a-struct just constructs another
// reflected_object_impl from that pair directly - only the single entry point
// (xnode_os_MakeReflectedObject<T>, at the bottom) needs a template, to bootstrap from a plugin's
// concrete property struct into the type-erased form.
#include "xnode_os_reflected_object.h"
#include "dependencies/xproperty/source/xcore/my_properties.h"
// my_properties.h's own UI-style wiring (my_property_ui.h) declares draw<T,S>::Render but never
// defines the general case out-of-line - only specific (T,S) pairs the host's own editors happen to
// use get a real body, via xPropertyImGuiInspector.cpp, which a plugin has no reason to compile (it
// never draws anything itself; the host does, through ixnode_os_reflected_object). Any (T,S)
// combination this plugin's own descriptor triggers that the host never happened to need would
// otherwise be left an unresolved symbol at link time. my_property_ui_null.h supplies exactly the
// missing general no-op definition - inline, so it costs nothing and stays private to this DLL.
#include "dependencies/xproperty/source/examples/imgui/my_property_ui_null.h"
#include <vector>
#include <string>
#include <variant>
// For GetString/SetString's std::wstring<->UTF-8 conversion (WideCharToMultiByte/MultiByteToWideChar)
// - a plain, non-lean include, NOMINMAX-guarded: whichever file hits <windows.h>'s include guard FIRST
// in a raw-#include unity build (this header is always included before imgui.cpp/xPropertyImGuiInspector
// .cpp in every plugin that uses them) decides what the rest of the translation unit gets, so this needs
// to ask for the full API surface itself rather than relying on a later include to have done so.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace xnode_os_property_adapter_detail
{
    // Which of the 5 fixed atomics (or COMPOUND/LIST) a raw xproperty member maps to - member kinds
    // this v1 doesn't support (list-of-compound, scope groups, callable members) classify as SKIP and
    // are simply left out of the reflected view, never surfaced as an error.
    enum class raw_kind { SKIP, FLOAT, INT, BOOL, STRING, ENUM, COMPOUND, LIST };

    inline raw_kind ClassifyAtomic(const xproperty::type::atomic& AT) noexcept
    {
        if (AT.m_IsEnum)                                             return raw_kind::ENUM;
        if (AT.m_GUID == xproperty::type::atomic_v<float>.m_GUID)       return raw_kind::FLOAT;
        if (AT.m_GUID == xproperty::type::atomic_v<int>.m_GUID)         return raw_kind::INT;
        if (AT.m_GUID == xproperty::type::atomic_v<bool>.m_GUID)        return raw_kind::BOOL;
        if (AT.m_GUID == xproperty::type::atomic_v<std::string>.m_GUID)  return raw_kind::STRING;
        // std::wstring shares the STRING abi kind with std::string - GetString/SetString detect which
        // of the two a given member actually is (via IsWideStringAt) and convert at the UTF-8 boundary,
        // so the host-side drawing/serialization code never needs to know the difference.
        if (AT.m_GUID == xproperty::type::atomic_v<std::wstring>.m_GUID) return raw_kind::STRING;
        return raw_kind::SKIP; // an atomic type outside the fixed 5 - excluded, not an error
    }

    inline raw_kind ClassifyMember(const xproperty::type::members& M) noexcept
    {
        if (auto* pVar = std::get_if<xproperty::type::members::var>(&M.m_Variant))
            return ClassifyAtomic(pVar->m_AtomicType);
        if (std::get_if<xproperty::type::members::props>(&M.m_Variant))
            return raw_kind::COMPOUND;
        if (auto* pList = std::get_if<xproperty::type::members::list_var>(&M.m_Variant))
            return ClassifyAtomic(pList->m_AtomicType) == raw_kind::SKIP ? raw_kind::SKIP : raw_kind::LIST;
        return raw_kind::SKIP; // list_props, scope, function - not supported in v1
    }

    static xnode_os_member_kind ToAbiKind(raw_kind K) noexcept
    {
        switch (K)
        {
            case raw_kind::FLOAT:    return xnode_os_member_kind::FLOAT;
            case raw_kind::INT:      return xnode_os_member_kind::INT;
            case raw_kind::BOOL:     return xnode_os_member_kind::BOOL;
            case raw_kind::STRING:   return xnode_os_member_kind::STRING;
            case raw_kind::ENUM:     return xnode_os_member_kind::ENUM;
            case raw_kind::COMPOUND: return xnode_os_member_kind::COMPOUND;
            default:                 return xnode_os_member_kind::LIST;
        }
    }

    //--------------------------------------------------------------------------------------------
    // Type-erased: holds a plain instance pointer and the xproperty schema for it, nothing else.
    // Never touched directly by plugin authors - xnode_os_MakeReflectedObject<T> (bottom of this
    // file) is the only entry point.
    //--------------------------------------------------------------------------------------------
    class reflected_object_impl final : public ixnode_os_reflected_object
    {
    public:
        reflected_object_impl(void* pInstance, const xproperty::type::object* pObject) noexcept
            : m_pInstance(pInstance), m_pObject(pObject)
        {
            if (!m_pObject) return;
            for (std::uint32_t i = 0; i < m_pObject->m_Members.size(); ++i)
            {
                const auto K = ClassifyMember(m_pObject->m_Members[i]);
                if (K != raw_kind::SKIP) m_Index.push_back(i);
            }
            m_Children.assign(m_Index.size(), nullptr);
        }

        void Destroy() noexcept override
        {
            for (auto* pChild : m_Children) if (pChild) pChild->Destroy();
            delete this;
        }

        int GetMemberCount() const noexcept override { return (int)m_Index.size(); }

        const char* GetMemberName(int Index) const noexcept override
        {
            return InRange(Index) ? Raw(Index).m_pName : "";
        }

        xnode_os_member_kind GetMemberKind(int Index) const noexcept override
        {
            return InRange(Index) ? ToAbiKind(ClassifyMember(Raw(Index))) : xnode_os_member_kind::FLOAT;
        }

        float GetFloat(int Index) const noexcept override { return ReadAs<float>(Index, 0.0f); }
        void  SetFloat(int Index, float Value) noexcept override { WriteAs(Index, Value); }

        int GetInt(int Index) const noexcept override
        {
            if (KindAt(Index) == raw_kind::ENUM) return ReadEnumAsInt(Index);
            return ReadAs<int>(Index, 0);
        }
        void SetInt(int Index, int Value) noexcept override
        {
            if (KindAt(Index) == raw_kind::ENUM) { WriteEnumFromInt(Index, Value); return; }
            WriteAs(Index, Value);
        }

        bool GetBool(int Index) const noexcept override { return ReadAs<bool>(Index, false); }
        void SetBool(int Index, bool Value) noexcept override { WriteAs(Index, Value); }

        const char* GetString(int Index) const noexcept override
        {
            m_Scratch.clear();
            if (InRange(Index))
            {
                xproperty::any Out; xproperty::settings::context Ctx{};
                if (Raw(Index).TryRead(m_pInstance, Out, Ctx))
                {
                    if (auto R = Out.tryGet<std::string>(); R) m_Scratch = *R.value();
                    else if (auto RW = Out.tryGet<std::wstring>(); RW) NarrowInto(*RW.value(), m_Scratch);
                }
            }
            return m_Scratch.c_str();
        }
        void SetString(int Index, const char* pValue) noexcept override
        {
            if (!InRange(Index) || !pValue) return;
            xproperty::settings::context Ctx{};
            if (IsWideStringAt(Index))
            {
                xproperty::any In{ Widen(pValue) };
                (void)Raw(Index).TryWrite(m_pInstance, In, Ctx);
            }
            else
            {
                xproperty::any In{ std::string(pValue) };
                (void)Raw(Index).TryWrite(m_pInstance, In, Ctx);
            }
        }

        int GetEnumValueCount(int Index) const noexcept override
        {
            if (KindAt(Index) != raw_kind::ENUM) return 0;
            return (int)EnumAtomicOf(Index).enumItems().size();
        }
        xnode_os_enum_value GetEnumValueAt(int Index, int EnumEntryIndex) const noexcept override
        {
            if (KindAt(Index) != raw_kind::ENUM) return { "", 0 };
            auto Span = EnumAtomicOf(Index).enumItems();
            if (EnumEntryIndex < 0 || (std::size_t)EnumEntryIndex >= Span.size()) return { "", 0 };
            return { Span[EnumEntryIndex].m_pName, (int)Span[EnumEntryIndex].m_Value };
        }

        ixnode_os_reflected_object* GetCompoundMember(int Index) noexcept override
        {
            if (KindAt(Index) != raw_kind::COMPOUND) return nullptr;
            if (m_Children[Index]) return m_Children[Index];
            auto* pProps = std::get_if<xproperty::type::members::props>(&Raw(Index).m_Variant);
            xproperty::settings::context Ctx{};
            auto [pNestedInstance, pNestedObject] = pProps->m_pCast(m_pInstance, Ctx);
            if (!pNestedInstance || !pNestedObject) return nullptr;
            return m_Children[Index] = new reflected_object_impl(pNestedInstance, pNestedObject);
        }

        int GetListCount(int Index) const noexcept override
        {
            if (KindAt(Index) != raw_kind::LIST) return 0;
            auto* pList = std::get_if<xproperty::type::members::list_var>(&Raw(Index).m_Variant);
            xproperty::settings::context Ctx{};
            auto R = pList->TryGetSize(m_pInstance, 0, Ctx);
            return R ? (int)R.value() : 0;
        }
        ixnode_os_reflected_object* GetListElement(int, int) noexcept override
        {
            // v1: GetListCount alone lets the panel show "(N entries)" without crashing; per-element
            // get/set for atomic lists is a natural follow-up once a real plugin needs to edit one.
            return nullptr;
        }

    private:
        bool InRange(int Index) const noexcept { return Index >= 0 && (std::size_t)Index < m_Index.size(); }
        const xproperty::type::members& Raw(int Index) const noexcept { return m_pObject->m_Members[m_Index[Index]]; }
        raw_kind KindAt(int Index) const noexcept { return InRange(Index) ? ClassifyMember(Raw(Index)) : raw_kind::SKIP; }

        // A STRING-kind member is either std::string or std::wstring (ClassifyAtomic folds both into
        // the one abi kind) - only var members can be wstring in practice, but this stays defensive.
        bool IsWideStringAt(int Index) const noexcept
        {
            if (!InRange(Index)) return false;
            if (auto* pVar = std::get_if<xproperty::type::members::var>(&Raw(Index).m_Variant))
                return pVar->m_AtomicType.m_GUID == xproperty::type::atomic_v<std::wstring>.m_GUID;
            return false;
        }

        static void NarrowInto(const std::wstring& W, std::string& Out) noexcept
        {
            Out.clear();
            if (W.empty()) return;
            const int Needed = WideCharToMultiByte(CP_UTF8, 0, W.c_str(), (int)W.size(), nullptr, 0, nullptr, nullptr);
            if (Needed <= 0) return;
            Out.resize((std::size_t)Needed);
            WideCharToMultiByte(CP_UTF8, 0, W.c_str(), (int)W.size(), Out.data(), Needed, nullptr, nullptr);
        }
        static std::wstring Widen(const char* pUtf8) noexcept
        {
            const int Needed = MultiByteToWideChar(CP_UTF8, 0, pUtf8, -1, nullptr, 0);
            if (Needed <= 1) return {};
            std::wstring W((std::size_t)Needed - 1, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, pUtf8, -1, W.data(), Needed);
            return W;
        }

        const xproperty::type::atomic& EnumAtomicOf(int Index) const noexcept
        {
            if (auto* pVar = std::get_if<xproperty::type::members::var>(&Raw(Index).m_Variant)) return pVar->m_AtomicType;
            auto* pList = std::get_if<xproperty::type::members::list_var>(&Raw(Index).m_Variant);
            return pList->m_AtomicType;
        }

        int ReadEnumAsInt(int Index) const noexcept
        {
            xproperty::any Out; xproperty::settings::context Ctx{};
            if (!InRange(Index) || !Raw(Index).TryRead(m_pInstance, Out, Ctx)) return 0;
            // An enum's underlying storage type varies by declaration; probe the common widths rather
            // than assuming one - TryRead already gave us whatever atomic width this enum was declared
            // with, we just need it as a plain int for the ABI.
            if (auto R = Out.tryGet<int>();               R) return *R.value();
            if (auto R = Out.tryGet<unsigned int>();       R) return (int)*R.value();
            if (auto R = Out.tryGet<std::int64_t>();       R) return (int)*R.value();
            if (auto R = Out.tryGet<std::uint64_t>();      R) return (int)*R.value();
            return 0;
        }
        void WriteEnumFromInt(int Index, int Value) noexcept
        {
            if (!InRange(Index)) return;
            const auto Found = EnumAtomicOf(Index).TryFindEnumByValue((std::uint64_t)Value);
            if (!Found) return; // not one of the enum's registered values - reject rather than write garbage
            // Write by name, the one enum-write path every atomic width supports uniformly (see
            // members::var::TryWrite's registered-enum handling).
            xproperty::any In{ std::string(Found.value()->m_pName) }; xproperty::settings::context Ctx{};
            (void)Raw(Index).TryWrite(m_pInstance, In, Ctx);
        }

        template <typename T>
        T ReadAs(int Index, T Default) const noexcept
        {
            if (!InRange(Index)) return Default;
            xproperty::any Out; xproperty::settings::context Ctx{};
            if (Raw(Index).TryRead(m_pInstance, Out, Ctx))
                if (auto R = Out.tryGet<T>(); R) return *R.value();
            return Default;
        }
        template <typename T>
        void WriteAs(int Index, T Value) noexcept
        {
            if (!InRange(Index)) return;
            xproperty::any In{ Value }; xproperty::settings::context Ctx{};
            (void)Raw(Index).TryWrite(m_pInstance, In, Ctx);
        }

        void*                          m_pInstance;
        const xproperty::type::object* m_pObject;
        std::vector<std::uint32_t>     m_Index;    // reflected index -> raw m_Members index (filtered)
        std::vector<ixnode_os_reflected_object*> m_Children; // lazily-created compound children, parallel to m_Index
        mutable std::string            m_Scratch;  // GetString's return buffer - valid until the next call
    };
}

//------------------------------------------------------------------------------------------------
// The one thing a plugin actually calls: given a pointer to its own (already XPROPERTY_DEF'd, already
// XPROPERTY_REG'd) property struct, returns a fresh ABI-safe reflected view over it. Cheap to create
// and destroy - callers are expected to request one, use it, and Destroy() it promptly (e.g. once per
// frame while a property panel is open), not to hold onto it for the property block's whole lifetime.
//------------------------------------------------------------------------------------------------
template <typename T>
ixnode_os_reflected_object* xnode_os_MakeReflectedObject(T* pInstance) noexcept
{
    return new xnode_os_property_adapter_detail::reflected_object_impl(pInstance, xproperty::getObjectByType<T>());
}

#endif
