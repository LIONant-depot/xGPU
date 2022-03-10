namespace xgpu
{
    // Shader Pipeline for Shader Model 5.0 GPUs
    //  Ref:https://rastergrid.com/blog/2010/09/history-of-hardware-tessellation/
    //  Vertex Sharer --> Tessellation Control Shader --> [[Tessellator]] --> Tessellation Evaluator Shader
    //     --> Geometry Shader --> [[Primitive Assembly]] --> [[Rasterizer]]--> Fragment Shader
    // Please note that a new pipeline for shaders is coming:
    //  https://developer.nvidia.com/blog/introduction-turing-mesh-shaders/
    struct shader
    {
        enum class type : std::uint8_t
        {
            UNDEFINED
        ,   VERTEX
        ,   TESSELLATION_CONTROL
        ,   TESSELLATION_EVALUATOR
        ,   GEOMETRY
        ,   FRAGMENT
        ,   COMPUTE
        ,   ENUM_COUNT
        };

        struct setup
        {
            struct raw_data { std::span<const std::int32_t> m_Value; };
            struct file_name{ const char* m_pValue; };

            type                                m_Type                  { type::UNDEFINED };
            const char*                         m_EntryPoint            { "main" };
            std::variant<file_name, raw_data >  m_Sharer                { file_name{nullptr} };
        };

        std::shared_ptr<details::shader_handle> m_Private;
    };
}