#pragma once
namespace xgpu::tools
{
    //-------------------------------------------------------------------------------

    inline
    view::view( void ) noexcept
    {
        // Reset all the flags
        m_StateFlags.m_Value = state_flags::mask::DEFAULT;

        //NOTE! The setting of the above flags also causes infinite clipping to be used!
        //So, infinite clipping ON is the default. You must turn it off, if you want it off.


        // Set the Clip matrix
        setNearZ    ( 0.1f );
        setFarZ     ( 100 );
        setFov      ( xcore::math::radian{ 95_xdeg } );
        setAspect   ( 1 );

        // Set the view matrix
        LookAt      ( xcore::vector3(0,0, 30), xcore::vector3(0,0,0) ); 
    }

    //-------------------------------------------------------------------------------
    inline
    void view::UpdatedView( void ) noexcept 
    {
        m_StateFlags.m_Value |= 
            state_flags::mask::W2V |
            state_flags::mask::W2C |
            state_flags::mask::W2S |
            state_flags::mask::V2C |
            state_flags::mask::V2S |
            state_flags::mask::C2W |
            state_flags::mask::C2V |
            state_flags::mask::PIXELBASED2D;
    }

    //-------------------------------------------------------------------------------
    inline
    void view::UpdatedClip( void ) noexcept 
    {
        m_StateFlags.m_Value |=
            state_flags::mask::W2C |
            state_flags::mask::W2S |
            state_flags::mask::V2C |
            state_flags::mask::V2S |
            state_flags::mask::C2W |
            state_flags::mask::C2V;
    }

    //-------------------------------------------------------------------------------
    inline
    void view::UpdatedScreen( void ) noexcept 
    {
        m_StateFlags.m_Value |=
            state_flags::mask::W2C |
            state_flags::mask::W2S |
            state_flags::mask::V2C |
            state_flags::mask::V2S |
            state_flags::mask::C2W |
            state_flags::mask::C2V |
            state_flags::mask::C2S |
            state_flags::mask::PIXELBASED2D;

    }

    //-------------------------------------------------------------------------------
    inline
    const xcore::matrix4& view::getW2V( void ) noexcept
    {
        if( m_StateFlags.m_W2V )
        {
            m_StateFlags.m_W2V = false;

            m_W2V    = m_V2W;
            m_W2V.InvertSRT();
        }
        return m_W2V;
    }

    //-------------------------------------------------------------------------------
    inline
    xcore::vector3 view::getV2CScales ( const bool bInfiniteClipping ) noexcept
    {
        xcore::vector3      Scales;
        const xcore::irect& ViewPort = getViewport();

    #ifdef _ENG_TARGET_DIRECTX
        if( x_FlagIsOn(m_Flags,FLAGS_USE_FOV) )
        {
            //Field-of-view based projection matrix
            float HalfVPHeight = (ViewPort.getHeight() + 1) * 0.5f;
            float HalfVPWidth  = (ViewPort.getWidth()  + 1) * 0.5f;
            float Distance     = HalfVPWidth / xcore::math::Tan( m_Fov *0.5f );
            float YFov         = xcore::math::ATan( HalfVPHeight / Distance ) * 2.0f;
            float XFov         = xcore::math::ATan(m_Aspect * xcore::math::Tan(m_Fov * 0.5f)) * 2.0f;

            // computing the fovx from yfov
            Scales.m_X = (float)( 1.0f / xcore::math::Tan( XFov*0.5f ));
            Scales.m_Y = (float)( 1.0f / xcore::math::Tan( YFov*0.5f ));

            if( bInfiniteClipping ) Scales.m_Z = -1;
            else                    Scales.m_Z = -m_FarZ/( m_FarZ - m_NearZ );
        }
        else
        {
            //custom frustum
            //(Note, other parts of the proj matrix change as well, not just diagonal. See getV2C)

            Scales.m_X = (float)( 2.0f*m_NearZ / (m_FrustumRight - m_FrustumLeft) );
            Scales.m_Y = (float)( 2.0f*m_NearZ / (m_FrustumTop - m_FrustumBottom) );

            if( bInfiniteClipping ) Scales.m_Z = -1;
            else                    Scales.m_Z = -m_FarZ/( m_FarZ - m_NearZ );
        }
    #else // Vulkan
        if( m_StateFlags.m_USE_FOV )
        {
            const float FocalLength = 1.0f / xcore::math::Tan(m_Fov * xcore::radian{ 0.5f });
            Scales.m_X = FocalLength / m_Aspect;
            Scales.m_Y = FocalLength;

            if( bInfiniteClipping ) Scales.m_Z = 1;
            else                    Scales.m_Z = m_FarZ /( m_FarZ - m_NearZ );
        }
        else
        {
            //custom frustum
            //(Note, other parts of the proj matrix change as well, not just diagonal. See getV2C)
            
            Scales.m_X = (float)( 2.0f*m_NearZ / (m_FrustumRight - m_FrustumLeft) );
            Scales.m_Y = (float)(-2.0f*m_NearZ / (m_FrustumTop - m_FrustumBottom) );
            
            if( bInfiniteClipping ) Scales.m_Z = 1;
            else                    Scales.m_Z = m_FarZ / (m_FarZ - m_NearZ);
        }
    #endif
        return Scales;
    }

