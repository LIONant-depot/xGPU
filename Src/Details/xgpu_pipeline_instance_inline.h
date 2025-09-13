namespace xgpu
{
    namespace details
    {
        struct pipeline_instance_handle
        {
            virtual                                    ~pipeline_instance_handle(void)                                                  noexcept = default;
        };
    }

    pipeline_instance::~pipeline_instance()
    {
        if (m_Device)
            m_Device->Destroy(std::move(*this));
    }
}