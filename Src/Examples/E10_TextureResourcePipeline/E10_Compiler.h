#include <windows.h>
#include <iostream>
#include "../dependencies/xtexture.plugin/dependencies/xresource_pipeline_v2/dependencies/xproperty/source/examples/xcore_sprop_serializer/xcore_sprop_serializer.h"
#pragma once

//------------------------------------------------------------------------------------------------

constexpr
auto setX(const wchar_t* pW )
{
    std::string S;
    if (pW == nullptr) return S;

    S.reserve( [=]{ int i=0; while(pW[i++]); return i-1; }() );
    for( int i=0; pW[i]; i++) S.push_back((char)pW[i]);
    return S;
}

//------------------------------------------------------------------------------------------------

constexpr
auto setX( const std::wstring_view W )
{
    std::string S;
    S.reserve(W.size());
    for (auto c : W) S.push_back((char)c);
    return S;
}

//------------------------------------------------------------------------------------------------

constexpr
auto setX(const std::wstring& W)
{
    std::string S;
    S.reserve(W.size());
    for (auto c : W) S.push_back((char)c);
    return S;
}

//------------------------------------------------------------------------------------------------

constexpr
auto setX(const std::string_view S)
{
    std::wstring W;
    W.reserve(S.size());
    for (auto c : S) W.push_back((wchar_t)c);
    return W;
}

//------------------------------------------------------------------------------------------------

constexpr
auto setX(const std::string& S)
{
    std::wstring W;
    W.reserve(S.size());
    for (auto c : S) W.push_back((wchar_t)c);
    return W;
}

//------------------------------------------------------------------------------------------------

void GetFileTimestamp(const std::string& filePath, SYSTEMTIME& systemTime)
{
    HANDLE hFile = CreateFile(
        setX(filePath).c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        std::cerr << "Error opening file: " << filePath << std::endl;
        return;
    }

    FILETIME fileTime;
    if (!GetFileTime(hFile, NULL, NULL, &fileTime)) {
        std::cerr << "Error getting file time: " << filePath << std::endl;
        CloseHandle(hFile);
        return;
    }

    if (!FileTimeToSystemTime(&fileTime, &systemTime)) {
        std::cerr << "Error converting file time to system time: " << filePath << std::endl;
    }

    CloseHandle(hFile);
}

namespace e10
{
    //------------------------------------------------------------------------------------------------

    // Function to convert a wstring to lowercase using standard functions
    template< typename T_STRING >
    inline T_STRING ToLower(const T_STRING& str) noexcept
    {
        using char_t = std::remove_reference_t<decltype(str[0])>;
        T_STRING lowerStr(str.size(), char_t{'\0'});
        std::transform(str.begin(), str.end(), lowerStr.begin(),
            [](char_t c) { return std::tolower(c, std::locale()); });
        return lowerStr;
    }

    //------------------------------------------------------------------------------------------------

