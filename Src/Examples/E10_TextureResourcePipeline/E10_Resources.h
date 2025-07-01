#ifndef E10_RESOURCES_H
#define E10_RESOURCES_H
#pragma once

#include "xGPU.h"
#include "../../dependencies/xResourceMgr/src/xresource_mgr.h"

//-----------------------------------------------------------------------

struct resource_mgr_user_data
{
     xgpu::device m_Device          = {};
};

//-----------------------------------------------------------------------

#include "../../dependencies/xtexture.plugin/source/xtexture_xgpu_rsc_loader.h"

#endif