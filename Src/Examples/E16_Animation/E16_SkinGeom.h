namespace e16
{
    struct skin_vertex
    {
        xcore::vector3d m_Position;
        xcore::vector2  m_UV;
        xcore::icolor   m_Normal;
        xcore::icolor   m_Tangent;
        xcore::icolor   m_BoneWeights;
        xcore::icolor   m_BoneIndex;
    };

    struct skin_geom
    {
        struct submesh
        {
            std::vector<skin_vertex>    m_Vertices;
            std::vector<int>            m_Indices;
            int                         m_iMaterial;
        };

        struct mesh
        {
            std::string                 m_Name;
            std::vector<submesh>        m_Submeshes;
        };

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
            xcore::icolor           m_DiffuseColor;
            xcore::icolor           m_SpecularColor;
            xcore::icolor           m_AmbientColor;
            xcore::icolor           m_EmmisiveColor;
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

        std::string                     m_FileName;
        std::vector<mesh>               m_Mesh;
        std::vector<material_instance>  m_MaterialInstance;
        std::vector<std::string>        m_TexturePaths;
    };
}