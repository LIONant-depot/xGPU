#include "source/xGPU.h"

#include "../../tools/xgpu_view.h"
#include "../../tools/xgpu_view_inline.h"

#include "dependencies/xprim_geom/source/xprim_geom.h"

#include "../../Examples/E16_Animation/E16_AnimCharacter.h"

//------------------------------------------------------------------------------------------------
static
void DebugMessage(std::string_view View)
{
    printf("%s\n", View.data());
}

//------------------------------------------------------------------------------------------------

namespace e18
{
    struct quantized_skin_render;
}

//------------------------------------------------------------------------------------------------

struct e18::quantized_skin_render
{
    struct mesh
    {
        std::string     m_Name;
        std::uint32_t   m_iSubmesh;
        std::uint32_t   m_nSubmeshes;
    };

    struct submesh
    {
        std::uint32_t   m_iVertex;
        std::uint32_t   m_iIndex;
        std::uint32_t   m_nIndices;
        std::uint32_t   m_iMaterialInstance;
        xmath::fvec3d   m_PosCompressionOffset;
        xmath::fvec2  m_UVCompressionOffset;
    };

    struct alignas(std::uint32_t) vertex_skin_pos
    {
        std::int16_t    m_QPosition_X           // All these go together they are represented by
                    ,   m_QPosition_Y           // SINT16_4D_NORMALIZED
                    ,   m_QPosition_Z           //
                    ,   m_QPosition_QNormalX;   // The normal X dimension plus a sign bit

        xcolori         m_BoneWeights;          // Bone weights represented by UINT8_4D_NORMALIZED
        xcolori         m_BoneIndex;            // Bone indices represented by UINT8_4D_UINT
    };

    struct alignas(std::uint32_t) vertex_extras
    {
        std::uint32_t   m_QNormalY  : 10          // This contains two signs bits (For the Tangent.z, and the Bitangent direction)
                    ,   m_QTangentX : 10          // All these guys go together, they are represented by
                    ,   m_QTangentY : 10          // SINT_A2RGB10_4D_NORMALIZED
                    ,   m_QAlpha    :  2          //
                    ;           

        std::int16_t    m_U                     // These two go together, they are represented by
                    ,   m_V;                    // SINT16_2D_NORMALIZED
    };

    static_assert( sizeof(vertex_skin_pos) + sizeof(vertex_extras) == 16+(4*2) );

    struct push_constants
    {
        xmath::fvec4  m_LightColor;
        xmath::fvec4  m_AmbientLightColor;
        xmath::fvec3  m_WorldSpaceLightPos;
        xmath::fvec3  m_WorldSpaceEyePos;

        xmath::fvec3  m_PosCompressionScale;
        xmath::fvec3  m_PosCompressionOffset;
        xmath::fvec4  m_UVDecompression;
    };

    struct alignas(256) shader_uniform_buffer
    {
        xmath::fmat4                  m_W2C;
        std::array<xmath::fmat4,256>  m_L2W;
    };

    //------------------------------------------------------------------------------------------

