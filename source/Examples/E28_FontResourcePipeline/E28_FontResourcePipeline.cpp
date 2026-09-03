#include "source/xGPU.h"

#include "dependencies/xproperty/source/xcore/my_properties.h"
#include "dependencies/xproperty/source/examples/imgui/xPropertyImGuiInspector.h"
#include "dependencies/xstrtool/source/xstrtool.h"

#include "source/tools/xgpu_xcore_bitmap_helpers.h"
#include <filesystem>
#include <iostream>
#include <atomic>
#include <mutex>

#define XRESOURCE_PIPELINE_NO_COMPILER
#include "dependencies/xresource_pipeline_v2/source/xresource_pipeline.h"
#include "source/xstrtool.h"
#include "source/Examples/E10_TextureResourcePipeline/E10_Resources.h"
#include "source/Examples/E10_TextureResourcePipeline/E10_AssetMgr.h"
#include "source/Examples/E10_TextureResourcePipeline/E10_AssetBrowser.h"

#include "plugins/xtexture.plugin/source/xtexture_xgpu_rsc_loader.h"

#include "plugins/xfont.plugin/source/xfont_rsc_descriptor.h"
#include "plugins/xfont.plugin/source/xfont_xgpu_rsc_loader.h"
#include "plugins/xfont.plugin/source/xfont_xgpu_rsc_loader.cpp"

#include "imgui_internal.h"

// Compiled SPIR-V for the MSDF text-preview shader pair - generated at build time from the two
// .glsl files sitting next to this .cpp (see the top-level CMakeLists.txt's FRAG/VERT_SHADER_SOURCES
// lists), same mechanism every other example's shader pair uses (e.g. E22_FramebufferTarget's
// draw_vert.h/draw_frag.h).
//
// Declared as std::uint32_t[] (NOT std::array{...} CTAD) - each generated .h is a flat list of plain
// hex word literals, and a SPIR-V word's top bit is legitimately set whenever the shader has ANY
// negative float constant (its IEEE-754 bit pattern has the sign bit on) - a hex literal like that is
// typed `unsigned int` by the language, not `int`. std::array{...}'s CTAD locks onto the FIRST
// literal's type (`int`, since every header starts with the small 0x07230203 magic number), so the
// moment a later word needs the sign bit, list-initialization fails as a narrowing conversion. This
// bit me for real adding E28_msdf_frag.glsl's kBevelLightDir = vec2(-1.0, -0.5). uint32_t sidesteps
// it entirely: every 32-bit literal - whichever of int/unsigned int the compiler picked for it -
// converts to uint32_t without narrowing, since the VALUE always fits.
inline std::span<const std::int32_t> AsShaderSpan(const std::uint32_t* pWords, std::size_t Count) noexcept
{
    // xgpu::shader::setup::raw_data wants int32_t specifically - reinterpret, not convert, since
    // these are raw bits (SPIR-V words), not values with sign semantics.
    return { reinterpret_cast<const std::int32_t*>(pWords), Count };
}
constexpr std::uint32_t g_MsdfVertSPVWords[] =
{
    #include "E28_msdf_vert.h"
};
constexpr std::uint32_t g_MsdfFragSPVWords[] =
{
    #include "E28_msdf_frag.h"
};
// Dedicated shader pair for the glyph-bounds debug overlay - see E28_wire_vert.glsl's own comment.
constexpr std::uint32_t g_WireVertSPVWords[] =
{
    #include "E28_wire_vert.h"
};
constexpr std::uint32_t g_WireFragSPVWords[] =
{
    #include "E28_wire_frag.h"
};

//-----------------------------------------------------------------------------------
//
// E28 - Font resource pipeline editor: asset browser + compile queue + property inspector
// (all shared E10 infrastructure, see E23_Skeleton_Editor.cpp for the pattern this mirrors)
// plus a Font-specific preview panel showing the compiled MSDF atlas (and companion SDF
// texture, when the descriptor's StoreSDF is on).
//
// What this does NOT do yet: render actual shaped/kerned text. That needs a small runtime
// text-layout module (walk a string, look up each codepoint via xfont_rsc::font's perfect
// hash, accumulate advance+kerning+bearing into quads) plus an MSDF sampling shader (median-
// of-3 reconstruction, fwidth()-based AA) - neither exists yet. The atlas/SDF preview below
// needs none of that: xfont::rt::m_pAtlasTexture/m_pSDFTexture are already-resolved
// xgpu::texture* (loaded through the ordinary xresource reference mechanism), so they render
// exactly like any other texture preview (E21_StaticGeomEditor's thumbnail pattern).
//
//-----------------------------------------------------------------------------------------

namespace e28
{
    static void Debugger(std::string_view View)
    {
        printf("%s\n", View.data());
    }

    //---------------------------------------------------------------------------

    struct font_state
    {
        xrsc::font_ref          m_Ref            = {};
        e10::library::guid      m_LibraryGUID    = {};
        xresource::full_guid    m_InfoGUID       = {};
        std::wstring             m_DescriptorPath = {};
        std::wstring             m_ResourcePath   = {}; // where the compiled binary would be - see LoadFont; checked with std::filesystem::exists before ever calling xresource::mgr::getResource, exactly like E10's own SelectedDescriptor.m_ResourcePath check, since a freshly selected/created font is routinely not compiled yet
        xfont_rsc::descriptor   m_Descriptor     = {}; // backs the "Font Properties" inspector - see LoadFont

        // Compile/save tracking - mirrors E23_SkeletonEditor's skeleton_state. Saving the
        // descriptor (SaveDescriptor) is what actually triggers a recompile - a background
        // file-watcher in the library manager picks up the change and runs xfont_compiler,
        // broadcasting progress via e10::g_LibMgr.m_OnCompilationState (see the registration
        // in E28_Example) to whichever shared_ptr<log> this GUID's compile currently owns.
        // m_Log is reassigned from CallBackForCompilation, which runs SYNCHRONOUSLY on whatever thread
        // calls m_OnCompilationState.NotifyAll() - that's CompilingThreadWorker, a background compile-
        // worker thread, not the render thread (xdelegate::thread_safe's "thread safe" only protects its
        // own subscriber list, not what a subscriber's body touches). The main thread reads/reassigns
        // this same shared_ptr every frame. An unsynchronized shared_ptr read/write race between those
        // two threads corrupted STL <memory> internals and crashed the app - confirmed via a real access
        // violation (0xc0000005) resolved to <memory> line 1151 across repeated crashes. Always go
        // through GetLog()/SetLog() below, never touch m_Log directly, and never hold a lock on *m_Log
        // (its own content-level xcontainer::lock, a separate and still-necessary protection) while also
        // holding m_LogMutex - copy the shared_ptr out via GetLog() first, then lock the copy's content.
        mutable std::mutex                                        m_LogMutex = {};
        std::shared_ptr<e10::compilation::historical_entry::log> m_Log = {};

        std::shared_ptr<e10::compilation::historical_entry::log> GetLog() const
        {
            std::lock_guard Lk(m_LogMutex);
            return m_Log;
        }

        void SetLog(std::shared_ptr<e10::compilation::historical_entry::log> NewLog)
        {
            std::lock_guard Lk(m_LogMutex);
            m_Log = std::move(NewLog);
        }

        // Also written from the background compile-worker thread (CallBackForCompilation) and read/
        // written on the main thread - atomic<bool> is enough here (unlike m_Log, there's no pointed-to
        // object whose lifetime an unsynchronized read could corrupt).
        std::atomic<bool> m_bReload = false;
        std::atomic<bool> m_bErrors = false;
        // Set the moment a compile is observed STARTING (not just on success) - the font's own compiled
        // binary, and everything it references (its virtual atlas/SDF textures), are about to be
        // overwritten on disk out from under whatever is currently loaded. The font itself is what owns
        // releasing its texture refs (xfont_xgpu_rsc_loader::Destroy already does this correctly) - our
        // job is only to stop holding a reference to the font itself the moment we know it's stale, so
        // nothing here keeps using a pointer whose target is being replaced. Checked and acted on early
        // in the main loop, before anything touches FontState.m_Ref for the frame.
        std::atomic<bool> m_bCompileDetected = false;
        // Frames left to wait before touching this font's atlas/SDF textures again after a reload -
        // both the raw ShowZoomableImage(ImGui::Image) calls and text_renderer's own pipeline_instance
        // crashed when used immediately after a live recompile (the compile-completion notification
        // that triggers the reload arrives from the background compile-worker thread, not the render
        // thread - see text_renderer::Reset's own comment for the fuller explanation). Not reset by
        // clear() - it's set deliberately at the one call site that detects a reload, not on every load.
        int m_ReloadCooldown = 0;

        // LoadFont() is called MID-FRAME (asset reselection at line ~961, m_bReload's own reload at
        // line ~973) - by that point the Font Preview window has already run ImGui::Image() for
        // m_Ref's CURRENT textures this same frame, and xgpu::tools::imgui::Render() (which actually
        // dereferences those texture pointers to build GPU commands) hasn't run yet. Releasing the
        // old ref immediately there frees the texture out from under that not-yet-processed draw
        // call - confirmed via cdb.exe as a genuine use-after-free (std::_Ref_count_base::_Incref on
        // a freed xgpu::details::texture_handle control block, 0xFEEEFEEE), all on the main thread -
        // not a cross-thread race despite looking like one. LoadFont stashes the old ref here instead
        // of releasing it directly; drained at the very top of the NEXT frame, after this frame's own
        // Render() has already fully consumed any pointer into it.
        xrsc::font_ref m_PendingRelease = {};

        // The font's own m_bReload only reflects the font's OWN top-level compile finishing - its
        // atlas/SDF virtual textures are a SEPARATE, later-enqueued cascade compile (see
        // xfont_compiler's own dependency emission), so the font can finish (and reload) before its
        // textures have. Deterministic virtual-resource GUIDs mean the texture's compiled file
        // already EXISTS on disk from the PREVIOUS compile round, so exists() alone can't tell fresh
        // from stale - only a compiled-file timestamp newer than when THIS compile started can.
        //
        // MUST be stamped on the BACKGROUND compile-worker thread, synchronously, the instant
        // COMPILING is observed (see SetCompileStartTime()'s call site in CallBackForCompilation) -
        // NOT lazily via now() when the main thread later processes m_bCompileDetected. A whole
        // font+cascade round trip measured as fast as ~0.5-1.5s; if the main thread doesn't get to
        // the flag until after that cascade has already finished and written its file, a now()
        // stamped at THAT point lands AFTER the very write it's meant to gate, so the freshness
        // check below misjudges an already-fresh file as stale forever (it won't be touched again
        // until the next compile) - the observed symptom was needing to press Compile a second time
        // to actually see it, which happened to smuggle in a new file write timed OK.
        mutable std::mutex               m_CompileStartTimeMutex = {};
        std::filesystem::file_time_type  m_CompileStartTime      = {};

        std::filesystem::file_time_type GetCompileStartTime() const
        {
            std::lock_guard Lk(m_CompileStartTimeMutex);
            return m_CompileStartTime;
        }

        void SetCompileStartTime(std::filesystem::file_time_type T)
        {
            std::lock_guard Lk(m_CompileStartTimeMutex);
            m_CompileStartTime = T;
        }

        bool empty() const noexcept { return m_InfoGUID.empty(); }

        void clear()
        {
            m_LibraryGUID.clear();
            m_InfoGUID.clear();
            m_DescriptorPath.clear();
            m_ResourcePath.clear();
            m_Descriptor = {};
            SetLog(std::make_shared<e10::compilation::historical_entry::log>(e10::compilation::historical_entry::communication{ .m_Result = e10::compilation::historical_entry::result::SUCCESS }));
            m_bReload    = false;
            m_bErrors    = false;
        }

        void SaveDescriptor()
        {
            xproperty::settings::context Context;
            if (auto Err = m_Descriptor.Serialize(false, m_DescriptorPath, Context); Err)
                assert(false);
        }
    };

