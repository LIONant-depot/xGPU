#ifndef PLUGIN_MGR_HPP
#define PLUGIN_MGR_HPP
#pragma once

#define NOMINMAX
#include <windows.h>
#include <iostream>
#include <filesystem>
#include "dependencies/xstrtool/source/xstrtool.h"

namespace e10
{
    //------------------------------------------------------------------------------------------------

    void GetFileTimestamp(const std::wstring& filePath, SYSTEMTIME& systemTime)
    {
        HANDLE hFile = CreateFile(
            filePath.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );

        if (hFile == INVALID_HANDLE_VALUE) {
            std::cerr << "Error opening file: " << xstrtool::To(filePath) << std::endl;
            return;
        }

        FILETIME fileTime;
        if (!GetFileTime(hFile, NULL, NULL, &fileTime)) {
            std::cerr << "Error getting file time: " << xstrtool::To(filePath) << std::endl;
            CloseHandle(hFile);
            return;
        }

        if (!FileTimeToSystemTime(&fileTime, &systemTime)) {
            std::cerr << "Error converting file time to system time: " << xstrtool::To(filePath) << std::endl;
        }

        CloseHandle(hFile);
    }

    //------------------------------------------------------------------------------------------------

    inline void ConsoleCompliantString( std::string& CurrentBuffer, std::size_t& OldLength, std::size_t& LastLinePos, const std::string_view NewChunk ) noexcept
    {
        auto NewLength = CurrentBuffer.length() + NewChunk.size();
        CurrentBuffer.append(NewChunk.data(), NewChunk.size());

        // Find any '\r' and delete all previous character until the previous line
        // This allows us to have the same output as a proper console
        for (auto i = OldLength; i < NewLength; ++i)
        {
            if (CurrentBuffer[i] == '\r')
            {
                if ((i + 1) != NewLength)
                {
                    // If we have this character right after a new line then skip it
                    if (CurrentBuffer[i + 1] == '\n') continue;
                }
                else
                {
                    // We will need to wait until we have more data...
                    NewLength -= 1;
                    break;
                }

                auto ToErrase = i - LastLinePos + 1;
                CurrentBuffer.erase(LastLinePos, ToErrase);
                NewLength -= ToErrase;
                i = LastLinePos;

                // Find a new LastLine
                while (LastLinePos > 0)
                {
                    if (CurrentBuffer[LastLinePos] == '\n')
                    {
                        LastLinePos++;
                        break;
                    }
                    LastLinePos--;
                }
            }
            else if (CurrentBuffer[i] == '\n')
            {
                LastLinePos = i + 1;
            }
        }

        OldLength = NewLength;
    }

    struct asset_plugins_db
    {
        struct pipeline_plugin
        {
            std::string                         m_TypeName;
            std::wstring                        m_ResourceFileExtension;
            xresource::type_guid                m_TypeGUID;
            std::vector<xresource::type_guid>   m_RunAfter;
            std::string                         m_Icon;
            std::wstring                        m_DebugCompiler;
            SYSTEMTIME                          m_DebugCompilerTimeStamp;
            std::wstring                        m_ReleaseCompiler;
            SYSTEMTIME                          m_ReleaseCompilerTimeStamp;
            std::wstring                        m_PluginPath;
            std::wstring                        m_CompilationScript;

                             pipeline_plugin()                  = default;
                            ~pipeline_plugin()                  = default;
                             pipeline_plugin(pipeline_plugin&&) = default;
            pipeline_plugin& operator = (pipeline_plugin&&)     = default;

            //------------------------------------------------------------------------------------------------

            std::string Serialize(bool bRead, std::wstring_view Path)
            {
                //
                // Read the config file
                //
                xtextfile::stream Stream;
                if (auto Err = Stream.Open(bRead, Path, xtextfile::file_type::TEXT); Err)
                {
                    return std::format("Error: Failed to read {} with error {}", xstrtool::To(Path), Err.getMessage() );
                }

                //
                // Read the configuration file
                //
                xproperty::settings::context    Context{};
                if (auto Err = xproperty::sprop::serializer::Stream(Stream, *this, Context); Err)
                {
                    return std::format("Error: Failed to read {} with error {}", xstrtool::To(Path), Err.getMessage());
                }

                return {};
            }

