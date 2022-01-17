#ifndef _XGPU_TOOLS_VIEW_H
#define _XGPU_TOOLS_VIEW_H
#pragma once

#ifndef _XCORE_H
    #error "you must include xcore.h before this file gets included"
#endif

namespace xgpu::tools
{
    class view 
    {
    public:
        inline                                  view                    ( void )                                                            noexcept;
        inline      void                        setViewport             ( const xcore::irect& Rect )                                        noexcept;
        inline      const xcore::irect          getViewport             ( void )                                                            noexcept;
        inline      const xcore::irect          getViewport             ( void )                                                    const   noexcept;
        inline      void                        RefreshViewport         ( void )                                                            noexcept;
        inline      void                        UpdateAllMatrices       ( void )                                                            noexcept;

        inline      xcore::vector3              getPosition             ( void )                                                    const   noexcept;
        inline      xcore::vector3              getV2CScales            ( const bool bInfiniteClipping = true )                             noexcept;
        inline      void                        setCubeMapView          ( const int Face, const xcore::matrix4& L2W )                       noexcept;
        inline      void                        setParabolicView        ( const int Side, const xcore::matrix4& L2W )                       noexcept;
        inline      void                        setInfiniteClipping     ( const bool bInfinite )                                            noexcept;
        inline      bool                        getInfiniteClipping     ( void )                                                    const   noexcept;

        inline      void                        setNearZ                ( const float NearZ )                                               noexcept;
        inline      float                       getNearZ                ( void )                                                    const   noexcept;
        inline      void                        setFarZ                 ( const float FarZ )                                                noexcept;
        inline      float                       getFarZ                 ( void )                                                    const   noexcept;
        inline      void                        setFov                  ( const xcore::radian Fov )                                         noexcept;
        constexpr   xcore::radian               getFov                  ( void )                                                    const   noexcept;
        inline      void                        setAspect               ( const float Aspect )                                              noexcept;
        constexpr   float                       getAspect               ( void )                                                    const   noexcept;
        inline      xcore::radian3              getAngles               ( void )                                                    const   noexcept;
        inline      xcore::quaternion           getRotation             ( void )                                                    const   noexcept;
        constexpr   const xcore::matrix4&       getMatrix               ( void )                                                    const   noexcept;
        inline      xcore::vector3              getForward              ( void )                                                    const   noexcept;

        //using SetFov or SetAspect counteracts this (and vice/versa):

        inline      void                        setCustomFrustum        ( const float Left, const float Right, const float Bottom, const float Top) noexcept;
        inline      void                        DisableCustomFrustum    ( void )                                                                    noexcept; 

        inline      view&                       LookAt                  ( const xcore::vector3& To )                                        noexcept;
        inline      view&                       LookAt                  ( const xcore::vector3& From, const xcore::vector3& To)                             noexcept;
        inline      view&                       LookAt                  ( const xcore::vector3& From, const xcore::vector3& To, const xcore::vector3& Up )  noexcept; 
        inline      view&                       LookAt                  ( const float Distance, const xcore::radian3& Angles, const xcore::vector3& At )    noexcept;

        inline      void                        setPosition             ( const xcore::vector3& Position )                                  noexcept;
        inline      void                        setRotation             ( const xcore::radian3& Rotation )                                  noexcept;
        inline      void                        Translate               ( const xcore::vector3& DeltaPos )                                  noexcept;

        inline      xcore::vector3              RayFromScreen           ( const float ScreenX, const float ScreenY )                        noexcept;
        inline      xcore::vector3              getWorldZVector         ( void )                                                    const   noexcept;
        inline      xcore::vector3              getWorldXVector         ( void )                                                    const   noexcept;
        inline      xcore::vector3              getWorldYVector         ( void )                                                    const   noexcept;

        inline  static const xcore::matrix4&    getParametric2D         ( void )                                                            noexcept;
        inline      const xcore::matrix4&       getPixelBased2D         ( void )                                                            noexcept;
        constexpr   const xcore::matrix4&       getPixelBased2D         ( void )                                                    const   noexcept;

