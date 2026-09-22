#ifndef XEDITOR_THUMBNAIL_H
#define XEDITOR_THUMBNAIL_H
#pragma once

// The "ask the editor to draw itself" half of the resource-thumbnail system: a resource type registers a
// small, persistent (constructed once, reused for every resource of that type) renderer that draws one
// resource into whatever render target is currently bound - pure xgpu::cmd_buffer work, no ImGui, no
// document/undo, no window. This is deliberately the same shape a future read-only viewer's own per-type
// renderer would have (mesh_preview::Draw(CmdBuffer,W,H) already has this shape for material/geom preview
// panels), so a type that grows real viewer support shares this instead of duplicating it. See
// xeditor_thumbnail_cache.h for what actually calls into this (the disk cache, the runtime atlas, the
// generation pipeline) - this header only knows how to reach a type's own renderer, nothing about caching.
//
// A resource type opts in next to its existing auto_register_resource_editor:
//   inline const xeditor::auto_register_thumbnail_renderer g_ThumbReg{ type_guid_v, []{ return std::make_unique<my_thumbnail_renderer>(); } };
// A type with no registration simply never gets asked - the browser keeps showing its type icon.
#include "dependencies/xresource_guid/source/xresource_guid.h"

#include <functional>
#include <memory>
#include <unordered_map>

namespace xgpu { struct device; struct cmd_buffer; }

namespace xeditor
{
    // One resource type's thumbnail renderer. Draw is called with a render pass already begun on a small
    // (128x128) offscreen target; it just needs to bind its own pipeline/samplers and issue the draw -
    // exactly like mesh_preview::Draw or any other panel's own draw code, just headless.
    struct thumbnail_renderer
    {
        virtual              ~thumbnail_renderer( void )                                                   noexcept = default;
        virtual bool          Init             ( xgpu::device& Device )                                    noexcept = 0;
        virtual bool          Draw             ( xgpu::device& Device, xgpu::cmd_buffer& CmdBuffer, xresource::full_guid Guid ) noexcept = 0;
    };

    using thumbnail_renderer_factory = std::function<std::unique_ptr<thumbnail_renderer>()>;

    inline std::unordered_map<xresource::type_guid, thumbnail_renderer_factory>& ThumbnailRendererFactories() noexcept
    {
        static std::unordered_map<xresource::type_guid, thumbnail_renderer_factory> s_Map;
        return s_Map;
    }

    // A resource type's editor header declares one of these at namespace scope, next to its
    // auto_register_resource_editor: `inline const xeditor::auto_register_thumbnail_renderer g_ThumbReg{ type, factory };`
    struct auto_register_thumbnail_renderer
    {
        auto_register_thumbnail_renderer(xresource::type_guid Type, thumbnail_renderer_factory Factory) noexcept
        {
            ThumbnailRendererFactories()[Type] = std::move(Factory);
        }
    };
}

#endif // XEDITOR_THUMBNAIL_H
