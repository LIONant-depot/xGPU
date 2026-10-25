// sebbbi/OffsetAllocator (MIT, LICENSE) as a header: every binary that uses it (xGPU's LIONCore, the LIONRender DLL, the tests' static
// library) gets its own copy without duplicate symbols. The one change to upstream (git 3610a73, as Godot vendors it): every function
// definition of offsetAllocator.cpp is marked inline (see PATCHES.md)
#pragma once
#include "offsetAllocator.hpp"
#include "offsetAllocator.cpp"
#undef ASSERT
