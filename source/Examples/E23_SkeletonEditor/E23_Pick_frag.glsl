#version 450
#extension GL_ARB_separate_shader_objects  : enable
#extension GL_ARB_shading_language_420pack : enable

// GPU object-ID picking: reuses draw_vert.glsl's vertex stage verbatim (position transform only -
// Color/UV below are its outputs, unused here but the struct must match or the stage interface
// mismatches), paired with this fragment shader instead of draw_frag.glsl's textured one.
//
// One bone's wedge/sphere geometry is drawn per call, with a per-draw BoneID pushed via the SAME
// push-constant block draw_vert.glsl reads its L2C from. Blend's color write mask is off for this
// pipeline, so outFragColor never reaches the screen - only PickBuffer.BestKey, the actual output
// of this pass, does.

layout(location = 0) in struct { vec4 Color; vec2 UV; } In;

layout (location = 0) out vec4 outFragColor;

layout (std140, push_constant) uniform PushConsts
{
    mat4 L2C;    // unused here - present only so BoneID lands at the offset the C++ side expects
    int  BoneID;
} pushConsts;

// A naked "Pick.PickedID = BoneID" store here is NOT resolved by the hardware depth test the way
// a color/depth attachment write is. early_fragment_tests only gates whether THIS invocation's
// shader runs at all (it must pass depth at the moment it's tested) - it says nothing about the
// relative order in which multiple PASSING invocations, from different draws covering the same
// pixel, perform their SSBO stores. Two bones can each individually pass depth (one after the
// other, as the depth buffer is progressively updated across draws) and both be fully eligible to
// store - whichever store physically lands last in memory wins, and that has no required
// relationship to which of them was actually closest. That's the real reason this alternated
// between two overlapping bones no matter how the depth test itself was tuned (LESS vs
// LESS_OR_EQUAL, a bias between candidates, etc.) - none of that changes who wins the store race.
//
// Fix: don't store the ID directly. Pack (quantized depth, BoneID) into one key and combine
// candidates with atomicMin. Atomic reduction is genuinely order-independent - the buffer
// converges to the true minimum key regardless of which invocation's store physically lands last,
// which a plain store can never guarantee. Depth sorts the high bits (so "closest" always wins
// regardless of draw order) and BoneID breaks any exact tie deterministically in the low bits.
layout (std430, set = 2, binding = 0) buffer PickBuffer
{
    uint BestKey; // (quantized depth << kBoneIDBits) | (BoneID & kBoneIDMask); 0xFFFFFFFF = "no hit"
} Pick;

const uint kBoneIDBits  = 12u;            // supports up to 4096 bones
const uint kBoneIDMask  = 0xFFFu;
const uint kDepthLevels = (1u << (32u - kBoneIDBits)) - 1u; // 20 bits of depth precision

// Still worth keeping: lets a candidate that's occluded by real, opaque scene geometry (e.g.
// behind the grid) skip the atomic op entirely instead of contesting a key it could never win.
layout(early_fragment_tests) in;

void main()
{
    uint DepthQuant = uint(clamp(gl_FragCoord.z, 0.0, 1.0) * float(kDepthLevels) + 0.5);
    uint Key        = (DepthQuant << kBoneIDBits) | (uint(pushConsts.BoneID) & kBoneIDMask);
    atomicMin(Pick.BestKey, Key);
    outFragColor = vec4(0.0);
}
