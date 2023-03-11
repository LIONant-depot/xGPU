namespace e16
{
    struct vertex
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
            std::vector<vertex>     m_Vertices;
            std::vector<int>        m_Indices;
            int                     m_iMaterial;
        };

        struct mesh
        {
            std::string             m_Name;
            std::vector<submesh>    m_Submeshes;
        };

        struct material_instance
        {
            std::string             m_Albedo;
            std::string             m_Normal;
            std::string             m_AmbientOcclusion;
            std::string             m_Roughness;
        };

        std::vector<mesh>               m_Mesh;
        std::vector<material_instance>  m_MaterialInstance;
    };
}