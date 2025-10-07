namespace xgpu
{
    namespace details
    {
        struct texture_handle
        {
            virtual                                    ~texture_handle          (void)                          noexcept = default;
            virtual std::array<int, 3>                  getTextureDimensions    (void)                  const   noexcept = 0;
            virtual int                                 getMipCount             (void)                  const   noexcept = 0;
            virtual xgpu::texture::format               getFormat               (void)                  const   noexcept = 0;
            virtual bool                                isCubemap               (void)                  const   noexcept = 0;
            virtual std::array<texture::address_mode, 3> getAdressModes         (void)                  const   noexcept = 0;
            virtual void                                DeathMarch              (xgpu::texture&&)               noexcept = 0;
        };
    }

    //-----------------------------------------------------------------------------

    std::array<int,3> texture::getTextureDimensions(void) const noexcept
    {
        return m_Private->getTextureDimensions();
    }

    //-----------------------------------------------------------------------------
    int texture::getMipCount( void ) const noexcept
    {
        return m_Private->getMipCount();
    }

    //-----------------------------------------------------------------------------
    texture::format texture::getFormat(void) const noexcept
    {
        return m_Private->getFormat();
    }

    //-----------------------------------------------------------------------------
    bool texture::isCubemap(void) const noexcept
    {
        return m_Private->isCubemap();
    }

    //-----------------------------------------------------------------------------
    inline
    std::array<texture::address_mode, 3> texture::getAdressModes(void) const noexcept
    {
        return m_Private->getAdressModes();
    }

    texture::~texture(void)
    {
        if (m_Private.use_count() > 1) return;
        if (!m_Private) return;
        m_Private->DeathMarch(std::move(*this));
        m_Private.reset();
    }
}