    //-------------------------------------------------------------------------------

    inline
    const xcore::matrix4& view::getV2C( void ) noexcept
    {
        if( m_StateFlags.m_V2C )
        {
            m_StateFlags.m_V2C = false;
            m_V2C.setZero();
            m_V2C.setScale( getV2CScales( m_StateFlags.m_INF_CLIP ) );
            if( !m_StateFlags.m_USE_FOV )
            {
                //if using a custom frustum, need to take possible off-centerness into account
                m_V2C(0,2) = (m_FrustumRight + m_FrustumLeft)/(m_FrustumRight - m_FrustumLeft);
                m_V2C(1,2) = (m_FrustumTop + m_FrustumBottom)/(m_FrustumTop - m_FrustumBottom);
            }

    #ifdef _ENG_TARGET_DIRECTX
            // now finish the rest
            m_V2C(2,3) = m_V2C(2,2) * m_NearZ;
            m_V2C(3,2) = -1;
    #else
            m_V2C(3,2) = 1;
            m_V2C(2,3) = -(m_FarZ * m_NearZ) / (m_FarZ - m_NearZ);
    #endif
        }

        return m_V2C;
    }

    //-------------------------------------------------------------------------------

    inline
    const xcore::matrix4& view::getC2S( void ) noexcept
    {
        if( m_StateFlags.m_C2S )
        {
            m_StateFlags.m_C2S    = false;
            const xcore::irect&   Viewport    = getViewport();
            const float       W           = Viewport.getWidth()  * 0.5f;
            const float       H           = Viewport.getHeight() * 0.5f;
            m_C2S.setIdentity();
            m_C2S(0,0) =  W;
            m_C2S(1,1) = -H;
            m_C2S(0,3) =  W + Viewport.m_Left;
            m_C2S(1,3) =  H + Viewport.m_Top;
        }
        return m_C2S;
    }

    //-------------------------------------------------------------------------------

    inline
    const xcore::matrix4& view::getC2V( void ) noexcept
    {
        if( m_StateFlags.m_C2V )
        {
            m_StateFlags.m_C2V = false;
            m_C2V = getV2C();
            m_C2V.FullInvert();
        }
        return m_C2V;
    }

    //-------------------------------------------------------------------------------

    inline
    const xcore::matrix4&  view::getV2S( void ) noexcept
    {
        if( m_StateFlags.m_V2S )
        {
            m_StateFlags.m_V2S = false;
            m_V2S    = getC2S() * getV2C();        
        }
        return m_V2S;
    }

    //-------------------------------------------------------------------------------

    inline
    const xcore::matrix4&  view::getW2C( void ) noexcept
    {
        if( m_StateFlags.m_W2C )
        {
            m_StateFlags.m_W2C = false;
            m_W2C    = getV2C() * getW2V();
        }
        return m_W2C;
    }

    //-------------------------------------------------------------------------------

    inline
    const xcore::matrix4& view::getW2S( void ) noexcept
    {
        if( m_StateFlags.m_W2S )
        {   
            m_StateFlags.m_W2S = false;
            m_W2S    = getV2S() * getW2V();
        }
        return m_W2S;
    }

    //-------------------------------------------------------------------------------

    inline
    const xcore::matrix4& view::getC2W( void ) noexcept
    {
        if( m_StateFlags.m_C2W )
        {   
            m_StateFlags.m_C2W = false;
            m_C2W    = getW2C();
            m_C2W.FullInvert();
        }
        return m_C2W;
    }