    // Function to find a case-insensitive substring in a wstring using standard functions
    template< typename T_STRING>
    inline size_t FindCaseInsensitive(const T_STRING& haystack, const T_STRING& needle, std::size_t Pos = 0) noexcept
    {
        T_STRING lowerHaystack = ToLower(haystack);
        T_STRING lowerNeedle   = ToLower(needle);
        return lowerHaystack.find(lowerNeedle, Pos);
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
            std::string                 m_TypeName;
            std::uint64_t               m_TypeGUID;
            std::vector<std::uint64_t>  m_RunAfter;
            std::string                 m_DebugCompiler;
            SYSTEMTIME                  m_DebugCompilerTimeStamp;
            std::string                 m_ReleaseCompiler;
            SYSTEMTIME                  m_ReleaseCompilerTimeStamp;
            std::string                 m_PluginPath;
            std::string                 m_CompilationScript;

                             pipeline_plugin()                  = default;
                            ~pipeline_plugin()                  = default;
                             pipeline_plugin(pipeline_plugin&&) = default;
            pipeline_plugin& operator = (pipeline_plugin&&)     = default;

            //------------------------------------------------------------------------------------------------

            std::string Serialize(bool bRead, std::string_view Path)
            {
                //
                // Read the config file
                //
                xcore::textfile::stream Stream;
                if (auto Err = Stream.Open(bRead, Path, {}); Err)
                {
                    return std::format("Error: Failed to read {} with error {}", Path, Err.getCode().m_pString );
                }

                //
                // Read the configuration file
                //
                xproperty::settings::context    Context{};
                if (auto Err = xproperty::sprop::serializer::Stream(Stream, *this, Context); Err)
                {
                    return std::format("Error: Failed to read {} with error {}", Path, Err.getCode().m_pString);
                }

                return {};
            }

            XPROPERTY_DEF
            ( "PipelinePlugin", pipeline_plugin
            , obj_member< "TypeName"
                , &pipeline_plugin::m_TypeName
                , member_flags<xproperty::flags::SHOW_READONLY
                >>
            , obj_member< "TypeGUID"
                , &pipeline_plugin::m_TypeGUID
                , member_ui<std::uint64_t>::drag_bar< 0.0f, 0, std::numeric_limits<std::uint64_t>::max(), "%llX">
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

        void SetupProject( std::string_view ProjectPath )
        {
            //
            // Go throw all the plugins and read their information
            //
            auto PluginPath = std::format("{}\\cache\\plugins", ProjectPath);
            for (const auto& entry : std::filesystem::recursive_directory_iterator(PluginPath))
            {
                if (std::filesystem::is_directory(entry.path()) == false) continue;

                std::string ConfigFile = std::format("{}\\resource_pipeline.config.txt", setX(entry.path()).c_str() );
                if (std::filesystem::exists(ConfigFile) == false) continue;

                // Read the plugin
                pipeline_plugin Plugin;
                if (auto Err = Plugin.Serialize(true, ConfigFile.c_str()); Err.empty() == false)
                {
                    std::cerr << Err << "\n";
                    continue;
                }

                //
                // get the timestamp of the compilers
                //
                GetFileTimestamp(std::format("{}\\{}", ProjectPath, Plugin.m_ReleaseCompiler.c_str()), Plugin.m_ReleaseCompilerTimeStamp);
                GetFileTimestamp(std::format("{}\\{}", ProjectPath, Plugin.m_DebugCompiler.c_str()), Plugin.m_DebugCompilerTimeStamp);

                //
                // Insert the plugin in the list
                //
                Plugin.m_PluginPath = std::move(entry.path().string());

                m_mPlugins[Plugin.m_TypeGUID] = static_cast<int>(m_lPlugins.size());
                m_lPlugins.push_back(std::move(Plugin));
            }
        }

        //------------------------------------------------------------------------------------------------

        pipeline_plugin* find(std::uint64_t TypeGUID)
        {
            auto it = m_mPlugins.find(TypeGUID);
            if (it == m_mPlugins.end()) return nullptr;
            return &m_lPlugins[it->second];
        }

        //------------------------------------------------------------------------------------------------

        void clear()
        {
            m_lPlugins.clear();
            m_mPlugins.clear();
        }

        std::vector<pipeline_plugin>                m_lPlugins;
        std::unordered_map<std::uint64_t, int>      m_mPlugins;
    };


    //------------------------------------------------------------------------------------------------
    //
    // This class deals with the compilation of resources
    //
    //------------------------------------------------------------------------------------------------
    struct compiler
    {
        enum class compilation_state
        { IDLE
        , COMPILING
        , DONE_ERROR
        , DONE_SUCCESS
        };

        enum class optimization
        { O0
        , O1
        , Oz
        };

        enum class debug
        { D0
        , D1
        , Dz
        };

        enum class open_explorer
        { SELECT_ONE
        , TO_THE_PROJECT
        , TO_RESOURCE_FILE
        , TO_LOG_FILE
        , TO_DESCRIPTOR_FILE
        , TO_THE_FIRST_ASSET
        };

        inline static constexpr auto open_explorer_v = std::array
        { xproperty::settings::enum_item("SELECT_ONE",          open_explorer::SELECT_ONE,          "CANCEL this menu. This selection does nothing in case you want to cancel out from the menu")
        , xproperty::settings::enum_item("TO_THE_PROJECT",      open_explorer::TO_THE_PROJECT,      "Opens an explorer window to the project")
        , xproperty::settings::enum_item("TO_RESOURCE_FILE",    open_explorer::TO_RESOURCE_FILE,    "Opens an explorer window to the xbmp file")
        , xproperty::settings::enum_item("TO_LOG_FILE",         open_explorer::TO_LOG_FILE,         "Opens an explorer window to the log file, also note that any debug information extra created "
                                                                                                    "by the compiler usually will be located here. Such will be the case for a .dds file")
        , xproperty::settings::enum_item("TO_DESCRIPTOR_FILE",  open_explorer::TO_DESCRIPTOR_FILE,  "Opens an exploere window to the descriptor file")
        , xproperty::settings::enum_item("TO_THE_FIRST_ASSET",  open_explorer::TO_THE_FIRST_ASSET,  "Opens an explorer window to the first asset in the Input list")
        };

        inline static constexpr auto debug_v = std::array
        { xproperty::settings::enum_item("D0 (Default)",   debug::D0, "Tell the compiler that we don't need any debug information")
        , xproperty::settings::enum_item("D1",             debug::D1, "Tell the compiler that we would like to see some debug information")
        , xproperty::settings::enum_item("Dz",             debug::Dz, "Tell the compiler that we would like as much debug information as possible")
        };

        inline static constexpr auto optimization_v = std::array
        { xproperty::settings::enum_item("O0",              optimization::O0, "Tell the compiler that time is very important and should not spend a lot of time optimizing")
        , xproperty::settings::enum_item("O1 (Default)",    optimization::O1, "Tell the compiler to work as expected and time reasonable time to get its results")
        , xproperty::settings::enum_item("Oz",              optimization::Oz, "Tell the compiler that we would like this resource to be thought about it very carefully "
                                                                              "and that the compiler should take all the time it needs to create the best result")
        };

        //------------------------------------------------------------------------------------------------

        xcore::lock::object<std::string, xcore::lock::spin>     m_CompilerFeedback;
        xcore::lock::object<std::string, xcore::lock::spin>     m_CompilerRawlog;
        std::vector<std::string>                                m_CompilationWarnings;
        std::vector<std::string>                                m_CompilationInfo;
        std::vector<std::string>                                m_CompilationErrors;
        std::vector<std::string>                                m_ValidationErrors;
        compilation_state                                       m_CompilationState  = compilation_state::IDLE;
        std::string                                             m_ProjectPath;
        std::string                                             m_DescriptorPath;
        std::string                                             m_DescriptorRelativePath;
        std::string                                             m_OutputPath;
        std::string                                             m_LogPath;
        std::string                                             m_ResourcePath;
        std::unique_ptr<std::thread>                            m_CompilerThread;
        std::unique_ptr<xresource_pipeline::descriptor::base>   m_pDescriptor;
        std::unique_ptr<xresource_pipeline::info>               m_pInfo;
        std::uint64_t                                           m_InstanceGUID = 0;
        std::uint64_t                                           m_TypeGUID = 0;
        bool                                                    m_bUseDebugCompiler = false;
        bool                                                    m_bOutputInConsole = false;
        debug                                                   m_DebugLevel = debug::D0;
        optimization                                            m_OptimizationLevel = optimization::O1;
        asset_plugins_db                                        m_AssetPlugins;

        //------------------------------------------------------------------------------------------------

        static void RunCommandLine
        ( const std::wstring& WStrCommanLine
        , xcore::lock::object<std::string, xcore::lock::spin>&  CompilerFeedback
        , xcore::lock::object<std::string, xcore::lock::spin>&  RawLogs
        , const bool bOutputInConsole 
        )
        {
            SECURITY_ATTRIBUTES saAttr;
            saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
            saAttr.bInheritHandle = TRUE;
            saAttr.lpSecurityDescriptor = NULL;

            HANDLE hChildStd_OUT_Rd = NULL;
            HANDLE hChildStd_OUT_Wr = NULL;
            if (!CreatePipe(&hChildStd_OUT_Rd, &hChildStd_OUT_Wr, &saAttr, 0))
                throw std::runtime_error("Stdout pipe creation failed");

            if (!SetHandleInformation(hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0))
                throw std::runtime_error("Stdout SetHandleInformation failed");

            STARTUPINFO siStartInfo;
            ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
            siStartInfo.cb = sizeof(STARTUPINFO);
            siStartInfo.hStdError = hChildStd_OUT_Wr;
            siStartInfo.hStdOutput = hChildStd_OUT_Wr;
            siStartInfo.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
            siStartInfo.wShowWindow = SW_HIDE;

            PROCESS_INFORMATION piProcInfo;
            ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));

            if (!CreateProcess(
                NULL,
                const_cast<LPWSTR>(WStrCommanLine.c_str()), // Command line
                NULL,           // Process handle not inheritable
                NULL,           // Thread handle not inheritable
                TRUE,           // Set handle inheritance to TRUE
                DETACHED_PROCESS | BELOW_NORMAL_PRIORITY_CLASS,              // No creation flags
                NULL,           // Use parent's environment block
                NULL,           // Use parent's starting directory
                &siStartInfo,   // Pointer to STARTUPINFO structure
                &piProcInfo))   // Pointer to PROCESS_INFORMATION structure
                throw std::runtime_error("CreateProcess failed");

            DWORD                   dwRead;
            std::array<CHAR, 16>    chBuf;
            std::size_t             LastLine = 0;
            std::size_t             OldLength = 0;
            {
                // Clear the feedback string
                xcore::lock::scope lock(CompilerFeedback);
                CompilerFeedback.get().clear();
            }

            while (true)
            {
                dwRead = 0;
                if (!PeekNamedPipe(hChildStd_OUT_Rd, NULL, 0, NULL, &dwRead, NULL))
                    break;

                if (dwRead)
                {                 // yes we do, so read it and print out to the edit ctrl
                    if (!ReadFile(hChildStd_OUT_Rd, chBuf.data(), static_cast<DWORD>(chBuf.size()), &dwRead, NULL))
                        break;

                    //
                    // Update the global variable with the compiler feedback
                    //
                    {
                        xcore::lock::scope lock(CompilerFeedback);
                        e10::ConsoleCompliantString( CompilerFeedback.get(), OldLength, LastLine, { chBuf.data(), dwRead } );
                    }

                    //
                    // Update the actual console... (if we want to)
                    //
                    if (bOutputInConsole)
                    {
                        std::cout.write(chBuf.data(), dwRead);
                        std::cout.flush();
                    }
                }
                else
                {
                    // no we don't have anything in the buffer
                    // maybe the program exited
                    if (WaitForSingleObject(piProcInfo.hProcess, 0) == WAIT_OBJECT_0)
                        break;        // so we should exit too

                    // Sleep a little bit so that we don't hog the CPU
                    Sleep(200);
                }

                // continue otherwise
            }

            CloseHandle(piProcInfo.hProcess);
            CloseHandle(piProcInfo.hThread);

            // Close the pipe when done
            CloseHandle(hChildStd_OUT_Wr);
            CloseHandle(hChildStd_OUT_Rd);

            //
            // Check if we were successful
            //
            {
                xcore::lock::scope lock1(RawLogs);
                xcore::lock::scope lock2(CompilerFeedback);

                RawLogs.get() = std::move(CompilerFeedback.get());
                
                auto& Str = RawLogs.get();
                if (FindCaseInsensitive( Str, {"[COMPILATION_SUCCESS]"} ) != std::string::npos)
                {
                    if (FindCaseInsensitive(Str, {"[Warning]"}) != std::string::npos)
                    {
                        CompilerFeedback.get() = "SUCCESSFUL + Warnings";
                    }
                    else
                    {
                        // Nothing to report so let us just tell the user everything is cool...
                        CompilerFeedback.get() = "SUCCESSFUL";
                    }
                }
                else
                {
                    CompilerFeedback.get() = "FAILURE";
                }
            }
        }

