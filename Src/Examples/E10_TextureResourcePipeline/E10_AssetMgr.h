#include <windows.h>
#include <filesystem>
#pragma once

namespace e10
{
    //------------------------------------------------------------------------------------------------
    //
    // Asset Mgr
    //
    //------------------------------------------------------------------------------------------------
    struct asset_mgr
    {
        void OpenProject( std::string_view ProjectPath )
        {
            assert(m_Library.empty());

            auto& Lib = *m_Library.emplace_back(std::make_unique<library>());
            Lib.m_ProjectPath        = ProjectPath;
            Lib.m_UserDescriptorPath = std::format("{}\\Descriptors",        Lib.m_ProjectPath.c_str());
            Lib.m_SysDescriptorPath  = std::format("{}\\Cache\\Descriptors", Lib.m_ProjectPath.c_str());

            // Start Reading descriptors
        }

        void CloseProject()
        {
            //
            // TODO: Need to stop all the threads before closing the project
            //
            m_Library.clear();
        }

        std::string m_ProjectPath;

        struct full_guid
        {
            std::uint64_t       m_Instance;     // also used in the pool as the next empty entry...
            std::uint64_t       m_Type;
        };

        struct asset_dep
        {
            std::string m_Name;
        };

        struct sys_rsc_info
        {
            full_guid                   m_GUID;
            std::vector<asset_dep>      m_lAssetDependencies;
            std::vector<std::uint64_t>  m_lVirtualDependencies;

            void clear()
            {
                m_lAssetDependencies.clear();
                m_lVirtualDependencies.clear();
            }
        };

        struct user_rsc_info : sys_rsc_info
        {
            std::string                 m_Name;
            std::uint64_t               m_TreeGUID;

            void clear()
            {
                sys_rsc_info::clear();
                m_Name.clear();
            }
        };

        template< typename T, auto T_MAX_ENTRIES_V >
        struct pool
        {
            inline static constexpr auto end_list_v     = ~static_cast<std::uint64_t>(0);
            inline static constexpr auto max_entries_v  = T_MAX_ENTRIES_V;

            using pool_entry    = xcore::lock::object<T, xcore::lock::semaphore>;
            using pool_list     = std::unique_ptr<pool_entry[]>;
            using index         = std::uint64_t;

            template< typename T>
            void ReadOnly(index Index, T&& Callback )
            {
                const auto& Entry = m_Pool[Index];
                xcore::lock::scope Lk(Entry);
                Callback( Entry );
            }

            template< typename T>
            void Write(index Index, T&& Callback)
            {
                auto& Entry = m_Pool[Index];
                xcore::lock::scope Lk(Entry);
                Callback(Entry);
            }

            template< typename T>
            void FreeEntry(index Index, T&& Callback = [](auto&){})
            {
                xcore::lock::scope Lk(m_Pool[Index].get());
                auto& Entry = m_Pool[Index].get();

                // Just in case the user wants to do something before we officially kill it
                Callback(Entry);

                // Clear entry...
                Entry.clear();

                std::uint64_t Local = m_EmptyList.load(std::memory_order_relaxed);
                do
                {
                    Entry.m_GUID.m_Instance = Local;
                    if ( m_EmptyList.compare_exchange_weak(Local, Index, std::memory_order_release, std::memory_order_relaxed) ) break;
                } while( true );
            }

            template< typename T>
            index Alloc(T&& Callback)
            {
                std::uint64_t Local = m_EmptyList.load(std::memory_order_relaxed);
                do
                {
                    xcore::lock::scope Lk(m_Pool[Local].get());

                    auto& Entry = m_Pool[Local].get();

                    assert(Entry.m_GUID.m_Instance != end_list_v);
                    if ( m_EmptyList.compare_exchange_weak(Local, Entry.m_GUID.m_Instance, std::memory_order_release, std::memory_order_relaxed) )
                    {
                        Callback(Entry);
                        break;
                    }
                } while (true);

                return Local;
            }

            pool_list                   m_Pool      = std::make_unique<pool_entry[]>(max_entries_v);
            std::atomic<std::uint64_t>  m_EmptyList = 0;
        };

        struct data_base
        {
            using user_rsc_pool = pool<user_rsc_info, 1024>;
            using sys_rsc_pool  = pool<sys_rsc_info, 1024>;
            using rsc_map       = xcore::lock::object<std::unordered_map<std::uint64_t, user_rsc_pool::index>, xcore::lock::semaphore>;

            user_rsc_pool   m_UserRscPool   = {};
            sys_rsc_pool    m_SysRscPool    = {};
            rsc_map         m_RscMap        = {};
        };

        struct library
        {
            std::string         m_ProjectPath;
            std::string         m_UserDescriptorPath;
            std::string         m_SysDescriptorPath;
            std::uint64_t       m_TreeRootGUID;
            data_base           m_RscDataBase;
        };

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


        std::vector<std::unique_ptr<library>>    m_Library;
    };
}

//
// Deal with std::unorde_map issues
//
bool operator==(const e10::compiler::asset_mgr::full_guid& A, const e10::compiler::asset_mgr::full_guid& B)
{
    return A.m_Instance == A.m_Instance && B.m_Type == B.m_Tyoe;
}
namespace std
{
    template<>
    struct hash<e10::compiler::asset_mgr::full_guid>
    {
        std::size_t operator()(const e10::compiler::asset_mgr::full_guid& k) const
        {
            // Use a combination of the two uint64_t values
            return std::hash<std::uint64_t>{}(k.m_Instance) ^ (std::hash<std::uint64_t>{}(k.m_Type) << 1);
        }
    };
}
