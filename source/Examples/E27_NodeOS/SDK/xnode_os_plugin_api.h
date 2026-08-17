#ifndef XNODE_OS_PLUGIN_API_H
#define XNODE_OS_PLUGIN_API_H
#pragma once

// The Node OS plugin ABI - the whole point of this header. A node "type" (Cube, Inspect Mesh,
// eventually Render, Import, etc.) is never compiled into the host executable: it lives in its own
// DLL, built completely separately (its own Visual Studio project, its own compile, its own
// ship/update cycle), and the host discovers it at RUNTIME by loading the DLL and calling the single
// exported function at the bottom of this file. The host never recompiles to gain a new node type.
//
// Deliberately plain C at this outer layer (no C++ classes/STL crossing the boundary in the
// descriptor itself): a DLL built with a different compiler version, STL implementation, or CRT than
// the host is common in a real plugin ecosystem, and C++ types (std::string, std::vector) are not
// guaranteed ABI-stable across that boundary. Strings are const char*, lists are a raw pointer +
// count, node behavior is a bare function pointer.
//
// Registration is PUSH, not pull: the host hands the plugin an ixnode_os_host* once (NodeOS_OnLoad,
// right after LoadLibrary), and it is the plugin's job to register whatever it offers through that
// interface - node types today, other systems later. A pure-virtual interface is the one exception to
// "no C++ crossing the boundary": vtable dispatch is stable across CRT/runtime-library choices (it
// only depends on the C++ compiler's calling convention, fixed by using the same toolchain), unlike
// STL container layout, which IS CRT-sensitive. Every interface method is effectively noexcept (no
// exception ever crosses a DLL boundary safely) and every object crossing it is destroyed by an
// explicit Destroy()-style call on whichever side allocated it - never `delete` through a pointer
// whose vtable came from a different binary's heap.
//
// Composing already-loaded nodes into a graph, and even promoting a whole subgraph into a new
// reusable node, both happen live inside the host with zero DLLs involved - only the PRIMITIVE,
// native node kinds need this plugin path at all.

#include "xnode_os_reflected_object.h"
#include "xnode_os_host_interface.h"

//------------------------------------------------------------------------------------------------
// This ABI's version. Lives as the FIRST field of xnode_os_node_type_desc so the host can reject a
// mismatched descriptor before touching any other field in it - the cheap insurance against a
// stale-header plugin (compiled against an older/newer copy of this file) corrupting memory once this
// ABI inevitably changes again.
//------------------------------------------------------------------------------------------------
#define XNODE_OS_ABI_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

//------------------------------------------------------------------------------------------------
// One input or output port on a node type. m_pTypeName is an informal tag (e.g. "Mesh", "Number")
// the host uses only to label pins and warn on an obviously mismatched wire - there is no real type
// system here, matching the scope of this first proof (see xnode_os.plugin's README).
//------------------------------------------------------------------------------------------------
struct xnode_os_port_desc
{
    const char* m_pName;
    const char* m_pTypeName;
};