        //------------------------------------------------------------------------------------------------

        xcore::err SetupDescriptor( std::uint64_t TypeGUID, std::uint64_t InstanceGUID )
        {
            // Make sure we have the project open first
            assert(m_ProjectPath.empty() == false);

            // Check that we have a plugin for this type
            auto pPlugin = m_AssetPlugins.find(TypeGUID);
            if ( pPlugin == nullptr )
            {
                return xerr_failure_s("The plugin type was not found");
            }

            m_InstanceGUID = InstanceGUID;
            m_TypeGUID     = TypeGUID;
            assert(m_InstanceGUID);
            assert(m_InstanceGUID&1);

            //
            // Set the relative path of the resource
            //
            std::string RelativePath = std::format("{}\\{:2X}\\{:2X}\\{:X}"
                , pPlugin->m_TypeName.c_str()
                , m_InstanceGUID&0xff
                , (m_InstanceGUID & 0xff00)>>8
                , m_InstanceGUID
                );

            // Set up the resource and descriptor paths
            m_ResourcePath              = std::format("{}\\{}\\WINDOWS\\{}.xbmp", m_ProjectPath.c_str(), m_OutputPath.c_str(), RelativePath.c_str());

            m_DescriptorRelativePath    = std::format("Descriptors\\{}.desc", RelativePath.c_str());
            m_DescriptorPath            = std::format("{}", m_DescriptorRelativePath.c_str());

            // Log path
            m_LogPath = std::format("Cache\\Resources\\Logs\\{}.log", RelativePath.c_str() );

            //
            // Allocate the descriptors memory
            //
            m_pDescriptor = xresource_pipeline::factory_base::Find(pPlugin->m_TypeName.c_str())->CreateDescriptor();
            m_pInfo        = std::make_unique<xresource_pipeline::info>(xresource_pipeline::factory_base::Find(pPlugin->m_TypeName.c_str())->CreateInfo(InstanceGUID));

            //
            // Ok let us try to load read in the current versions
            //
            if (auto Err = Serialize(true); Err)
            {
                // If we fail to load them is fine not big deal...
                Err.clear();
            }

            return {};
        }

