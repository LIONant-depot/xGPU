#ifndef ASSERT_MGR_HPP
#define ASSERT_MGR_HPP
#include <windows.h>
#include <filesystem>
#include <cstdlib>

#include "../../dependencies/xtexture.plugin/dependencies/xresource_pipeline_v2/dependencies/xresource/dependencies/xcontainer/source/xcontainer.h"
#pragma once

namespace e10
{

    //------------------------------------------------------------------------------------------------

    struct tag_type
    {
        std::uint64_t  m_GUID;
        std::string    m_Name;
    };

    //------------------------------------------------------------------------------------------------

    struct tag_type_db
    {
        using db = xcore::lock::object<std::vector<tag_type>, xcore::lock::semaphore>;
        tag_type    m_Type;
        db          m_DB;
    };

    static constexpr xresource::type_guid folder_type_guid_v = xresource::type_guid{ xcore::guid::unit<>("folder").m_Value };
    static constexpr xresource::type_guid folder_type_guid2_v = xresource::type_guid::generate_guid("folder");


    //------------------------------------------------------------------------------------------------

    struct library
    {
        static constexpr auto   project_type_guid_v = xresource::type_guid{ xcore::guid::unit<>("project").m_Value };
        using                   project_guid        = xresource::def_guid<project_type_guid_v>;

        static constexpr auto  library_type_guid_v  = xresource::type_guid{ xcore::guid::unit<>("library").m_Value };
        using                  library_guid         = xresource::def_guid<library_type_guid_v>;

                                        library     ()          = default;
                                        library     (library&&) = default;
                            library&    operator =  (library&&) = default;

        xresource::full_guid            m_GUID;
        std::wstring                    m_Path;
        std::wstring                    m_UserDescriptorPath;
        std::wstring                    m_SysDescriptorPath;
        std::vector<library>            m_Libraries;
        bool                            m_bRootProject      = false;

        XPROPERTY_DEF
        ( "Library", library
        , obj_member <"GUID", &library::m_GUID>
        , obj_member <"Path", +[](library& O, bool bRead, std::string& Value )
            {
                if (bRead) Value    = strXstr(O.m_Path);
                else       O.m_Path = strXstr(Value);
            }>
        , obj_member <"Libraries", &library::m_Libraries
            , member_dynamic_flags<+[]( const library& O )
            {
                xproperty::flags::type Flags{};
                Flags.m_bDontSave = O.m_bRootProject == false;
                return Flags;
            }
            >>
        )
    };
    XPROPERTY_REG(library)

    //================================================================================================
    struct process_info_job;

    struct library_db
    {
        struct asset_dep
        {
            std::string m_Name;
        };

        struct info_node
        {
            std::wstring                        m_Path;
            xresource_pipeline::info            m_Info;
            std::vector<asset_dep>              m_lAssetDependencies;
            std::vector<xresource::full_guid>   m_lChildLinks;

            void clear()
            {
                m_lAssetDependencies.clear();
                m_lChildLinks.clear();
            }
        };

        void clear()
        {
            m_InfoByTypeDataBase.clear();
        }

        struct info_db
        {
            using map = xcontainer::unordered_lockless_map<xresource::instance_guid, library_db::info_node>;
            xresource::type_guid    m_TypeGUID;
            map                     m_InfoDataBase;
        };

        using map = xcontainer::unordered_lockless_map<xresource::type_guid, std::unique_ptr<library_db::info_db>>;
        std::unique_ptr<process_info_job>   m_pProcessInfoJob;
        library                             m_Library;
        map                                 m_InfoByTypeDataBase;
    };

    //================================================================================================

    struct process_info_job final : xcore::scheduler::job<0>
    {
        enum class state
        { IDLE
        , READY_TO_RUN
        , RUNNING
        , DONE
        };

        using err_list = xcore::lock::object<std::vector<std::wstring>, xcore::lock::spin>;

        library_db*                  m_pLibraryDB    = nullptr;
        std::atomic<state>           m_State         = { state::IDLE };
        std::atomic<bool>            m_bCancel       = { false };
        err_list                     m_lErrors       = {};
        xproperty::settings::context m_Context       = {};


        //------------------------------------------------------------------------------------------

