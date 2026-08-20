#ifndef XNODE_OS_HOST_INTERFACE_H
#define XNODE_OS_HOST_INTERFACE_H
#pragma once

//------------------------------------------------------------------------------------------------
// The interface the host hands a plugin exactly once, as the sole argument to
// NodeOS_CreateFactory(ixnode_os_host&) - see xnode_os_plugin_api.h. A plugin that wants to keep it
// around past that call (e.g. to route Execute()-time diagnostics through Log()) may do so safely:
// each factory instance is created fresh per host that asks for one (never a DLL-wide static), so
// there is exactly one host per factory and nothing here is ever shared between hosts.
//
// Pure virtual for the same reason as xnode_os_node/xnode_os_node_factory: vtable dispatch is
// stable across CRT/runtime-library choices, so plugins remain free to build with any compiler
// settings they like. Every method is effectively noexcept (no exception ever crosses a DLL
// boundary safely), and nothing is destroyed with `delete` through this pointer.
//------------------------------------------------------------------------------------------------
class ixnode_os_host
{
public:
    // Routes to the same logger the host uses for its own diagnostics, so a plugin's failures show
    // up in the same place as everything else instead of being silently swallowed.
    virtual void Log(const char* pMessage) noexcept = 0;

protected:
    ~ixnode_os_host() noexcept = default; // never through this base pointer
};

#endif
