namespace xgpu
{
    struct mouse
    {
        enum class error : std::uint8_t
        {
            FAILURE
        ,   ENUM_COUNT
        };

        struct setup
        {
            int m_Index = 0;
        };

        enum class digital : std::uint8_t
        {
            BTN_LEFT
        ,   BTN_MIDDLE
        ,   BTN_RIGHT

        ,   BTN_0
        ,   BTN_1
        ,   BTN_2
        ,   BTN_3
        ,   BTN_4

        ,   ENUM_COUNT
        };

        enum class analog : std::uint8_t
        {
            POS_REL
        ,   WHEEL_REL
        ,   POS_ABS

        ,   ENUM_COUNT
        };

        XGPU_INLINE [[nodiscard]] bool                  isPressed       ( digital ButtonID ) const noexcept;
        XGPU_INLINE [[nodiscard]] bool                  wasPressed      ( digital ButtonID ) const noexcept;
        XGPU_INLINE [[nodiscard]] std::array<float,2>   getValue        ( analog  PosID )    const noexcept;

        std::shared_ptr<details::mouse_handle> m_Private{};
    };
}