        void setup( library_db& LibraryDB, xproperty::settings::context&& Context )
        {
            m_pLibraryDB = &LibraryDB;
            m_Context    = std::move(Context);
            m_State      = state::READY_TO_RUN;
        }

        //------------------------------------------------------------------------------------------

        process_info_job()
        {
            setupName({"Process_Infos"});
            setupLifeTime(xcore::scheduler::lifetime::DONT_DELETE_WHEN_DONE);
        }

        //------------------------------------------------------------------------------------------

        xcore::err LoadInfo( const std::wstring_view Path )
        {
            // Load the file
            xresource_pipeline::info Info;
            if (auto Err = Info.Serialize(true, strXstr(Path), m_Context); Err)
            {
                xcore::lock::scope Lk(m_lErrors);
                m_lErrors.get().push_back(std::format(L"Fail to load info file[{}] with error[{}]", Path.data(), strXstr(Err.getCode().m_pString).c_str()));
                printf("[INFO] %ls\n", m_lErrors.get().back().c_str());
                return Err;
            }

            // Insert the info into the database
            m_pLibraryDB->m_InfoByTypeDataBase.FindAsReadOnlyOrCreate
            ( {Info.m_Guid.m_Type}
            , [&](std::unique_ptr<library_db::info_db>& Entry)
            {
                Entry = std::make_unique<library_db::info_db>();
            }
            ,[&](const std::unique_ptr<library_db::info_db>& Entry)
            {
                Entry->m_TypeGUID = Info.m_Guid.m_Type;
                Entry->m_InfoDataBase.FindAsWriteOrCreate
                ( Info.m_Guid.m_Instance
                , [&](library_db::info_node&)
                {
                    // No need to do anything special at creation side...
                }
                , [&](library_db::info_node& InfoNode)
                {
                    // Make sure we update the path and the info
                    InfoNode.m_Path = Path;
                    InfoNode.m_Info = std::move(Info);

                    //
                    // Insert all the child links
                    //
                    for (const auto& LinkGuid : InfoNode.m_Info.m_RscLinks )
                    {
                        m_pLibraryDB->m_InfoByTypeDataBase.FindAsReadOnlyOrCreate
                        ({ LinkGuid.m_Type }
                        , [&](std::unique_ptr<library_db::info_db>& Entry)
                        {
                            Entry = std::make_unique<library_db::info_db>();
                        }
                        , [&](const std::unique_ptr<library_db::info_db>& Entry)
                        {
                            Entry->m_InfoDataBase.FindAsWriteOrCreate
                            (LinkGuid.m_Instance
                            , [&](library_db::info_node&)
                            {
                                // No need to do anything special at creation side...
                            }
                            , [&](library_db::info_node& ParentInfoNode)
                            {
                                ParentInfoNode.m_lChildLinks.push_back({ InfoNode.m_Info.m_Guid });
                            });
                        });
                    }
                });
            });

            return {};
        }

        //------------------------------------------------------------------------------------------

        void ProcessInfos( std::wstring_view Path )
        {
            xcore::scheduler::channel Channel({"Collect Infos from Path"});

            //
            // Read all the infos
            //
            for (const auto& entry : std::filesystem::recursive_directory_iterator(Path))
            {
                if (m_bCancel) return;

                if (std::filesystem::is_directory(entry.path()))
                {
                    if (entry.path().extension() == L".desc")
                    {
                        Channel.SubmitJob( [this, Path = std::wstring(entry.path())]
                        {
                            if (m_bCancel) return;
                            if (auto Err = LoadInfo(std::format(L"{}\\info.txt", Path) ); Err )
                            {
                                // We choose to ignore the errors...
                                Err.clear();
                            }
                        });
                    }
                }
                else
                {
                    // Files are ignore...
                }
            }
        }

        //------------------------------------------------------------------------------------------

        void qt_onDone() noexcept override
        {
            m_State = state::DONE;
        }

        //------------------------------------------------------------------------------------------

        void qt_onRun() noexcept override
        {
            m_State = state::RUNNING;

            if ( m_bCancel == false ) ProcessInfos(m_pLibraryDB->m_Library.m_UserDescriptorPath);
            if ( m_bCancel == false ) ProcessInfos(m_pLibraryDB->m_Library.m_SysDescriptorPath);
        }
    };

