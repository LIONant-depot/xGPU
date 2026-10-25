# OffsetAllocator in xGPU

Upstream: https://github.com/sebbbi/OffsetAllocator, git 3610a7377088b1e8c8f1525f458c96038a4e6fc0 (the copy Godot vendors in
thirdparty/offset_allocator), MIT (LICENSE).

One local change: every function definition in `offsetAllocator.cpp` is `inline`, and `offsetAllocator_inline.hpp` includes the .cpp, so
each binary that uses the allocator (LIONCore, LIONRender.dll, the renderer tests' static library) compiles its own copy without duplicate
symbols. Include `offsetAllocator_inline.hpp`, never the .cpp directly.
