#include <windows.h>
#include <iostream>
#include "../dependencies/xtexture.plugin/dependencies/xresource_pipeline_v2/dependencies/xproperty/source/examples/xcore_sprop_serializer/xcore_sprop_serializer.h"

#pragma once


namespace e10
{
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
        std::string                                             m_DescriptorPath;
        std::string                                             m_DescriptorRelativePath;
        std::string                                             m_OutputPath;
        std::string                                             m_LogPath;
        std::string                                             m_ResourcePath;
        std::unique_ptr<std::thread>                            m_CompilerThread;
        std::unique_ptr<xresource_pipeline::descriptor::base>   m_pDescriptor;
        std::unique_ptr<xresource_pipeline::info>               m_pInfo;
        xresource::full_guid                                    m_ResourceGUID {};
        bool                                                    m_bUseDebugCompiler = false;
        bool                                                    m_bOutputInConsole = false;
        debug                                                   m_DebugLevel = debug::D0;
        optimization                                            m_OptimizationLevel = optimization::O1;
        e10::library_mgr*                                       m_pLibraryMgr = nullptr;

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

        xcore::err SetupDescriptor(const std::string& TypeName, xresource::instance_guid InstanceGUID)
        {
            assert(m_pLibraryMgr);
            auto p = m_pLibraryMgr->m_AssetPluginsDB.find(TypeName);
            if (p == nullptr)
                return xerr_failure_s("The plugin type by name was not found");

            return SetupDescriptor({ InstanceGUID, p->m_TypeGUID });
        }

        //------------------------------------------------------------------------------------------------

        xcore::err SetupDescriptor( xresource::full_guid GUID )
        {
            // Make sure we have the project open first
            assert(m_pLibraryMgr->m_ProjectPath.empty() == false);

            // Check that we have a plugin for this type
            auto pPlugin = m_pLibraryMgr->m_AssetPluginsDB.find(GUID.m_Type);
            if ( pPlugin == nullptr )
            {
                return xerr_failure_s("The plugin type by guid was not found");
            }

            m_ResourceGUID = GUID;
            assert(GUID.isValid());

            //
            // Set the relative path of the resource
            //
            std::string RelativePath = std::format("{}\\{:02X}\\{:02X}\\{:X}"
                , pPlugin->m_TypeName.c_str()
                , m_ResourceGUID.m_Instance.m_Value &0xff
                , (m_ResourceGUID.m_Instance.m_Value & 0xff00)>>8
                , m_ResourceGUID.m_Instance.m_Value
                );

            // Set up the resource and descriptor paths
            m_ResourcePath              = std::format("{}\\{}\\WINDOWS\\{}.xbmp", strXstr(m_pLibraryMgr->m_ProjectPath).c_str(), m_OutputPath.c_str(), RelativePath.c_str());

            m_DescriptorRelativePath    = std::format("Descriptors\\{}.desc", RelativePath.c_str());
            m_DescriptorPath            = std::format("{}", m_DescriptorRelativePath.c_str());

            // Log path
            m_LogPath = std::format("Cache\\Resources\\Logs\\{}.log", RelativePath.c_str() );

            //
            // Allocate the descriptors memory
            //
            m_pDescriptor = xresource_pipeline::factory_base::Find(pPlugin->m_TypeName.c_str())->CreateDescriptor();
            m_pInfo        = std::make_unique<xresource_pipeline::info>(xresource_pipeline::factory_base::Find(pPlugin->m_TypeName.c_str())->CreateInfo(m_ResourceGUID.m_Instance));

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

        void SetupProject( library_mgr& LibraryMgr )
        {
            m_pLibraryMgr = &LibraryMgr;

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

            if (auto Err = m_pInfo->Serialize(isRead, std::format("{}\\{}\\Info.txt", strXstr(m_pLibraryMgr->m_ProjectPath).c_str(), m_DescriptorPath.c_str()), Context); Err)
            {
                return Err;
            }

            if (auto Err = m_pDescriptor->Serialize(isRead, std::format("{}\\{}\\Descriptor.txt", strXstr(m_pLibraryMgr->m_ProjectPath).c_str(), m_DescriptorPath.c_str()), Context); Err)
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
                        for (auto pos = FindCaseInsensitive(RawLog, { "[Error]" }); pos != std::string::npos; pos = FindCaseInsensitive(RawLog, { "[Error]" }, pos + 1))
                        {
                            auto End = RawLog.find_first_of("\n", pos);
                            m_CompilationErrors.push_back(RawLog.substr(pos, End - pos));
                            pos = End - 1;
                        }

                        // Collect all warnings
                        for (auto pos = FindCaseInsensitive(RawLog, { "[Warning]" }); pos != std::string::npos; pos = FindCaseInsensitive(RawLog, { "[Warning]" }, pos + 1))
                        {
                            auto End = RawLog.find_first_of("\n", pos);
                            m_CompilationWarnings.push_back(RawLog.substr(pos, End - pos));
                            pos = End - 1;
                        }

                        // Collect all warnings
                        for (auto pos = FindCaseInsensitive(RawLog, { "[Info]" }); pos != std::string::npos; pos = FindCaseInsensitive(RawLog, { "[Info]" }, pos + 1))
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
            auto pPlugin = m_pLibraryMgr->m_AssetPluginsDB.find(m_ResourceGUID.m_Type);

            // Make sure the path for the compiler is clean for the command line
            auto CompilerPath = std::format("{}\\{}", strXstr(m_pLibraryMgr->m_ProjectPath).c_str(), m_bUseDebugCompiler ? pPlugin->m_DebugCompiler.c_str() : pPlugin->m_ReleaseCompiler.c_str());
            {
                std::filesystem::path p(CompilerPath);
                CompilerPath = p.lexically_normal().string();
            }

            // Generate the command line
            auto CommandLine = std::format(R"("{}" -PROJECT "{}" -OPTIMIZATION {} -DEBUG {} -DESCRIPTOR "{}" -OUTPUT "{}\{}")"
                , CompilerPath.c_str()
                , strXstr(m_pLibraryMgr->m_ProjectPath).c_str()
                , m_OptimizationLevel == optimization::O0 ? "O0" : m_OptimizationLevel == optimization::O1 ? "O1" : "Oz"
                , m_DebugLevel == debug::D0 ? "D0" : m_DebugLevel == debug::D1 ? "D1" : "Dz"
                , m_DescriptorPath.c_str()
                , strXstr(m_pLibraryMgr->m_ProjectPath).c_str()
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

        static void AddNewResource(compiler& O, bool bRead, std::string& Value)
        {
            static bool bOpenPopup = false;
            if (bRead)
            {
                Value = "Press to Add";
            }
            else
            {
                if (O.m_pLibraryMgr->m_AssetPluginsDB.m_lPlugins.size())
                {
                    bOpenPopup = true;
                }
            }

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f,4.0f));
            if (bOpenPopup) ImGui::OpenPopup("PopupMenu");
            if (ImGui::BeginPopup("PopupMenu"))
            {
                for (auto& E : O.m_pLibraryMgr->m_AssetPluginsDB.m_lPlugins)
                {
                    if (ImGui::MenuItem(E.m_TypeName.c_str()))
                    {
                        //O.SetupDescriptor(E.m_TypeGUID, 0);
                        ImGui::CloseCurrentPopup();
                        bOpenPopup = false;
                    }
                }

                if (ImGui::IsMouseClicked(0) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow))
                {
                    ImGui::CloseCurrentPopup();
                    bOpenPopup = false;
                }

                // Ends the menu...
                ImGui::EndPopup();
            }
            ImGui::PopStyleVar();
        }