    //------------------------------------------------------------------------------------------------
    //
    // Library Mgr
    //
    //------------------------------------------------------------------------------------------------
    struct library_mgr
    {
        struct process_descriptors_info
        {
            xcore::scheduler::channel&      m_Channel;
            library_db&                     m_Project;
            xproperty::settings::context&   m_Context;
            const bool                      m_isUser;
        };

        //------------------------------------------------------------------------------------------------

        xcore::err CreatePath( const std::wstring_view Path) const noexcept
        {
            std::error_code         ec;
            std::filesystem::path   path{ Path };

            std::filesystem::create_directories(path, ec);
            if (ec)
            {
                printf("Fail to create a directory [%s] with error [%s]", strXstr(Path).c_str(), ec.message().c_str());
                return xerr_failure_s("Fail to create a directory");
            }

            return {};
        }

        //------------------------------------------------------------------------------------------------

        xcore::err MoveDescriptor(const xresource::full_guid& libraryGUID, const xresource::full_guid& sourceDescriptor, const xresource::full_guid& sourceParent, const xresource::full_guid& Target)
        {
            xcore::err Error;

            assert(libraryGUID.m_Type == e10::library::project_type_guid_v || libraryGUID.m_Type == e10::library::library_type_guid_v );
            assert(sourceDescriptor.empty()==false);
            assert(sourceParent.empty() == false);
            assert(Target.empty() == false);
            assert(libraryGUID.empty() == false);
            assert(sourceDescriptor != Target);
            assert(sourceDescriptor != sourceParent);
            assert(sourceParent != Target);

            bool foundLibrary = m_mLibraryDB.FindAsReadOnly(libraryGUID, [&](const library_db& library) 
            {
                // Find the source descriptor in the library database
                bool bFoundSourceType = library.m_InfoByTypeDataBase.FindAsReadOnly(sourceDescriptor.m_Type, [&](const std::unique_ptr<library_db::info_db>& sourceInfoDB) 
                {
                    bool bSourceIntanceFound = sourceInfoDB->m_InfoDataBase.FindAsWrite(sourceDescriptor.m_Instance, [&](library_db::info_node& sourceNode) 
                    {
                        // Remove the current parent
                        int IndexToRemove = -1;
                        for( int i=0; i< sourceNode.m_Info.m_RscLinks.size(); ++i )
                        {
                            if (sourceNode.m_Info.m_RscLinks[i] == sourceParent)
                            {
                                IndexToRemove = i;
                                break;
                            }
                        }

                        if (IndexToRemove ==  -1 )
                        {
                            Error = xerr_failure_s("The source parent was not found in the source descriptor");
                            return;
                        }

                        bool bTargetType = library.m_InfoByTypeDataBase.FindAsReadOnly(Target.m_Type, [&](const std::unique_ptr<library_db::info_db>& targetInfoDB)
                        {
                            bool bTargetInstance = targetInfoDB->m_InfoDataBase.FindAsWrite(Target.m_Instance, [&](library_db::info_node& targetNode)
                            {
                                bool bParentType = library.m_InfoByTypeDataBase.FindAsReadOnly(sourceParent.m_Type, [&](const std::unique_ptr<library_db::info_db>& parentInfoDB)
                                {
                                    // Find the target parent in the library database
                                    bool bFoundParent = parentInfoDB->m_InfoDataBase.FindAsWrite(sourceParent.m_Instance, [&](library_db::info_node& parentNode)
                                    {
                                        int ChildIndexToRemove = -1;
                                        for( int i=0; i<parentNode.m_lChildLinks.size(); ++i)
                                        {
                                            if (parentNode.m_lChildLinks[i] == sourceDescriptor)
                                            {
                                                ChildIndexToRemove = i;
                                                break;
                                            }
                                        }

                                        if (ChildIndexToRemove == -1)
                                        {
                                            Error = xerr_failure_s("The source descriptor was not found in the parent");
                                            return;
                                        }

                                        //
                                        // Finally we can do our atomic operation...
                                        //

                                        // Remove the child link from the parent
                                        parentNode.m_lChildLinks.erase(parentNode.m_lChildLinks.begin() + ChildIndexToRemove);

                                        // Update the child links of the target parent to include the source descriptor
                                        targetNode.m_lChildLinks.push_back(sourceDescriptor);

                                        // Add the new parent... (Let's hope that we can find the parent...)
                                        sourceNode.m_Info.m_RscLinks.erase(sourceNode.m_Info.m_RscLinks.begin() + IndexToRemove);
                                        sourceNode.m_Info.m_RscLinks.push_back(Target);
                                    });

                                    if (bFoundParent==false)
                                    {
                                        Error = xerr_failure_s("The parent instance was not found in the library database");
                                        return;
                                    }
                                });

                                if (bParentType == false)
                                {
                                    Error = xerr_failure_s("The parent type was not found in the library database");
                                    return;
                                }
                            });

                            if (bTargetInstance == false )
                            {
                                Error = xerr_failure_s("Target instance not found in the database");
                                return;
                            }
                        });

                        if (bTargetType == false)
                        {
                            Error = xerr_failure_s("The target type was not found in the library database");
                            return;
                        }
                    });

                    if (bSourceIntanceFound == false )
                    {
                        Error = xerr_failure_s("The source intance was not found in the database");
                        return;
                    }
                    });

                if (bFoundSourceType == false )
                {
                    Error = xerr_failure_s("The source type was not found in the library database");
                    return;
                }
            });

            return Error;
        }