    //---------------------------------------------------------------------------

    void GenerateDescriptorPath(font_state& State, const std::wstring& InfoPath)
    {
        State.m_DescriptorPath = InfoPath;
        if (auto Pos = InfoPath.find(L"info.txt"); Pos != std::wstring::npos)
            State.m_DescriptorPath.replace(Pos, std::wstring_view(L"info.txt").length(), L"Descriptor.txt");
    }

    //---------------------------------------------------------------------------

    // Rebuilds the inspector's binding every call, exactly like E10/E23's own selection handlers do
    // (clear() + AppendEntity() + AppendEntityComponent() against the just-loaded descriptor) -
    // see this file's own top-of-inspector comment for why binding once at startup against an
    // empty descriptor is what to avoid.
    void LoadFont(font_state& State, e10::library::guid LibraryGUID, xresource::full_guid InfoGUID, xproperty::inspector& Inspector)
    {
        // Deferred, not immediate - see font_state::m_PendingRelease's own comment. LoadFont runs
        // mid-frame, after this frame's Font Preview window may have already queued an ImGui::Image()
        // draw call against State.m_Ref's current textures.
        if (State.m_Ref.empty() == false)
            State.m_PendingRelease = State.m_Ref;
        State.clear();
        State.m_Ref.clear();

        State.m_LibraryGUID = LibraryGUID;
        State.m_InfoGUID    = InfoGUID;

        e10::g_LibMgr.getNodeInfo(State.m_LibraryGUID, State.m_InfoGUID, [&](e10::library_db::info_node& NodeInfo)
        {
            GenerateDescriptorPath(State, NodeInfo.m_Path);
        });

        if (!State.m_DescriptorPath.empty() && std::filesystem::exists(State.m_DescriptorPath))
        {
            xproperty::settings::context Context;
            if (auto Err = State.m_Descriptor.Serialize(true, State.m_DescriptorPath, Context); Err)
                assert(false);
        }

        State.m_ResourcePath = xresource::g_Mgr.getResourcePath(InfoGUID, L"Font");

        Inspector.clear();
        Inspector.AppendEntity();
        Inspector.AppendEntityComponent(*State.m_Descriptor.getProperties(), &State.m_Descriptor);

        State.m_Ref.m_Instance = InfoGUID.m_Instance;
    }

    //---------------------------------------------------------------------------

    // Per-panel pan/zoom state for ShowZoomableImage - mirrors E10_TextureResourcePipeline's own 2D
    // viewer (m_2DMouseScale/m_2DMouseTranslate: wheel zooms toward the cursor, drag pans, a
    // "Recenter" button resets pan only). Two independent instances are kept - one for the raw
    // Texture preview, one for Live Text - so zooming into the atlas doesn't also zoom the text.
    struct pan_zoom
    {
        float        m_Zoom { 1.0f };
        xmath::fvec2 m_Pan  { 0.0f, 0.0f };
    };

    // Shows an already-rendered texture inside a scrollable/zoomable canvas: mouse wheel zooms
    // toward the cursor, left-drag pans, "Recenter" resets pan back to centered (zoom untouched) -
    // same interaction model as the Texture Editor's own 2D preview. TexW/TexH are the pixel
    // dimensions the image should be drawn at, at Zoom=1 (UVMin/UVMax let the caller show a sub-rect,
    // e.g. text_renderer's scene texture, which is bucket-snapped larger than its actual content).
    // pOverlayRectsPx (optional): each ImVec4{x,y,w,h} is one glyph's packed atlas rect, in the SAME
    // texture-pixel space as TexW/TexH (i.e. relative to the full un-cropped texture, matching
    // UVMin=0/UVMax=1's own space) - drawn as red outlines using the same pan/zoom transform as the
    // image itself, so they land exactly on the glyph they describe at any zoom. Lets RenderSettings/
    // Debug/ShowGlyphBounds double as a packer-overlap check: two rects visibly overlapping here means
    // the atlas packer placed them on top of each other, not a LayoutText/shader bug.
    void ShowZoomableImage(const char* pID, void* pTextureHandle, int TexW, int TexH, pan_zoom& View, ImVec2 UVMin = ImVec2(0, 0), ImVec2 UVMax = ImVec2(1, 1), const std::vector<ImVec4>* pOverlayRectsPx = nullptr)
    {
        if (pTextureHandle == nullptr || TexW <= 0 || TexH <= 0)
        {
            ImGui::TextDisabled("(not available)");
            return;
        }

        ImGui::PushID(pID);
        ImGui::Text("%d x %d", TexW, TexH);
        ImGui::SameLine();
        if (ImGui::SmallButton("Recenter")) View.m_Pan = { 0.0f, 0.0f };
        ImGui::SameLine();
        ImGui::Text("Zoom: %.2fx", View.m_Zoom);

        ImGui::BeginChild("##canvas", ImGui::GetContentRegionAvail(), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);

        const ImVec2 CanvasP0 = ImGui::GetCursorScreenPos();
        // InvisibleButton below asserts on a zero-size argument - GetContentRegionAvail() can
        // legitimately be (0,0) here for a frame or two (window collapsed, still settling right after
        // a font reload swaps which preview windows are visible) so clamp rather than pass it through raw.
        const ImVec2 Avail        = ImGui::GetContentRegionAvail();
        const ImVec2 CanvasSize   = ImVec2(std::max(Avail.x, 1.0f), std::max(Avail.y, 1.0f));
        const ImVec2 CanvasCenter = ImVec2(CanvasP0.x + CanvasSize.x * 0.5f, CanvasP0.y + CanvasSize.y * 0.5f);

        ImGui::InvisibleButton("##canvas_btn", CanvasSize);
        const bool bHovered = ImGui::IsItemHovered();

        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            const ImVec2 Delta = ImGui::GetIO().MouseDelta;
            View.m_Pan.m_X += Delta.x;
            View.m_Pan.m_Y += Delta.y;
        }

        if (bHovered && ImGui::GetIO().MouseWheel != 0.0f)
        {
            const float OldZoom = View.m_Zoom;
            View.m_Zoom = std::clamp(View.m_Zoom * (1.0f + ImGui::GetIO().MouseWheel * 0.1f), 0.05f, 40.0f);

            // Zoom toward the cursor, not the canvas center - keep whatever point is currently under
            // the mouse fixed on screen by rescaling its offset from the pan origin by the same ratio.
            const ImVec2 Mouse = ImGui::GetIO().MousePos;
            const ImVec2 MouseRelCenter { Mouse.x - CanvasCenter.x, Mouse.y - CanvasCenter.y };
            const float  Ratio = View.m_Zoom / OldZoom;
            View.m_Pan.m_X = MouseRelCenter.x - (MouseRelCenter.x - View.m_Pan.m_X) * Ratio;
            View.m_Pan.m_Y = MouseRelCenter.y - (MouseRelCenter.y - View.m_Pan.m_Y) * Ratio;
        }

        const float DrawW = TexW * View.m_Zoom;
        const float DrawH = TexH * View.m_Zoom;
        const ImVec2 ImgMin(CanvasCenter.x + View.m_Pan.m_X - DrawW * 0.5f, CanvasCenter.y + View.m_Pan.m_Y - DrawH * 0.5f);
        const ImVec2 ImgMax(ImgMin.x + DrawW, ImgMin.y + DrawH);
        ImGui::GetWindowDrawList()->AddImage(pTextureHandle, ImgMin, ImgMax, UVMin, UVMax);

        if (pOverlayRectsPx != nullptr)
        {
            auto* pDrawList = ImGui::GetWindowDrawList();
            for (const auto& R : *pOverlayRectsPx)
            {
                const ImVec2 RMin(ImgMin.x + R.x * View.m_Zoom, ImgMin.y + R.y * View.m_Zoom);
                const ImVec2 RMax(ImgMin.x + (R.x + R.z) * View.m_Zoom, ImgMin.y + (R.y + R.w) * View.m_Zoom);
                pDrawList->AddRect(RMin, RMax, IM_COL32(255, 0, 0, 255));
            }
        }

