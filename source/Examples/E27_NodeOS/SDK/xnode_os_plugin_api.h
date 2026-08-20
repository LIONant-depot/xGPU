#ifndef XNODE_OS_PLUGIN_API_H
#define XNODE_OS_PLUGIN_API_H
#pragma once

// The Node OS plugin ABI - the whole point of this header. A node "type" (Cube, Inspect Mesh,
// eventually Render, Import, etc.) is never compiled into the host executable: it lives in its own
// DLL, built completely separately, and the host discovers it at RUNTIME by loading the DLL and
// calling the two exported functions at the bottom of this file. The host never recompiles to gain
// a new node type.
//
// Second generation of this ABI (the first was a plain C struct-of-function-pointers plus a raw
// `void* pProperties` blob per node instance). This one is a real C++ factory/instance object pair:
//
//   - xnode_os_node_factory is the node TYPE - one instance per plugin, created fresh per host via
//     NodeOS_CreateFactory (never a DLL-wide static: see the "why not a static singleton" note
//     below), and it is what creates/destroys node INSTANCES.
//   - xnode_os_node is one node INSTANCE dropped on the canvas. It owns its own property state
//     directly as ordinary reflected C++ members (XPROPERTY_VDEF/XPROPERTY_VREG - see cube_node.cpp)
//     rather than a detached opaque blob, so there is only one object per node, not two.
//
// Both derive from xproperty::base (getProperties()) and hand the HOST a real, live
// xproperty::type::object* - this is deliberately NOT the old ABI-safe-primitives-only design. That
// design existed only because an earlier attempt at exactly this crashed: xproperty.h's atomic_v<T>
// singletons are per-binary `inline` globals, so a naive pointer-identity comparison
// (`m_pType == &atomic_v<T>`) can never succeed across a DLL boundary. xproperty.h has since been
// fixed to compare by GUID VALUE instead (`m_pType->m_GUID == atomic_v<T>.m_GUID`) - GUIDs are a
// compile-time hash of a name string, not an address, so they agree across separately-linked
// binaries. Empirically verified end-to-end (a real cross-DLL getProperties() walk, host-compiled
// xproperty::any reading plugin-compiled data) before this ABI generation was built. This means the
// host can now draw ANY plugin's properties with the host's own real xproperty::inspector - a
// plugin never needs its own compiled copy of ImGui/xPropertyImGuiInspector.cpp just to show a
// property panel (contrast cube_node.cpp's old DrawProperties, which did exactly that).
//
// Why not a DLL-wide static factory instance: it would be shared by however many host instances
// happen to load this same DLL image within one process (Windows gives every LoadLibrary call in a
// process the same module and the same statics). A plugin that ever caches anything host-specific
// (the ixnode_os_host& it was given, say) in a shared static would have that state silently
// clobbered the moment a second host instance called in. Creating a fresh factory per
// NodeOS_CreateFactory call makes that bug structurally impossible instead of relying on every
// plugin author remembering not to cache host state in DLL-global storage.
//
// A pure-virtual interface is the one exception to "no C++ crossing the boundary" beyond
// xproperty::base itself: vtable dispatch is stable across CRT/runtime-library choices (it only
// depends on the C++ compiler's calling convention, fixed by using the same toolchain), unlike STL
// container layout, which IS CRT-sensitive. Every interface method is effectively noexcept (no
// exception ever crosses a DLL boundary safely), and every object crossing it is destroyed by an
// explicit Destroy-style call on whichever side allocated it - never `delete` through a pointer
// whose vtable came from a different binary's heap (xnode_os_node's and xnode_os_node_factory's own
// destructors are protected for exactly this reason: a host holding only the abstract base can't
// accidentally `delete` through it, only the plugin - which knows the concrete type - can, via
// DestroyNodeInstance/NodeOS_DestroyFactory).
//
// Composing already-loaded nodes into a graph, and even promoting a whole subgraph into a new
// reusable node, both happen live inside the host with zero DLLs involved - only the PRIMITIVE,
// native node kinds need this plugin path at all.

#include "xnode_os_host_interface.h"
#include "dependencies/xproperty/source/xcore/my_properties.h"
#include <string_view>
#include <span>

struct xnode_os_node_factory;

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
// One node INSTANCE. getInputs()/getOutputs() are per-instance (not per-factory) on purpose: a
// future node type with variable-arity ports (e.g. a "Sum" node with N inputs) can already vary
// them per instance without any ABI change.
//
// Execute receives one void* per input pin (whatever the upstream node's output produced - null if
// unconnected) and must fill in one void* per output pin; this instance's own property members
// (reflected via getProperties(), inherited from xproperty::base) are read directly as `this`'s own
// state, not passed in separately. Memory for an output belongs to this instance until the host
// calls FreeOutputs on it (before re-Execute, or when the node/graph is torn down).
//------------------------------------------------------------------------------------------------
struct xnode_os_node : xproperty::base
{
    xnode_os_node_factory* m_pFactory = nullptr;

    virtual std::span<const xnode_os_port_desc> getInputs()  const noexcept = 0;
    virtual std::span<const xnode_os_port_desc> getOutputs() const noexcept = 0;
    virtual void Execute(void** Inputs, void** Outputs) noexcept = 0;

    // Default no-op - correct as-is for any node type with zero outputs (most of them; only a node
    // that heap-allocates its own output data, like Cube's mesh, needs to override this).
    virtual void FreeOutputs(void** /*Outputs*/) noexcept {}

protected:
    ~xnode_os_node() noexcept = default; // never through this pointer - see DestroyNodeInstance
};

//------------------------------------------------------------------------------------------------
// One node TYPE a plugin DLL exposes - see this file's top comment for why this is a real object
// created per-host rather than a DLL-wide static.
//------------------------------------------------------------------------------------------------
struct xnode_os_node_factory : xproperty::base
{
    virtual std::string_view getVersion()  const noexcept = 0;
    virtual std::string_view getName()     const noexcept = 0;
    virtual std::string_view getCategory() const noexcept = 0;

    virtual xnode_os_node& CreateNodeInstance()                = 0;
    virtual void           DestroyNodeInstance(xnode_os_node&) = 0;

protected:
    ~xnode_os_node_factory() noexcept = default; // never through this pointer - see NodeOS_DestroyFactory
};

//------------------------------------------------------------------------------------------------
// The plugin DLL must export exactly these two functions (undecorated, thanks to extern "C"), so
// GetProcAddress can find them by fixed names regardless of what the DLL is called.
// NodeOS_CreateFactory is called once, immediately after LoadLibrary, and NodeOS_DestroyFactory
// exactly once more, on the same factory reference, when the host is done with this plugin (e.g.
// before recompiling it, or at shutdown) - see this file's top comment for why the factory is a
// fresh heap object each time rather than something the plugin could just return by static
// reference.
//------------------------------------------------------------------------------------------------
#define XNODE_OS_EXPORT __declspec(dllexport)

using xnode_os_pfn_create_factory  = xnode_os_node_factory&(ixnode_os_host&);
using xnode_os_pfn_destroy_factory = void(xnode_os_node_factory&);

#define XNODE_OS_CREATE_FACTORY_NAME  "NodeOS_CreateFactory"
#define XNODE_OS_DESTROY_FACTORY_NAME "NodeOS_DestroyFactory"

#endif