        constexpr   const xcore::matrix4&       getV2W                  ( void )                                                    const   noexcept;
        constexpr   const xcore::matrix4&       getW2V                  ( void )                                                    const   noexcept;
        constexpr   const xcore::matrix4&       getW2C                  ( void )                                                    const   noexcept;
        constexpr   const xcore::matrix4&       getW2S                  ( void )                                                    const   noexcept;
        constexpr   const xcore::matrix4&       getV2C                  ( void )                                                    const   noexcept;
        constexpr   const xcore::matrix4&       getV2S                  ( void )                                                    const   noexcept;
        constexpr   const xcore::matrix4&       getC2S                  ( void )                                                    const   noexcept;
        constexpr   const xcore::matrix4&       getC2W                  ( void )                                                    const   noexcept;
        constexpr   const xcore::matrix4&       getC2V                  ( void )                                                    const   noexcept;

        inline      const xcore::matrix4&       getW2V                  ( void )                                                            noexcept;
        inline      const xcore::matrix4&       getW2C                  ( void )                                                            noexcept;
        inline      const xcore::matrix4&       getW2S                  ( void )                                                            noexcept;
        inline      const xcore::matrix4&       getV2C                  ( void )                                                            noexcept;
        inline      const xcore::matrix4&       getV2S                  ( void )                                                            noexcept;
        inline      const xcore::matrix4&       getC2S                  ( void )                                                            noexcept;
        inline      const xcore::matrix4&       getC2W                  ( void )                                                            noexcept;
        inline      const xcore::matrix4&       getC2V                  ( void )                                                            noexcept;
     
        inline      float                       getScreenSize           ( const xcore::vector3& Position, const float Radius )              noexcept;

    //-------------------------------------------------------------------------------

    protected:

        inline      void                        UpdatedView             ( void )                                                            noexcept;
        inline      void                        UpdatedClip             ( void )                                                            noexcept;
        inline      void                        UpdatedScreen           ( void )                                                            noexcept;

    //-------------------------------------------------------------------------------

    protected:

        union state_flags
        {
            enum mask : std::uint32_t
            { W2V             = (1 <<  0)
            , W2C             = (1 <<  1)
            , W2S             = (1 <<  2)
            , V2C             = (1 <<  3)
            , V2S             = (1 <<  4)
            , C2V             = (1 <<  5)
            , C2S             = (1 <<  6)
            , C2W             = (1 <<  7)
            , PIXELBASED2D    = (1 <<  8)
            , INF_CLIP        = (1 <<  9)
            , USE_FOV         = (1 << 10)
            , DEFAULT         = 0xffffffff
            };


            std::uint32_t   m_Value = mask::DEFAULT;

            struct
            {
                std::uint32_t
                      m_W2V:1
                    , m_W2C:1
                    , m_W2S:1
                    , m_V2C:1
                    , m_V2S:1
                    , m_C2V:1
                    , m_C2S:1
                    , m_C2W:1
                    , m_PIXELBASED2D:1
                    , m_INF_CLIP:1
                    , m_USE_FOV:1
                    , m_VIEWPORT:8;
            };

        };

    //-------------------------------------------------------------------------------

    protected:

        xcore::matrix4      m_V2W               {};
        xcore::matrix4      m_W2V               {};
        xcore::matrix4      m_W2C               {};
        xcore::matrix4      m_W2S               {};
        xcore::matrix4      m_V2C               {};
        xcore::matrix4      m_V2S               {};
        xcore::matrix4      m_C2S               {};
        xcore::matrix4      m_C2W               {};
        xcore::matrix4      m_C2V               {};
        xcore::matrix4      m_PixelBased2D      {};
        state_flags         m_StateFlags        {};
        float               m_NearZ             {};
        float               m_FarZ              {};
        xcore::radian       m_Fov               {};
        float               m_Aspect            {};

        // when not using FOV, these are used instead
        float               m_FrustumLeft       {};
        float               m_FrustumRight      {};
        float               m_FrustumBottom     {};
        float               m_FrustumTop        {};
    };
}

#include "xgpu_view_inline.h"

#endif
