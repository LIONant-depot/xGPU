#ifndef XNODE_OS_HOST_INTERFACE_H
#define XNODE_OS_HOST_INTERFACE_H
#pragma once

// Forward-declared, not included: xnode_os_plugin_api.h only ever needs ixnode_os_host* (a pointer),
// and this file only ever needs xnode_os_node_type_desc* likewise - neither header actually needs the
// other's full definition, so there is no circular include here.
struct xnode_os_node_type_desc;

//------------------------------------------------------------------------------------------------
// The interface the host hands a plugin exactly once, via NodeOS_OnLoad(ixnode_os_host*), immediately
// after LoadLibrary. Everything the plugin offers - node types today, other systems later - is pushed
// through this interface during that one call, rather than the host pulling it out afterward
// (the old NodeOS_GetNodeTypeCount/NodeOS_GetNodeType pair this replaces).
//
// Pure virtual for the same reason as ixnode_os_reflected_object (see that header): vtable dispatch
// is stable across CRT/runtime-library choices, so plugins remain free to build with any compiler
// settings they like. Same rules apply here: every method is effectively noexcept (no exception ever
// crosses a DLL boundary safely), and nothing is destroyed with `delete` through this pointer.
//------------------------------------------------------------------------------------------------
class ixnode_os_host
{
public:
    // Must equal XNODE_OS_ABI_VERSION (xnode_os_plugin_api.h) - lets a plugin bail out of its own
    // NodeOS_OnLoad early if it was built against an incompatible header, as a second line of defense
    // alongside the host's own per-descriptor m_AbiVersion check.
    virtual int GetAbiVersion() const noexcept = 0;

    // pDesc is read and copied by the host during this call - the plugin doesn't need to keep the
    // pointed-to object alive beyond it, though every function pointer inside it (Execute,
    // FreeOutputs, the property functions) must remain valid for the whole lifetime of the module,
    // same as the ABI this replaces already required.
    virtual void RegisterNodeType(const xnode_os_node_type_desc* pDesc) noexcept = 0;

    // Routes to the same logger the host uses for its own diagnostics, so a plugin's startup
    // failures show up in the same place as everything else instead of being silently swallowed.
    virtual void Log(const char* pMessage) noexcept = 0;

    // Dear ImGui's own documented pattern for a DLL that wants to call ImGui:: functions and have
    // them land in the HOST's actual window - see imgui.h's own "DLL users: heaps and globals are not
    // shared across DLL boundaries! You will need to call SetCurrentContext() + SetAllocatorFunctions()"
    // comment. Returned as void*/void** here (not ImGuiContext*/ImGuiMemAllocFunc*) so this header
    // stays ImGui-agnostic like the rest of this ABI; a plugin that wants to draw its own UI includes
    // imgui.h itself and reinterpret_casts these back. This is NOT the same risk class as a real
    // xproperty::type::object crossing the boundary (see xnode_os_plugin_api.h's comment on
    // m_pDrawProperties) - ImGuiContext holds no per-binary `inline` singleton identity to compare,
    // it is just heap memory the plugin's own compiled ImGui code is told to dereference through a
    // shared pointer, exactly as Dear ImGui itself documents for this scenario.
    virtual void* GetImGuiContext() const noexcept = 0;
    virtual void  GetImGuiAllocatorFunctions(void** ppAllocFunc, void** ppFreeFunc, void** ppUserData) const noexcept = 0;

protected:
    ~ixnode_os_host() noexcept = default; // never through this base pointer
};

#endif
