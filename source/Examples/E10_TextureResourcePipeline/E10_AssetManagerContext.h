#ifndef ASSERT_MGR_CONTEXT_H
#define ASSERT_MGR_CONTEXT_H
#pragma once

#include "E10_AssetMgr.h"
#include "dependencies/xtexture.plugin/dependencies/xresource_guid/source/xresource_guid.h"
//#include "xundo_system.h"

namespace e10
{

    struct asset_mgr_context
    {
        struct library
        {
            xresource::full_guid                m_Guid;
            std::vector<xresource::full_guid>   m_ActiveSelection;
        };


    };

} // namespace e10

#endif