        ImGui::EndChild();
        ImGui::PopID();
    }

    //---------------------------------------------------------------------------

    // Every glyph's packed atlas rect, in texture-pixel space - for the Atlas view's own
    // RenderSettings/Debug/ShowGlyphBounds overlay (see ShowZoomableImage's own comment on why: two
    // rects visibly overlapping here is a packer bug, not a rendering one). BITMAP has its glyphs
    // split across size_groups with no single flat array; MTSDF/SDF already expose one via Glyphs().
    std::vector<ImVec4> CollectGlyphAtlasRects(const xfont_rsc::font& Font)
    {
        std::vector<ImVec4> Rects;
        auto AddIfInk = [&](const xfont_rsc::glyph& G)
        {
            if (G.m_AtlasW > 0 && G.m_AtlasH > 0)
                Rects.emplace_back(static_cast<float>(G.m_AtlasX), static_cast<float>(G.m_AtlasY), static_cast<float>(G.m_AtlasW), static_cast<float>(G.m_AtlasH));
        };

        if (Font.m_OutputType == xfont_rsc::output_type::BITMAP)
        {
            const char* pRegionStart = Font.SizeGroupRegionStart();
            for (std::uint32_t g = 0; g < Font.m_nSizeGroups; ++g)
            {
                const auto& Group  = Font.SizeGroups()[g];
                const auto* pGlyph = xfont_rsc::font::GlyphsInSizeGroup(pRegionStart, Group);
                for (std::uint32_t i = 0; i < Group.m_nGlyphs; ++i) AddIfInk(pGlyph[i]);
            }
        }
        else
        {
            const auto* pGlyph = Font.Glyphs();
            for (std::uint32_t i = 0; i < Font.m_nGlyphs; ++i) AddIfInk(pGlyph[i]);
        }
        return Rects;
    }

    //---------------------------------------------------------------------------

    // Editor-wide preview controls - NOT per-font asset state (unlike font_state::m_Descriptor),
    // so binding this inspector once at startup is safe: it never transitions from an empty/
    // default object to a later-populated one the way an unselected font_state::m_Descriptor did,
    // and its own property paths ("RenderSettings/...") are unique to this struct, so there's no
    // cross-binding cache collision risk either (see LoadFont's own comment on that bug).
    struct render_settings
    {
        // The text mesh is always generated at this resolution (px per em) - not user-adjustable any
        // more. Viewing bigger/smaller is now a pure display-level pan/zoom (see pan_zoom/
        // ShowZoomableImage), same as looking at any other texture in the Texture Editor, rather than
        // a "Scale"/"Zoom" pair that re-rendered the MSDF mesh itself - that distinction was more
        // confusing than useful in practice.
        static constexpr float kBasePixelsPerEm = 64.0f;

        // Mirrors the CURRENTLY SELECTED font's own OutputType - render_settings itself has no idea
        // which font is selected (that's tracked in font_state, outside this struct - see this
        // struct's own top comment), but several Effects below only make sense for a real distance
        // field (BITMAP is plain rasterized coverage, no signed distance to threshold-shift or take a
        // gradient of) - the main loop sets this once per frame before the inspector renders, purely
        // so THIS struct's own dynamic_flags lambdas (which only ever see a render_settings&) have
        // something to check.
        xfont_rsc::output_type m_CurrentFontOutputType{ xfont_rsc::output_type::MTSDF };

        std::string m_Text            { "Hello, World!" };
        // BITMAP only - which baked size to preview, snapped to the closest one actually compiled (see
        // xfont_rsc::font::FindClosestSizeGroup) - a font may have baked several (8/12/16px etc.), so
        // this replaces the old "just always ask for kBasePixelsPerEm and hope" auto-pick.
        float       m_BitmapPreviewSize{ 16.0f };
        bool        m_bShowOutline    { false };  // only meaningful when the selected font's SDF companion exists (StoreSDF was on at compile time)
        float       m_OutlineWidth    { 0.08f };  // em units
        bool        m_bBold           { false };  // synthetic bold - shifts the fill threshold, no separate bold glyph data needed
        float       m_FontWeight      { 0.03f };  // em-equivalent units (converted to screen px the same way OutlineWidth is)
        bool        m_bShowShadow     { false };  // drop shadow - redraws the same real glyph mesh translated, then the normal fill on top
        float       m_ShadowOffsetX   { 0.04f };  // em units
        float       m_ShadowOffsetY   { -0.05f }; // em units
        bool        m_bBevel          { false };  // pseudo-lit edge bevel, from the SDF's own screen-space gradient - MTSDF/SDF only, no-op on BITMAP
        float       m_BevelWeight     { 0.06f };  // em-equivalent units (converted to screen px the same way OutlineWidth is)
        bool        m_bItalic          { false }; // synthetic italic - vertex-level shear (real skew, not a UV trick), see E28_msdf_vert.glsl
        float       m_ItalicShear      { 0.2f };  // slope (dx per unit y), dimensionless
        bool        m_bShowGlyphBounds{ false };  // debug: draws the real glyph mesh in red wireframe, on top of the rendered text

        XPROPERTY_DEF
        ( "RenderSettings", render_settings
        , obj_member<"Text",  &render_settings::m_Text,  member_help<"The string to render in the Font Preview window.">>
        , obj_member<"BitmapPreviewSize", &render_settings::m_BitmapPreviewSize
            , member_ui<float>::drag_bar<0.5f, 4.0f, 256.0f>
            , member_dynamic_flags<+[](const render_settings& O) { xproperty::flags::type F{}; F.m_bDontShow = O.m_CurrentFontOutputType != xfont_rsc::output_type::BITMAP; return F; }>
            , member_help<"BITMAP fonts bake fixed pixel sizes - pick which one to preview (snaps to whichever was actually compiled closest to this).">>
        , obj_scope<"Effects"
            // ShowOutline/Bold/Bevel all need a real distance field to threshold/shade from - BITMAP
            // is plain rasterized alpha coverage, so the shader's own BITMAP branch returns before any
            // of them would apply (see E28_msdf_frag.glsl) - hidden here to match, rather than leaving
            // a toggle that silently does nothing. Shadow and Italic DO still work on BITMAP (shadow
            // just resamples the same real alpha coverage translated; italic is a pure vertex shear) -
            // left visible/functional for it.
            , obj_member<"ShowOutline",  &render_settings::m_bShowOutline, member_help<"Draws an outline using the font's companion SDF texture - only available when the selected font's StoreSDF was on at compile time.">
                , member_dynamic_flags<+[](const render_settings& O) { xproperty::flags::type F{}; F.m_bDontShow = O.m_CurrentFontOutputType == xfont_rsc::output_type::BITMAP; return F; }>>
            , obj_member<"OutlineWidth", &render_settings::m_OutlineWidth
                // drag_bar's template order is <Speed, Min, Max> - the old <0.0f, 0.5f> actually meant
                // Speed=0 (frozen drag) and Min=0.5 (floor), not Min=0/Max=0.5 as intended. See
                // xfont_rsc_descriptor.h's PixelRange/AngleThreshold/GlyphSize for the same fix.
                , member_ui<float>::drag_bar<0.005f, 0.0f, 0.5f>
                , member_dynamic_flags<+[](const render_settings& O) { xproperty::flags::type F{}; F.m_bDontShow = !O.m_bShowOutline || O.m_CurrentFontOutputType == xfont_rsc::output_type::BITMAP; return F; }>
                , member_help<"Outline thickness, in em units.">>
            , obj_member<"Bold", &render_settings::m_bBold, member_help<"Synthetic bold - thickens the fill by shifting the SDF threshold, no separate bold font needed.">
                , member_dynamic_flags<+[](const render_settings& O) { xproperty::flags::type F{}; F.m_bDontShow = O.m_CurrentFontOutputType == xfont_rsc::output_type::BITMAP; return F; }>>
            , obj_member<"FontWeight", &render_settings::m_FontWeight
                , member_ui<float>::drag_bar<0.005f, -0.1f, 0.2f>
                , member_dynamic_flags<+[](const render_settings& O) { xproperty::flags::type F{}; F.m_bDontShow = !O.m_bBold || O.m_CurrentFontOutputType == xfont_rsc::output_type::BITMAP; return F; }>
                , member_help<"Bold amount, in em units - positive thickens, negative thins.">>
            , obj_member<"ShowShadow", &render_settings::m_bShowShadow, member_help<"Draws a drop shadow by redrawing the same glyph mesh translated behind the normal fill - correctly shaped/antialiased since it samples the real glyph SDF (or, for BITMAP, the real alpha coverage), just offset.">>
            , obj_member<"ShadowOffsetX", &render_settings::m_ShadowOffsetX
                , member_ui<float>::drag_bar<0.005f, -0.3f, 0.3f>
                , member_dynamic_flags<+[](const render_settings& O) { xproperty::flags::type F{}; F.m_bDontShow = !O.m_bShowShadow; return F; }>
                , member_help<"Shadow offset, in em units (+X = right).">>
            , obj_member<"ShadowOffsetY", &render_settings::m_ShadowOffsetY
                , member_ui<float>::drag_bar<0.005f, -0.3f, 0.3f>
                , member_dynamic_flags<+[](const render_settings& O) { xproperty::flags::type F{}; F.m_bDontShow = !O.m_bShowShadow; return F; }>
                , member_help<"Shadow offset, in em units (+Y = up).">>
            , obj_member<"Bevel", &render_settings::m_bBevel, member_help<"Pseudo-lit edge bevel, shaded from the SDF's own screen-space gradient - MTSDF/SDF only, no effect on BITMAP fonts (no distance field to shade from).">
                , member_dynamic_flags<+[](const render_settings& O) { xproperty::flags::type F{}; F.m_bDontShow = O.m_CurrentFontOutputType == xfont_rsc::output_type::BITMAP; return F; }>>
            , obj_member<"BevelWeight", &render_settings::m_BevelWeight
                , member_ui<float>::drag_bar<0.005f, 0.0f, 0.3f>
                , member_dynamic_flags<+[](const render_settings& O) { xproperty::flags::type F{}; F.m_bDontShow = !O.m_bBevel || O.m_CurrentFontOutputType == xfont_rsc::output_type::BITMAP; return F; }>
                , member_help<"How far the bevel band reaches inward from the edge, in em units.">>
            , obj_member<"Italic", &render_settings::m_bItalic, member_help<"Synthetic italic - shears the actual glyph geometry (not just the texture sampling), no separate italic font needed.">>
            , obj_member<"ItalicShear", &render_settings::m_ItalicShear
                , member_ui<float>::drag_bar<0.005f, -0.6f, 0.6f>
                , member_dynamic_flags<+[](const render_settings& O) { xproperty::flags::type F{}; F.m_bDontShow = !O.m_bItalic; return F; }>
                , member_help<"Shear slope - how far X shifts per unit Y (positive leans right at the top).">>
            >
        , obj_scope<"Debug"
            , obj_member<"ShowGlyphBounds", &render_settings::m_bShowGlyphBounds, member_help<"Draws the actual triangles used to render each glyph, in red wireframe, on top of the rendered text - useful for spotting atlas clamping, UV bleeding, and padding issues.">>
            >
        )
    };
    XPROPERTY_REG(render_settings)

    //---------------------------------------------------------------------------

    // Decodes a UTF-8 std::string into an ORDERED codepoint sequence (unlike the compiler's own
    // CollectCodepointsFromUtf8File, which collects a unique SET - here order and repeats matter).
    std::vector<std::uint32_t> DecodeUtf8(const std::string& Text)
    {
        std::vector<std::uint32_t> Out;
        std::size_t i = 0;
        while (i < Text.size())
        {
            const unsigned char c0 = static_cast<unsigned char>(Text[i]);
            std::uint32_t Cp; int Len;
            if (c0 < 0x80)                                     { Cp = c0;        Len = 1; }
            else if ((c0 & 0xE0) == 0xC0 && i + 1 < Text.size()) { Cp = c0 & 0x1F; Len = 2; }
            else if ((c0 & 0xF0) == 0xE0 && i + 2 < Text.size()) { Cp = c0 & 0x0F; Len = 3; }
            else if ((c0 & 0xF8) == 0xF0 && i + 3 < Text.size()) { Cp = c0 & 0x07; Len = 4; }
            else { ++i; continue; }

            bool bValid = true;
            for (int k = 1; k < Len; ++k)
            {
                const unsigned char Ck = static_cast<unsigned char>(Text[i + k]);
                if ((Ck & 0xC0) != 0x80) { bValid = false; break; }
                Cp = (Cp << 6) | (Ck & 0x3F);
            }
            if (bValid) Out.push_back(Cp);
            i += bValid ? static_cast<std::size_t>(Len) : 1;
        }
        return Out;
    }

    // One glyph quad ready for the GPU: NDC-agnostic local-space corners (em units, pen-relative,
    // Y-up) and the matching UV rect (normalized [0,1], atlas pixel bounds divided by atlas size).
    struct text_quad
    {
        xmath::fvec2 m_Min, m_Max;
        xmath::fvec2 m_UVMin, m_UVMax;
        // Size of ONE atlas texel, in em-units, for this specific glyph (X/Y separately, though
        // they're normally equal) - computed once here in LayoutText, where the glyph's own
        // AtlasW/H is at hand, so callers like Draw()'s debug wireframe can inset by "1 texel" without
        // needing to re-derive it from UV span + a runtime texture-dimension query.
        xmath::fvec2 m_TexelEm;
    };

    // Standard advance/bearing/kerning text layout (TextMeshPro's own model, nothing MSDF-specific -
    // see xfont_rsc_descriptor.h's own top comment) - walks Text, looks up each codepoint via the
    // font's perfect hash, accumulates the pen position, and emits one quad per non-whitespace glyph.
    // AtlasWidth/AtlasHeight convert the glyph's pixel-space atlas bounds into normalized UVs.
    // RequestedPixelSize (only consulted for BITMAP fonts - ignored otherwise) is "how big the text
    // would be at Scale=1,Zoom=1" (kBasePixelsPerEm) times the caller's actual Scale*Zoom - used to
    // automatically pick whichever baked size_group is closest, same automatic-by-OutputType
    // behavior the fill/outline shader branch already has, just for glyph lookup instead of sampling.
    void LayoutText(const xfont_rsc::font& Font, int AtlasWidth, int AtlasHeight, const std::string& Text, std::vector<text_quad>& OutQuads, float& OutAdvance, float RequestedPixelSize)
    {
        OutQuads.clear();
        OutAdvance = 0.0f;
        if (AtlasWidth <= 0 || AtlasHeight <= 0) return;

        const auto Codepoints = DecodeUtf8(Text);
        float PenX = 0.0f;
        std::uint32_t PrevCodepoint = 0;

        if (Font.m_OutputType == xfont_rsc::output_type::BITMAP)
        {
            const auto* pGroup = Font.FindClosestSizeGroup(RequestedPixelSize);
            if (pGroup == nullptr || pGroup->m_nGlyphs == 0) return;
            const char* pRegionStart = Font.SizeGroupRegionStart();
            // The compiler already normalizes each glyph's plane bounds/advance by this group's own
            // baked pixel size before packing them into fixed-point (see xfont_compiler.cpp's own
            // comment) - so FromFixed() alone yields the same "fraction of an em" units MTSDF/SDF
            // already use, no further scaling needed here. Draw()'s EffectiveScale multiplies back up
            // by kBasePixelsPerEm*Scale*Zoom afterward, same as MTSDF/SDF, so Scale/Zoom behave
            // consistently across every output type even though BITMAP itself can't truly scale (it
            // just snaps to whichever baked size is closest, via FindClosestSizeGroup above).
            for (auto Cp : Codepoints)
            {
                const auto* pGlyph = xfont_rsc::font::FindGlyphInSizeGroup(pRegionStart, *pGroup, Cp);
                if (pGlyph == nullptr) continue; // missing glyph - skip, no tofu box baked (yet); no kerning for BITMAP

                if (pGlyph->m_AtlasW > 0 && pGlyph->m_AtlasH > 0)
                {
                    text_quad Q;
                    const float PLeft   = xfont_rsc::FromFixed(pGlyph->m_PlaneLeft);
                    const float PRight  = xfont_rsc::FromFixed(pGlyph->m_PlaneRight);
                    const float PBottom = xfont_rsc::FromFixed(pGlyph->m_PlaneBottom);
                    const float PTop    = xfont_rsc::FromFixed(pGlyph->m_PlaneTop);
                    // Extend half a texel OUTWARD on every edge - see the MTSDF/SDF branch below for
                    // why (same fix, needed here too since BITMAP's own coverage alpha is sampled the
                    // same way). Geometry grows by the same half-texel, converted to em-units via this
                    // glyph's own texel-per-em ratio, so the extra sampled fringe actually becomes
                    // visible extra pixels rather than being squeezed into the original box (which
                    // would just resample/blur, not reveal anything new).
                    const float HalfTexelEmX = pGlyph->m_AtlasW > 0 ? 0.5f * (PRight - PLeft) / pGlyph->m_AtlasW : 0.0f;
                    const float HalfTexelEmY = pGlyph->m_AtlasH > 0 ? 0.5f * (PTop - PBottom) / pGlyph->m_AtlasH : 0.0f;
                    Q.m_Min = xmath::fvec2{ PenX + PLeft - HalfTexelEmX, PBottom - HalfTexelEmY };
                    Q.m_Max = xmath::fvec2{ PenX + PRight + HalfTexelEmX, PTop + HalfTexelEmY };
                    Q.m_UVMin = xmath::fvec2{ (pGlyph->m_AtlasX - 0.5f) / static_cast<float>(AtlasWidth), (pGlyph->m_AtlasY + pGlyph->m_AtlasH + 0.5f) / static_cast<float>(AtlasHeight) };
                    Q.m_UVMax = xmath::fvec2{ (pGlyph->m_AtlasX + pGlyph->m_AtlasW + 0.5f) / static_cast<float>(AtlasWidth), (pGlyph->m_AtlasY - 0.5f) / static_cast<float>(AtlasHeight) };
                    Q.m_TexelEm = xmath::fvec2{ HalfTexelEmX * 2.0f, HalfTexelEmY * 2.0f };
                    OutQuads.push_back(Q);
                }
                PenX += xfont_rsc::FromFixed(pGlyph->m_Advance);
            }
            OutAdvance = PenX;
            return;
        }

        for (std::size_t i = 0; i < Codepoints.size(); ++i)
        {
            const auto Cp = Codepoints[i];
            const auto* pGlyph = Font.FindGlyph(Cp);
            if (pGlyph == nullptr) { PrevCodepoint = 0; continue; } // missing glyph - skip, no tofu box baked (yet)

            if (i > 0 && PrevCodepoint != 0)
                PenX += xfont_rsc::FromFixed(Font.FindKernAdjust(PrevCodepoint, Cp));

            if (pGlyph->m_AtlasW > 0 && pGlyph->m_AtlasH > 0) // whitespace glyphs carry no ink - advance only, no quad
            {
                text_quad Q;
                const float PLeft   = xfont_rsc::FromFixed(pGlyph->m_PlaneLeft);
                const float PRight  = xfont_rsc::FromFixed(pGlyph->m_PlaneRight);
                const float PBottom = xfont_rsc::FromFixed(pGlyph->m_PlaneBottom);
                const float PTop    = xfont_rsc::FromFixed(pGlyph->m_PlaneTop);
                // Extend half a texel OUTWARD on every edge, geometry and UV together - sampling
                // exactly at a texel BOUNDARY (the un-extended math) let the GPU's bilinear filter
                // blend 50/50 with whatever sits just outside this glyph's own rect (padding, or a
                // neighbor), diluting the outermost row/column of real coverage toward transparent -
                // most visible as "the edge looks cut off" at high zoom. Growing the quad by the same
                // half-texel (converted to em-units via this glyph's own texel-per-em ratio) means that
                // extra sampled fringe becomes actual extra visible pixels instead of being squeezed
                // into the original box, which would just resample/blur rather than fix anything.
                const float HalfTexelEmX = pGlyph->m_AtlasW > 0 ? 0.5f * (PRight - PLeft) / pGlyph->m_AtlasW : 0.0f;
                const float HalfTexelEmY = pGlyph->m_AtlasH > 0 ? 0.5f * (PTop - PBottom) / pGlyph->m_AtlasH : 0.0f;
                Q.m_Min = xmath::fvec2{ PenX + PLeft - HalfTexelEmX, PBottom - HalfTexelEmY };
                Q.m_Max = xmath::fvec2{ PenX + PRight + HalfTexelEmX, PTop + HalfTexelEmY };
                Q.m_UVMin = xmath::fvec2{ (pGlyph->m_AtlasX - 0.5f) / static_cast<float>(AtlasWidth), (pGlyph->m_AtlasY + pGlyph->m_AtlasH + 0.5f) / static_cast<float>(AtlasHeight) };
                Q.m_UVMax = xmath::fvec2{ (pGlyph->m_AtlasX + pGlyph->m_AtlasW + 0.5f) / static_cast<float>(AtlasWidth), (pGlyph->m_AtlasY - 0.5f) / static_cast<float>(AtlasHeight) };
                Q.m_TexelEm = xmath::fvec2{ HalfTexelEmX * 2.0f, HalfTexelEmY * 2.0f };
                OutQuads.push_back(Q);
            }

            PenX += xfont_rsc::FromFixed(pGlyph->m_Advance);
            PrevCodepoint = Cp;
        }

        OutAdvance = PenX;
    }

    //---------------------------------------------------------------------------

    struct msdf_vert
    {
        float m_X, m_Y, m_U, m_V;
    };

    // Layout must match E28_msdf_vert.glsl/E28_msdf_frag.glsl's own PC block exactly (field order,
    // no vec3/mat fields so the default push_constant packing needs no manual padding).
    struct msdf_push_constants
    {
        xmath::fvec2   m_Scale;
        xmath::fvec2   m_Translate;
        float          m_PixelRange;
        std::uint32_t  m_Color;
        std::uint32_t  m_bOutline;
        std::uint32_t  m_OutlineColor;
        float          m_OutlineWidthPx;
        std::uint32_t  m_OutputType; // mirrors xfont_rsc::output_type: 0=MTSDF, 1=SDF, 2=BITMAP
        float          m_FontWeightPx;
        float          m_BevelWeightPx;
        float          m_ItalicShear; // slope (dx per unit y) - see E28_msdf_vert.glsl's own comment on why field order/size here must stay in sync with that shader's own (shorter) PC block
    };

    // Push constants for the glyph-bounds debug overlay's OWN dedicated pipeline (E28_wire_vert/frag)
    // - see E28_wire_vert.glsl's own comment on why this is a separate shader pair rather than a
    // branch in msdf_push_constants/the MSDF shader. Color is fixed (opaque red) in the shader itself.
    struct wire_push_constants
    {
        xmath::fvec2 m_Scale;
        xmath::fvec2 m_Translate;
    };

    //---------------------------------------------------------------------------

    // Renders LayoutText's quads into an offscreen texture using the MSDF shader pair, for display
    // via ImGui::Image - same offscreen-render-to-texture pattern E22_FramebufferTarget uses, sized
    // to fit whatever text is currently laid out rather than a fixed viewport. Effects (outline) are
    // computed here, per-frame, from the companion SDF texture - never baked, per the descriptor's
    // own design note.
    struct text_renderer
    {
        static constexpr int MaxQuads = 1024;
        static constexpr int MaxSceneDim = 4096;
        // Pixel offset of the pen origin (em-space 0,0) from the viewport's left edge, at PanPx={0,0} -
        // shared between Draw()'s own transform and the caller's zoom-toward-cursor math (both need to
        // agree on where world (0,0) sits on screen to convert between the two consistently).
        static constexpr float kLeftMarginPx = 20.0f;

        // The scene texture is sized to exactly fit the current content (like a normal render-to-
        // texture setup) and is destroyed/recreated whenever that size changes - but only past a
        // hysteresis threshold (see Draw()'s SizeChangedEnough check), not on every pixel of difference.
        // Recreating on literally every frame (e.g. while continuously dragging the Zoom slider, where
        // the requested size changes slightly every frame) raced the GPU still using the previous
        // frame's texture/renderpass/pipeline_instance and crashed the app with invalid Vulkan image
        // layouts. Snapping to fixed size buckets keeps recreation rare during a drag while still
        // tracking the real content size closely enough that the display never looks visibly padded.
        xgpu::vertex_descriptor  m_VertexDescriptor;
        xgpu::pipeline           m_Pipeline;
        // Glyph-bounds debug overlay's own pipeline (E28_wire_vert/frag) - see that shader's own
        // comment on why it's separate rather than a branch in the main MSDF pipeline. Has no texture
        // samplers and no dependency on the atlas/SDF texture, so unlike m_PipelineInstance below it's
        // created once here and never needs rebuilding.
        xgpu::pipeline           m_WirePipeline;
        xgpu::pipeline_instance  m_WirePipelineInstance;
        xgpu::buffer             m_VertexBuffer;
        xgpu::buffer             m_IndexBuffer;
        xgpu::texture            m_DummyTexture;      // bound to the SDF slot when the font has no companion
        xgpu::texture            m_SceneTexture;
        xgpu::renderpass         m_RenderPass;
        xgpu::pipeline_instance  m_PipelineInstance;
        // Identity of the underlying GPU handle actually bound (xgpu::texture::m_Private.get()), NOT
        // the outer xfont::rt::m_pAtlasTexture pointer - when the user edits the descriptor (e.g.
        // toggling CompressAtlas) and hits Compile, the live file-watcher recompiles the font+texture
        // and xresource::mgr reloads it, which can reuse the SAME pooled xgpu::texture* address for a
        // genuinely NEW underlying Vulkan image. Comparing the outer pointer alone would then wrongly
        // conclude "nothing changed" and keep drawing with a pipeline_instance bound to the now-destroyed
        // old image view - exactly what crashed with "imageView is invalid or has been destroyed".
        void*                    m_pCachedAtlasHandle = nullptr;
        void*                    m_pCachedSDFHandle   = nullptr;
        int                      m_SceneW = 0, m_SceneH = 0; // the scene texture's own current allocated size
        int                      m_UsedW = 0, m_UsedH = 0;   // the sub-rect of it actually drawn into last Draw() call
        int                      m_WireQuadCount = 0, m_WireIndexStart = 0; // last Draw() call's debug wireframe range - see Draw()'s own comment

        int Create(xgpu::device& Device)
        {
            {
                auto Attributes = std::array
                {
                    xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(msdf_vert, m_X), .m_Format = xgpu::vertex_descriptor::format::FLOAT_2D }
                ,   xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(msdf_vert, m_U), .m_Format = xgpu::vertex_descriptor::format::FLOAT_2D }
                };
                auto Setup = xgpu::vertex_descriptor::setup{ .m_VertexSize = sizeof(msdf_vert), .m_Attributes = Attributes };
                if (auto Err = Device.Create(m_VertexDescriptor, Setup); Err) return xgpu::getErrorInt(Err);
            }

            xgpu::shader FragShader, VertShader;
            {
                xgpu::shader::setup Setup{ .m_Type = xgpu::shader::type::bit::FRAGMENT, .m_Sharer = xgpu::shader::setup::raw_data{ AsShaderSpan(g_MsdfFragSPVWords, std::size(g_MsdfFragSPVWords)) } };
                if (auto Err = Device.Create(FragShader, Setup); Err) return xgpu::getErrorInt(Err);
            }
            {
                xgpu::shader::setup Setup{ .m_Type = xgpu::shader::type::bit::VERTEX, .m_Sharer = xgpu::shader::setup::raw_data{ AsShaderSpan(g_MsdfVertSPVWords, std::size(g_MsdfVertSPVWords)) } };
                if (auto Err = Device.Create(VertShader, Setup); Err) return xgpu::getErrorInt(Err);
            }

            {
                auto Shaders  = std::array<const xgpu::shader*, 2>{ &FragShader, &VertShader };
                auto Samplers = std::array{ xgpu::pipeline::sampler{}, xgpu::pipeline::sampler{} };
                auto Setup = xgpu::pipeline::setup
                {
                    .m_VertexDescriptor  = m_VertexDescriptor
                ,   .m_Shaders           = Shaders
                ,   .m_PushConstantsSize = sizeof(msdf_push_constants)
                ,   .m_Samplers          = Samplers
                ,   .m_DepthStencil      = { .m_bDepthTestEnable = false, .m_bDepthWriteEnable = false }
                ,   .m_Blend             = xgpu::pipeline::blend::getAlphaOriginal()
                };
                if (auto Err = Device.Create(m_Pipeline, Setup); Err) return xgpu::getErrorInt(Err);
            }

            {
                xgpu::shader WireFragShader, WireVertShader;
                {
                    xgpu::shader::setup Setup{ .m_Type = xgpu::shader::type::bit::FRAGMENT, .m_Sharer = xgpu::shader::setup::raw_data{ AsShaderSpan(g_WireFragSPVWords, std::size(g_WireFragSPVWords)) } };
                    if (auto Err = Device.Create(WireFragShader, Setup); Err) return xgpu::getErrorInt(Err);
                }
                {
                    xgpu::shader::setup Setup{ .m_Type = xgpu::shader::type::bit::VERTEX, .m_Sharer = xgpu::shader::setup::raw_data{ AsShaderSpan(g_WireVertSPVWords, std::size(g_WireVertSPVWords)) } };
                    if (auto Err = Device.Create(WireVertShader, Setup); Err) return xgpu::getErrorInt(Err);
                }

                auto Shaders = std::array<const xgpu::shader*, 2>{ &WireFragShader, &WireVertShader };
                auto Setup = xgpu::pipeline::setup
                {
                    .m_VertexDescriptor  = m_VertexDescriptor // same aPos/aUV layout as the MSDF pipeline - aUV simply unused here
                ,   .m_Shaders           = Shaders
                ,   .m_PushConstantsSize = sizeof(wire_push_constants)
                // WIRELINE = the real fix: this draws the SAME triangles as the real glyph quads (see
                // Draw()'s own comment), just rasterized as polygon edges instead of filled - showing
                // the actual mesh, diagonal included, not a synthetic bounding-box border. Cull::NONE
                // since we want every edge regardless of the quads' winding order.
                ,   .m_Primitive         = { .m_Raster = xgpu::pipeline::primitive::raster::WIRELINE, .m_Cull = xgpu::pipeline::primitive::cull::NONE }
                ,   .m_DepthStencil      = { .m_bDepthTestEnable = false, .m_bDepthWriteEnable = false }
                ,   .m_Blend             = xgpu::pipeline::blend::getAlphaOriginal()
                };
                if (auto Err = Device.Create(m_WirePipeline, Setup); Err) return xgpu::getErrorInt(Err);
                if (auto Err = Device.Create(m_WirePipelineInstance, { .m_PipeLine = m_WirePipeline }); Err) return xgpu::getErrorInt(Err);
            }

            {
                std::array<std::byte, 4> Pixels{};
                if (auto Err = Device.Create(m_DummyTexture, { .m_Format = xgpu::texture::format::R8G8B8A8_UNORM, .m_AdressModes = {}, .m_Width = 1, .m_Height = 1, .m_AllFacesData = Pixels, .m_isGamma = false }); Err)
                    return xgpu::getErrorInt(Err);
            }

            if (auto Err = Device.Create(m_VertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_Usage = xgpu::buffer::setup::usage::CPU_WRITE_GPU_READ, .m_EntryByteSize = sizeof(msdf_vert), .m_EntryCount = MaxQuads * 4 }); Err)
                return xgpu::getErrorInt(Err);
            if (auto Err = Device.Create(m_IndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_Usage = xgpu::buffer::setup::usage::CPU_WRITE_GPU_READ, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = MaxQuads * 6 }); Err)
                return xgpu::getErrorInt(Err);

            return 0;
        }

        // Snaps a requested dimension up to the nearest 25%-growth bucket (min 64px) so the scene
        // texture only needs recreating when the requested size crosses a bucket boundary - not on
        // every single-pixel change while a slider is being dragged continuously.
        static int SnapToBucket(int Value)
        {
            int Bucket = 64;
            while (Bucket < Value && Bucket < MaxSceneDim) Bucket = Bucket + Bucket / 4 + 1;
            return std::min(Bucket, MaxSceneDim);
        }

        int UpdateRenderTarget(xgpu::device& Device, int PixelW, int PixelH)
        {
            const int BucketW = SnapToBucket(PixelW);
            const int BucketH = SnapToBucket(PixelH);

            if (m_SceneTexture.m_Private && BucketW == m_SceneW && BucketH == m_SceneH)
                return 0;

            if (m_SceneTexture.m_Private)
            {
                xgpu::tools::imgui::ClearTexture(m_SceneTexture);
                Device.Destroy(std::move(m_SceneTexture));
            }
            if (m_RenderPass.m_Private)        Device.Destroy(std::move(m_RenderPass));
            if (m_PipelineInstance.m_Private)  Device.Destroy(std::move(m_PipelineInstance));

            m_SceneW = BucketW;
            m_SceneH = BucketH;

            if (auto Err = Device.Create(m_SceneTexture, { .m_Format = xgpu::texture::format::R8G8B8A8_UNORM, .m_Width = BucketW, .m_Height = BucketH, .m_isGamma = false }); Err)
                return xgpu::getErrorInt(Err);

            std::array<xgpu::renderpass::attachment, 1> Attachments{ m_SceneTexture };
            auto RPSetup = xgpu::renderpass::setup{ .m_Attachments = Attachments, .m_ClearColorR = 0, .m_ClearColorG = 0, .m_ClearColorB = 0, .m_ClearColorA = 0 };
            if (auto Err = Device.Create(m_RenderPass, RPSetup); Err) return xgpu::getErrorInt(Err);

            m_pCachedAtlasHandle = nullptr; // pipeline_instance doesn't reference the renderpass, but was destroyed above - force it to rebuild
            return 0;
        }

        int m_SkipFrames = 0; // set by Reset() after a live recompile - see its own comment

        // Called whenever the selected font is about to reload (a recompile completed) - a font's
        // atlas texture reference resolves to the SAME virtual-texture GUID across recompiles, so the
        // resource manager reloads it in place rather than handing back a new identity; the compile-
        // completion notification that triggers this also arrives on the background compile-worker
        // thread (see CompilingThreadWorker/m_OnCompilationState), not the render thread. Dropping our
        // own cached pipeline_instance immediately - rather than trusting the handle-identity check in
        // UpdatePipelineInstance to catch it - and skipping a few frames before touching the texture
        // again gives that reload time to actually land before we bind it into a new descriptor set.
        // Without this, toggling a compression setting and recompiling while the preview is visible
        // reliably crashed with "imageView is invalid or has been destroyed".
        void Reset(xgpu::device& Device)
        {
            if (m_PipelineInstance.m_Private) Device.Destroy(std::move(m_PipelineInstance));
            m_pCachedAtlasHandle = nullptr;
            m_pCachedSDFHandle   = nullptr;
            m_SkipFrames         = 3;
        }

        int UpdatePipelineInstance(xgpu::device& Device, xgpu::texture* pAtlas, xgpu::texture* pSDF)
        {
            void* pAtlasHandle = pAtlas ? pAtlas->m_Private.get() : nullptr;
            void* pSDFHandle   = pSDF   ? pSDF->m_Private.get()   : nullptr;

            if (pAtlasHandle == m_pCachedAtlasHandle && pSDFHandle == m_pCachedSDFHandle && m_PipelineInstance.m_Private)
                return 0;

            if (m_PipelineInstance.m_Private) Device.Destroy(std::move(m_PipelineInstance));

            auto& SDFTex   = pSDF ? *pSDF : m_DummyTexture;
            auto  Bindings = std::array{ xgpu::pipeline_instance::sampler_binding{ *pAtlas }, xgpu::pipeline_instance::sampler_binding{ SDFTex } };
            auto  Setup    = xgpu::pipeline_instance::setup{ .m_PipeLine = m_Pipeline, .m_SamplersBindings = Bindings };
            if (auto Err = Device.Create(m_PipelineInstance, Setup); Err) return xgpu::getErrorInt(Err);

            m_pCachedAtlasHandle = pAtlasHandle;
            m_pCachedSDFHandle   = pSDFHandle;
            return 0;
        }

        // Camera-driven viewport, like a 3D scene panel (e.g. E25_SkinGeomEditor's own view) - NOT
        // "render small, then stretch a picture". The render target is sized to the ACTUAL on-screen
        // panel (ViewW x ViewH, caller-provided, straight from ImGui::GetContentRegionAvail()) and
        // stays that size regardless of zoom; PxPerEm (the caller folds its own view-zoom multiplier
        // into this) and PanPx (pixel-space offset of the pen origin) are the camera. Zooming in
        // therefore renders MORE detail at the SAME output resolution - text can render larger than
        // the viewport and get panned/clipped, exactly like moving a camera - rather than rendering
        // once at a fixed size and blowing up the resulting bitmap for display. This is also why
        // m_LineWidth=1 on the glyph-bounds wire pipeline below is a genuine 1 SCREEN pixel at any
        // zoom: the wireframe is rasterized at final display resolution, never magnified afterward.
        int Draw
        ( xgpu::device&                    Device
        , xgpu::window&                    MainWindow
        , const xfont_rsc::font&           Font
        , xgpu::texture*                   pAtlas
        , xgpu::texture*                   pSDF
        , const std::vector<text_quad>&    Quads
        , int                              ViewW
        , int                              ViewH
        , float                            PxPerEm
        , xmath::fvec2                     PanPx
        , bool                             bOutline
        , float                            OutlineWidthEm
        , bool                             bBold
        , float                            FontWeightEm
        , bool                             bShowShadow
        , xmath::fvec2                     ShadowOffsetEm
        , bool                             bBevel
        , float                            BevelWeightEm
        , bool                             bItalic
        , float                            ItalicShear
        , bool                             bShowBounds = false
        )
        {
            if (pAtlas == nullptr) return 0;

            if (m_SkipFrames > 0) { --m_SkipFrames; return 0; } // settling after Reset() - see its own comment

            const int PixelW = std::clamp(ViewW, 8, MaxSceneDim);
            const int PixelH = std::clamp(ViewH, 8, MaxSceneDim);
            m_UsedW = PixelW;
            m_UsedH = PixelH;

            if (auto Err = UpdateRenderTarget(Device, PixelW, PixelH); Err) return Err;
            if (auto Err = UpdatePipelineInstance(Device, pAtlas, pSDF); Err) return Err;

            const int QuadCount = std::min<int>(static_cast<int>(Quads.size()), MaxQuads);

            if (QuadCount > 0)
            {
                (void)m_VertexBuffer.MemoryMap(0, QuadCount * 4, [&](void* pData)
                {
                    auto* pV = static_cast<msdf_vert*>(pData);
                    for (int i = 0; i < QuadCount; ++i)
                    {
                        const auto& Q = Quads[i];
                        pV[i * 4 + 0] = { Q.m_Min.x(), Q.m_Min.y(), Q.m_UVMin.x(), Q.m_UVMin.y() };
                        pV[i * 4 + 1] = { Q.m_Max.x(), Q.m_Min.y(), Q.m_UVMax.x(), Q.m_UVMin.y() };
                        pV[i * 4 + 2] = { Q.m_Max.x(), Q.m_Max.y(), Q.m_UVMax.x(), Q.m_UVMax.y() };
                        pV[i * 4 + 3] = { Q.m_Min.x(), Q.m_Max.y(), Q.m_UVMin.x(), Q.m_UVMax.y() };
                    }
                });

                // Round the mapped range up to a multiple of 16 indices (16*4 bytes = 64 bytes) - the
                // GPU's nonCoherentAtomSize requires flush/invalidate ranges to be 64-byte aligned, and
                // QuadCount*6 isn't one in general. The extra slots (never referenced by Draw() below,
                // which still only draws the real QuadCount*6) are harmless to write.
                const int AlignedIndexCount = std::min(MaxQuads * 6, ((QuadCount * 6) + 15) / 16 * 16);
                (void)m_IndexBuffer.MemoryMap(0, AlignedIndexCount, [&](void* pData)
                {
                    auto* pI = static_cast<std::uint32_t*>(pData);
                    for (int i = 0; i < QuadCount; ++i)
                    {
                        const std::uint32_t Base = static_cast<std::uint32_t>(i * 4);
                        *pI++ = Base + 0; *pI++ = Base + 1; *pI++ = Base + 2;
                        *pI++ = Base + 0; *pI++ = Base + 2; *pI++ = Base + 3;
                    }
                });

                // Debug glyph-bounds overlay (RenderSettings/Debug/ShowGlyphBounds) - a SEPARATE,
                // slightly-inset copy of each glyph's quad, appended right after the real glyphs in
                // the same vertex/index buffers (capped to whatever's left of MaxQuads) and drawn
                // through the wire pipeline's WIRELINE mode below. Inset is debug-display-only - the
                // REAL fill quads above are untouched, so this never affects actual rendering, only
                // how tightly the overlay hugs each glyph. Inset by exactly 1 atlas TEXEL (each
                // quad's own m_TexelEm, computed once in LayoutText from that glyph's real
                // AtlasW/H) - not a screen-pixel amount, since the point of this overlay is comparing
                // against the atlas's own packed rects (e.g. spotting packer overlaps), which are
                // texel-relative, not tied to whatever zoom the viewport happens to be at.
                m_WireQuadCount = bShowBounds ? std::min(QuadCount, MaxQuads - QuadCount) : 0;
                if (m_WireQuadCount > 0)
                {
                    (void)m_VertexBuffer.MemoryMap(QuadCount * 4, m_WireQuadCount * 4, [&](void* pData)
                    {
                        auto* pV = static_cast<msdf_vert*>(pData);
                        for (int i = 0; i < m_WireQuadCount; ++i)
                        {
                            const auto& Q = Quads[i];
                            const float MinX = Q.m_Min.x() + Q.m_TexelEm.x(), MinY = Q.m_Min.y() + Q.m_TexelEm.y();
                            const float MaxX = Q.m_Max.x() - Q.m_TexelEm.x(), MaxY = Q.m_Max.y() - Q.m_TexelEm.y();
                            pV[i * 4 + 0] = { MinX, MinY, 0.0f, 0.0f };
                            pV[i * 4 + 1] = { MaxX, MinY, 0.0f, 0.0f };
                            pV[i * 4 + 2] = { MaxX, MaxY, 0.0f, 0.0f };
                            pV[i * 4 + 3] = { MinX, MaxY, 0.0f, 0.0f };
                        }
                    });

                    m_WireIndexStart = AlignedIndexCount; // already 16-index (64-byte) aligned
                    const int WireIndexCountAligned = std::min(MaxQuads * 6 - m_WireIndexStart, ((m_WireQuadCount * 6) + 15) / 16 * 16);
                    (void)m_IndexBuffer.MemoryMap(m_WireIndexStart, WireIndexCountAligned, [&](void* pData)
                    {
                        auto* pI = static_cast<std::uint32_t*>(pData);
                        for (int i = 0; i < m_WireQuadCount; ++i)
                        {
                            const std::uint32_t Base = static_cast<std::uint32_t>(i * 4);
                            *pI++ = Base + 0; *pI++ = Base + 1; *pI++ = Base + 2;
                            *pI++ = Base + 0; *pI++ = Base + 2; *pI++ = Base + 3;
                        }
                    });
                }
            }
            else
            {
                m_WireQuadCount = 0;
            }

            auto CmdBuffer = MainWindow.StartRenderPass(m_RenderPass);
            // The scene texture is bucket-snapped (see SnapToBucket) and so is usually somewhat larger
            // than the actual content - constrain the viewport/scissor to exactly [PixelW x PixelH] (the
            // size the NDC transform below assumes) rather than letting it default to the full, larger
            // attachment, or the content renders squashed to fill the whole bucket and then gets cropped
            // on top of that. Display reads back this same [PixelW x PixelH] sub-rect via UV.
            CmdBuffer.setViewport(0, 0, static_cast<float>(PixelW), static_cast<float>(PixelH));
            CmdBuffer.setScissor(0, 0, PixelW, PixelH);
            CmdBuffer.setPipelineInstance(m_PipelineInstance);
            CmdBuffer.setBuffer(m_VertexBuffer);
            CmdBuffer.setBuffer(m_IndexBuffer);

            // Anchor the pen origin (em-space 0,0 - where LayoutText starts) at a fixed pixel position
            // in the viewport (a left margin, vertically centered), offset by the camera's own pan -
            // NOT "fit the whole text's bounding box to the viewport" like the old content-driven
            // sizing did. That old approach silently re-scaled everything whenever PenXFinal changed
            // (every keystroke); anchoring a fixed world point instead means only PxPerEm/PanPx move
            // the camera, exactly like a real viewport.
            const xmath::fvec2 AnchorPx{ kLeftMarginPx + PanPx.x(), PixelH * 0.5f + PanPx.y() };

            msdf_push_constants PC{};
            const float ScaleX = 2.0f * PxPerEm / static_cast<float>(PixelW);
            const float ScaleY = -2.0f * PxPerEm / static_cast<float>(PixelH); // flip: +Y (ascender) renders toward the top of the image
            PC.m_Scale          = xmath::fvec2{ ScaleX, ScaleY };
            PC.m_Translate      = xmath::fvec2{ AnchorPx.x() * 2.0f / static_cast<float>(PixelW) - 1.0f, AnchorPx.y() * 2.0f / static_cast<float>(PixelH) - 1.0f };
            PC.m_PixelRange     = Font.m_PixelRange;
            PC.m_Color          = 0xFFFFFFFFu; // opaque white fill
            // BITMAP has no distance field to compute an outline from - see the shader's own early-out.
            PC.m_bOutline       = (bOutline && pSDF != nullptr && Font.m_OutputType != xfont_rsc::output_type::BITMAP) ? 1u : 0u;
            PC.m_OutlineColor   = 0xFF000000u; // opaque black outline
            PC.m_OutlineWidthPx = OutlineWidthEm * PxPerEm;
            PC.m_OutputType     = static_cast<std::uint32_t>(Font.m_OutputType);
            PC.m_FontWeightPx   = bBold ? FontWeightEm * PxPerEm : 0.0f;
            // Bevel needs a real SDF gradient to shade from - no-op (and left at 0, the shader's own
            // off switch) for BITMAP, same reasoning as outline just above.
            PC.m_BevelWeightPx  = (bBevel && Font.m_OutputType != xfont_rsc::output_type::BITMAP) ? BevelWeightEm * PxPerEm : 0.0f;
            PC.m_ItalicShear    = bItalic ? ItalicShear : 0.0f; // a dimensionless slope, not an em length - no PxPerEm conversion needed

            if (bShowShadow && QuadCount > 0)
            {
                // Drop shadow = the SAME real glyph mesh (same vertex/index range, offset 0), redrawn
                // BEFORE the fill pass with the pen origin translated by ShadowOffsetEm and a flat dark
                // color - no outline/bevel on the shadow itself. Because it's the real mesh sampling
                // its own correct UV rect (not a same-texture UV-shift trick), there's no risk of
                // bleeding into a neighboring glyph's atlas cell regardless of offset size, and the
                // shadow comes out correctly shaped/antialiased for free since it's the real SDF.
                msdf_push_constants ShadowPC = PC;
                ShadowPC.m_Translate    = xmath::fvec2{ PC.m_Translate.x() + ShadowOffsetEm.x() * ScaleX, PC.m_Translate.y() + ShadowOffsetEm.y() * ScaleY };
                ShadowPC.m_Color        = 0xC0000000u; // translucent black
                ShadowPC.m_bOutline     = 0u;
                ShadowPC.m_BevelWeightPx = 0.0f;
                CmdBuffer.setPushConstants(ShadowPC);
                CmdBuffer.Draw(QuadCount * 6);
            }

            CmdBuffer.setPushConstants(PC);

            if (QuadCount > 0)
                CmdBuffer.Draw(QuadCount * 6);

            if (m_WireQuadCount > 0)
            {
                // Draws the inset copy built above (own dedicated pipeline - see E28_wire_vert.glsl's
                // own comment), NOT the real glyph triangles - shows each glyph's own mesh shape
                // (diagonal included), just inset by a debug-only margin so it isn't swallowed by
                // msdf-atlas-gen's own antialiasing padding on the real quad. Same Scale/Translate as
                // the glyphs above (unchanged), so it lands in the same em-space transform.
                CmdBuffer.setPipelineInstance(m_WirePipelineInstance);
                wire_push_constants WirePC{ .m_Scale = PC.m_Scale, .m_Translate = PC.m_Translate };
                CmdBuffer.setPushConstants(WirePC);
                CmdBuffer.Draw(m_WireQuadCount * 6, m_WireIndexStart, QuadCount * 4);
            }

            return 0;
        }
    };
}

