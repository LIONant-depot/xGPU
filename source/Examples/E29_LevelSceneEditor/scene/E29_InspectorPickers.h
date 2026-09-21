#pragma once

// Resource-picker wiring shared by every inspector with a resource reference.
// Split out of E29_LevelSceneEditorKit.h; included from there at the position this code used to occupy.
namespace e29
{
    // Registers the two stateless resource-picker delegates (m_OnResourceWigzmos/m_OnResourceBrowser)
    // on an entity/component inspector - identical wiring every editor with a resource-ref property
    // needs, extracted here so it's one call instead of re-typing both Register<...> lambdas per
    // editor.
    inline void WireResourcePickerCallbacks(xproperty::inspector& Inspector) noexcept
    {
        Inspector.m_OnResourceWigzmos.Register<[](xproperty::inspector&, const xproperty::type::object&, void*, std::string_view, bool& bOpen, const xresource::full_guid& PreFullGuid)
        {
            e29::RenderResourceWigzmos(bOpen, PreFullGuid);
        }>();
        Inspector.m_OnResourceBrowser.Register<[](xproperty::inspector&, const xproperty::type::object&, void*, std::string_view Path, bool& bOpen, xresource::full_guid& Out, std::span<const xresource::type_guid> Filters)
        {
            const void* pUID = reinterpret_cast<const void*>(std::hash<std::string_view>{}(Path));
            e29::ResourceBrowserPopup(pUID, bOpen, Out, Filters);
        }>();
    }
}
