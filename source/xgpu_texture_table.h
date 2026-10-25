namespace xgpu
{
    // The textures one draw reads by index (a material's): pairs of texture_id and sampler_id, read by the shader through
    // XGPU_TEXTURE / XGPU_SAMPLER (set 3 of a pipeline made with pipeline::setup::m_nTextureSlots).
    // Bindless device (device::isBindless): set 3 is the global heaps; the ids themselves travel in the caller's own data (the
    // material's parameter block) and the table only keeps its textures alive. Otherwise: the table is its own set of
    // m_nTextureSlots combined image samplers, slot i at binding i, built from the same ids. Callers write the same code for both.
    struct texture_table
    {
        enum class error : std::uint8_t
        {
            FAILURE
        };

        struct slot
        {
            texture_id      m_Texture   {};     // invalid: a white texture
            sampler_id      m_Sampler   {};     // invalid: the texture's own sampler (texture::getSampler)
        };

        struct setup
        {
            std::span<const slot>   m_Slots     {};     // as many as the pipelines it is used with have (m_nTextureSlots)
        };

        // A slot changed (a texture swapped at run time): bindless, nothing to rebuild; otherwise a new set for the frames recorded
        // from now on (the one in flight keeps the old one until it retires)
        [[nodiscard]] XGPU_INLINE error*    Update          ( int iSlot, const slot& Slot ) noexcept;
        [[nodiscard]] XGPU_INLINE int               getSlotCount    ( void ) const noexcept;
        [[nodiscard]] XGPU_INLINE slot              getSlot         ( int iSlot ) const noexcept;
        XGPU_INLINE                                ~texture_table   ( void ) noexcept;

        std::shared_ptr<details::texture_table_handle> m_Private;
    };
}
