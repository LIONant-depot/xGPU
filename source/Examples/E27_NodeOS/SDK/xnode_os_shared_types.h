#ifndef XNODE_OS_SHARED_TYPES_H
#define XNODE_OS_SHARED_TYPES_H
#pragma once

// Data layouts that more than one plugin needs to agree on the shape of, so a "Mesh" produced by
// one DLL (e.g. Cube) can be read by a completely different DLL (e.g. Inspect Mesh) neither of
// which links against the other. Plain C POD only - see xnode_os_plugin_api.h for why.

#ifdef __cplusplus
extern "C" {
#endif

struct xnode_os_mesh_data
{
    unsigned int    m_VertexCount;
    float*          m_pPositions;   // m_VertexCount * 3 floats (xyz)
    unsigned int    m_IndexCount;
    unsigned int*   m_pIndices;
};

#ifdef __cplusplus
}
#endif

#endif