    //-------------------------------------------------------------------------------

    inline
    view& view::LookAt( const xcore::vector3& To ) noexcept
    {
        return LookAt( getPosition(), To, xcore::vector3(0,1,0) );
    }

    //-------------------------------------------------------------------------------

    inline
    view& view::LookAt( const xcore::vector3& From, const xcore::vector3& To ) noexcept
    {
        return LookAt( From, To, xcore::vector3(0,1,0) );
    }

    //-------------------------------------------------------------------------------

    inline
    view& view::LookAt( const xcore::vector3& From, const xcore::vector3& To, const xcore::vector3& Up ) noexcept
    {
        UpdatedView();
        m_W2V.LookAt( From, To, Up );
        m_V2W = m_W2V;
        m_V2W.InvertSRT();
        m_StateFlags.m_W2V = false;
        return *this;
    }

    //-------------------------------------------------------------------------------

    inline
    void view::setNearZ( const float NearZ ) noexcept
    {
        m_NearZ = NearZ;
        UpdatedClip();
    }

    //-------------------------------------------------------------------------------

    inline
    float view::getNearZ( void ) const noexcept
    {
        return m_NearZ;
    }

    //-------------------------------------------------------------------------------

    inline
    void view::setFarZ( const float FarZ ) noexcept
    {
        m_FarZ = FarZ;
        UpdatedClip();
    }

    //-------------------------------------------------------------------------------

    inline
    float view::getFarZ( void ) const  noexcept
    {
        return m_FarZ;
    }

    //-------------------------------------------------------------------------------

    inline
    void view::setFov( const xcore::radian Fov ) noexcept
    {
        //enable use of FOV
        m_StateFlags.m_USE_FOV = true;
        m_Fov = Fov;
        UpdatedClip();
    }

    //-------------------------------------------------------------------------------

    constexpr
    xcore::radian view::getFov( void ) const noexcept
    {
        return m_Fov;
    }

    //-------------------------------------------------------------------------------

    inline
    void view::setAspect( const float Aspect ) noexcept
    {
        // enable use of FOV
        m_StateFlags.m_USE_FOV = true;
        m_Aspect = Aspect;
        UpdatedClip();
    }

    //-------------------------------------------------------------------------------

    constexpr
    float view::getAspect( void ) const noexcept
    {
        return m_Aspect;
    }

    //-------------------------------------------------------------------------------

    inline
    void view::setCustomFrustum( const float Left, const float Right, const float Bottom, const float Top ) noexcept
    {
        // disable use of FOV
        m_StateFlags.m_USE_FOV = false;

        m_FrustumLeft   = Left;
        m_FrustumRight  = Right;
        m_FrustumBottom = Bottom;
        m_FrustumTop    = Top;

        UpdatedClip();
    }

    //-------------------------------------------------------------------------------
    // this goes back to the FOV method with currently set FOV & Aspect
    inline
    void view::DisableCustomFrustum( void ) noexcept
    {
        // reenable use of FOV
        m_StateFlags.m_USE_FOV = true;

        UpdatedClip();
    }

    //-------------------------------------------------------------------------------

    inline
    void view::setViewport( const xcore::irect& Rect ) noexcept
    {
        m_FrustumLeft   = static_cast<float>(Rect.m_Left);
        m_FrustumRight  = static_cast<float>(Rect.m_Right);
        m_FrustumTop    = static_cast<float>(Rect.m_Top);
        m_FrustumBottom = static_cast<float>(Rect.m_Bottom);
        m_StateFlags.m_VIEWPORT = false;

        setAspect((m_FrustumRight - m_FrustumLeft) / (float)(m_FrustumBottom-m_FrustumTop) );
        UpdatedScreen();
    }

    //-------------------------------------------------------------------------------

    inline
    void view::RefreshViewport( void ) noexcept
    {
        UpdatedScreen();
    }


    //-------------------------------------------------------------------------------

    inline
    const xcore::irect view::getViewport( void ) const noexcept 
    {
        xassert(m_StateFlags.m_VIEWPORT == false);
        return { static_cast<int>(m_FrustumLeft)
               , static_cast<int>(m_FrustumTop)
               , static_cast<int>(m_FrustumRight)
               , static_cast<int>(m_FrustumBottom)
               };
    }

    //-------------------------------------------------------------------------------

