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

}

