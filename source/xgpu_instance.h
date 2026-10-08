namespace xgpu
{
    struct instance
    {
        enum class error : std::uint8_t
        {
            FAILURE
        };

        using callback_error   = void( const std::string_view ErrorString );
        using callback_warning = void( const std::string_view ErrorString );

        struct setup
        {
            enum class driver : std::uint8_t
            {
                VULKAN
            ,   DIRECTX12
            ,   METAL
            };

            const char*         m_pAppName          { "xgpu - App"              };
            bool                m_bDebugMode        { true                      };
            bool                m_bEnableRenderDoc  { true                      };
            driver              m_Driver            { driver::VULKAN            };
            callback_error*     m_pLogErrorFunc     { nullptr };
            callback_warning*   m_pLogWarning       { nullptr };
        };

        [[nodiscard]] XGPU_INLINE xgpu::device::error*          Create              ( xgpu::device&    Device,      const xgpu::device::setup&   Setup = xgpu::device::setup{}      ) noexcept;
        [[nodiscard]] XGPU_INLINE xgpu::keyboard::error*        Create              ( xgpu::keyboard&  Keyboard,    const xgpu::keyboard::setup& Setup = xgpu::keyboard::setup{}    ) noexcept;
        [[nodiscard]] XGPU_INLINE xgpu::mouse::error*           Create              ( xgpu::mouse&     Mouse,       const xgpu::mouse::setup&    Setup = xgpu::mouse::setup{}       ) noexcept;
        [[nodiscard]] XGPU_INLINE bool                          ProcessInputEvents  ( void ) noexcept;

        std::shared_ptr<details::instance_handle>   m_Private           {};
    };

    //----------------------------------------------------------------------------------------

    [[nodiscard]] instance::error* CreateInstance      (xgpu::instance& Instance, const instance::setup& Setup) noexcept;
}