        //------------------------------------------------------------------------------------------------

        void SetupProject( std::string_view ProjectPath )
        {
            m_ProjectPath = ProjectPath;

            //
            // Setup the asset plugins
            //
            m_AssetPlugins.SetupProject(m_ProjectPath);

            //
            // Set the current directory to the asset folder
            //
            std::wstring wProjectPath = setX(ProjectPath) + L"\\Assets";
            SetCurrentDirectory(wProjectPath.c_str());

            //
            // Set this for the properties out default path
            //
            xproperty::member_ui<std::string>::g_CurrentPath = std::format("{}\\Assets", m_ProjectPath.c_str());
            m_OutputPath          = std::format("Cache\\Resources\\Platforms");

            // Set our global feedback variable to ready
            {
                xcore::lock::scope lock(m_CompilerFeedback);
                m_CompilerFeedback.get() = "Ready";
            }
        }

        //------------------------------------------------------------------------------------------------

        xcore::err Serialize( bool isRead ) const noexcept
        {
            assert(m_pInfo != nullptr);
            assert(m_pDescriptor != nullptr);


            xproperty::settings::context Context;

            if (auto Err = m_pInfo->Serialize(isRead, std::format("{}\\{}\\Info.txt", m_ProjectPath.c_str(), m_DescriptorPath.c_str()), Context); Err)
            {
                return Err;
            }

            if (auto Err = m_pDescriptor->Serialize(isRead, std::format("{}\\{}\\Descriptor.txt", m_ProjectPath.c_str(), m_DescriptorPath.c_str()), Context); Err)
            {
                return Err;
            }

            return {};
        }