        //------------------------------------------------------------------------------------------------

        xresource::full_guid NewAsset(const xresource::type_guid ResourceType, const xresource::full_guid LibraryGUID, const xresource::full_guid ParentGUID, const std::string_view Name = {} )
        {
            assert(m_mLibraryDB.empty() == false);

            const xresource::full_guid NewGuid { xcore::guid::unit<64>(xcore::not_null).m_Value | 1 , ResourceType  };

            // Get the name of the type
            std::string TypeName;
            if ( auto P = m_AssetPluginsDB.find(ResourceType); P )
            {
                TypeName = P->m_TypeName;
            }

            bool bFindLib = m_mLibraryDB.FindAsReadOnly(LibraryGUID, [&](const library_db& Library )
            {
                // TODO: The info path is completle incorrect
                std::wstring FinalPath = std::format(L"{}\\Descriptors\\{}\\{:02X}\\{:02X}\\{:X}.desc"
                    , Library.m_Library.m_Path.c_str()
                    , strXstr(TypeName).c_str()
                    , NewGuid.m_Instance.m_Value & 0xff
                    , (NewGuid.m_Instance.m_Value & 0xff00) >> 8
                    , NewGuid.m_Instance.m_Value
                );

                if (auto Err = CreatePath(FinalPath); Err)
                {
                    printf("Fail to create a directory [%s] with error [%s]", strXstr(FinalPath).c_str(), Err.getCode().m_pString);
                    exit(1);
                }

                xresource_pipeline::info Info;
                Info.m_Name = Name;
                Info.m_Guid = NewGuid;
                if( ParentGUID.empty() == false ) Info.m_RscLinks.push_back(ParentGUID);

                xproperty::settings::context Context;
                if ( auto Err = Info.Serialize( false, std::format("{}\\Info.txt", strXstr(FinalPath).c_str()).c_str(), Context); Err)
                {
                    printf("Fail to create a directory [%s] with error [%s]", strXstr(FinalPath).c_str(), Err.getCode().m_pString);
                    exit(1);
                }

                // Create a new asset
                Library.m_InfoByTypeDataBase.FindAsReadOnlyOrCreate
                ( NewGuid.m_Type
                , [&](std::unique_ptr<library_db::info_db>& InfoDB)
                {
                    InfoDB = std::make_unique<library_db::info_db>();
                }
                , [&](const std::unique_ptr<library_db::info_db>& InfoDB)
                {
                    InfoDB->m_InfoDataBase.Insert(NewGuid.m_Instance, [&](library_db::info_node& InfoNode)
                    {
                        InfoNode.m_Path   = std::move(FinalPath);
                        InfoNode.m_Info   = std::move(Info);

                        //
                        // Insert all the child links
                        //
                        for (const auto& LinkGuid : InfoNode.m_Info.m_RscLinks)
                        {
                            Library.m_InfoByTypeDataBase.FindAsReadOnlyOrCreate
                            ( LinkGuid.m_Type 
                            , [&](std::unique_ptr<library_db::info_db>& Entry)
                            {
                                Entry = std::make_unique<library_db::info_db>();
                            }
                            , [&](const std::unique_ptr<library_db::info_db>& Entry)
                            {
                                Entry->m_InfoDataBase.FindAsWriteOrCreate
                                (LinkGuid.m_Instance
                                , [&](library_db::info_node&)
                                {
                                    // No need to do anything special at creation side...
                                }
                                , [&](library_db::info_node& ParentInfoNode)
                                {
                                    ParentInfoNode.m_lChildLinks.push_back({ InfoNode.m_Info.m_Guid });
                                });
                            });
                        }
                    });
                });
            });

            assert(bFindLib);

            return NewGuid;
        }

