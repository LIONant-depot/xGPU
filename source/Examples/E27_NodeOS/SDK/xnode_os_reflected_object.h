#ifndef XNODE_OS_REFLECTED_OBJECT_H
#define XNODE_OS_REFLECTED_OBJECT_H
#pragma once

// The ABI-safe, primitive-only view of one property instance's members - crosses the plugin DLL
// boundary as a pure-virtual interface. Vtable dispatch is stable across CRT/runtime-library choices
// (it only depends on the C++ compiler's calling convention, fixed by using the same toolchain); the
// real xproperty::any / xproperty::type::object underneath is NOT CRT-agnostic, so it never leaves
// the plugin's own binary - see xnode_os_property_adapter.h, which builds an implementation of this
// interface automatically from any ordinary XPROPERTY_DEF'd struct.
//
// Exactly 5 atomic (leaf) kinds are fixed here - the ONLY vocabulary the host's property panel needs
// to know how to draw. Everything else (a struct, a list) is a COMPOUND or LIST composed of these,
// and recurses generically through GetCompoundMember/GetListElement - a plugin never registers a new
// drawable type, it just composes the fixed five, and the panel walks arbitrarily deep automatically.
enum class xnode_os_member_kind : int
{
    FLOAT,
    INT,
    BOOL,
    STRING,
    ENUM,
    COMPOUND,
    LIST,
};

//------------------------------------------------------------------------------------------------
// One named value of an ENUM member, for populating a combo box.
//------------------------------------------------------------------------------------------------
struct xnode_os_enum_value
{
    const char* m_pName;
    int         m_Value;
};

//------------------------------------------------------------------------------------------------
// No C++ exceptions cross this interface (every method is effectively noexcept - MSVC's exception
// unwind state is not guaranteed compatible across CRT/EH-model differences), and nothing here is
// ever destroyed with `delete` through this pointer - only Destroy(), so the object is freed by
// whichever binary's heap actually allocated it.
//------------------------------------------------------------------------------------------------
class ixnode_os_reflected_object
{
public:
    virtual void Destroy() noexcept = 0;

    virtual int                  GetMemberCount() const noexcept = 0;
    virtual const char*          GetMemberName(int Index) const noexcept = 0;
    virtual xnode_os_member_kind GetMemberKind(int Index) const noexcept = 0;

    // Atomic get/set - only the pair matching GetMemberKind(Index) is meaningful for a given index.
    virtual float GetFloat(int Index) const noexcept = 0;
    virtual void  SetFloat(int Index, float Value) noexcept = 0;
    virtual int   GetInt(int Index) const noexcept = 0;
    virtual void  SetInt(int Index, int Value) noexcept = 0;
    virtual bool  GetBool(int Index) const noexcept = 0;
    virtual void  SetBool(int Index, bool Value) noexcept = 0;
    // Valid only until the next call into this object (the adapter returns a pointer into its own
    // reused scratch buffer, since the real value's storage is transient) - copy it out immediately
    // if the caller needs it to outlive that.
    virtual const char* GetString(int Index) const noexcept = 0;
    virtual void        SetString(int Index, const char* pValue) noexcept = 0;

    // ENUM is an int value plus a name table for display/combo population.
    virtual int                 GetEnumValueCount(int Index) const noexcept = 0;
    virtual xnode_os_enum_value GetEnumValueAt(int Index, int EnumEntryIndex) const noexcept = 0;

    // COMPOUND/LIST recursion. Returned pointers are owned by THIS object and live at least as long
    // as it does - never call Destroy() on one of these directly, only on the root object the plugin
    // handed you.
    virtual ixnode_os_reflected_object* GetCompoundMember(int Index) noexcept = 0;
    virtual int                         GetListCount(int Index) const noexcept = 0;
    virtual ixnode_os_reflected_object* GetListElement(int Index, int ElementIndex) noexcept = 0;

protected:
    ~ixnode_os_reflected_object() noexcept = default; // never through this base pointer - use Destroy()
};

#endif