        //------------------------------------------------------------------------------------------------
        // Entry point for the compilation thread
        static void RunCompiler( compiler& Instance, const std::string CommandLine )
        {
            Instance.m_CompilationState = compilation_state::COMPILING;
            try
            {
                std::wstring FinalCommand;
                std::transform(CommandLine.begin(), CommandLine.end(), std::back_inserter(FinalCommand), [](char c) {return (wchar_t)c; });

                RunCommandLine
                ( FinalCommand
                , Instance.m_CompilerFeedback
                , Instance.m_CompilerRawlog
                , Instance.m_bOutputInConsole
                );

                Instance.m_CompilationState = compilation_state::DONE_SUCCESS;
            }
            catch (const std::exception& e)
            {
                std::cerr << "Error: " << e.what() << std::endl;
                Instance.m_CompilationState = compilation_state::DONE_ERROR;
            }
        }

        //------------------------------------------------------------------------------------------------

        bool ReloadTexture()
        {
            if (m_CompilationState == compilation_state::DONE_SUCCESS
                || m_CompilationState == compilation_state::DONE_ERROR)
            {
                m_CompilerThread->join();
                m_CompilerThread.reset();

                //
                // Update the logs
                //
                {
                    xcore::lock::scope lock(m_CompilerRawlog);
                    auto& RawLog = m_CompilerRawlog.get();

                    m_CompilationErrors.clear();
                    m_CompilationWarnings.clear();
                    m_CompilationInfo.clear();

                    //
                    // Collect all errors
                    //
                    if (RawLog.size())
                    {
                        // Collect all errors 
                        for (auto pos = e10::FindCaseInsensitive(RawLog, { "[Error]" }); pos != std::string::npos; pos = e10::FindCaseInsensitive(RawLog, { "[Error]" }, pos + 1))
                        {
                            auto End = RawLog.find_first_of("\n", pos);
                            m_CompilationErrors.push_back(RawLog.substr(pos, End - pos));
                            pos = End - 1;
                        }

                        // Collect all warnings
                        for (auto pos = e10::FindCaseInsensitive(RawLog, { "[Warning]" }); pos != std::string::npos; pos = e10::FindCaseInsensitive(RawLog, { "[Warning]" }, pos + 1))
                        {
                            auto End = RawLog.find_first_of("\n", pos);
                            m_CompilationWarnings.push_back(RawLog.substr(pos, End - pos));
                            pos = End - 1;
                        }

                        // Collect all warnings
                        for (auto pos = e10::FindCaseInsensitive(RawLog, { "[Info]" }); pos != std::string::npos; pos = e10::FindCaseInsensitive(RawLog, { "[Info]" }, pos + 1))
                        {
                            auto End = RawLog.find_first_of("\n", pos);

                            if (auto p = RawLog.find("-----", pos); p < End)
                            {
                                // This is just a delimiter... ignore it
                            }
                            else
                            {
                                m_CompilationInfo.push_back(RawLog.substr(pos, End - pos));
                            }

                            pos = End - 1;
                        }
                    }
                }

                //
                // Update the state
                //
                if (m_CompilationState == compilation_state::DONE_SUCCESS)
                {
                    m_CompilationState = compilation_state::IDLE;
                    return true;
                }

                m_CompilationState = compilation_state::IDLE;
            }

            return false;
        }

