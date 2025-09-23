namespace xgpu
{
    namespace details
    {
        struct pipeline_handle
        {
            virtual                                    ~pipeline_handle(void)                                                    noexcept = default;
        };
    }

    //----------------------------------------------------------------------------

    constexpr
    pipeline::blend pipeline::blend::getBlendOff(void) noexcept
    {
        return pipeline::blend
        { .m_ColorWriteMask     = 0b1111
        , .m_bEnable            = false
        , .m_ColorSrcFactor     = factor::ONE
        , .m_ColorDstFactor     = factor::ZERO
        , .m_ColorOperation     = op::ADD
        , .m_AlphaSrcFactor     = factor::ONE
        , .m_AlphaDstFactor     = factor::ZERO
        , .m_AlphaOperation     = op::ADD
        };
    }

    //----------------------------------------------------------------------------
    // DestinationColor.rgb = (SourceColor.rgb * SourceColor.a) + (DestinationColor.rgb * (1 - SourceColor.a));
    constexpr
    pipeline::blend pipeline::blend::getAlphaOriginal(void) noexcept
    {
        return pipeline::blend
        { .m_ColorWriteMask     = 0b1111
        , .m_bEnable            = true
        , .m_ColorSrcFactor     = factor::SRC_ALPHA
        , .m_ColorDstFactor     = factor::ONE_MINUS_SRC_ALPHA
        , .m_ColorOperation     = op::ADD
        , .m_AlphaSrcFactor     = factor::ONE
        , .m_AlphaDstFactor     = factor::ZERO
        , .m_AlphaOperation     = op::ADD
        };
    }

    //----------------------------------------------------------------------------
    // Reference:    https://developer.nvidia.com/content/alpha-blending-pre-or-not-pre
    // DestinationColor.rgb = (SourceColor.rgb * One) + (DestinationColor.rgb * (1 - SourceColor.a))
    constexpr
    pipeline::blend pipeline::blend::getAlphaPreMultiply(void) noexcept
    {
        return pipeline::blend
        { .m_ColorWriteMask     = 0b1111
        , .m_bEnable            = true
        , .m_ColorSrcFactor     = factor::ONE
        , .m_ColorDstFactor     = factor::ONE_MINUS_SRC_ALPHA
        , .m_ColorOperation     = op::ADD
        , .m_AlphaSrcFactor     = factor::ONE
        , .m_AlphaDstFactor     = factor::ZERO
        , .m_AlphaOperation     = op::ADD
        };
    }

    //----------------------------------------------------------------------------
    // DestinationColor.rgb = (SourceColor.rgb * Dst.rgb) + (DestinationColor.rgb * 0)
    constexpr
    pipeline::blend pipeline::blend::getMuliply(void) noexcept
    {
        return pipeline::blend
        { .m_ColorWriteMask     = 0b1111
        , .m_bEnable            = true
        , .m_ColorSrcFactor     = factor::DST_COLOR
        , .m_ColorDstFactor     = factor::ZERO
        , .m_ColorOperation     = op::ADD
        , .m_AlphaSrcFactor     = factor::ONE
        , .m_AlphaDstFactor     = factor::ZERO
        , .m_AlphaOperation     = op::ADD
        };
    }

    //----------------------------------------------------------------------------
    // DestinationColor.rgb = (SourceColor.rgb * One) + (DestinationColor.rgb * One)
    constexpr
    pipeline::blend pipeline::blend::getAdd(void) noexcept
    {
        return pipeline::blend
        { .m_ColorWriteMask     = 0b1111
        , .m_bEnable            = true
        , .m_ColorSrcFactor     = factor::ONE
        , .m_ColorDstFactor     = factor::ONE
        , .m_ColorOperation     = op::ADD
        , .m_AlphaSrcFactor     = factor::ONE
        , .m_AlphaDstFactor     = factor::ZERO
        , .m_AlphaOperation     = op::ADD
        };
    }

    //----------------------------------------------------------------------------
    // DestinationColor.rgb = (DestinationColor.rgb * One) - (SourceColor.rgb * One) 
    constexpr
    pipeline::blend pipeline::blend::getSubSrcFromDest(void) noexcept
    {
        return pipeline::blend
        { .m_ColorWriteMask     = 0b1111
        , .m_bEnable            = true
        , .m_ColorSrcFactor     = factor::ONE
        , .m_ColorDstFactor     = factor::ONE
        , .m_ColorOperation     = op::REVERSE_SUBTRACT
        , .m_AlphaSrcFactor     = factor::ONE
        , .m_AlphaDstFactor     = factor::ZERO
        , .m_AlphaOperation     = op::ADD
        };
    }

    //----------------------------------------------------------------------------
    // DestinationColor.rgb = (SourceColor.rgb * One) - (DestinationColor.rgb * One)
    constexpr
    pipeline::blend pipeline::blend::getSubDestFromSrc(void) noexcept
    {
        return pipeline::blend
        { .m_ColorWriteMask     = 0b1111
        , .m_bEnable            = true
        , .m_ColorSrcFactor     = factor::ONE
        , .m_ColorDstFactor     = factor::ONE
        , .m_ColorOperation     = op::SUBTRACT
        , .m_AlphaSrcFactor     = factor::ONE
        , .m_AlphaDstFactor     = factor::ZERO
        , .m_AlphaOperation     = op::ADD
        };
    }
}