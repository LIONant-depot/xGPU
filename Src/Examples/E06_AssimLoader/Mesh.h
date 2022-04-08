#ifndef MESH_H
#define MESH_H

#include <string>
//#include <fstream>
//#include <sstream>
//#include <iostream>
#include <vector>
//#include <stdexcept>
#include "xcore.h"
#include "xGPU.h"

namespace xgpu::assimp
{
    class model_loader;

    struct vertex
    {
        xcore::vector3 m_Position;
        xcore::vector2 m_Texcoord;
        xcore::icolor  m_Color;
    };

    struct sampler
    {
        std::string     m_HintForType;
        int             m_iBindingTexture;
    };

    struct texture 
    {
        std::string     m_Path;
        xgpu::texture   m_XGPUTexture;
    };

    struct material
    {
        std::size_t             m_GUID;
        std::string             m_Name;
        std::vector<sampler>    m_Samplers;
    };

    class mesh 
    {
    public:

        model_loader&                 m_Loader;
        std::vector<vertex>           m_Vertices;
        std::vector<std::uint32_t>    m_Indices;
        xgpu::device                  m_Device;
        int                           m_iMaterial;

        mesh
        ( xgpu::device&                       Dev
        , model_loader&                       Loader
        , const std::vector<vertex>&&         Vertices
        , const std::vector<std::uint32_t>&&  Indices
        , const int                           iMaterial
        ) noexcept
        : m_Loader      ( Loader )
        , m_Vertices    ( std::move(Vertices) )
        , m_Indices     ( std::move(Indices)  )
        , m_Device      { Dev }
        , m_iMaterial   { iMaterial }
        {
            setupMesh();
        }

        void Draw( xgpu::cmd_buffer& CmdBuffer ) 
        {
            CmdBuffer.setBuffer( m_VertexBuffer );
            CmdBuffer.setBuffer( m_IndexBuffer );
            CmdBuffer.Draw( m_IndexBuffer.getEntryCount() );
        }

    private:

        // Render data
        xgpu::buffer            m_VertexBuffer;
        xgpu::buffer            m_IndexBuffer;
        xgpu::vertex_descriptor m_VertexDescriptor;
        int                     m_iMaterialIndex{-1};


        // Functions
        // Initializes all the buffer objects/arrays
        xgpu::device::error* setupMesh( void ) noexcept
        {
            //
            // Setup vertex buffer
            //
            {
                xgpu::buffer::setup Setup
                { .m_Type           = xgpu::buffer::type::VERTEX
                , .m_Usage          = xgpu::buffer::setup::usage::GPU_READ
                , .m_EntryByteSize  = sizeof(vertex)
                , .m_EntryCount     = static_cast<int>(m_Vertices.size())
                , .m_pData          = m_Vertices.data()
                };

                if( auto Err = m_Device.Create( m_VertexBuffer, Setup); Err )
                    return Err;
            }

            //
            // Setup index buffer
            //
            {
                xgpu::buffer::setup Setup
                { .m_Type           = xgpu::buffer::type::INDEX
                , .m_Usage          = xgpu::buffer::setup::usage::GPU_READ
                , .m_EntryByteSize  = sizeof(std::uint32_t)
                , .m_EntryCount     = static_cast<int>(m_Indices.size())
                , .m_pData          = m_Indices.data()
                };

                if( auto Err = m_Device.Create( m_IndexBuffer, Setup); Err )
                    return Err;
            }

            return nullptr;
        }
    };
}

#endif