        //------------------------------------------------------------------------------------------------

        void RunTheCompilers()
        {
            //
            // First check the basic validation
            //
            if (hasValidationErrors())
            {
                std::cout << "*** ERROR you can not compile as there are validation errors ***\n";
                return;
            }

            //
            // Let us serialize the descriptor
            //
            if (auto Err = Serialize(false); Err)
            {
                std::cout << "*** ERROR failed to serialize the descriptor ***\n";
                return;
            }

            //
            // Now we can call the compiler
            //
            auto pPlugin = m_AssetPlugins.find(m_TypeGUID);

            // Make sure the path for the compiler is clean for the command line
            auto CompilerPath = std::format("{}\\{}", m_ProjectPath.c_str(), m_bUseDebugCompiler ? pPlugin->m_DebugCompiler.c_str() : pPlugin->m_ReleaseCompiler.c_str());
            {
                std::filesystem::path p(CompilerPath);
                CompilerPath = p.lexically_normal().string();
            }

            // Generate the command line
            auto CommandLine = std::format(R"("{}" -PROJECT "{}" -OPTIMIZATION {} -DEBUG {} -DESCRIPTOR "{}" -OUTPUT "{}\{}")"
                , CompilerPath.c_str()
                , m_ProjectPath.c_str()
                , m_OptimizationLevel == optimization::O0 ? "O0" : m_OptimizationLevel == optimization::O1 ? "O1" : "Oz"
                , m_DebugLevel == debug::D0 ? "D0" : m_DebugLevel == debug::D1 ? "D1" : "Dz"
                , m_DescriptorPath.c_str()
                , m_ProjectPath.c_str()
                , m_OutputPath.c_str()
            );

            if (m_bOutputInConsole)
            {
                std::cout << "Command Line:\n" << CommandLine << "\n";
            }

            // Run the compiler
            m_CompilerThread = std::make_unique<std::thread>(RunCompiler, std::ref(*this), std::move(CommandLine));
        }