    int QuantizeAndCreateVertexBuffers(xgpu::device& Device, const import3d::geom& SkinGeom, const std::size_t nVertices )
    {
        //
        // Compute the following...
        //
        xmath::fbbox GlobalPosBBOx;
        xmath::fbbox GlobalUVBBOx;
        {
            GlobalPosBBOx.setupZero();
            GlobalUVBBOx.setupZero();

            auto iSubmesh = 0;
            for (auto& Mesh : SkinGeom.m_Mesh)
            {
                for (auto& Submesh : Mesh.m_Submeshes)
                {
                    xmath::fbbox PosBBox;
                    xmath::fbbox UVBBox;

                    PosBBox.setupZero();
                    UVBBox.setupZero();
                    for( auto& V : Submesh.m_Vertices )
                    {
                        xmath::fvec3 xx = V.m_Position;
                        PosBBox.AddVerts( {&xx, 1} );

                        xmath::fvec3 UV(V.m_UV.m_X, V.m_UV.m_Y, 0);
                        UVBBox.AddVerts( {&UV, 1} );
                    }

                    // Add both bboxes to the global total
                    // This is going to be determine by our worse case mesh bounds
                    {
                        xmath::fvec3 PosS(PosBBox.getSize());
                        xmath::fvec3 UVS(UVBBox.getSize());

                        GlobalPosBBOx.AddVerts({ &PosS, 1});
                        GlobalUVBBOx.AddVerts({&UVS,1});
                    }

                    // Store the offsets
                    {
                        auto& RenderSubmesh = m_Submeshes[iSubmesh++];

                        RenderSubmesh.m_PosCompressionOffset = PosBBox.getCenter();
                        RenderSubmesh.m_UVCompressionOffset.setup(UVBBox.getCenter().m_X, UVBBox.getCenter().m_Y );
                    }
                }
            }        
        }

        //
        // Set the global scale for the quantization...
        // This will allow us to normalize
        //
        m_PosCompressionScale = GlobalPosBBOx.getSize();
        m_UVCompressionScale.setup( GlobalUVBBOx.getSize().m_X, GlobalUVBBOx.getSize().m_Y );

        //
        // Allocate the CPU memory for the vertex and compress vertices
        // We are also splitting them into two different streams of data
        //
        auto lSkinPos    = std::make_unique<vertex_skin_pos[]>(nVertices);
        auto lSkinExtras = std::make_unique<vertex_extras[]>(nVertices);
        {
            auto iVertex     = 0;
            auto iSubmesh    = 0;

            for (auto& Mesh     : SkinGeom.m_Mesh)
            for (auto& Submesh  : Mesh.m_Submeshes)
            {
                const auto& RenderSubmesh = m_Submeshes[iSubmesh++];

                for (auto& V : Submesh.m_Vertices)
                {
                    // Quantize the normal, tangent, bitangent (sign) and UVs
                    {
                        auto& SkinExtra = lSkinExtras[iVertex];
                        float f;

                        SkinExtra.m_QNormalY  = static_cast<std::int16_t>(V.m_Normal.m_Y  * (V.m_Normal.m_Y  >= 0 ? 0x1FF : 0x200));
                        SkinExtra.m_QTangentX = static_cast<std::int16_t>(V.m_Tangent.m_X * (V.m_Tangent.m_X >= 0 ? 0x1FF : 0x200));
                        SkinExtra.m_QTangentY = static_cast<std::int16_t>(V.m_Tangent.m_Y * (V.m_Tangent.m_Y >= 0 ? 0x1FF : 0x200));

                        // Store the tangent z sign here so it will be [-1 || 1]
                        SkinExtra.m_QAlpha = V.m_Tangent.m_Z >= 0 ? 0x1 : 0x3;

                        // Compress the UVs

                        // Convert to [-1, 1]
                        f = (V.m_UV.m_X - RenderSubmesh.m_UVCompressionOffset.m_X) / m_UVCompressionScale.m_X;
                        SkinExtra.m_U = static_cast<std::int16_t>(f >= 0 ? f * 0x7FFF : f * 0x8000);

                        // Convert to [-1, 1]
                        f = (V.m_UV.m_Y - RenderSubmesh.m_UVCompressionOffset.m_Y) / m_UVCompressionScale.m_Y;
                        SkinExtra.m_V = static_cast<std::int16_t>(f >= 0 ? f * 0x7FFF : f * 0x8000);
                    }

                    // Quantize the position (and store x dimension of the normal, wih the sign of the normal z)
                    {
                        auto& SkinPos = lSkinPos[iVertex];

                        float f;

                        f = (V.m_Position.m_X - RenderSubmesh.m_PosCompressionOffset.m_X) / m_PosCompressionScale.m_X;
                        assert(f >= -1 && f <= 1);
                        SkinPos.m_QPosition_X = static_cast<std::int16_t>( f >= 0 ? f * 0x7FFF : f * 0x8000 );

                        f = (V.m_Position.m_Y - RenderSubmesh.m_PosCompressionOffset.m_Y) / m_PosCompressionScale.m_Y;
                        assert(f >= -1 && f <= 1);
                        SkinPos.m_QPosition_Y = static_cast<std::int16_t>(f >= 0 ? f * 0x7FFF : f * 0x8000 );

                        f = (V.m_Position.m_Z - RenderSubmesh.m_PosCompressionOffset.m_Z) / m_PosCompressionScale.m_Z;
                        assert( f>= -1 && f <= 1);
                        SkinPos.m_QPosition_Z = static_cast<std::int16_t>(f >= 0 ? f * 0x7FFF : f * 0x8000 );

                        //
                        // Store the Normal.X and some bits in the W part... 
                        //

                        // Now we must convert the normal to 14 bits... (we reserve the last 2 bit for something else...)
                        std::int16_t Nx       = std::min( (short)0x3FFF, static_cast<std::int16_t>( ((V.m_Normal.m_X+1)/2.0f) * 0x3FFF ) );

                        // Store the sign bit of Normal.Z in the last bit of the normal
                        // If NX is zero we will steal a little bit of precision to insert the negative sign...
                        if(V.m_Normal.m_Z < 0 )
                        {
                            if( Nx == 0 )
                            {
                                Nx = -1;

                                // If we steal it we must compensate for it in other dimensions
                                // Other wise we are going to get infinities...
                                // This is really only important if you really want to be aggressive towards precision...
                                if constexpr (false)
                                {
                                    auto& SkinExtra = lSkinExtras[iVertex];
                                    if( SkinExtra.m_QNormalY > 0 ) SkinExtra.m_QNormalY -= 1;
                                    else                           SkinExtra.m_QNormalY += 1;
                                }
                            }
                            else
                            {
                                Nx = -Nx;
                            }
                        }

                        // We are also going to store the bitangent sign bit in bit 14
                        if( V.m_Tangent.Cross(V.m_Normal).Dot(V.m_Bitangent) < 0 )
                        {
                            if (Nx < 0)  Nx  = -((1 << 14) | (-Nx));
                            else         Nx |= (1 << 14);
                        }

                        SkinPos.m_QPosition_QNormalX = Nx;

                        // Store the skin information
                        SkinPos.m_BoneWeights = V.m_BoneWeights;
                        SkinPos.m_BoneIndex   = V.m_BoneIndex;
                    }

                    //
                    // Sanity Check the data (decompress and check with original data)
                    //
                    if constexpr (true)
                    {
                        auto& SkinPos   = lSkinPos[iVertex];
                        auto& SkinExtra = lSkinExtras[iVertex];
                        auto  Mask      = (std::uint32_t)(~((1 << 10) - 1));

                        int yy = (static_cast<std::uint32_t>(SkinExtra.m_QNormalY) & (1<<9))? static_cast<int>(SkinExtra.m_QNormalY | Mask) : static_cast<int>(SkinExtra.m_QNormalY);
                        const float in_w = static_cast<float>(SkinPos.m_QPosition_QNormalX) / (SkinPos.m_QPosition_QNormalX >= 0 ? 0x7FFF : 0x8000);
                        const float in_y = yy / float(yy >= 0 ? 0x1FF : 0x200 );

                        xmath::fvec3 NormalA, TangentA;

                        const float BitangentSign = std::abs(in_w) > 0.5f ? -1.0f : 1.0f;

                        NormalA.setup(4 * std::abs(in_w) - (2- BitangentSign), in_y, in_w < 0 ? -1.0f : 1.0f );
                        NormalA.m_Z *= xmath::Sqrt(1.01f - NormalA.m_X * NormalA.m_X - NormalA.m_Y * NormalA.m_Y );

                        {
                            int   yy1 = (static_cast<std::uint32_t>(SkinExtra.m_QTangentX) & (1 << 9)) ? static_cast<int>(SkinExtra.m_QTangentX | Mask) : static_cast<int>(SkinExtra.m_QTangentX);
                            int   yy2 = (static_cast<std::uint32_t>(SkinExtra.m_QTangentY) & (1 << 9)) ? static_cast<int>(SkinExtra.m_QTangentY | Mask) : static_cast<int>(SkinExtra.m_QTangentY);
                            int   w   = (static_cast<std::uint32_t>(SkinExtra.m_QAlpha)    & (1 << 1)) ? static_cast<int>(SkinExtra.m_QAlpha    | (~1)) : static_cast<int>(SkinExtra.m_QAlpha);
                            TangentA.setup(yy1 / float(0x200), yy2 / float(0x200), w / float(1 << 0));
                            TangentA.m_Z *= xmath::Sqrt(1.01f - TangentA.m_X * TangentA.m_X - TangentA.m_Y * TangentA.m_Y);
                        }

                        float SignedBit = V.m_Tangent.Cross(V.m_Normal).Dot(V.m_Bitangent) < 0 ? -1.0f : 1.0f;

                        assert( NormalA.Dot(V.m_Normal)   >= 0.9f );
                        assert( TangentA.Dot(V.m_Tangent) >= 0.9f);
                        assert( SignedBit == BitangentSign );
                    }

                    // Move to the next vertex
                    iVertex++;
                }
            }
        }

        //
        // Create the vertex buffers for both
        //
        if (auto Err = Device.Create
            ( m_Buffer[1]
            , { .m_Type          = xgpu::buffer::type::VERTEX
              , .m_EntryByteSize = sizeof(vertex_skin_pos)
              , .m_EntryCount    = static_cast<int>(nVertices)
              , .m_pData         = lSkinPos.get()
              }); Err)
            return xgpu::getErrorInt(Err);

        if (auto Err = Device.Create
            ( m_Buffer[2]
            , { .m_Type          = xgpu::buffer::type::VERTEX
              , .m_EntryByteSize = sizeof(vertex_extras)
              , .m_EntryCount    = static_cast<int>(nVertices)
              , .m_pData         = lSkinExtras.get()
              }); Err)
            return xgpu::getErrorInt(Err);

        return 0;
    }

