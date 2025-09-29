#ifndef E10_RESOURCES_H
#define E10_RESOURCES_H
#pragma once

#include "source/xGPU.h"
#include "dependencies/xresource_mgr/source/xresource_mgr.h"

//-----------------------------------------------------------------------

struct resource_mgr_user_data
{
     xgpu::device m_Device          = {};
};

//-----------------------------------------------------------------------

#include "source/xtexture_xgpu_rsc_loader.h"

#endif