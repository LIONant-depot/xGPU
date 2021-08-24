namespace xgpu
{
    namespace details
    {
        struct instance_handle
        {
                                                instance_handle     ( void )                                                                                                     = default;
            virtual                            ~instance_handle     ( void )                                                                                                     = default;
            virtual xgpu::device::error*        CreateDevice        ( xgpu::device& Device, const xgpu::device::setup& Setup = xgpu::device::setup{} )                  noexcept = 0;
            virtual xgpu::keyboard::error*      CreateKeyboard      ( xgpu::keyboard&  Keyboard,    const xgpu::keyboard::setup& Setup = xgpu::keyboard::setup{}    )   noexcept = 0;
            virtual xgpu::mouse::error*         CreateMouse         ( xgpu::mouse&     Mouse,       const xgpu::mouse::setup&    Setup = xgpu::mouse::setup{}       )   noexcept = 0;
            virtual bool                        ProcessInputEvents  ( void )                                                                                            noexcept = 0;

            xgpu::instance::callback_error*     m_pErrorCallback    { nullptr };
            xgpu::instance::callback_error*     m_pWarningCallback  { nullptr };
        };
    }

    //--------------------------------------------------------------------------------------

    XGPU_INLINE [[nodiscard]]
    xgpu::device::error* instance::Create( xgpu::device& Device, const xgpu::device::setup& Setup ) noexcept
    {
        return m_Private->CreateDevice( Device, Setup );
    }

    //--------------------------------------------------------------------------------------
    XGPU_INLINE [[nodiscard]]
    xgpu::keyboard::error* instance::Create(xgpu::keyboard& Keyboard, const xgpu::keyboard::setup& Setup ) noexcept
    {
        return m_Private->CreateKeyboard( Keyboard, Setup );
    }

    //--------------------------------------------------------------------------------------
    XGPU_INLINE [[nodiscard]]
    xgpu::mouse::error* instance::Create(xgpu::mouse& Mouse, const xgpu::mouse::setup& Setup ) noexcept
    {
        return m_Private->CreateMouse( Mouse, Setup );
    }

    //--------------------------------------------------------------------------------------
    XGPU_INLINE [[nodiscard]]
    bool instance::ProcessInputEvents( void ) noexcept
    {
        return m_Private->ProcessInputEvents();
    }

}