    //-----------------------------------------------------------------------------------

    int Initialize(xgpu::device& Device, const import3d::geom& SkinGeom)
    {
        auto nVertices  = 0ull;
        auto nIndices   = 0ull;
        auto nSubmeshes = 0ull;

        for (auto& Mesh : SkinGeom.m_Mesh)
        {
            nSubmeshes += Mesh.m_Submeshes.size();
            for (auto& Submesh : Mesh.m_Submeshes)
            {
                nVertices += Submesh.m_Vertices.size();
                nIndices  += Submesh.m_Indices.size();
            }
        }

        //
        // Create final structures for us
        //
        m_Submeshes.resize( nSubmeshes );
        m_Meshes.resize(SkinGeom.m_Mesh.size());
        {
            auto nVerts   = 0u;
            auto nIndices = 0u;
            for (auto iMesh = 0u, iSubmesh = 0u; iMesh < SkinGeom.m_Mesh.size(); ++iMesh)
            {
                m_Meshes[iMesh].m_Name       = SkinGeom.m_Mesh[iMesh].m_Name;
                m_Meshes[iMesh].m_iSubmesh   = static_cast<int>(iSubmesh);
                m_Meshes[iMesh].m_nSubmeshes = static_cast<int>( SkinGeom.m_Mesh[iMesh].m_Submeshes.size() );

                for (auto& Submesh : SkinGeom.m_Mesh[iMesh].m_Submeshes)
                {
                    m_Submeshes[iSubmesh].m_iIndex   = nIndices;
                    m_Submeshes[iSubmesh].m_nIndices = static_cast<std::uint32_t>(Submesh.m_Indices.size());
                    m_Submeshes[iSubmesh].m_iVertex  = nVerts;
                    m_Submeshes[iSubmesh].m_iMaterialInstance = Submesh.m_iMaterial;

                    iSubmesh ++;
                    nIndices += static_cast<std::uint32_t>(Submesh.m_Indices.size());
                    nVerts   += static_cast<std::uint32_t>(Submesh.m_Vertices.size());
                }
            }
        }

        //
        // Create the vertices
        //
        if( auto Err = QuantizeAndCreateVertexBuffers(Device, SkinGeom, nVertices); Err )
            return Err;


        //
        // Create the indices
        //
        if (auto Err = Device.Create(m_Buffer[0], { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = static_cast<int>(nIndices) }); Err)
            return xgpu::getErrorInt(Err);

        (void)m_Buffer[0].MemoryMap(0, static_cast<int>(nIndices), [&](void* pData)
        {
            auto pIndex = static_cast<std::uint32_t*>(pData);
            for (auto& Mesh    : SkinGeom.m_Mesh)
            for (auto& Submesh : Mesh.m_Submeshes)
            {
                for (auto Index : Submesh.m_Indices)
                {
                    *pIndex = Index;
                    pIndex++;
                }
            }
        });

        //
        // Setup the pipeline
        //
        xgpu::pipeline PipeLine;
        {
            // Vertex descriptor
            xgpu::vertex_descriptor VertexDescriptor;

            // For the position
            {
                auto Attributes = std::array
                { xgpu::vertex_descriptor::attribute
                  { .m_Offset  = offsetof(vertex_skin_pos, m_QPosition_X)
                  , .m_Format  = xgpu::vertex_descriptor::format::SINT16_4D_NORMALIZED
                  , .m_iStream = 0
                  }
                , xgpu::vertex_descriptor::attribute
                  { .m_Offset  = offsetof(vertex_skin_pos, m_BoneWeights)
                  , .m_Format  = xgpu::vertex_descriptor::format::UINT8_4D_NORMALIZED
                  , .m_iStream = 0
                  }
                , xgpu::vertex_descriptor::attribute
                  { .m_Offset  = offsetof(vertex_skin_pos, m_BoneIndex)
                  , .m_Format  = xgpu::vertex_descriptor::format::UINT8_4D_UINT
                  , .m_iStream = 0
                  }
                , xgpu::vertex_descriptor::attribute
                  { .m_Offset  = 0 //offsetof(vertex_extras, m_QNormalY)
                  , .m_Format  = xgpu::vertex_descriptor::format::SINT_RGB10A2_4D_NORMALIZED
                  , .m_iStream = 1
                  }
                , xgpu::vertex_descriptor::attribute
                  { .m_Offset  = offsetof(vertex_extras, m_U)
                  , .m_Format  = xgpu::vertex_descriptor::format::SINT16_2D_NORMALIZED
                  , .m_iStream = 1
                  }
                };
                auto Setup = xgpu::vertex_descriptor::setup
                { .m_bUseStreaming = true
                , .m_VertexSize    = 0 // we are using streaming no this is not relevant... 
                , .m_Attributes    = Attributes
                };

                if (auto Err = Device.Create(VertexDescriptor, Setup); Err)
                    return xgpu::getErrorInt(Err);
            }

            xgpu::shader MyFragmentShader;
            {
                auto RawData = xgpu::shader::setup::raw_data
                { std::array
                    {
                        #include "E17_skingeom_frag.h"
                    }
                };

                if (auto Err = Device.Create(MyFragmentShader, { .m_Type = xgpu::shader::type::bit::FRAGMENT, .m_Sharer = RawData }); Err)
                    return xgpu::getErrorInt(Err);
            }

            xgpu::shader MyVertexShader;
            {
                static std::uint32_t pV[] =
                {
                    #include "E18_quantize_skingeom_vert.h"
                };

                auto RawData = xgpu::shader::setup::raw_data
                { std::span((std::int32_t*)pV, sizeof(pV)/sizeof(*pV))
                };

                if (auto Err = Device.Create(MyVertexShader, { .m_Type = xgpu::shader::type::bit::VERTEX, .m_Sharer = RawData }); Err)
                    return xgpu::getErrorInt(Err);
            }

            auto UBuffersUsage = std::array{ xgpu::shader::type{xgpu::shader::type::bit::VERTEX} };
            auto Shaders       = std::array<const xgpu::shader*, 2>{ &MyFragmentShader, &MyVertexShader };
            auto Samplers      = std::array
                                { xgpu::pipeline::sampler{} // [INPUT_TEXTURE_NORMAL]
                                , xgpu::pipeline::sampler{} // [INPUT_TEXTURE_DIFFUSE]
                                , xgpu::pipeline::sampler{} // [INPUT_TEXTURE_AO]
                                , xgpu::pipeline::sampler{} // [INPUT_TEXTURE_GLOSSINESS]
                                , xgpu::pipeline::sampler{} // [INPUT_TEXTURE_ROUGHNESS]
                                };
            auto Setup         = xgpu::pipeline::setup
            { .m_VertexDescriptor   = VertexDescriptor
            , .m_Shaders            = Shaders
            , .m_PushConstantsSize  = sizeof(push_constants)
            , .m_UniformBufferUsage = UBuffersUsage
            , .m_Samplers           = Samplers
            };

            if (auto Err = Device.Create(PipeLine, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        //
        // Load all the textures
        //
        if( auto Err = m_LoadedTextures.Initialize( Device, SkinGeom.m_FileName, SkinGeom.m_TexturePaths, SkinGeom.m_MaterialInstance); Err )
            return Err;

        //
        // Setup the material instance
        //
        xgpu::buffer UBO;
        if (auto Err = Device.Create(UBO, { .m_Type = xgpu::buffer::type::UNIFORM, .m_Usage = xgpu::buffer::setup::usage::CPU_WRITE_GPU_READ, .m_EntryByteSize = sizeof(shader_uniform_buffer), .m_EntryCount = 2 }); Err)
            return xgpu::getErrorInt(Err);

        m_PipeLineInstance.resize( SkinGeom.m_MaterialInstance.size() );
        for (auto i = 0u; i < m_PipeLineInstance.size(); ++i)
        {
            auto iDiffuse       = SkinGeom.m_MaterialInstance[i].m_DiffuseSampler.m_iTexture;
            auto iNormal        = SkinGeom.m_MaterialInstance[i].m_NormalSampler.m_iTexture;
            auto Bindings       = std::array
                                { xgpu::pipeline_instance::sampler_binding{ iNormal  == -1 ? m_LoadedTextures.m_DefaultTexture : m_LoadedTextures.m_Textures[iNormal]  } // [INPUT_TEXTURE_NORMAL]
                                , xgpu::pipeline_instance::sampler_binding{ iDiffuse == -1 ? m_LoadedTextures.m_DefaultTexture : m_LoadedTextures.m_Textures[iDiffuse] } // [INPUT_TEXTURE_DIFFUSE]
                                , xgpu::pipeline_instance::sampler_binding{ iDiffuse == -1 ? m_LoadedTextures.m_DefaultTexture : m_LoadedTextures.m_Textures[iDiffuse] } // [INPUT_TEXTURE_AO]
                                , xgpu::pipeline_instance::sampler_binding{ iDiffuse == -1 ? m_LoadedTextures.m_DefaultTexture : m_LoadedTextures.m_Textures[iDiffuse] } // [INPUT_TEXTURE_GLOSSINESS]
                                , xgpu::pipeline_instance::sampler_binding{ iDiffuse == -1 ? m_LoadedTextures.m_DefaultTexture : m_LoadedTextures.m_Textures[iDiffuse] } // [INPUT_TEXTURE_ROUGHNESS]
                                };
            auto UniformBuffers = std::array{ xgpu::pipeline_instance::uniform_buffer{ UBO } };

            auto  Setup    = xgpu::pipeline_instance::setup
            { .m_PipeLine               = PipeLine
            , .m_UniformBuffersBindings = UniformBuffers
            , .m_SamplersBindings       = Bindings
            };

            if( auto Err = Device.Create(m_PipeLineInstance[i], Setup ); Err )
                return xgpu::getErrorInt(Err);
        }

        return 0;
    }

    //------------------------------------------------------------------------------------------

    template< typename T_CALLBACK>
    void Render( xgpu::cmd_buffer& CmdBuffer, T_CALLBACK&& Callback, std::uint64_t MeshMask = ~0ull )
    {
        if(!MeshMask) return;

        CmdBuffer.setStreamingBuffers(m_Buffer);

        bool bComputedMatrices = false;
        for (int i = 0; i < m_Meshes.size(); ++i, MeshMask >>= 1 )
        {
            if((1 & MeshMask) == 0) continue;
            auto& Mesh = m_Meshes[i];
            for (auto j = 0u; j < Mesh.m_nSubmeshes; ++j)
            {
                auto& Submesh = m_Submeshes[Mesh.m_iSubmesh + j];
                CmdBuffer.setPipelineInstance(m_PipeLineInstance[Submesh.m_iMaterialInstance]);

                if (bComputedMatrices == false)
                {
                    bComputedMatrices = true;
                    auto& UBO = CmdBuffer.getUniformBufferVMem<shader_uniform_buffer>(xgpu::shader::type::bit::VERTEX);
                    Callback(UBO);
                }

                m_PushConstants.m_PosCompressionScale  = m_PosCompressionScale;
                m_PushConstants.m_PosCompressionOffset = Submesh.m_PosCompressionOffset;
                m_PushConstants.m_UVDecompression.setup(m_UVCompressionScale.m_X, m_UVCompressionScale.m_Y, Submesh.m_UVCompressionOffset.m_X, Submesh.m_UVCompressionOffset.m_Y );
                CmdBuffer.setPushConstants(m_PushConstants);

                CmdBuffer.Draw( Submesh.m_nIndices, Submesh.m_iIndex, Submesh.m_iVertex );
            }
        }
    }

    xmath::fvec3d                      m_PosCompressionScale;
    xmath::fvec2                       m_UVCompressionScale;
    push_constants                       m_PushConstants;
    e06::load_textures                   m_LoadedTextures;
    std::array<xgpu::buffer,3>           m_Buffer;                  // 0 - Indices, 1 - Pos, 2 - Extra
    std::vector<xgpu::pipeline_instance> m_PipeLineInstance;
    std::vector<submesh>                 m_Submeshes;
    std::vector<mesh>                    m_Meshes;
};

//------------------------------------------------------------------------------------------------

int E18_Example()
{
    xgpu::instance Instance;
    if( auto Err = xgpu::CreateInstance( Instance
                                       , { .m_bDebugMode       = true
                                         , .m_bEnableRenderDoc = true
                                         , .m_pLogErrorFunc    = DebugMessage
                                         , .m_pLogWarning      = DebugMessage 
                                         }
                                       ); Err ) return xgpu::getErrorInt(Err);

    xgpu::device Device;
    if( auto Err = Instance.Create( Device ); Err )
        return xgpu::getErrorInt(Err);

    xgpu::window Window;
    if (auto Err = Device.Create(Window, {}); Err)
        return xgpu::getErrorInt(Err);

    //
    // Load the animated character
    //
    e16::anim_character AnimCharacter;
    {
        import3d::importer Importer;
        if( Importer.Import(
         // L"./../../dependencies/Assets/Animated/ImperialWalker/source/AT-AT.fbx"
            L"./../../dependencies/Assets/Animated/walking-while-listening/source/Walking.fbx"
            , &AnimCharacter.m_SkinGeom
            , &AnimCharacter.m_Skeleton
            , &AnimCharacter.m_AnimPackage
        ) ) exit(1);
    }

    //
    // Initialize the skin render
    //
    e18::quantized_skin_render SkinRender;
    SkinRender.Initialize( Device, AnimCharacter.m_SkinGeom );

    //
    // Compute the BBOX of the mesh
    //
    xmath::fbbox MeshBBox;
    MeshBBox.setupIdentity();
    for( auto& M : AnimCharacter.m_SkinGeom.m_Mesh )
    for (auto& S : M.m_Submeshes)
    for (auto& V : S.m_Vertices)
    {
        xmath::fvec3 x = V.m_Position;
        MeshBBox.AddVerts( {&x, 1} );
    }

    //
    // Basic loop
    //
    xgpu::mouse    Mouse;
    xgpu::keyboard Keyboard;
    {
        Instance.Create(Mouse);
        Instance.Create(Keyboard);
    }

    xmath::fvec3      FrozenLightPosition;
    xmath::radian3      Angles;
    float               Distance     = 2;
    bool                FollowCamera = true;
    auto                Clock        = std::chrono::system_clock::now();
    xmath::fvec3      CameraTarget(0,0,0);
    xgpu::tools::view   View;

    View.setFov(60_xdeg);

    while (Instance.ProcessInputEvents())
    {
        const float DeltaTime = [&]
        {
            auto                         Now            = std::chrono::system_clock::now();
            std::chrono::duration<float> ElapsedSeconds = Now - Clock;
            Clock     = Now;
            return ElapsedSeconds.count();
        }();

        //
        // Input
        //
        if (Mouse.isPressed(xgpu::mouse::digital::BTN_RIGHT))
        {
            auto MousePos = Mouse.getValue(xgpu::mouse::analog::POS_REL);
            Angles.m_Pitch.m_Value -= 0.01f * MousePos[1];
            Angles.m_Yaw.m_Value -= 0.01f * MousePos[0];
        }

        if (Keyboard.wasPressed(xgpu::keyboard::digital::KEY_SPACE))
        {
            FrozenLightPosition = View.getPosition();
            FollowCamera = !FollowCamera;
        }

        if(Mouse.isPressed(xgpu::mouse::digital::BTN_MIDDLE))
        {
            auto MousePos = Mouse.getValue(xgpu::mouse::analog::POS_REL);
            CameraTarget -= View.getWorldYVector() * (0.005f * MousePos[1]);
            CameraTarget -= View.getWorldXVector() * (0.005f * MousePos[0]);
        }

        Distance += Distance * -0.2f * Mouse.getValue(xgpu::mouse::analog::WHEEL_REL)[0];
        if (Distance < 0.5f) 
        {
            CameraTarget += View.getWorldZVector() * (0.5f * (0.5f - Distance));
            Distance = 0.5f;
        }

        // Update the camera
        View.LookAt(Distance, Angles, CameraTarget );

        // Update light position
        auto LightPosition = FollowCamera ? View.getPosition() : FrozenLightPosition;

        //
        // Rendering
        //
        // Windows can not display when they are minimize
        if (Window.BeginRendering()) continue;

        // Update the view with latest window size
        View.setViewport({ 0, 0, Window.getWidth(), Window.getHeight() });

        // Get the command buffer ready to render
        {
            auto        CmdBuffer     = Window.getCmdBuffer();
            const auto  W2C           = View.getW2C();
            const float MeshScale     = 1.0f/MeshBBox.getRadius();

            AnimCharacter.m_AnimPlayer.Update(DeltaTime);

            SkinRender.m_PushConstants.m_AmbientLightColor.setup(0.1f, 0.1f, 0.1f, 1.0f);
            SkinRender.m_PushConstants.m_LightColor.setup(1.0f, 1.0f, 1.0f, 1.0f);
            SkinRender.m_PushConstants.m_WorldSpaceEyePos   = View.getPosition();
            SkinRender.m_PushConstants.m_WorldSpaceEyePos.m_W = 1;
            SkinRender.m_PushConstants.m_WorldSpaceLightPos = LightPosition;

            // set the gamma here
            SkinRender.m_PushConstants.m_WorldSpaceLightPos.m_W = 2.2f;

            // animate the character
            SkinRender.Render( CmdBuffer, [&]( e18::quantized_skin_render::shader_uniform_buffer& UBO )
            {
                UBO.m_W2C = W2C;

                xmath::fmat4 L2W(MeshScale, xmath::fquat::fromIdentity(), { 0,-1,0 });
                
                AnimCharacter.m_AnimPlayer.ComputeMatrices(UBO.m_L2W, L2W);
            });
        }

        // Swap the buffers
        Window.PageFlip();
    }

    return 0;
}