    inline
    const xcore::irect view::getViewport( void ) noexcept 
    {
        if( m_StateFlags.m_VIEWPORT )
        {
            // If the view port is not set just return the screen resolution
            xassume( false );
        }

        return xcore::irect( static_cast<int>(m_FrustumLeft), 
                static_cast<int>(m_FrustumTop), 
                static_cast<int>(m_FrustumRight), 
                static_cast<int>(m_FrustumBottom) );
    }

    //-------------------------------------------------------------------------------
    inline
    view& view::LookAt( const float Distance, const xcore::radian3& aAngles, const xcore::vector3& At ) noexcept
    {
        xcore::vector3 From   ( 0, 0, Distance );
        xcore::vector3 WorldUp( 0, 1, 0);

        // Clamp the angles
        xcore::radian3 Angles( aAngles );
        Angles.m_Pitch = xcore::math::Max( Angles.m_Pitch, xcore::radian{ xcore::degree{ -89.0f } } );
        Angles.m_Pitch = xcore::math::Min( Angles.m_Pitch, xcore::radian{ xcore::degree{  89.0f } } );
        

        // Set the origin of the camera
        From.RotateX( Angles.m_Pitch );
        From.RotateY( Angles.m_Yaw );
        From = At + From;

        // set the world up
        WorldUp.RotateZ( Angles.m_Roll );

        // build the matrix
        LookAt( From, At, WorldUp );

        return *this;
    }

    //-------------------------------------------------------------------------------
    inline
    xcore::vector3 view::getPosition( void ) const noexcept
    {
        return getV2W().getTranslation();
    }

    //-------------------------------------------------------------------------------
    inline
    void view::setCubeMapView( const int Face, const xcore::matrix4& L2W ) noexcept
    {
        xcore::vector3 vLookDir;
        xcore::vector3 vUpDir;
        xcore::vector3 vEye(0,0,0);

        switch( Face )
        {
            case 1: // D3DCUBEMAP_FACE_POSITIVE_X
                vLookDir.setup( 1.0f, 0.0f, 0.0f );
                vUpDir.setup  ( 0.0f, 1.0f, 0.0f );
                break;

            case 0: // D3DCUBEMAP_FACE_NEGATIVE_X
                vLookDir.setup(-1.0f, 0.0f, 0.0f );
                vUpDir.setup  ( 0.0f, 1.0f, 0.0f );
                break;

            case 2: // D3DCUBEMAP_FACE_POSITIVE_Y
                vLookDir.setup( 0.0f, 1.0f, 0.0f );
                vUpDir.setup  ( 0.0f, 0.0f,-1.0f );
                break;

            case 3: // D3DCUBEMAP_FACE_NEGATIVE_Y
                vLookDir.setup( 0.0f,-1.0f, 0.0f );
                vUpDir.setup  ( 0.0f, 0.0f, 1.0f );
                break;

            case 4: // D3DCUBEMAP_FACE_POSITIVE_Z
                vLookDir.setup( 0.0f, 0.0f, 1.0f );
                vUpDir.setup  ( 0.0f, 1.0f, 0.0f );
                break;

            case 5: // D3DCUBEMAP_FACE_NEGATIVE_Z
                vLookDir.setup( 0.0f, 0.0f,-1.0f );
                vUpDir.setup  ( 0.0f, 1.0f, 0.0f );
                break;

            default:
                xassume( 0 );
        }

        // Make sure that the FOV is set to 90
        // as well as the aspect ratio is set to 1
        // We will let the user set the near and far
        setFov      ( xcore::radian{ xcore::degree{ 90.0f } } );
        setAspect   ( 1 );

        // Set the view transform for this cubemap surface
        xcore::matrix4  NoScales( L2W );
        NoScales.ClearScale();
        vEye     = NoScales * vEye ;
        vLookDir = NoScales * vLookDir;
        vUpDir   = NoScales.RotateVector( vUpDir );
        LookAt( vEye, vLookDir, vUpDir );
    }

