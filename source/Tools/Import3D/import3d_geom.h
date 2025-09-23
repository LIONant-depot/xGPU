#include "dependencies/xbitmap/source/xcolor.h"

namespace import3d
{
    struct material_instance
    {
        enum class shading_model : std::uint8_t
        { UNKOWN
        , GOURAUD
        , PBR
        , TOON
        , UNLIGHT
        };

        enum class address_mode : std::uint8_t
        { TILE
        , MIRROR
        , CLAMP
        };

        struct sampler
        {
            int                 m_iTexture=-1;
            address_mode        m_UMode;
            address_mode        m_VMode;
        };

        std::string             m_Name;
        shading_model           m_ShadingModel;
        xcolori                 m_DiffuseColor;
        xcolori                 m_SpecularColor;
        xcolori                 m_AmbientColor;
        xcolori                 m_EmmisiveColor;
        float                   m_ShininessFactor;
        float                   m_ShininessStreanthFactor;
        float                   m_OpacityFactor;
        sampler                 m_DiffuseSampler;
        sampler                 m_SpecularSampler;
        sampler                 m_OpacitySampler;
        sampler                 m_AmbientSampler;
        sampler                 m_EmissiveSampler;
        sampler                 m_ShininessSampler;
        sampler                 m_LightmapSampler;
        sampler                 m_NormalSampler;
        sampler                 m_HeightSampler;
    };

    struct vertex
    {
        xmath::fvec3d           m_Position;
        xmath::fvec2            m_UV;
        xmath::fvec3            m_Normal;                  
        xmath::fvec3            m_Tangent;                 
        xmath::fvec3            m_Bitangent;               
        xcolori                 m_BoneWeights;             
        xcolori                 m_BoneIndex;               
    };

    struct submesh
    {
        std::vector<vertex>     m_Vertices;
        std::vector<int>        m_Indices;
        int                     m_iMaterial;
    };

    struct mesh
    {
        std::string             m_Name;
        std::vector<submesh>    m_Submeshes;
    };

    struct geom
    {
        std::wstring                    m_FileName;
        std::vector<mesh>               m_Mesh;
        std::vector<material_instance>  m_MaterialInstance;
        std::vector<std::wstring>       m_TexturePaths;
    };
}