        //------------------------------------------------------------------------------------------------

        bool isCompilerWorking() const noexcept
        {
            return m_CompilerThread != nullptr;
        }

        //------------------------------------------------------------------------------------------------

        bool hasValidationErrors(void) const noexcept
        {
            std::vector<std::string> Errors;
            m_pDescriptor->Validate(Errors);
            return Errors.size() != 0;
        }

        //------------------------------------------------------------------------------------------------

        XPROPERTY_DEF
        ( "Compiler", compiler
        , obj_scope< "Project Details"
            , obj_member
                < "Project Path"
                , +[]( compiler& O, bool bRead, std::string& Var )
                {
                    if (bRead) Var = O.m_ProjectPath;
                    else
                    {
                        O.m_ProjectPath = Var;
                    }
                }
                , member_ui<std::string>::folder_dialog<"LION Project\0 *.lion_project; *.lion_library\0" >
                , member_help<"Path of the project"
                >>
            , obj_member_ro
                < "Descriptor Path"
                , &compiler::m_DescriptorPath
                , member_dynamic_flags < +[](const compiler& O)
                {   xproperty::flags::type Flags{ xproperty::flags::DONT_SAVE };
                    Flags.m_bDontShow = O.m_ProjectPath.empty();
                    return Flags;
                }>
                , member_help<"Path to the descriptor"
                >>
            , obj_member_ro
                < "Output Path"
                , &compiler::m_OutputPath
                , member_dynamic_flags < +[](const compiler& O)
                {   xproperty::flags::type Flags{ xproperty::flags::DONT_SAVE };
                    Flags.m_bDontShow = O.m_ProjectPath.empty();
                    return Flags;
                }>
                , member_help<"Path for the complied resource"
                >>
            , obj_member_ro
                < "Log Path"
                , &compiler::m_LogPath
                , member_dynamic_flags < +[](const compiler& O)
                {   xproperty::flags::type Flags{ xproperty::flags::DONT_SAVE };
                    Flags.m_bDontShow = O.m_ProjectPath.empty();
                    return Flags;
                }>
                , member_help<"Path for the log information of the resource"
                >>
            , obj_member< "Plugins"
                , +[](compiler& O)->auto& { return O.m_AssetPlugins.m_lPlugins; }
                , member_ui_open<false
                >>


            , member_ui_open<false>
            >
        , obj_member
            < "Compiler"
            , +[](compiler& O, bool bRead, std::string& Value)
            {
                if (bRead)
                {
                    if (O.isCompilerWorking() == false) Value = "Press To Compile";
                    else                                Value = "Compiling...";
                }
                else if (O.isCompilerWorking() == false) O.RunTheCompilers();
            }
            , member_ui<std::string>::button<>
            , member_dynamic_flags < +[](const compiler& O)
            {   xproperty::flags::type Flags{ xproperty::flags::DONT_SAVE };
                Flags.m_bShowReadOnly = O.isCompilerWorking() || O.hasValidationErrors();
                return Flags;
            }>
            , member_help<"You can press the button you will save the descriptor to reflect the latest version. "
                          "Then it will trigger a compilation base on the options seem below and it will generate a new dds file. "
                          "Once it finish the compilation the editor will reload the texture and show the new version. "
            >>
        , obj_scope< "Compilation Info" 
            , obj_member_ro<"Validation Errors", &compiler::m_ValidationErrors, member_ui_open<true>>
            , obj_member_ro
                < "Compiler Result"
                , +[](compiler& O, bool bRead, std::string& Value)
                {
                    xcore::lock::scope lock(O.m_CompilerFeedback);
                    Value = O.m_CompilerFeedback.get();
                }
                , member_help<"Reports the state of the compilation. However to see the compiler working, or if there are errors there will be here to see..."
                >>
            , obj_member_ro<"Errors", &compiler::m_CompilationErrors, member_ui_open<true>>
            , obj_member_ro<"Warnings", &compiler::m_CompilationWarnings, member_ui_open<true>>
            , obj_member_ro<"Info", &compiler::m_CompilationInfo, member_ui_open<false>>
            >
        , obj_member
            < "bOutputInConsole"
            , &compiler::m_bOutputInConsole
            , member_help<"Prints the std::cout from the compiler into our own console window. This can help for debugging."
            >>
        , obj_member
            < "bUseDebugCompiler"
            , &compiler::m_bUseDebugCompiler
            , member_help<"We possibly have two versions of the compiler one in debug mode the other in release "
                          "mode, you can specify which version you want to run depending of what you are trying to do"
            >>
        , obj_member
            < "Debug Level"
            , &compiler::m_DebugLevel
            , member_enum_span<debug_v>
            , member_help<"Choose a debug level for the compiler, D0 means no debug information"
            >>
        , obj_member
            < "Optimize Level"
            , &compiler::m_OptimizationLevel
            , member_enum_span<optimization_v>
            , member_help<"How much time should the compiler take to do a great job"
            >>
        , obj_member
            < "Open Explorer Window"
            , +[]( compiler& O, bool bRead, open_explorer& Value )
            {
                if (bRead)
                {
                    Value = open_explorer::SELECT_ONE;
                }
                else
                {

                    switch (Value)
                    {
                    case open_explorer::SELECT_ONE: break;
                    case open_explorer::TO_THE_PROJECT: ShellExecute( NULL, L"open", L"explorer", xcore::string::To<wchar_t>(O.m_ProjectPath.c_str()).data(), NULL, SW_SHOW); break;
                    case open_explorer::TO_DESCRIPTOR_FILE: ShellExecute(NULL, L"open", L"explorer", xcore::string::To<wchar_t>(xcore::string::Fmt("%s\\%s", O.m_ProjectPath.c_str(), O.m_DescriptorPath.c_str())).data(), NULL, SW_SHOW); break;
                    case open_explorer::TO_RESOURCE_FILE: 
                    {
                        auto Str = xcore::string::Fmt("explorer /select,%s", O.m_ResourcePath.c_str());
                        system(Str.data());
                        break;
                    }
                    case open_explorer::TO_LOG_FILE:
                    {
                        auto Str = xcore::string::Fmt("explorer /select,%s\\%s\\Log.txt", O.m_ProjectPath.c_str(), O.m_LogPath.c_str());
                        system(Str.data());
                        break;
                    }
                    case open_explorer::TO_THE_FIRST_ASSET: ShellExecute(NULL, L"open", L"explorer", xcore::string::To<wchar_t>(xcore::string::Fmt("%s\\Assets",O.m_ProjectPath.c_str())).data(), NULL, SW_SHOW); break;
                    default: assert(false); break;
                    }
                }
            }
            , member_enum_span<open_explorer_v>
            , member_help<"This menu opens an explorer window to different parts of the project... it is helpful for debugging"
            >>
        )
    };
    XPROPERTY_REG(compiler)
    XPROPERTY_REG2( pepe, asset_plugins_db::pipeline_plugin)
}