    //-------------------------------------------------------------------------------
    inline
    void view::setParabolicView( const int Side, const xcore::matrix4& L2W ) noexcept
    {
        xcore::vector3 vLookDir;
        xcore::vector3 vUpDir;
        xcore::vector3 vEye(0,0,0);

        switch( Side )
        {
            case 0:
                vLookDir.setup( 0.0f, 0.0f, 1.0f );
                vUpDir.setup  ( 0.0f, 1.0f, 0.0f );
                break;
            case 1: 
                vLookDir.setup( 0.0f, 0.0f,-1.0f );
                vUpDir.setup  ( 0.0f, 1.0f, 0.0f );
                break;

            default:
                xassume( 0 );
        }

        // Make sure that the FOV is set to 180
        // as well as the aspect ratio is set to 1
        // We will let the user set the near and far
        setFov      ( xcore::radian{ xcore::degree{ 90.0f } } );
        setAspect   ( 1 );

        // Set the view transform for this cubemap surface
        xcore::matrix4  NoScales( L2W );
        NoScales.ClearScale();
        vEye     = NoScales * vEye;
        vLookDir = NoScales * vLookDir;
        vUpDir   = NoScales.RotateVector( vUpDir );
        LookAt( vEye, vLookDir, vUpDir );
    }

    //-------------------------------------------------------------------------------
    inline
    void view::setInfiniteClipping( const bool bInfinite ) noexcept
    {
        if( m_StateFlags.m_INF_CLIP == bInfinite )
            return;

        // Set the flag
        if( bInfinite ) m_StateFlags.m_INF_CLIP = true;
        else            m_StateFlags.m_INF_CLIP = false;

        // update the cache entries
        UpdatedClip();
    }

    //-------------------------------------------------------------------------------
    inline
    bool view::getInfiniteClipping( void ) const noexcept
    {
        return m_StateFlags.m_INF_CLIP;
    }

    //-------------------------------------------------------------------------------
    inline
    xcore::vector3 view::getWorldZVector( void ) const noexcept
    {
        const xcore::matrix4& V2W = getV2W();
        xcore::vector3        Normal( V2W(0,2), V2W(1,2), V2W(2,2) );
        Normal.Normalize();
        return Normal;
    }

    //-------------------------------------------------------------------------------
    inline
    xcore::vector3 view::getWorldXVector( void ) const noexcept
    {
        const xcore::matrix4& V2W = getV2W();
        xcore::vector3        Normal( V2W(0,0), V2W(1,0), V2W(2,0) );
        Normal.Normalize();
        return Normal;
    }

    //-------------------------------------------------------------------------------
    inline
    xcore::vector3 view::getWorldYVector( void ) const noexcept
    {
        const xcore::matrix4& V2W = getV2W();
        xcore::vector3        Normal( V2W(0,1), V2W(1,1), V2W(2,1) );
        Normal.Normalize();
        return Normal;
    }

    //-------------------------------------------------------------------------------
    inline
    void view::setPosition( const xcore::vector3& Position  ) noexcept
    {
        m_V2W.setTranslation( Position );
        UpdatedView();
    }

    //-------------------------------------------------------------------------------
    inline
    void view::setRotation( const xcore::radian3& Rotation ) noexcept
    {
        m_V2W.setRotation( Rotation );
        UpdatedView();
    }

    //-------------------------------------------------------------------------------
    inline
    void view::Translate( const xcore::vector3& DeltaPos ) noexcept
    {
        m_V2W.Translate( DeltaPos );    
        UpdatedView();
    }

    //-------------------------------------------------------------------------------
    inline
    xcore::vector3 view::RayFromScreen( const float ScreenX, const float ScreenY ) noexcept
    {
        const xcore::vector3  Distance = getV2CScales( true );
        const xcore::irect&   Viewport = getViewport();

        // Build ray in viewspace.
        const float       HalfW = (Viewport.m_Left + Viewport.m_Right) * 0.5f;
        xcore::vector3        Ray( -(ScreenX - HalfW ),
                             -(ScreenY - (Viewport.m_Top  + Viewport.m_Bottom) * 0.5f) * m_Aspect,
                             -HalfW*Distance.m_X );

        // Take the ray into the world space
        Ray = getV2W().RotateVector( Ray );
        Ray.Normalize();

        return Ray;
    }

    //-------------------------------------------------------------------------------
    inline
    float view::getScreenSize( const xcore::vector3& Position, const float Radius ) noexcept
    {
        const xcore::matrix4& V2W     = getV2W();
        const xcore::vector3  vPos    = Position - V2W.getTranslation();
        float             ViewZ   = V2W(0,2) * vPos.m_X + V2W(1,2) * vPos.m_Y + V2W(2,2) * vPos.m_Z;
     
        // Is it completly behind the camera?
        if( ViewZ < -Radius )
            return 0;

        // get the closest distance of the sphere to the camera
        ViewZ = xcore::math::Min( ViewZ, Radius );

        // get the biggest scale such the circle will be conservative
        const xcore::matrix4&     V2C = getV2C();
        const float           ViewPortScale = xcore::math::Max( V2C(0,0), V2C(1,1) );

        // get the diameter of the circle in screen space
        const float           ScreenRadius  = ( 2 * Radius * ViewPortScale)/ViewZ;

        return ScreenRadius;
    }


