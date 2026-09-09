#ifndef E10_PLUGIN_ICON_ATLAS_H
#define E10_PLUGIN_ICON_ATLAS_H
#pragma once

// Builds the single shared texture (asset_plugins_db::m_IconAtlas) that the Asset Browser draws
// every plugin's 128x128 PNG icon(s) from (E10_asset_browser_virtual_tree_tab.h's WrappedButton2 and
// E10_asset_browser_compiler_tab.h) - replaces the old per-plugin font-glyph (m_Icon) rendering.
//
// Direct user design: "the icons can be loading in different threads into xbitmaps, which later
// are combined into the final atlas" - each icon file is loaded on its own std::async worker (same
// background-work convention this project already uses for Game.dll rebuilds, see
// E29_GamePluginBuild.h's own comment on why std::async), then every resulting xbitmap is packed
// with the pre-existing, previously-unused xbmp::tools::atlas::Pack (dependencies/xbmp_tools) into
// ONE bitmap, uploaded once as a single xgpu::texture. Rebuilt fresh every launch (~15 tiny 128x128
// loads is sub-second work) - no on-disk atlas cache, per explicit user direction.
//
// A plugin can list MORE than one icon in m_IconPaths - direct user design: "the folder should have
// 2 icons... one empty and one full... the asset browser should know which one to render" - so this
// packs every (plugin, icon-index) pair as its own atlas entry, not one entry per plugin.

#include <future>
#include "dependencies/xbmp_tools/src/xbmp_tools.h"
#include "source/Tools/xgpu_xcore_bitmap_helpers.h"
#include "E10_PluginMgr.h"

namespace e10
{
    inline xerr BuildPluginIconAtlas(asset_plugins_db& Db, xgpu::device& Device) noexcept
    {
        //
        // Flatten every (plugin, icon-index) pair into one job list
        //
        struct job { int m_PluginIdx; int m_IconIdx; std::wstring m_Path; };
        std::vector<job> Jobs;
        for (int p = 0, end = static_cast<int>(Db.m_lPlugins.size()); p < end; ++p)
        {
            auto& Plugin = Db.m_lPlugins[p];
            Plugin.m_IconUVs.assign(Plugin.m_IconPaths.size(), {});
            for (int i = 0, iend = static_cast<int>(Plugin.m_IconPaths.size()); i < iend; ++i)
            {
                if (Plugin.m_IconPaths[i].empty()) continue;
                Jobs.push_back({ p, i, std::format(L"{}\\{}", Plugin.m_PluginPath, Plugin.m_IconPaths[i]) });
            }
        }

        const int N = static_cast<int>(Jobs.size());
        if (N == 0) return {};

        //
        // Load every icon in parallel, one worker each
        //
        std::vector<std::future<xbitmap>> Futures;
        Futures.reserve(N);
        for (auto& Job : Jobs)
        {
            Futures.push_back(std::async(std::launch::async, [Path = Job.m_Path]() noexcept
            {
                xbitmap Bmp;
                if (auto Err = xbmp::tools::loader::LoadSTDImage(Bmp, Path); Err)
                    Bmp = {};   // leave empty - handled as a zero-size rect below
                return Bmp;
            }));
        }

        std::vector<xbitmap> Bitmaps(N);
        for (int i = 0; i < N; ++i)
            Bitmaps[i] = Futures[i].get();

        //
        // Pack every icon into one atlas
        //
        std::vector<xbmp::tools::atlas::rect_xywhf>  Rects(N);
        std::vector<xbmp::tools::atlas::rect_xywhf*> RectPtrs(N);
        for (int i = 0; i < N; ++i)
        {
            const int W = static_cast<int>(Bitmaps[i].getWidth());
            const int H = static_cast<int>(Bitmaps[i].getHeight());
            Rects[i] = xbmp::tools::atlas::rect_xywhf(0, 0, W > 0 ? W : 1, H > 0 ? H : 1);
            RectPtrs[i] = &Rects[i];
        }

        std::vector<xbmp::tools::atlas::bin> Bins;
        xbmp::tools::atlas                   Packer;
        if (false == Packer.Pack(RectPtrs.data(), N, 2048, Bins, xbmp::tools::atlas::pack_mode::POWER_OF_TWO_SQUARE)
            || Bins.empty())
        {
            return xerr::create_f<xbmp::tools::state, "Failed to pack the plugin icon atlas">();
        }

        // A handful of icons at 128x128 comfortably fit a single 2048-max bin - every icon is
        // expected in Bins[0]. If that ever stops being true (many more plugin icons added later),
        // icons in any later bin simply keep their zeroed UVs (no icon drawn) rather than crashing.
        const auto& Bin = Bins[0];
        const int AtlasW = Bin.m_Size.m_W;
        const int AtlasH = Bin.m_Size.m_H;

        xbitmap Atlas;
        Atlas.CreateBitmap(static_cast<std::uint32_t>(AtlasW), static_cast<std::uint32_t>(AtlasH));
        auto AtlasPixels = Atlas.getMip<xcolori>(0);

        for (auto* pRect : Bin.m_Rects)
        {
            const int i = static_cast<int>(pRect - Rects.data());
            auto& Src = Bitmaps[i];
            if (Src.getWidth() == 0 || Src.getHeight() == 0) continue;
            if (Src.getFormat() != xbitmap::format::R8G8B8A8) continue; // icons are authored as RGBA PNGs

            auto SrcPixels = Src.getMip<xcolori>(0);
            const int SrcW = static_cast<int>(Src.getWidth());
            const int SrcH = static_cast<int>(Src.getHeight());

            for (int y = 0; y < SrcH; ++y)
            for (int x = 0; x < SrcW; ++x)
            {
                const int dx = pRect->m_bFlipped ? y : x;
                const int dy = pRect->m_bFlipped ? x : y;
                AtlasPixels[(pRect->m_Y + dy) * AtlasW + (pRect->m_X + dx)] = SrcPixels[y * SrcW + x];
            }

            auto& Job = Jobs[i];
            auto& UV  = Db.m_lPlugins[Job.m_PluginIdx].m_IconUVs[Job.m_IconIdx];
            UV.m_U0 = static_cast<float>(pRect->m_X) / AtlasW;
            UV.m_V0 = static_cast<float>(pRect->m_Y) / AtlasH;
            UV.m_U1 = static_cast<float>(pRect->m_X + SrcW) / AtlasW;
            UV.m_V1 = static_cast<float>(pRect->m_Y + SrcH) / AtlasH;
        }

        if (auto Err = xgpu::tools::bitmap::Create(Db.m_IconAtlas, Device, Atlas); Err)
            return xerr::create_f<xbmp::tools::state, "Failed to upload the plugin icon atlas texture">();

        return {};
    }
}

#endif
