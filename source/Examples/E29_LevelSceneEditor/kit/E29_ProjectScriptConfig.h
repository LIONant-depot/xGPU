#ifndef E29_PROJECT_SCRIPT_CONFIG_H
#define E29_PROJECT_SCRIPT_CONFIG_H
#pragma once

// Project-level Scripting-resource build membership - {ProjectPath}\Project.config\
// Script.config.txt, same fixed settings-file convention as Library.config.txt/
// SystemOrder.config.txt (plain xtextfile + xproperty::sprop::serializer::Stream against an
// ordinary XPROPERTY_DEF'd struct, no descriptor/factory/resource-pipeline visibility).

namespace e29
{
    struct script_config
    {
        // Script-Module resources that are part of this project's build - the project's own build
        // membership list (see AddProjectModuleReference/RemoveProjectModuleReference,
        // E29_Commands_Scripting.h). A bare list of guids, same convention library::m_ParentLibraries
        // already uses - no separate "which library" qualifier, resolved via the global resource guid
        // space.
        std::vector<xresource::full_guid> m_ModuleRefs = {};

        XPROPERTY_DEF
        ( "ScriptConfig", script_config
        , obj_member<"ModuleRefs", &script_config::m_ModuleRefs>
        )
    };
    XPROPERTY_REG(script_config)

    inline script_config g_ScriptConfig; // one per process - matches g_pUndo/g_pGameMgr's own "one instance" assumption

    inline xerr SaveScriptConfig(const std::wstring& ProjectPath, const script_config& Config) noexcept
    {
        const auto ConfigFolder = std::format(L"{}\\Project.config", ProjectPath);
        if (!std::filesystem::exists(ConfigFolder)) std::filesystem::create_directories(ConfigFolder);

        xtextfile::stream Stream;
        if (auto Err = Stream.Open(false, std::format(L"{}\\Script.config.txt", ConfigFolder), { xtextfile::file_type::TEXT }); Err)
            return Err;

        xproperty::settings::context Context;
        return xproperty::sprop::serializer::Stream(Stream, const_cast<script_config&>(Config), Context);
    }

    // Missing file (nothing saved yet) is not an error - Config is left default (empty).
    inline xerr LoadScriptConfig(const std::wstring& ProjectPath, script_config& Config) noexcept
    {
        xtextfile::stream Stream;
        if (auto Err = Stream.Open(true, std::format(L"{}\\Project.config\\Script.config.txt", ProjectPath), { xtextfile::file_type::TEXT }); Err)
            return {};

        xproperty::settings::context Context;
        return xproperty::sprop::serializer::Stream(Stream, Config, Context);
    }
}

#endif // E29_PROJECT_SCRIPT_CONFIG_H