    //-------------------------------------------------------------------------------
    inline
    void view::UpdateAllMatrices( void ) noexcept
    {
        getW2V();
        getW2C();
        getW2S();
        getV2C();
        getV2W();
        getV2S();
        getC2S();
        getC2W();
        getC2V();
        getPixelBased2D();
    }

    //-------------------------------------------------------------------------------
    inline   
    xcore::radian3 view::getAngles( void ) const noexcept
    {
        return m_V2W.getRotationR3();
    }

    //-------------------------------------------------------------------------------
    inline   
    xcore::quaternion view::getRotation( void ) const noexcept
    {
        return xcore::quaternion{ m_V2W };
    }

    //-------------------------------------------------------------------------------
    constexpr   
    const xcore::matrix4& view::getMatrix( void )const noexcept
    {
        return m_V2W; 
    }

    //-------------------------------------------------------------------------------
    inline   
    xcore::vector3 view::getForward( void ) const noexcept
    {
        return m_V2W.getForward();
    }

    //-------------------------------------------------------------------------------

    constexpr   const xcore::matrix4& view::getV2W( void ) const noexcept { return m_V2W;                                         }
    constexpr   const xcore::matrix4& view::getW2V( void ) const noexcept { xassert( m_StateFlags.m_W2V == false ); return m_W2V; }
    constexpr   const xcore::matrix4& view::getW2C( void ) const noexcept { xassert( m_StateFlags.m_W2C == false ); return m_W2C; }
    constexpr   const xcore::matrix4& view::getW2S( void ) const noexcept { xassert( m_StateFlags.m_W2S == false ); return m_W2S; }
    constexpr   const xcore::matrix4& view::getV2C( void ) const noexcept { xassert( m_StateFlags.m_V2C == false ); return m_V2C; }
    constexpr   const xcore::matrix4& view::getV2S( void ) const noexcept { xassert( m_StateFlags.m_V2S == false ); return m_V2S; }
    constexpr   const xcore::matrix4& view::getC2S( void ) const noexcept { xassert( m_StateFlags.m_C2S == false ); return m_C2S; }
    constexpr   const xcore::matrix4& view::getC2W( void ) const noexcept { xassert( m_StateFlags.m_C2W == false ); return m_C2W; }
    constexpr   const xcore::matrix4& view::getC2V( void ) const noexcept { xassert( m_StateFlags.m_C2V == false ); return m_C2V; }
    constexpr   const xcore::matrix4& view::getPixelBased2D( void ) const noexcept { xassert(m_StateFlags.m_PIXELBASED2D == false); return m_PixelBased2D; }

    //-------------------------------------------------------------------------------
    inline  
    const xcore::matrix4& view::getParametric2D ( void ) noexcept
    {
        static const xcore::matrix4 Parametric2D = 
        []{
            xcore::matrix4 M;
            M.setIdentity   ();
            M.setScale      ( xcore::vector3(2.0f,2.0f,1.0f) );
            M.setTranslation( xcore::vector3( -1.0f, -1.0f, 0.1f ) );
            return M;
        }();

        return Parametric2D;
    }

    //-------------------------------------------------------------------------------
    inline  
    const xcore::matrix4& view::getPixelBased2D ( void ) noexcept
    {
        if( m_StateFlags.m_PIXELBASED2D )
        {
            m_StateFlags.m_PIXELBASED2D = false;
            const auto  Viewport    = getViewport();
            const float   XRes        = static_cast<float>(Viewport.getWidth());
            const float   YRes        = static_cast<float>(Viewport.getHeight());
            m_PixelBased2D.setIdentity   ();
            m_PixelBased2D.setScale      ( xcore::vector3( 2.0f/XRes,  2.0f/YRes,  1.0f) );
            m_PixelBased2D.setTranslation( xcore::vector3( -1.0f, -1.0f, 0.1f ) );
        }

        return m_PixelBased2D;
    }
}