//-----------------------------------------------------------------------------------------

int E28_Example()
{
    xgpu::instance Instance;
    if (auto Err = xgpu::CreateInstance(Instance, { .m_bDebugMode = true, .m_bEnableRenderDoc = true, .m_pLogErrorFunc = e28::Debugger, .m_pLogWarning = e28::Debugger }); Err)
        return xgpu::getErrorInt(Err);

    xgpu::device Device;
    if (auto Err = Instance.Create(Device); Err)
        return xgpu::getErrorInt(Err);

    xgpu::window MainWindow;
    if (auto Err = Device.Create(MainWindow, {}); Err)
        return xgpu::getErrorInt(Err);

    xresource::g_Mgr.Initiallize(20000);

    resource_mgr_user_data ResourceMgrUserData;

    e10::assert_browser AsserBrowser;
    e28::font_state      FontState;

    e28::text_renderer TextRenderer;
    if (auto Err = TextRenderer.Create(Device); Err)
        return Err;

    auto CallBackForCompilation = [&](e10::library_mgr&, e10::library::guid, xresource::full_guid gCompilingEntry, std::shared_ptr<e10::compilation::historical_entry::log>& LogInformation)
    {
        if (FontState.m_InfoGUID != gCompilingEntry) return;

        if (FontState.GetLog().get() != LogInformation.get())
            FontState.SetLog(LogInformation);

        e10::compilation::historical_entry::result Result;
        {
            const auto LogCopy = FontState.GetLog();
            xcontainer::lock::scope lk(*LogCopy);
            Result = LogCopy->get().m_Result;
        }

        if (Result == e10::compilation::historical_entry::result::COMPILING || Result == e10::compilation::historical_entry::result::COMPILING_WARNINGS)
        {
            // The on-disk resource this reference points to is about to be overwritten - stop holding
            // it NOW rather than waiting for SUCCESS. Only a flag is set here (this runs on the
            // background compile-worker thread - see m_bCompileDetected's own comment); the actual
            // xresource::mgr::ReleaseRef happens on the main thread.
            //
            // Stamped HERE, synchronously, on this same background thread - not later when the main
            // thread gets around to consuming m_bCompileDetected. See m_CompileStartTime's own
            // comment for why using now() at consumption time was the actual bug behind needing to
            // press Compile twice.
            FontState.SetCompileStartTime(std::filesystem::file_time_type::clock::now());
            FontState.m_bCompileDetected = true;
        }
        else if (Result == e10::compilation::historical_entry::result::SUCCESS || Result == e10::compilation::historical_entry::result::SUCCESS_WARNINGS)
        {
            FontState.m_bReload = true;
            FontState.m_bErrors = false;
        }
        else if (Result == e10::compilation::historical_entry::result::FAILURE)
        {
            FontState.m_bErrors = true;
        }
    };
    e10::g_LibMgr.m_OnCompilationState.Register(CallBackForCompilation);

    //
    // Property inspector - the selected font descriptor's own properties, driven by xproperty
    // rather than hardcoded ImGui widgets. NOT bound here at startup against FontState.m_Descriptor
    // while it's still empty/default - xproperty::inspector's own internal per-property-path value
    // cache turns out to not be keyed strictly to the bound object's identity, so showing it against
    // an empty object first can display stale content left behind by an unrelated earlier binding.
    // E10/E23 never hit this because they only ever call AppendEntityComponent AFTER a real
    // selection has already loaded real data (see LoadFont below, which does the (re)binding) -
    // mirror that exactly rather than binding once up front.
    //
    xproperty::inspector Inspector("Font Properties");

    // Editor-wide preview controls (text to render, effect toggles) - bound once, safe to do so
    // here since it's not per-asset state, see render_settings' own top comment.
    e28::render_settings RenderSettings;
    xproperty::inspector RenderInspector("Rendering Settings");
    RenderInspector.AppendEntity();
    RenderInspector.AppendEntityComponent(*xproperty::getObjectByType<e28::render_settings>(), &RenderSettings);

    // Independent pan/zoom per preview window - see pan_zoom's own comment.
    e28::pan_zoom AtlasView{};
    e28::pan_zoom LiveTextView{};

    xgpu::tools::imgui::CreateInstance(MainWindow);
    ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = 0.5f;

    //
    // Set the project path
    //
    {
        TCHAR szFileName[MAX_PATH];
        GetModuleFileName(NULL, szFileName, MAX_PATH);

        std::wcout << L"Full path: " << szFileName << L"\n";
        if (auto I = xstrtool::findI(std::wstring{ szFileName }, { L"xGPU" }); I != std::string::npos)
        {
            I += 4; // Skip the xGPU part
            szFileName[I] = 0;
            std::wcout << L"Found xGPU at: " << szFileName << L"\n";

            TCHAR LIONantProject[] = L"\\example.lionprj";
            for (int i = 0; szFileName[I++] = LIONantProject[i]; ++i);

            std::wcout << "Project Path: " << szFileName << "\n";

            if (auto Err = e10::g_LibMgr.OpenProject(szFileName); Err)
            {
                e28::Debugger(Err.getMessage());
                return 1;
            }

            ImGuiIO& io = ImGui::GetIO();
            static std::string IniSave = std::format("{}/Assets/imgui.ini", xstrtool::To(szFileName));
            io.IniFilename = IniSave.c_str();

            ResourceMgrUserData.m_Device = Device;
            xresource::g_Mgr.setUserData(&ResourceMgrUserData, false);
            xresource::g_Mgr.setRootPath(std::format(L"{}//Cache//Resources//Platforms//Windows", e10::g_LibMgr.m_ProjectPath));
        }
    }

    //
    // Main Loop
    //
    while (Instance.ProcessInputEvents())
    {
        if (xgpu::tools::imgui::BeginRendering(true)) continue;

        // Drain last frame's deferred release, if any - see font_state::m_PendingRelease's own
        // comment. Safe here specifically because last frame's xgpu::tools::imgui::Render() (which
        // dereferences any ImGui::Image() texture pointers to build GPU commands) already ran to
        // completion before we got back around to the top of this loop.
        if (FontState.m_PendingRelease.empty() == false)
        {
            xresource::g_Mgr.ReleaseRef(FontState.m_PendingRelease);
            FontState.m_PendingRelease.clear();
        }

        // A compile just started on the selected font's own resource file - release our reference to
        // it NOW, before anything below this point can touch a texture pointer whose target is about
        // to be overwritten on disk. See m_bCompileDetected's own comment for why this can't happen in
        // the compile-worker thread's own notification callback.
        if (FontState.m_bCompileDetected.exchange(false))
        {
            TextRenderer.Reset(Device);
            if (FontState.m_Ref.empty() == false)
            {
                xresource::g_Mgr.ReleaseRef(FontState.m_Ref);
                FontState.m_Ref.clear();
            }
            FontState.m_ReloadCooldown  = 60;
            // m_CompileStartTime is stamped earlier, in CallBackForCompilation itself (background
            // thread, at the instant COMPILING was observed) - not here. See its own comment.
        }

        //
        // Font Preview - three independently dockable windows (Atlas / SDF / Live Preview), matching
        // the rest of this codebase's convention (e.g. E22_FramebufferTarget's separate Viewport/
        // Controls windows) so each can be arranged wherever the user wants rather than being locked
        // into one fixed layout. All three share one "is there anything valid to show right now"
        // computation - resolved ONCE per frame, not duplicated per window.
        //
        xfont::rt* pFont = nullptr;
        enum class preview_state { kNone, kNotCompiled, kReloading, kReady };
        preview_state PreviewState = preview_state::kNone;

        if (!FontState.empty())
        {
            // A freshly selected/created font routinely has no compiled binary on disk yet -
            // check existence before ever calling getResource(), exactly like E10's own
            // SelectedDescriptor.m_ResourcePath check (xresource::loader<>::Load asserts on any
            // failure to load, matching every other loader in this codebase, so the caller is
            // what's responsible for not asking it to load something that isn't there yet).
            if (!std::filesystem::exists(FontState.m_ResourcePath))
            {
                PreviewState = preview_state::kNotCompiled;
            }
            else if (FontState.m_Ref.empty())
            {
                // Released by the compile-detected handler above, not yet reacquired by a
                // completed LoadFont() reload - getResource() on an empty ref isn't safe to call.
                PreviewState = preview_state::kReloading;
            }
            else if (pFont = xresource::g_Mgr.getResource(FontState.m_Ref); pFont == nullptr)
            {
                PreviewState = preview_state::kNotCompiled;
            }
            else if (FontState.m_ReloadCooldown > 0)
            {
                // Right after a live recompile, the atlas/SDF textures are being torn down and
                // rebuilt by a background thread (see font_state::m_ReloadCooldown's own comment) -
                // touching them here crashed. Wait out the cooldown instead.
                PreviewState = preview_state::kReloading;
            }
            else
            {
                // The font's own reload can be ready before its cascaded texture compile is - see
                // font_state::m_CompileStartTime's own comment. Uses xfont::rt's own
                // m_TextureResourcePath (captured once at Load time), NOT pFont->m_pFont->m_Texture
                // directly - that ref gets MUTATED by xresource::mgr::getResource(def_guid&) the
                // moment the loader resolves it (it overwrites m_Instance with a raw pointer as a
                // self-caching optimization), so reconstructing a full_guid from it here would hand
                // getResourcePath() a pointer instead of a GUID - confirmed via a real
                // isPointer()==false assert the first time this ran against an already-loaded font.
                const auto CompileStartTime = FontState.GetCompileStartTime();
                const bool bStale = pFont->m_TextureResourcePath.empty() == false
                    && (!std::filesystem::exists(pFont->m_TextureResourcePath) || std::filesystem::last_write_time(pFont->m_TextureResourcePath) < CompileStartTime);
                PreviewState = bStale ? preview_state::kReloading : preview_state::kReady;
            }
        }

        if (!FontState.empty())
        {
            // One window now, not two - there's only ever one texture per font (see this file's own
            // 2026-09-03 redesign: MTSDF/SDF/BITMAP each emit exactly one virtual texture; MTSDF's
            // former separate SDF companion is now that same texture's alpha channel).
            ImGui::SetNextWindowPos(ImVec2(20, 40), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(300, 340), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Font Texture"))
            {
                switch (PreviewState)
                {
                case preview_state::kNotCompiled: ImGui::TextDisabled("Not compiled yet - press Compile."); break;
                case preview_state::kReloading:   ImGui::TextDisabled("(reloading...)"); break;
                case preview_state::kReady:
                {
                    const auto Dims = pFont->m_pTexture->getTextureDimensions();
                    std::vector<ImVec4> GlyphRects;
                    if (RenderSettings.m_bShowGlyphBounds) GlyphRects = e28::CollectGlyphAtlasRects(*pFont->m_pFont);
                    e28::ShowZoomableImage("##AtlasView", static_cast<void*>(pFont->m_pTexture), Dims[0], Dims[1], AtlasView, ImVec2(0, 0), ImVec2(1, 1), RenderSettings.m_bShowGlyphBounds ? &GlyphRects : nullptr);
                    break;
                }
                default: break;
                }
            }
            ImGui::End();

            ImGui::SetNextWindowPos(ImVec2(20, 400), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(610, 380), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Font Live Preview"))
            {
                switch (PreviewState)
                {
                case preview_state::kNotCompiled: ImGui::TextDisabled("Not compiled yet - press Compile."); break;
                case preview_state::kReloading:   ImGui::TextDisabled("(reloading...)"); break;
                case preview_state::kReady:
                {
                    ImGui::Text("Glyphs: %u   Kern pairs: %u", pFont->m_pFont->m_nGlyphs, pFont->m_pFont->m_nKernPairs);
                    ImGui::Text("Line Height: %.3f   Ascender: %.3f   Descender: %.3f", pFont->m_pFont->m_LineHeight, pFont->m_pFont->m_Ascender, pFont->m_pFont->m_Descender);
                    ImGui::Separator();

                    // Live text render - the actual point of this whole pipeline: type a string in
                    // the Rendering Settings inspector and see it shaped/kerned/sampled through the
                    // real MSDF shader, not just the raw atlas texture above.
                    ImGui::Text("Live Text");
                    if (pFont->m_pTexture == nullptr)
                    {
                        ImGui::TextDisabled("(texture not compiled yet)");
                    }
                    else
                    {
                        const auto AtlasDims = pFont->m_pTexture->getTextureDimensions();

                        std::vector<e28::text_quad> Quads;
                        float                        PenXFinal = 0.0f;
                        // MTSDF/SDF are true distance fields - correct at ANY resolution, so
                        // kBasePixelsPerEm is just an arbitrary (but fine) reference size for them.
                        // BITMAP is NOT resolution-independent - it's pre-rasterized pixels at
                        // whichever fixed sizes the descriptor baked, so "native" means whichever
                        // baked size_group is closest to the user's OWN requested preview size
                        // (RenderSettings/BitmapPreviewSize - a font may have baked several sizes,
                        // there's no single universal "native" any more), render at THAT group's own
                        // real pixel size. Either way this is the FONT's own native resolution,
                        // unrelated to the view camera's zoom below.
                        const float NativeScale = (pFont->m_pFont->m_OutputType == xfont_rsc::output_type::BITMAP)
                            ? [&]
                              {
                                  const auto* pGroup = pFont->m_pFont->FindClosestSizeGroup(RenderSettings.m_BitmapPreviewSize);
                                  return (pGroup != nullptr) ? pGroup->m_PixelSize : RenderSettings.m_BitmapPreviewSize;
                              }()
                            : e28::render_settings::kBasePixelsPerEm;
                        e28::LayoutText(*pFont->m_pFont, AtlasDims[0], AtlasDims[1], RenderSettings.m_Text, Quads, PenXFinal, NativeScale);

                        // Camera-driven viewport, same interaction model as ShowZoomableImage (wheel
                        // zooms toward the cursor, drag pans, "Recenter" resets pan) but driving
                        // text_renderer::Draw's OWN camera (PxPerEm/PanPx) instead of stretching an
                        // already-rendered picture afterward - see Draw()'s own comment on why. Input
                        // is captured BEFORE Draw() runs so this frame's render already reflects it.
                        ImGui::PushID("##LiveTextView");
                        if (ImGui::SmallButton("Recenter")) LiveTextView.m_Pan = { 0.0f, 0.0f };
                        ImGui::SameLine();
                        ImGui::Text("Zoom: %.2fx", LiveTextView.m_Zoom);

                        ImGui::BeginChild("##canvas", ImGui::GetContentRegionAvail(), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);
                        // InvisibleButton asserts on a zero-size argument - see ShowZoomableImage's own
                        // comment on why GetContentRegionAvail() can legitimately be (0,0) here.
                        const ImVec2 RawAvail   = ImGui::GetContentRegionAvail();
                        const ImVec2 CanvasSize = ImVec2(std::max(RawAvail.x, 1.0f), std::max(RawAvail.y, 1.0f));
                        ImGui::InvisibleButton("##canvas_btn", CanvasSize);
                        const ImVec2 CanvasMin = ImGui::GetItemRectMin();
                        const bool   bHovered  = ImGui::IsItemHovered();

                        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                        {
                            const ImVec2 Delta = ImGui::GetIO().MouseDelta;
                            LiveTextView.m_Pan.m_X += Delta.x;
                            LiveTextView.m_Pan.m_Y += Delta.y;
                        }
                        if (bHovered && ImGui::GetIO().MouseWheel != 0.0f)
                        {
                            const float OldZoom = LiveTextView.m_Zoom;
                            LiveTextView.m_Zoom = std::clamp(OldZoom * (1.0f + ImGui::GetIO().MouseWheel * 0.1f), 0.05f, 40.0f);

                            // Zoom toward the cursor, not the fixed left-margin/vertical-center anchor -
                            // find the world point currently under the mouse (using the OLD camera),
                            // then solve for the Pan that puts that SAME world point back under the
                            // mouse with the NEW zoom. Mirrors ShowZoomableImage's own cursor-zoom math,
                            // just expressed in this camera's own AnchorPx/PxPerEm terms (see Draw()'s
                            // own comment) instead of a canvas-center-relative one.
                            const float  OldPxPerEm = NativeScale * OldZoom;
                            const float  NewPxPerEm = NativeScale * LiveTextView.m_Zoom;
                            const ImVec2 Mouse      = ImGui::GetIO().MousePos;
                            const float  MouseCanvasX = Mouse.x - CanvasMin.x;
                            const float  MouseCanvasY = Mouse.y - CanvasMin.y;
                            const float  WorldX = (MouseCanvasX - e28::text_renderer::kLeftMarginPx - LiveTextView.m_Pan.m_X) / OldPxPerEm;
                            const float  WorldY = (CanvasSize.y * 0.5f + LiveTextView.m_Pan.m_Y - MouseCanvasY) / OldPxPerEm;
                            LiveTextView.m_Pan.m_X = MouseCanvasX - e28::text_renderer::kLeftMarginPx - WorldX * NewPxPerEm;
                            LiveTextView.m_Pan.m_Y = MouseCanvasY - CanvasSize.y * 0.5f + WorldY * NewPxPerEm;
                        }

                        const int   ViewW   = std::max(static_cast<int>(CanvasSize.x), 8);
                        const int   ViewH   = std::max(static_cast<int>(CanvasSize.y), 8);
                        const float PxPerEm = NativeScale * LiveTextView.m_Zoom;

                        // pFont->m_pTexture passed for BOTH slots: every OutputType now emits exactly
                        // one texture (see this file's own 2026-09-03 redesign), so there's no separate
                        // SDF companion object any more; the shader picks how to interpret it via
                        // pc.uOutputType (fill: median3 for MTSDF, R for SDF, alpha-as-coverage for
                        // BITMAP; outline: alpha for MTSDF, R for SDF, unsupported for BITMAP).
                        if (auto Err = TextRenderer.Draw
                            ( Device, MainWindow, *pFont->m_pFont
                            , pFont->m_pTexture, pFont->m_pTexture
                            , Quads, ViewW, ViewH, PxPerEm, LiveTextView.m_Pan
                            , RenderSettings.m_bShowOutline, RenderSettings.m_OutlineWidth
                            , RenderSettings.m_bBold, RenderSettings.m_FontWeight
                            , RenderSettings.m_bShowShadow, xmath::fvec2{ RenderSettings.m_ShadowOffsetX, RenderSettings.m_ShadowOffsetY }
                            , RenderSettings.m_bBevel, RenderSettings.m_BevelWeight
                            , RenderSettings.m_bItalic, RenderSettings.m_ItalicShear
                            , RenderSettings.m_bShowGlyphBounds
                            ); Err)
                        {
                            ImGui::TextDisabled("(text render failed: %d)", Err);
                        }
                        else
                        {
                            // Rendered directly at [ViewW x ViewH] above (matching this canvas 1:1) -
                            // just display it, no further scaling. UV1 crops the bucket-snapped scene
                            // texture down to the actually-used sub-rect (see SnapToBucket's own
                            // comment); ImgMax intentionally uses CanvasSize, not m_UsedW/H, so the
                            // image exactly fills the canvas even when ViewW/H got clamped up from a
                            // sub-8px CanvasSize.
                            const ImVec2 UV1
                            ( static_cast<float>(TextRenderer.m_UsedW) / static_cast<float>(TextRenderer.m_SceneW)
                            , static_cast<float>(TextRenderer.m_UsedH) / static_cast<float>(TextRenderer.m_SceneH)
                            );
                            const ImVec2 ImgMax(CanvasMin.x + CanvasSize.x, CanvasMin.y + CanvasSize.y);
                            ImGui::GetWindowDrawList()->AddImage(static_cast<void*>(&TextRenderer.m_SceneTexture), CanvasMin, ImgMax, ImVec2(0, 0), UV1);
                        }
                        ImGui::EndChild();
                        ImGui::PopID();
                    }
                    break;
                }
                default: break;
                }
            }
            ImGui::End();
        }

        //
        // Main menu bar - Save/Compile/Feedback mirror E23_SkeletonEditor's so this editor
        // doesn't feel like a different tool. Saving the descriptor is what actually kicks off
        // a recompile (see the m_OnCompilationState subscriber above); this is just the button
        // + status readout.
        //
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("\xEE\x98\xAB Home\xee\xa5\xb2"))
            {
                if (ImGui::MenuItem("Resource Browser", "Ctrl-Space"))
                    AsserBrowser.Show(true);

                ImGui::Separator();
                {
                    const bool bDisableSave = !e10::g_LibMgr.isReadyToSave() && FontState.empty();
                    if (bDisableSave) ImGui::BeginDisabled();
                    if (ImGui::MenuItem("\xEE\x9D\x8E Save ", "Ctrl+S"))
                    {
                        xproperty::settings::context Context;
                        e10::g_LibMgr.Save(Context);
                    }
                    if (bDisableSave) ImGui::EndDisabled();
                }
                ImGui::EndMenu();
            }

            ImGui::SameLine(410);

            if (!FontState.empty())
            {
                const auto LogCopy = FontState.GetLog();
                xcontainer::lock::scope lk(*LogCopy);
                auto& Log = LogCopy->get();

                bool bDisable = Log.m_Result == e10::compilation::historical_entry::result::COMPILING
                             || Log.m_Result == e10::compilation::historical_entry::result::COMPILING_WARNINGS;

                std::vector<std::string> ValidationErrors;
                if (!bDisable)
                {
                    FontState.m_Descriptor.Validate(ValidationErrors);
                    if (!ValidationErrors.empty()) bDisable = true;
                }

                if (bDisable) ImGui::BeginDisabled();
                if (ImGui::Button("\xEF\x96\xB0 Compile "))
                    FontState.SaveDescriptor();
                if (bDisable) ImGui::EndDisabled();

                std::uint32_t Color = IM_COL32(255, 255, 255, 255);
                switch (Log.m_Result)
                {
                case e10::compilation::historical_entry::result::COMPILING_WARNINGS: Color = IM_COL32(255, 255, 0,   255); break;
                case e10::compilation::historical_entry::result::COMPILING:          Color = IM_COL32(0,   255, 0,   255); break;
                case e10::compilation::historical_entry::result::FAILURE:            Color = IM_COL32(255, 170, 140, 255); break;
                case e10::compilation::historical_entry::result::SUCCESS_WARNINGS:   Color = IM_COL32(255, 255, 0,   255); break;
                case e10::compilation::historical_entry::result::SUCCESS:            Color = IM_COL32(255, 255, 255, 255); break;
                }

                ImGui::PushStyleColor(ImGuiCol_Text, Color);
                if (ImGui::Button("Feedback:\xee\xa5\xb2"))
                {
                    const ImVec2 ButtonPos  = ImGui::GetItemRectMin();
                    const ImVec2 ButtonSize = ImGui::GetItemRectSize();
                    ImGui::SetNextWindowPos(ImVec2(ButtonPos.x, ButtonPos.y + ButtonSize.y));
                    ImGui::OpenPopup("Feedback");
                }
                ImGui::PopStyleColor();

                if (ImGui::BeginPopup("Feedback"))
                {
                    ImGui::BeginChild("###Feedback-Child", ImVec2(600, 300));
                    ImGui::PushTextWrapPos(600);

                    for (auto& S : ValidationErrors)
                        ImGui::TextUnformatted(S.data(), S.data() + S.size());

                    if (!Log.m_Log.empty())
                        ImGui::TextUnformatted(Log.m_Log.data(), Log.m_Log.data() + Log.m_Log.size());

                    ImGui::PopTextWrapPos();
                    ImGui::EndChild();
                    ImGui::EndPopup();
                }
            }

            ImGui::EndMainMenuBar();
        }

        AsserBrowser.Render(e10::g_LibMgr, xresource::g_Mgr);

        if (auto SelAsset = AsserBrowser.getSelectedAsset(); SelAsset.empty() == false && SelAsset.m_Type == xrsc::font_type_guid_v)
        {
            e28::LoadFont(FontState, AsserBrowser.getSelectedLibrary(), SelAsset, Inspector);
        }

        // A successful recompile means the runtime resource changed under us - reload the same
        // way a fresh selection would. Preserve the log across the reload (clear() inside
        // LoadFont would otherwise wipe it back to a blank SUCCESS entry, discarding the very
        // compile output the Feedback popup is about to show).
        if (FontState.m_bReload)
        {
            FontState.m_bReload = false;
            TextRenderer.Reset(Device); // drop any cached pipeline_instance before the texture underneath it reloads - see Reset()'s own comment
            const auto SavedLog = FontState.GetLog();
            e28::LoadFont(FontState, FontState.m_LibraryGUID, FontState.m_InfoGUID, Inspector);
            FontState.SetLog(SavedLog);
            // The background compile+reload isn't synchronized to render frames at all (texture
            // compression in particular can take a noticeable fraction of a second), so this is a
            // generous fixed margin, not a tight estimate - showing "(reloading...)" for up to ~1s is a
            // fine tradeoff for a dev tool against crashing. See the field's own comment.
            FontState.m_ReloadCooldown = 60;
        }
        else if (FontState.m_ReloadCooldown > 0)
        {
            --FontState.m_ReloadCooldown;
        }

        if (!FontState.empty())
        {
            xproperty::settings::context Context;
            ImGui::SetNextWindowPos(ImVec2(600, 40), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(360, 500), ImGuiCond_FirstUseEver);
            Inspector.Show(Context, []{});

            // See render_settings::m_CurrentFontOutputType's own comment - this is the only place that
            // knows both "which font is selected" (pFont) and "the render_settings the inspector below
            // is about to render", so it's the one spot that can keep the mirror in sync.
            RenderSettings.m_CurrentFontOutputType = (pFont != nullptr && pFont->m_pFont != nullptr) ? pFont->m_pFont->m_OutputType : xfont_rsc::output_type::MTSDF;

            ImGui::SetNextWindowPos(ImVec2(600, 360), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(360, 200), ImGuiCond_FirstUseEver);
            RenderInspector.Show(Context, []{});
        }

        xgpu::tools::imgui::Render();
        MainWindow.PageFlip();

        // Let the resource manager know we have changed the frame
        xresource::g_Mgr.OnEndFrameDelegate();
    }

    xgpu::tools::imgui::Shutdown();
    return 0;
}