        //------------------------------------------------------------------------------------------------

        XPROPERTY_DEF
        ( "Compiler", compiler
            , obj_member < "Add New Resource Type", +[](compiler& O, bool bRead, std::string& Value)
            {
                AddNewResource(O, bRead, Value);
            }
            , member_ui<std::string>::button<>
            , member_help<"Press this button to add a new resource to the project"
            >> 
        , obj_scope< "Project Details"
            , obj_member
                < "Project Path"
                , +[]( compiler& O, bool bRead, std::string& Var )
                {
                    if ( O.m_pLibraryMgr == nullptr )
                    {
                        // Nothing to do here
                    }
                    else
                    {
                        if (bRead) Var = strXstr(O.m_pLibraryMgr->m_ProjectPath);
                        else
                        {
                            O.m_pLibraryMgr->m_ProjectPath = strXstr(Var);
                        }
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
                    Flags.m_bDontShow = O.m_pLibraryMgr == nullptr || O.m_pLibraryMgr->m_ProjectPath.empty();
                    return Flags;
                }>
                , member_help<"Path to the descriptor"
                >>
            , obj_member_ro
                < "Output Path"
                , &compiler::m_OutputPath
                , member_dynamic_flags < +[](const compiler& O)
                {   xproperty::flags::type Flags{ xproperty::flags::DONT_SAVE };
                    Flags.m_bDontShow = O.m_pLibraryMgr == nullptr || O.m_pLibraryMgr->m_ProjectPath.empty();
                    return Flags;
                }>
                , member_help<"Path for the complied resource"
                >>
            , obj_member_ro
                < "Log Path"
                , &compiler::m_LogPath
                , member_dynamic_flags < +[](const compiler& O)
                {   xproperty::flags::type Flags{ xproperty::flags::DONT_SAVE };
                    Flags.m_bDontShow = O.m_pLibraryMgr == nullptr || O.m_pLibraryMgr->m_ProjectPath.empty();
                    return Flags;
                }>
                , member_help<"Path for the log information of the resource"
                >>
            , obj_member< "Plugins"
                , +[](compiler& O)->auto& { return O.m_pLibraryMgr->m_AssetPluginsDB.m_lPlugins; }
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
                    if (O.m_pLibraryMgr == nullptr) return;

                    switch (Value)
                    {
                    case open_explorer::SELECT_ONE: break;
                    case open_explorer::TO_THE_PROJECT: ShellExecute( NULL, L"open", L"explorer", xcore::string::To<wchar_t>(strXstr(O.m_pLibraryMgr->m_ProjectPath).c_str()).data(), NULL, SW_SHOW); break;
                    case open_explorer::TO_DESCRIPTOR_FILE: ShellExecute(NULL, L"open", L"explorer", xcore::string::To<wchar_t>(xcore::string::Fmt("%s\\%s", strXstr(O.m_pLibraryMgr->m_ProjectPath).c_str(), O.m_DescriptorPath.c_str())).data(), NULL, SW_SHOW); break;
                    case open_explorer::TO_RESOURCE_FILE: 
                    {
                        auto Str = xcore::string::Fmt("explorer /select,%s", O.m_ResourcePath.c_str());
                        system(Str.data());
                        break;
                    }
                    case open_explorer::TO_LOG_FILE:
                    {
                        auto Str = xcore::string::Fmt("explorer /select,%s\\%s\\Log.txt", strXstr(O.m_pLibraryMgr->m_ProjectPath).c_str(), O.m_LogPath.c_str());
                        system(Str.data());
                        break;
                    }
                    case open_explorer::TO_THE_FIRST_ASSET: ShellExecute(NULL, L"open", L"explorer", xcore::string::To<wchar_t>(xcore::string::Fmt("%s\\Assets", strXstr(O.m_pLibraryMgr->m_ProjectPath).c_str())).data(), NULL, SW_SHOW); break;
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