        //------------------------------------------------------------------------------------------------

        xresource::full_guid NewAsset(const xresource::type_guid ResourceType )
        {
            return NewAsset(ResourceType, m_ProjectGUID, {0,0});
        }

        //------------------------------------------------------------------------------------------------

        xcore::err OpenProject( std::wstring_view ProjectPath )
        {
            assert(m_mLibraryDB.empty());

            /*
            project Project;

            Project.m_GUID.m_Instance = xcore::guid::unit<64>(xcore::not_null).m_Value;
            Project.m_GUID.m_Type     = project::project_type_guid_v;
            Project.m_Path            = ProjectPath;
            */

            m_ProjectPath = ProjectPath;

            //
            // Set this for the properties system... default path
            //
            xproperty::member_ui<std::string>::g_CurrentPath = std::format("{}\\Assets", strXstr(m_ProjectPath).c_str());

            //
            // Set the current directory to the asset folder
            //
            SetCurrentDirectory((std::wstring(ProjectPath) + L"\\Assets").c_str());

            //
            // Setup the asset plugins
            //
            m_AssetPluginsDB.SetupProject(m_ProjectPath);

            //
            // Load the config file for the project
            // TODO: Need to add the code load also libraries...
            library Library;
            {
                xcore::textfile::stream Stream;
                if (auto Err = Stream.Open(true, strXstr(std::format(L"{}\\Project.config\\Library.config.txt", ProjectPath).c_str()), {xcore::textfile::file_type::TEXT}); Err)
                    return Err;

                xproperty::settings::context Context;
                if ( auto Err = xproperty::sprop::serializer::Stream( Stream, Library, Context ); Err )
                    return Err;

                //
                // Fix to the proper path
                //
                Library.m_Path = ProjectPath;
                Library.m_UserDescriptorPath = std::format(L"{}\\Descriptors", Library.m_Path);
                Library.m_SysDescriptorPath = std::format(L"{}\\Cache\\Descriptors", Library.m_Path);
            }

            //
            // Set the project as the root project
            //
            m_ProjectGUID = Library.m_GUID;

            //
            // Load all the infos
            //
            m_mLibraryDB.Insert(Library.m_GUID, [&](library_db& ProjectDB)
            {
                ProjectDB.m_Library = std::move(Library);
                ProjectDB.m_pProcessInfoJob = std::make_unique<process_info_job>();

                ProjectDB.m_pProcessInfoJob->setup(ProjectDB, {});

                xcore::get().m_Scheduler.AddJobToQuantumWorld(*ProjectDB.m_pProcessInfoJob);
            });

            //
            // Insert all the plugins found in the libraries
            //
            for(auto& L : m_mLibraryDB )
            {
                for ( auto& P : m_AssetPluginsDB.m_lPlugins )
                {
                    L.second.m_InfoByTypeDataBase.FindAsReadOnlyOrCreate
                    ( P.m_TypeGUID
                    , [&](std::unique_ptr<library_db::info_db>& Entry)
                    {
                        Entry = std::make_unique<library_db::info_db>();
                        Entry->m_TypeGUID = P.m_TypeGUID;
                    }
                    ,[](const std::unique_ptr<library_db::info_db>&){}
                    );
                }
            }

            return {};
        }

        //------------------------------------------------------------------------------------------------

        void CloseProject()
        {
            //
            // TODO: Need to stop all the threads before closing the project
            //
            m_mLibraryDB.clear();
        }

        //------------------------------------------------------------------------------------------------

