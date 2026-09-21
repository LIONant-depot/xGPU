# Screenshot capture and TGA writing

> How to use xgpu::window::Screenshot() for visual verification, the two real gotchas in doing so, and the xbmp_tools-based way to write the result as a real PNG (preferred over a hand-rolled TGA writer whenever xbmp_tools is already linked into the target)
>
> Migrated from the working notes on 2026-09-21 (last edited 2026-08-27).

**Update (2026-08-25, E27_NodeOS):** when the target already links `xbmp_tools` (check CMakeLists.txt's
`FetchAndPopulate(".../xbmp_tools.git", ...)` and whether another example in the SAME executable already
`#include`s it, e.g. E05_BitmapInspector.h) - prefer that over the hand-rolled TGA writer described
below, for a real compressed PNG with no manual header bytes. BUT: `xbitmap::setup()` (`xbitmap.cpp`)
has the *exact same* leading-mip-offset-entry requirement as the raw-span constructor in gotcha 1 below -
it is NOT exempt, and its own asserts don't catch the mistake at the call site, they just abort() once
`getFrameCount()`/`getFaceSize()` math comes out wrong. `m_pData` is always `reinterpret_cast<mip*>
(Data.data())` - slot 0 of whatever span you pass is ALWAYS read back as an `xbitmap::mip{ m_Offset }`
header, never plain pixel 0. The correct construction for a plain 1-mip/1-frame capture: allocate
`1 + W*H` uint32 slots, set slot 0 to `sizeof(xbitmap::mip)` (the byte offset to skip past this one
header entry), copy the real `W*H` pixels into slots `[1..W*H]`, then call `Bitmap.setup(W, H,
xbitmap::format::B8G8R8A8, /*FaceSize*/W*H*sizeof(uint32_t), std::as_writable_bytes(std::span(Padded)),
/*bFreeMemoryOnDestruction*/false, /*nMips*/1, /*nFrames*/1)` - FaceSize is the pixel payload size only,
excluding the header slot (`Padded.size()*4 == FaceSize + sizeof(mip)`, matching `setupFromColor`'s own
`sizeof(xcolori)*(Data.size()-1)` pattern one level up). Skipping the padding and passing the raw pixel
buffer directly compiles fine and does NOT silently misread - it crashes with `abort()`/a Debug Error
dialog the first time `SaveSTDImage` walks the (wrong) mip table, which is what happened when this was
first tried without it. `B8G8R8A8` matches the captured buffer's byte order exactly (see gotcha 2
below), so no channel conversion is needed on top of this. Then `xbmp::tools::writers::SaveSTDImage
(std::wstring_view Path, const xbitmap&)` (from `dependencies/xbmp_tools/src/xbmp_tools.h`) writes real
PNG/BMP/TGA/JPG, dispatching on the path's own extension via stb_image_write - and sidesteps gotcha 2
below entirely (stb's writers don't have the honors-alpha viewer problem a naively-viewed 32bpp TGA
does). Only fall back to a hand-rolled writer (below) when the target does NOT already link xbmp_tools
and pulling it in isn't worth it for a one-off.

`xgpu::window::Screenshot(std::vector<uint32_t>& Dest, int& Width, int& Height)` exists on the engine's
window class but had never been called by any consumer before 2026-08-15 - it's a real, working capture
path (call before `PageFlip()`, read `Dest`/`Width`/`Height` only after `PageFlip()` returns - the actual
GPU readback happens inside `PageFlip`), useful for headlessly verifying UI/rendering changes without a
human at the monitor.

**Two real gotchas, both confirmed via direct testing:**
1. `xbitmap::SaveTGA()` writes far fewer bytes than the pixel buffer it's given when constructed from a
   raw span via `xbitmap(span, W, H, bReleaseWhenDone)` - the constructor itself also expects a leading
   4-byte mip-offset-table entry before the pixel data, not just bare pixels (assert:
   `(W*H*sizeof(xcolori) + sizeof(int)) == DataSize`). Simplest reliable path: skip `xbitmap` entirely and
   write an uncompressed 32bpp TGA by hand from the raw `Dest` buffer (18-byte header, then the pixels
   as-is - captured format is B8G8R8A8, which is TGA's native byte order, so no channel swap needed).
2. **Not a bug, but looks exactly like one:** the captured back-buffer's alpha channel contains real,
   varying values (glyph-coverage-looking data) since it's whatever the render pipeline happened to leave
   there - a presentation swapchain's alpha is ignored at present time (opaque composite), so a human
   looking at the monitor never sees it. Any image viewer/library that honors alpha when displaying the
   captured TGA (PIL's default `.convert('RGBA')` display, some viewers) will show what looks like severely
   corrupted/scrambled text while flat color regions look fine - because RGB is genuinely correct and
   alpha is genuinely garbage-looking-but-irrelevant. **Fix: always drop the alpha channel (extract RGB
   only) before viewing a captured frame.** Spent a full debugging pass chasing this as a Vulkan
   sync/format/row-stride bug before realizing it was purely a viewing artifact - check this first next
   time.

**How to apply:** when asked to visually verify an xGPU UI/rendering change, this capture method plus a
hand-rolled TGA writer plus RGB-only viewing is fast and reliable - much cheaper than asking the user to
screenshot manually, and confirmed to correctly show real ImGui panel content (dropdowns, checkboxes,
colored vector fields all render legibly once alpha is dropped).

**Update (2026-08-27, xproperty inspector work):** reached for an ad-hoc PowerShell `PrintWindow`+
`SetWindowPos` capture instead of this in-process method, purely out of habit - it has real, wasted-effort
quirks this method doesn't: `EnumWindows` can latch onto the wrong ImGui sub-window (captured "Dear ImGui
Demo" instead of the target panel, since every `xproperty::inspector` window is a separate floating ImGui
window, not a separate OS window, and PrintWindow just grabs whatever OS-level HWND it's given); resizing
the OS window via `SetWindowPos` does NOT resize the app's own swapchain/render viewport (no WM_SIZE
handling wired up for that in this engine), so the newly-revealed canvas area comes back flat gray/stale
rather than newly-rendered content - it does not reveal windows positioned outside the original small
canvas the way you'd hope. Prefer `xgpu::window::Screenshot()` + `WriteScreenshotImage`-style PNG writing
(above) every time going forward for this engine - it captures the real, current backbuffer regardless of
which ImGui windows are open/where they're positioned, with none of PrintWindow's window-identification or
stale-viewport problems.
