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
        union type
        {
            static constexpr int count_v = 8;   // How many types do we have

            enum class bit : std::uint8_t
            { UNDEFINED                   = 0
            , VERTEX                      = (1<<0)
            , TESSELLATION_CONTROL        = (1<<1)
            , TESSELLATION_EVALUATOR      = (1<<2)
            , GEOMETRY                    = (1<<3)
            , FRAGMENT                    = (1<<4)
            , COMPUTE                     = (1<<5)
            };

            bit                 m_Value;

            struct 
            {
                std::uint8_t    m_bVertex               :1
                            ,   m_bTessellationControl  :1
                            ,   m_bTessellationEvaluator:1
                            ,   m_bGeometry             :1
                            ,   m_bFragment             :1
                            ,   m_bCompute              :1; 
            };    
        };

        struct setup
        {
            struct raw_data { std::span<const std::int32_t> m_Value; };
            struct file_name{ const char* m_pValue; };

            type::bit                           m_Type                  { type::bit::UNDEFINED };
            const char*                         m_EntryPoint            { "main" };
            std::variant<file_name, raw_data >  m_Sharer                { file_name{nullptr} };
        };

        std::shared_ptr<details::shader_handle> m_Private;
    };
}

constexpr xgpu::shader::type::bit operator | (xgpu::shader::type::bit A, xgpu::shader::type::bit B ) noexcept
{
    return xgpu::shader::type::bit( static_cast<std::uint8_t>(A) | static_cast<std::uint8_t>(B) );
}
