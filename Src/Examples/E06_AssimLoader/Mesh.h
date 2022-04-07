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
    struct vertex
    {
        xcore::vector3 m_Position;
        xcore::vector2 m_Texcoord;
        xcore::icolor  m_Color;
    };

    struct texture 
    {
        std::string     m_HintForType;
        std::string     m_Path;
        xgpu::texture   m_XGPUTexture;
    };

    class mesh 
    {
    public:

        std::vector<vertex>           m_Vertices;
        std::vector<std::uint32_t>    m_Indices;
        std::vector<texture>          m_Textures;
        xgpu::device                  m_Device;

        mesh
        ( xgpu::device&                       Dev
        , const std::vector<vertex>&&         Vertices
        , const std::vector<std::uint32_t>&&  Indices
        , const std::vector<texture>&&        Textures 
        ) noexcept
        : m_Vertices    ( std::move(Vertices) )
        , m_Indices     ( std::move(Indices)  )
        , m_Textures    ( std::move(Textures) )
        , m_Device      { Dev }
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