        void add_watch(const std::filesystem::path& path, std::unordered_map<HANDLE, std::filesystem::path>& watch_descriptors)
        {
            HANDLE hSubDir = CreateFile
            ( path.c_str()
            , FILE_LIST_DIRECTORY
            , FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE
            , NULL
            , OPEN_EXISTING
            , FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED
            , NULL
            );

            if (hSubDir == INVALID_HANDLE_VALUE) 
            {
                std::cerr << "Error adding watch for " << path << std::endl;
            }
            else 
            {
                watch_descriptors[hSubDir] = path;
                std::cout << "Watching: " << path << std::endl;
            }
        }

        //------------------------------------------------------------------------------------------------

        void MonitorDirectory( const std::filesystem::path& path )
        {
            // Make sure that the path is a directory
            if (!std::filesystem::exists(path) || !std::filesystem::is_directory(path)) 
            {
                std::cerr << "Invalid directory path." << std::endl;
                return;
            }

            HANDLE hDir = CreateFile
            ( path.c_str()
            , FILE_LIST_DIRECTORY
            , FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE
            , NULL
            , OPEN_EXISTING
            , FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED
            , NULL
            );

            if (hDir == INVALID_HANDLE_VALUE) 
            {
                std::cerr << "Error initializing directory watch" << std::endl;
                return;
            }

            std::unordered_map<HANDLE, std::filesystem::path> watch_descriptors;
            add_watch(path, watch_descriptors);

            for (const auto& entry : std::filesystem::recursive_directory_iterator(path)) 
            {
                if (std::filesystem::is_directory(entry.path())) 
                {
                    add_watch(entry.path(), watch_descriptors);
                }
            }

            const DWORD buffer_size = 1024 * (sizeof(FILE_NOTIFY_INFORMATION) + MAX_PATH);
            std::vector<BYTE> buffer(buffer_size);
            DWORD bytes_returned;

            while (true) 
            {
                if (ReadDirectoryChangesW
                    ( hDir
                    , buffer.data()
                    , buffer_size
                    , TRUE
                    , FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME   | FILE_NOTIFY_CHANGE_ATTRIBUTES |
                      FILE_NOTIFY_CHANGE_SIZE      | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION
                    , &bytes_returned
                    , NULL
                    , NULL
                )) {
                    FILE_NOTIFY_INFORMATION*    pNotify;
                    std::size_t                 offset = 0;

                    do 
                    {
                        pNotify = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(&buffer[offset]);
                        std::wstring file_name(pNotify->FileName, pNotify->FileNameLength / sizeof(WCHAR));
                        std::filesystem::path event_path = watch_descriptors[hDir] / file_name;

                        switch (pNotify->Action)
                        {
                        case FILE_ACTION_ADDED:
                            std::cout << "File created: " << event_path << std::endl;
                            if (std::filesystem::is_directory(event_path)) 
                            {
                                add_watch(event_path, watch_descriptors);
                            }
                            break;
                        case FILE_ACTION_REMOVED:
                            std::cout << "File deleted: " << event_path << std::endl;
                            break;
                        case FILE_ACTION_MODIFIED:
                            std::cout << "File modified: " << event_path << std::endl;
                            break;
                        case FILE_ACTION_RENAMED_OLD_NAME:
                            std::cout << "File renamed from: " << event_path << std::endl;
                            break;
                        case FILE_ACTION_RENAMED_NEW_NAME:
                            std::cout << "File renamed to: " << event_path << std::endl;
                            break;
                        default:
                            break;
                        }

                        offset += pNotify->NextEntryOffset;
                    } while (pNotify->NextEntryOffset != 0);
                }
                else 
                {
                    std::cerr << "Error reading directory changes" << std::endl;
                    break;
                }
            }

            // Clean up all handles
            for (const auto& [handle, path] : watch_descriptors) 
            {
                CloseHandle(handle);
            }
            CloseHandle(hDir);
        }

        //------------------------------------------------------------------------------------------------

        using map = xcontainer::unordered_lockless_map<xresource::full_guid, library_db>;
        xresource::full_guid    m_ProjectGUID;
        std::wstring            m_ProjectPath;
        map                     m_mLibraryDB;
        asset_plugins_db        m_AssetPluginsDB;
    };
}
#endif