//------------------------------------------------------------------------------------------------
// One node type a plugin DLL exposes.
//
// m_pExecute receives one void* per input pin (whatever the upstream node's output produced - null
// if unconnected), a const void* to this NODE INSTANCE's own property block (nullptr if
// m_PropertyStructSize is 0), and must fill in one void* per output pin. Memory for an output belongs
// to whichever node produced it until the host calls m_pFreeOutputs on that same node (before
// re-Execute, or when the node/graph is torn down) - keeps ownership obvious without needing a shared
// allocator across the DLL boundary.
//
// Properties follow the identical ownership pattern: m_pCreateDefaultProperties allocates and
// default-inits one property block (called once, when a node instance is created), the host holds
// the returned pointer opaquely for that node's whole lifetime, and m_pDestroyProperties frees it
// (when the node is deleted). A node type with no properties leaves m_PropertyStructSize at 0 and
// both function pointers null - the host never allocates or calls into properties for that type.
//
// m_pGetReflectedObject, when non-null, returns an ixnode_os_reflected_object* view over a property
// block the host can read/draw generically (the 5 fixed atomic kinds - see xnode_os_reflected_object.h
// - plus recursive compound/list) without ever needing to know this plugin's concrete property struct.
// This is the ABI-safe DEFAULT path, always available, costs nothing extra to compile, and is what a
// plugin should use unless it specifically wants the engine's official xproperty::inspector look.
//
// m_pDrawProperties is an OPTIONAL upgrade a plugin may offer instead: when non-null, the host calls it
// directly (in place of the ABI-safe walk) so the PLUGIN draws its own properties, using its own
// compiled xproperty::inspector over its own xproperty::type::object. This is deliberately different
// from an earlier, abandoned design where the HOST tried to inspect the plugin's real xproperty object
// across the boundary - that's unsound no matter what (xproperty.h's atomic_v<T> singletons are
// per-binary `inline` globals, so a pointer stored by one binary's xproperty::type::object can never
// compare equal to another binary's copy of the same singleton; confirmed by an actual
// "Assertion failed: m_pType == &atomic_v<T>" / abort() crash, not a hunch). Here, by contrast, the
// plugin's OWN compiled code builds AND walks its OWN reflection data - nothing xproperty-shaped ever
// crosses the ABI. The only thing that crosses is ImGui draw calls, via Dear ImGui's own documented
// DLL-plugin pattern (see imgui.h's "DLL users: heaps and globals are not shared across DLL
// boundaries!" comment): the plugin calls ImGui::SetCurrentContext()/SetAllocatorFunctions() once in
// NodeOS_OnLoad using ixnode_os_host::GetImGuiContext()/GetImGuiAllocatorFunctions(), then its own
// compiled ImGui:: calls (inside m_pDrawProperties) render straight into the host's real window. A
// plugin that wants this must compile ImGui's own sources into itself (see cube_node.cpp for the
// pattern - it's the same raw #include of imgui.cpp/xPropertyImGuiInspector.cpp this engine already
// uses for its own executable, in source/Examples/E04_Properties/E04_Properties.cpp).
//------------------------------------------------------------------------------------------------
struct xnode_os_node_type_desc
{
    int                          m_AbiVersion;   // must be XNODE_OS_ABI_VERSION - checked before anything else in this struct
    const char*                  m_pName;
    const char*                  m_pCategory;
    int                          m_InputCount;
    const xnode_os_port_desc*    m_pInputs;
    int                          m_OutputCount;
    const xnode_os_port_desc*    m_pOutputs;
    void (*m_pExecute)(const void* pProperties, void** Inputs, void** Outputs);
    void (*m_pFreeOutputs)(void** Outputs, int OutputCount);

    // Properties - all six are null/0 for a node type with no properties.
    unsigned long long           m_PropertyStructSize;
    void* (*m_pCreateDefaultProperties)(void);
    void  (*m_pDestroyProperties)(void* pProperties);
    ixnode_os_reflected_object* (*m_pGetReflectedObject)(void* pProperties); // required for save/load too - see host's SerializeReflectedMembers
    void (*m_pDrawProperties)(void* pProperties); // optional - see comment above; null if unused
};

//------------------------------------------------------------------------------------------------
// The plugin DLL must export exactly this one function (undecorated, thanks to extern "C"), so
// GetProcAddress can find it by a fixed name regardless of what the DLL is called. It is called
// exactly once, immediately after LoadLibrary: the plugin registers every node type it offers via
// pHost->RegisterNodeType(...) during this call. Returning false tells the host the plugin failed to
// initialize (e.g. it couldn't set up something it depends on) and should be treated as not loaded.
//------------------------------------------------------------------------------------------------
#define XNODE_OS_EXPORT __declspec(dllexport)

typedef bool (*xnode_os_pfn_on_load)(ixnode_os_host* pHost);

#define XNODE_OS_ON_LOAD_NAME "NodeOS_OnLoad"

#ifdef __cplusplus
}
#endif

#endif
