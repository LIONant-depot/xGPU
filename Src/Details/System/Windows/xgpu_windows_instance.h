namespace xgpu::windows
{
    struct instance : xgpu::details::instance_handle
    {
        bool ProcessInputEvents( void ) noexcept;

        local_storage               m_LocalStorage  {};
        std::shared_ptr<keyboard>   m_Keyboard      = std::make_shared<keyboard>();
        std::shared_ptr<mouse>      m_Mouse         = std::make_shared<mouse>();
    };

}