            XPROPERTY_DEF
            ( "PipelinePlugin", pipeline_plugin
            , obj_member< "TypeName"
                , &pipeline_plugin::m_TypeName
                , member_flags<xproperty::flags::SHOW_READONLY
                >>
            , obj_member< "FileExtension"
                , &pipeline_plugin::m_ResourceFileExtension
                , member_flags<xproperty::flags::SHOW_READONLY
                >>
            , obj_member< "TypeGUID"
                , &pipeline_plugin::m_TypeGUID
                , member_ui<std::uint64_t>::drag_bar< 0.0f, 0, std::numeric_limits<std::uint64_t>::max(), "%llX">
                , member_flags<xproperty::flags::SHOW_READONLY
                >>
            , obj_member< "Icon"
                , &pipeline_plugin::m_Icon
                , member_flags<xproperty::flags::SHOW_READONLY
                >>
            , obj_scope< "Details"
                , obj_member_ro< "PluginPath"
                    , &pipeline_plugin::m_PluginPath
                    , member_flags<xproperty::flags::SHOW_READONLY
                    >>
                , obj_member< "RunAfter"
                    , &pipeline_plugin::m_RunAfter
                    , member_flags<xproperty::flags::SHOW_READONLY
                    >>
                , obj_member< "DebugCompiler"
                    , &pipeline_plugin::m_DebugCompiler
                    , member_flags<xproperty::flags::SHOW_READONLY
                    >>
                , obj_member_ro< "DebugCompilerTimeStamp"
                    , +[](pipeline_plugin& O, bool, std::string& Value )
                    {
                        Value = std::format("{:02}/{:02}/{:04} {:02}:{:02}:{:02}"
                            , O.m_DebugCompilerTimeStamp.wDay
                            , O.m_DebugCompilerTimeStamp.wMonth
                            , O.m_DebugCompilerTimeStamp.wYear
                            , O.m_DebugCompilerTimeStamp.wHour
                            , O.m_DebugCompilerTimeStamp.wMinute
                            , O.m_DebugCompilerTimeStamp.wSecond
                            );
                    }
                    >
                , obj_member< "ReleaseCompiler"
                    , &pipeline_plugin::m_ReleaseCompiler
                    , member_flags<xproperty::flags::SHOW_READONLY
                    >>
                , obj_member_ro < "ReleaseCompilerTimeStamp"
                    , +[](pipeline_plugin& O, bool, std::string& Value)
                    {
                        Value = std::format("{:02}/{:02}/{:04} {:02}:{:02}:{:02}"
                            , O.m_ReleaseCompilerTimeStamp.wDay
                            , O.m_ReleaseCompilerTimeStamp.wMonth
                            , O.m_ReleaseCompilerTimeStamp.wYear
                            , O.m_ReleaseCompilerTimeStamp.wHour
                            , O.m_ReleaseCompilerTimeStamp.wMinute
                            , O.m_ReleaseCompilerTimeStamp.wSecond
                        );
                    }
                    >
                , obj_member< "CompilationScript"
                    , &pipeline_plugin::m_CompilationScript
                    , member_flags<xproperty::flags::SHOW_READONLY
                    >>
                , member_ui_open<false
                >>
            )
        };
        //------------------------------------------------------------------------------------------------

        void SetupProject( std::wstring_view ProjectPath )
        {
            //
            // Go throw all the plugins and read their information
            //
            auto PluginPath = std::format(L"{}\\cache\\plugins", ProjectPath);
            for (const auto& entry : std::filesystem::directory_iterator(PluginPath))
            {
                if (std::filesystem::is_directory(entry) == false) continue;

                std::wstring ConfigFile = std::format(L"{}\\plugin.config\\resource_pipeline.config.txt", entry.path().c_str() );
                if (std::filesystem::exists(ConfigFile) == false) continue;

                // Read the plugin
                pipeline_plugin Plugin;
                if (auto Err = Plugin.Serialize(true, ConfigFile ); Err.empty() == false)
                {
                    std::cerr << Err << "\n";
                    continue;
                }

                //
                // get the timestamp of the compilers
                //
                if( Plugin.m_ReleaseCompiler.empty() == false ) GetFileTimestamp(std::format(L"{}\\{}", ProjectPath, Plugin.m_ReleaseCompiler), Plugin.m_ReleaseCompilerTimeStamp);
                if (Plugin.m_DebugCompiler.empty() == false)    GetFileTimestamp(std::format(L"{}\\{}", ProjectPath, Plugin.m_DebugCompiler),   Plugin.m_DebugCompilerTimeStamp);

                //
                // Insert the plugin in the list
                //
                Plugin.m_PluginPath = std::move(entry.path().wstring());

                m_mPluginsByTypeName[Plugin.m_TypeName] = static_cast<int>(m_lPlugins.size());
                m_mPluginsByTypeGUID[Plugin.m_TypeGUID] = static_cast<int>(m_lPlugins.size());
                m_lPlugins.push_back(std::move(Plugin));
            }
        }

        //------------------------------------------------------------------------------------------------

        pipeline_plugin* find(xresource::type_guid TypeGUID)
        {
            auto it = m_mPluginsByTypeGUID.find(TypeGUID);
            if (it == m_mPluginsByTypeGUID.end()) return nullptr;
            return &m_lPlugins[it->second];
        }

        //------------------------------------------------------------------------------------------------

        pipeline_plugin* find(const std::string& String)
        {
            auto it = m_mPluginsByTypeName.find(String);
            if (it == m_mPluginsByTypeName.end()) return nullptr;
            return &m_lPlugins[it->second];
        }

        //------------------------------------------------------------------------------------------------

        void clear()
        {
            m_lPlugins.clear();
            m_mPluginsByTypeGUID.clear();
        }

        std::vector<pipeline_plugin>                    m_lPlugins;
        std::unordered_map<xresource::type_guid, int>   m_mPluginsByTypeGUID;
        std::unordered_map<std::string, int>            m_mPluginsByTypeName;
    };
}
XPROPERTY_REG2( plugin_mgr_reg, e10::asset_plugins_db::pipeline_plugin )
#endif
