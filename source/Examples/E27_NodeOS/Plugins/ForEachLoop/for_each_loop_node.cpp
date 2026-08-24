// ForEachLoop - a control-flow node under the flat-spine, recursive-descent model
// (NODE_SCRIPTING_DESIGN.md section 4). No exec pins at all (unlike the earlier ForEachLoopNode
// plugin from the Unreal-comparison exercise, which still used the old exec-pin shape and is
// unrelated to this one). Its body is whatever nodes physically follow it in the same spine, up to
// its owned End marker.
//
// Span is typed "Span<Any>" - a container wildcard, not a fixed "Array<Float>" - named for
// std::span specifically, since that's what actually accepts a std::vector, std::array, C array, or
// any other contiguous container of any element type T, without owning or copying it. Once something
// is wired in, Element/Index resolve off it (see E27_NodeOS_Editor.cpp's ResolveNodeWildcardType/
// UnwrapSpanElementType: T is unwrapped from whatever "Span<T>" got wired to Span). Element is T&,
// not a copy - ReadOnlyElement (defaults to true/read-only, i.e. const T&) decides whether the loop
// body can write back through it; the node's own Element pin label shows const/& directly (see
// DisplayTypeText) so this isn't an invisible side-panel-only setting. Index is always Int
// regardless of T - an array index is inherently integral, it doesn't vary with the element type the
// way Element does.
//
// Execute is a no-op - this is the UI/ownership half of the feature only, compilation isn't wired up
// yet.
#include "../../SDK/xnode_os_plugin_api.h"
#include "../../SDK/xnode_os_shared_types.h"
#include <string>

namespace
{
    struct for_each_loop_node : xnode_os_node
    {
        bool        m_bReadOnlyElement = true;
        std::string m_ResolvedType     = "Any"; // pushed in by the host each frame - see "Resolved Type" below

        XPROPERTY_VDEF
        ( "for_each_loop_node2", for_each_loop_node
        , obj_member<"ReadOnlyElement", &for_each_loop_node::m_bReadOnlyElement
            , member_help<"Whether the loop body can write back through Element (false) or only read it (true, the default) - shown directly on the Element pin's own const/& label, not just here.">>
        , obj_member<"Resolved Type", &for_each_loop_node::m_ResolvedType
            , member_flags<xproperty::flags::SHOW_READONLY, xproperty::flags::DONT_SAVE>
            , member_help<"The element type Element/Index currently resolve to, unwrapped from whatever's wired into Span - live debug info, pushed in by the host each frame, never itself saved.">>
        )

        std::span<const xnode_os_port_desc> getInputs() const noexcept override
        {
            static const xnode_os_port_desc s_Inputs[1] = { { "Span", "Span<Any>" } };
            return s_Inputs;
        }

        // "End" is the read-only ownership pin - the host creates and connects it automatically,
        // to this node's own owned End marker, in CreateOwnedPair. Never dragged by the user (see
        // link_instance::m_bReadOnly in E27_NodeOS_Editor.cpp). Must stay LAST - the host locates it
        // by "getOutputs().size() - 1" when wiring the ownership link (see CreateOwnedPair/
        // SetEndElseState in E27_NodeOS_Editor.cpp).
        std::span<const xnode_os_port_desc> getOutputs() const noexcept override
        {
            // Element/Index only have meaning inside this loop's own body - flagged m_bLocalScope so
            // E27_NodeOS_Editor.cpp's IsDataLinkScopeValid restricts them to links whose other
            // endpoint is physically inside this node's own scope span. Previously unflagged, which
            // let a wire from Element/Index reach ANY node anywhere (including after the loop closes,
            // or a completely unrelated spine) - a real bug, now fixed by the same mechanism a
            // Function's mirrored parameter pins use.
            static const xnode_os_port_desc s_Outputs[3] = { { "Element", "Any", true, true, true }, { "Index", "Int", true, true, true }, { "End", "Scope" } };
            return s_Outputs;
        }

        void Execute(void** /*Inputs*/, void** /*Outputs*/) noexcept override {}
    };
}
XPROPERTY_VREG(for_each_loop_node)

namespace
{
    struct for_each_loop_node_factory : xnode_os_node_factory
    {
        XPROPERTY_VDEF("for_each_loop_node_factory2", for_each_loop_node_factory)

        std::string_view getVersion()  const noexcept override { return "1.0"; }
        std::string_view getName()     const noexcept override { return "ForEachLoop"; }
        std::string_view getCategory() const noexcept override { return "Flow Control"; }

        bool             needsOwnedEndMarker()        const noexcept override { return true; }
        std::string_view getOwnedEndMarkerPluginDir() const noexcept override { return "End"; }

        xnode_os_node& CreateNodeInstance() override
        {
            auto* pNode = new for_each_loop_node();
            pNode->m_pFactory = this;
            return *pNode;
        }

        void DestroyNodeInstance(xnode_os_node& Node) override
        {
            delete static_cast<for_each_loop_node*>(&Node);
        }
    };
}
XPROPERTY_VREG(for_each_loop_node_factory)

extern "C" XNODE_OS_EXPORT xnode_os_node_factory& NodeOS_CreateFactory(ixnode_os_host& /*Host*/) noexcept
{
    return *new for_each_loop_node_factory();
}

extern "C" XNODE_OS_EXPORT void NodeOS_DestroyFactory(xnode_os_node_factory& Factory) noexcept
{
    delete static_cast<for_each_loop_node_factory*>(&Factory);
}
