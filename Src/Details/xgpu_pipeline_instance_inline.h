namespace xgpu
{
    namespace details
    {
        struct pipeline_instance_handle
        {
            virtual                                    ~pipeline_instance_handle(void)                                                  noexcept = default;
            virtual void*                               getUniformBufferVMem    (xgpu::shader::type::bit ShaderType, std::size_t Size ) noexcept = 0;
        };
    }



    template<typename T>
    T& pipeline_instance::getUniformBufferVMem(xgpu::shader::type::bit ShaderType) noexcept
    {
        return *reinterpret_cast<T*>(m_Private->getUniformBufferVMem( ShaderType, sizeof(T) ));
    }

}