#include <windows.h>
#include <iostream>
#pragma once

namespace e10
{
    void RunCommandLine(const std::wstring& WStrCommanLine)
    {
        std::cout << "Begin Compilation\n";

        SECURITY_ATTRIBUTES saAttr;
        saAttr.nLength                  = sizeof(SECURITY_ATTRIBUTES);
        saAttr.bInheritHandle           = TRUE;
        saAttr.lpSecurityDescriptor     = NULL;

        HANDLE hChildStd_OUT_Rd = NULL;
        HANDLE hChildStd_OUT_Wr = NULL;
        if (!CreatePipe(&hChildStd_OUT_Rd, &hChildStd_OUT_Wr, &saAttr, 0))
            throw std::runtime_error("Stdout pipe creation failed");

        if (!SetHandleInformation(hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0))
            throw std::runtime_error("Stdout SetHandleInformation failed");

        STARTUPINFO siStartInfo;
        ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
        siStartInfo.cb = sizeof(STARTUPINFO);
        siStartInfo.hStdError       = hChildStd_OUT_Wr;
        siStartInfo.hStdOutput      = hChildStd_OUT_Wr;
        siStartInfo.dwFlags        |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        siStartInfo.wShowWindow     = SW_HIDE;

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

        DWORD dwRead;
        const int BUFSIZE = 16;
        CHAR chBuf[BUFSIZE];

        std::cout << "Recording Compilation\n";
        while (true)
        {
            dwRead = 0;
            if (!PeekNamedPipe(hChildStd_OUT_Rd, NULL, 0, NULL, &dwRead, NULL))
                break;

            if (dwRead)
            {                 // yes we do, so read it and print out to the edit ctrl
                if (!ReadFile(hChildStd_OUT_Rd, &chBuf, sizeof(chBuf), &dwRead, NULL))
                    break;

                std::cout.write(chBuf, dwRead);
                std::cout.flush();
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
        std::cout << "Done Compilation\n";
    }

    //------------------------------------------------------------------------------------------------

    // Function to convert a wstring to lowercase using standard functions
    std::wstring ToLower(const std::wstring& str)
    {
        std::wstring lowerStr(str.size(), L'\0');
        std::transform(str.begin(), str.end(), lowerStr.begin(),
            [](wchar_t c) { return std::tolower(c, std::locale()); });
        return lowerStr;
    }

    // Function to find a case-insensitive substring in a wstring using standard functions
    size_t FindCaseInsensitive(const std::wstring& haystack, const std::wstring& needle)
    {
        std::wstring lowerHaystack = ToLower(haystack);
        std::wstring lowerNeedle = ToLower(needle);
        return lowerHaystack.find(lowerNeedle);
    }

    //------------------------------------------------------------------------------------------------
    enum class compilation_state
    { IDLE
    , COMPILING
    , DONE_ERROR
    , DONE_SUCCESS
    };

    static compilation_state s_CompilationState = compilation_state::IDLE;

    //
    // This class deals with the compilation of resources
    //
    struct compiler
    {
        inline static std::string                               s_CommandLine = {};
        std::string                                             m_CompilerDebugPath;
        std::string                                             m_CompilerReleasePath;
        std::string                                             m_ProjectPath;
        std::string                                             m_DescriptorPath;
        std::string                                             m_DescriptorRelativePath;
        std::string                                             m_OutputPath;
        std::string                                             m_ResourcePath;
        std::unique_ptr<std::thread>                            m_CompilerThread;
        std::unique_ptr<xresource_pipeline::descriptor::base>   m_pDescriptor;
        std::unique_ptr<xresource_pipeline::info>               m_pInfo;
        std::uint64_t                                           m_InstanceGUID = 0;
        bool                                                    m_bUseDebugCompiler = true;

        //------------------------------------------------------------------------------------------------

        void SetupDescriptor(const char* pType, std::uint64_t InstanceGUID )
        {
            SetupPaths();

            m_InstanceGUID = InstanceGUID;
            assert(m_InstanceGUID);
            assert(m_InstanceGUID&1);


            //
            // Set the relative path of the resource
            //
            std::string RelativePath = std::format("{}\\{:2X}\\{:2X}\\{:X}"
                , pType
                , m_InstanceGUID&0xff
                , (m_InstanceGUID & 0xff00)>>8
                , m_InstanceGUID
                );

            // Set up the resource and descriptor paths
            m_ResourcePath              = std::format("{}\\WINDOWS\\{}.dds",      m_OutputPath.c_str(),  RelativePath.c_str());

            m_DescriptorRelativePath    = std::format("Descriptors\\{}.desc", RelativePath.c_str());
            m_DescriptorPath            = std::format("{}\\{}", m_ProjectPath.c_str(), m_DescriptorRelativePath.c_str());

            //
            // Allocate the descriptors memory
            //
            m_pDescriptor = xresource_pipeline::factory_base::Find("Texture")->CreateDescriptor();
            m_pInfo        = std::make_unique<xresource_pipeline::info>(xresource_pipeline::factory_base::Find("Texture")->CreateInfo(InstanceGUID));

            //
            // Ok let us try to load read in the current versions
            //
            if (auto Err = Serialize(true); Err)
            {
                // If we fail to load them is fine not big deal...
                Err.clear();
            }
        }

        //------------------------------------------------------------------------------------------------

        void SetupPaths()
        {
            TCHAR szFileName[MAX_PATH];
            GetModuleFileName(NULL, szFileName, MAX_PATH);
            std::string xGPU;

            std::wcout << L"Full path: " << szFileName << L"\n";
            if (auto I = FindCaseInsensitive(szFileName, L"xGPU"); I != -1)
            {
                I += 4; // Skip the xGPU part
                szFileName[I] = 0;
                std::wcout << L"Found xGPU at: " << szFileName << L"\n";

                std::transform(szFileName, &szFileName[I], std::back_inserter(xGPU), [](wchar_t c) {return (char)c; });

                TCHAR LIONantProject[] = L"\\dependencies\\xtexture.plugin\\bin\\example.lion_project";
                for (int i = 0; szFileName[I++] = LIONantProject[i]; ++i);

                // Set the current directory to the project
                {
                    std::wstring a = szFileName;
                    a = a + L"\\Assets";
                    SetCurrentDirectory(a.c_str());
                }

                std::transform(szFileName, &szFileName[I], std::back_inserter(m_ProjectPath), [](wchar_t c) {return (char)c; });

                std::cout << "Project Path: " << m_ProjectPath.c_str() << "\n";
            }

            m_CompilerReleasePath = std::format("{}\\dependencies\\xtexture.plugin\\Build\\xtexture_compiler.vs2022\\x64\\Release\\xtexture_compiler.exe", xGPU.c_str());
            m_CompilerDebugPath   = std::format("{}\\dependencies\\xtexture.plugin\\Build\\xtexture_compiler.vs2022\\x64\\Debug\\xtexture_compiler.exe", xGPU.c_str());
            m_OutputPath          = std::format("{}\\Cache\\Resources\\Platforms", m_ProjectPath.c_str());
        }

        //------------------------------------------------------------------------------------------------

        xcore::err Serialize( bool isRead ) const noexcept
        {
            assert(m_pInfo != nullptr);
            assert(m_pDescriptor != nullptr);


            xproperty::settings::context Context;

            if (auto Err = m_pInfo->Serialize(isRead, std::format("{}\\Info.txt", m_DescriptorPath.c_str()), Context); Err)
            {
                return Err;
            }

            if (auto Err = m_pDescriptor->Serialize(isRead, std::format("{}\\Descriptor.txt", m_DescriptorPath.c_str()), Context); Err)
            {
                return Err;
            }

            return {};
        }

        //------------------------------------------------------------------------------------------------

        static void RunCompiler( void )
        {
            s_CompilationState = compilation_state::COMPILING;
            try
            {
                std::wstring FinalCommand;
                std::transform(s_CommandLine.begin(), s_CommandLine.end(), std::back_inserter(FinalCommand), [](char c) {return (wchar_t)c; });

                RunCommandLine(FinalCommand);

                s_CompilationState = compilation_state::DONE_SUCCESS;
            }
            catch (const std::exception& e)
            {
                std::cerr << "Error: " << e.what() << std::endl;
                s_CompilationState = compilation_state::DONE_ERROR;
            }
        }

        //------------------------------------------------------------------------------------------------

        bool ReloadTexture()
        {
            if (s_CompilationState == compilation_state::DONE_SUCCESS
                || s_CompilationState == compilation_state::DONE_ERROR)
            {
                m_CompilerThread->join();
                m_CompilerThread.reset();

                if (s_CompilationState == compilation_state::DONE_SUCCESS)
                {
                    s_CompilationState = compilation_state::IDLE;
                    return true;
                }

                s_CompilationState = compilation_state::IDLE;
            }

            return false;
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
        ( "Compiler", e10::compiler
        , obj_member< "Compiler"
            , +[](e10::compiler& O, bool bRead, std::string& Value)
            {
                if (bRead)
                {
                    Value = "Press To Compile";
                }
                else
                {
                    if ( O.isCompilerWorking() == false )
                    {
                        //
                        // First check the basic validation
                        //
                        if (O.hasValidationErrors())
                        {
                            std::cout << "*** ERROR you can not compile as there are validation errors ***\n";
                            return;
                        }

                        //
                        // Let us serialize the descriptor
                        //
                        if ( auto Err = O.Serialize(false); Err )
                        {
                            std::cout << "*** ERROR failed to serialize the descriptor ***\n";
                            return;
                        }

                        //
                        // Now we can call the compiler
                        //
                        s_CommandLine = std::format(R"("{}" -PROJECT "{}" -DESCRIPTOR "{}" -OUTPUT "{}")"
                            , O.m_bUseDebugCompiler ? O.m_CompilerDebugPath.c_str() : O.m_CompilerReleasePath.c_str()
                            , O.m_ProjectPath.c_str()
                            , O.m_DescriptorRelativePath.c_str()
                            , O.m_OutputPath.c_str()
                            );

                        O.m_CompilerThread = std::make_unique<std::thread>(RunCompiler);
                        
                    }
                }
            }
            , member_ui<std::string>::button<>
            , member_dynamic_flags < +[](const compiler& O)
            {   xproperty::flags::type Flags{ xproperty::flags::DONT_SAVE };
                Flags.m_bShowReadOnly = O.isCompilerWorking() || O.hasValidationErrors();
                return Flags;
            }
            >>
        , obj_member< "bUseDebugCompiler", &compiler::m_bUseDebugCompiler >
        )
    };
    XPROPERTY_REG(compiler)
}