#ifndef ASSERT_MGR_HPP
#define ASSERT_MGR_HPP
#pragma once

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#ifdef ERROR
    #undef ERROR
#endif

#include <algorithm>
#include <filesystem>
#include <cstdlib>
#include <chrono>
#include <queue>
#include <cwctype>
#include <iostream>


#include "dependencies/xproperty/source/xcore/my_properties.h"
#include "dependencies/xcontainer/source/xcontainer.h"
#include "dependencies/xdelegate/source/xdelegate.h"
#include "dependencies/xscheduler/source/xscheduler.h"
#include "dependencies/xstrtool/source/xstrtool.h"
#include "dependencies/xresource_guid/source/xresource_guid.h"

#define XRESOURCE_PIPELINE_NO_COMPILER
#include "dependencies/xresource_pipeline_v2/source/xresource_pipeline.h"

#include "E10_PluginMgr.h"

namespace e10
{
    namespace details
    {

        // Primary template
        template<typename T>
        struct function_traits;

        // Specialization for function pointers
        template<typename R, typename... Args>
        struct function_traits<R(*)(Args...)>
    {
            using result_type = R;
            static constexpr std::size_t arity = sizeof...(Args);

            template<std::size_t N>
            struct arg
            {
                static_assert(N < arity, "Argument index out of range");
                using type = typename std::tuple_element<N, std::tuple<Args...>>::type;
            };
        };

        // Specialization for member function pointers (e.g., lambdas)
        template<typename C, typename R, typename... Args>
        struct function_traits<R(C::*)(Args...) const> {
            using result_type = R;
            static constexpr std::size_t arity = sizeof...(Args);

            template<std::size_t N>
            struct arg
            {
                static_assert(N < arity, "Argument index out of range");
                using type = typename std::tuple_element<N, std::tuple<Args...>>::type;
            };
        };

        // Helper to deduce from callable objects (like lambdas)
        template<typename T>
        struct function_traits : function_traits<decltype(&T::operator())> {};
    }

    //------------------------------------------------------------------------------------------------

    inline std::chrono::zoned_time<std::chrono::milliseconds> ConvertToStdTime(std::filesystem::file_time_type FileTime)
{
        // Convert file_time_type to system_clock time_point
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>
        (
            FileTime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
        );

        // Cast to milliseconds
        auto sctp_milliseconds = std::chrono::time_point_cast<std::chrono::milliseconds>(sctp);

        // Create zoned_time with the current time zone
        return std::chrono::zoned_time{ std::chrono::current_zone(), sctp_milliseconds };
    }

    //------------------------------------------------------------------------------------------------
    inline
    std::string move_directory(std::wstring_view destination_path, std::wstring_view source_path, bool bOverride = true ) noexcept
    {
        try
        {
            if (bOverride)
            {
                if (std::filesystem::exists(destination_path))
                {
                    std::filesystem::remove_all(destination_path); // Remove existing destination
                }
            }

            // Move the directory from source_path to destination_path
            std::filesystem::rename(source_path, destination_path);
        }
        catch (const std::filesystem::filesystem_error& e)
        {
            return std::format("Error moving directory: {}", e.what() );
        }
        catch (const std::exception& e)
        {
            return std::format ("Unexpected error: {}", e.what() );
        }
        return {};
    }

    //------------------------------------------------------------------------------------------------
    inline
    std::string create_directory_path(std::wstring_view full_path) noexcept
    {
        try
        {
            std::wstring current_path;

            // Handle paths starting with a separator (e.g., L"/root" or L"C:\\root")
            if (!full_path.empty() && (full_path[0] == L'/' || full_path[0] == L'\\'))
            {
                current_path = std::wstring(1, full_path[0]); // Start with root separator
            }

            // Iterate through the path segments
            size_t start = (current_path.empty() ? 0 : 1); // Skip initial separator if already added
            while (start < full_path.size())
            {
                // Find the next separator
                size_t end = full_path.find_first_of(L"/\\", start);
                if (end == std::wstring_view::npos)
                    end = full_path.size(); // No more separators, use the rest of the string

                // Append the current segment to the path
                current_path += full_path.substr(start, end - start);

                // Create the directory if it doesn't exist
                if (!std::filesystem::exists(current_path))
                {
                    std::filesystem::create_directory(current_path);
                }

                // Move to the next segment (skip the separator)
                start = end + 1;

                // Add separator for the next iteration if not at the end
                if (start < full_path.size())
                    current_path += full_path[end];
            }
        }
        catch (const std::filesystem::filesystem_error& e)
        {
            return std::format( "Error creating directory path: {}", e.what() );
        }
        catch (const std::exception& e)
        {
            return std::format( "Unexpected error: ", e.what() );
        }

        return {};
    }

    //------------------------------------------------------------------------------------------------
    inline
    std::variant<bool, std::string> has_content(std::wstring_view folder_path) noexcept
    {
        try
        {
            // Convert std::wstring_view to std::filesystem::path
            std::filesystem::path path(folder_path);

            // Check if the path exists and is a directory
            if (!std::filesystem::exists(path))
                return std::format("Error There is not such directory path: {}", xstrtool::To(folder_path) );
            
            if (!std::filesystem::is_directory(path))
                return std::format("Error This given path is not a directory: {}", xstrtool::To(folder_path));

            // Check if the directory has any entries
            auto dir_iter = std::filesystem::directory_iterator(path);
            return dir_iter != std::filesystem::directory_iterator(); // True if not empty
        }
        catch (const std::filesystem::filesystem_error& e)
        {
            return std::format("Error creating directory path: {}", e.what());
        }
        catch (const std::exception& e)
        {
            return std::format("Unexpected error: ", e.what());
        }
    }


    //------------------------------------------------------------------------------------------------
    // Function to extract the directory path (excluding filename) from a std::wstring_view
    inline
    std::wstring get_directory_path(std::wstring_view full_path) noexcept
    {
        // Convert std::wstring_view to std::filesystem::path
        std::filesystem::path path(full_path);

        // Get the parent directory path (excludes filename)
        return path.parent_path().wstring();
    }

    //------------------------------------------------------------------------------------------------
    // Function to extract the filename from a std::wstring_view
    inline
    std::wstring get_filename(std::wstring_view full_path)
    {
        // Convert std::wstring_view to std::filesystem::path
        std::filesystem::path path(full_path);

        // Get the filename (last component of the path)
        return path.filename().wstring();
    }

    //------------------------------------------------------------------------------------------------
    // Function to remove a substring from a std::wstring
    inline
    std::wstring remove_substring(std::wstring_view str, std::wstring_view to_remove)
    {
        if (to_remove.empty()) return std::wstring(str); // Nothing to remove, return as-is

        std::wstring result(str); // Convert str to modifiable wstring
        size_t pos = 0;

        while ((pos = result.find(to_remove, pos)) != std::wstring::npos)
        {
            result.erase(pos, to_remove.length());
            // pos remains the same after erase to check for overlapping matches
        }

        return result;
    }

    //------------------------------------------------------------------------------------------------

    namespace folder
    {
        static constexpr xresource::type_guid type_guid_v  = xresource::type_guid("folder");
        using guid                                         = xresource::def_guid<type_guid_v>;
        static constexpr guid                 trash_guid_v = {1};
    }

    //------------------------------------------------------------------------------------------------

    struct library
    {
        static constexpr auto  type_guid_v  = xresource::type_guid("library");
        using                  guid         = xresource::def_guid<type_guid_v>;

                                        library     ()          = default;
                                        library     (library&&) = default;
                            library&    operator =  (library&&) = default;

        guid                            m_GUID;                     // This is actually a full guid because it can be either a library or a project
        std::wstring                    m_Path;
        std::wstring                    m_UserDescriptorPath;
        std::wstring                    m_SysDescriptorPath;
        std::wstring                    m_ResourcePath;
        std::vector<library>            m_Libraries;
        bool                            m_bRootProject      = false;

        XPROPERTY_DEF
        ( "Library", library
        , obj_member <"GUID", +[](library& L)->std::uint64_t& { return L.m_GUID.m_Instance.m_Value; } >
        , obj_member <"Path", &library::m_Path>
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

    //------------------------------------------------------------------------------------------------

    namespace project
    {
        static constexpr auto   type_guid_v = xresource::type_guid("project");
        using                   guid        = xresource::def_guid<type_guid_v>;
    }

    //================================================================================================
    struct process_info_job;
    struct library_mgr;

    //================================================================================================
    namespace compilation
    {
        struct entry
        {
            xresource::full_guid    m_FullGuid = {};
            library::guid           m_gLibrary = {};
            int                     m_Priority = {};
        };
    }

    inline void CompilingThreadWorker(library_mgr& LibMgr, compilation::entry&& Entry);

    namespace compilation
    {
        struct historical_entry
        {
            enum class result : std::uint8_t
            { COMPILING
            , COMPILING_WARNINGS
            , SUCCESS
            , SUCCESS_WARNINGS
            , FAILURE
            };

            struct communication
            {
                std::string     m_Log       = {};
                result          m_Result    = {};
            };

            historical_entry()                                      = default;
            historical_entry(historical_entry&&)                    = default;
            historical_entry(const historical_entry&)               = default;
            historical_entry& operator = (historical_entry&&)       = default;
            historical_entry& operator = (const historical_entry&)  = default;

            using log = xcontainer::lock::object<communication, xcontainer::lock::spin>;
            entry                   m_Entry     = {};
            std::shared_ptr<log>    m_Log       = {};
        };

        namespace lockless
        {
            template<typename T_NODE>
            struct tagged_ptr
            {
                std::size_t m_Tag=0;

                inline constexpr static std::size_t shift_v         = 48;
                inline constexpr static std::size_t one_shift_v     = 1ULL << shift_v;
                inline constexpr static std::size_t bitmask_v       = 0xFFFFULL << shift_v;
                inline constexpr static std::size_t ptrmask_v       = ~bitmask_v;

                tagged_ptr(void)                          noexcept = default;
                tagged_ptr(T_NODE* ptr, std::size_t tag)  noexcept { assert(!(tag & ptrmask_v)); m_Tag = (reinterpret_cast<std::size_t>(ptr) & ptrmask_v) | tag; }
                tagged_ptr(T_NODE* ptr)                   noexcept : m_Tag{ reinterpret_cast<std::size_t>(ptr) } { m_Tag &= ptrmask_v; }

                void        IncTag      (void)            noexcept { m_Tag = IncTagCopy() | (m_Tag & ptrmask_v); }
                std::size_t IncTagCopy  (void)    const   noexcept { return (m_Tag + one_shift_v) & bitmask_v; }
                T_NODE*     data        (void)    const   noexcept { return reinterpret_cast<T_NODE*>(m_Tag & ptrmask_v); }
                std::size_t getTag      (void)    const   noexcept { return m_Tag & bitmask_v; }
            };

            template< typename T_USER_DATA >
            struct entry
            {
                using user_data     = T_USER_DATA;
                using tagged_ptr    = tagged_ptr<entry>;

                user_data                m_UserData;
                std::atomic<tagged_ptr>  m_pNext;
            };

            // (Michael-Scott algorithm queue (lockless queue) with ABA)
            template< typename T_ENTRY, typename T_MEMORY_POOL >
            struct queue
            {
                using entry         = T_ENTRY;
                using user_data     = typename entry::user_data;
                using tagged_ptr    = typename entry::tagged_ptr;

                queue(T_MEMORY_POOL& Pool) noexcept : m_Pool{ Pool }
                {
                    auto pDummy = m_Pool.pop();
                    assert(pDummy != nullptr);
                    pDummy->m_pNext.store({ nullptr }, std::memory_order_relaxed);
                    m_Head.store({ pDummy }, std::memory_order_relaxed);
                    m_Tail.store({ pDummy }, std::memory_order_relaxed);
                }

                // transfer ownership by exchanging to null
                queue(queue&& other) noexcept
                    : m_Pool{other.m_Pool}, m_Head(nullptr), m_Tail(nullptr) {
                    m_Head.store(other.m_Head.exchange(tagged_ptr{nullptr}, std::memory_order_relaxed),std::memory_order_relaxed);
                    m_Tail.store(other.m_Tail.exchange(tagged_ptr{nullptr}, std::memory_order_relaxed),std::memory_order_relaxed);
                }

                // move assignment
                queue& operator=(queue&& other) noexcept
                {
                    if (this != &other)
                    {
                        assert(&m_Pool == &other.m_Pool); // Pools must match
                        auto old_head = m_Head.exchange(other.m_Head.exchange(tagged_ptr{ nullptr }, std::memory_order_relaxed),std::memory_order_relaxed);
                        auto old_tail = m_Tail.exchange(other.m_Tail.exchange(tagged_ptr{ nullptr }, std::memory_order_relaxed),std::memory_order_relaxed);

                        // Drain old queue
                        for (entry* pCurrent = old_head.data(); pCurrent; pCurrent = pCurrent->m_pNext.load(std::memory_order_relaxed).data())
                        {
                            m_Pool.push(*pCurrent);
                        }
                    }
                    return *this;
                }

                ~queue(void) noexcept
                {
                    tagged_ptr LocalHead = m_Head.load(std::memory_order_relaxed);
                    for (entry* pCurrent = LocalHead.data(); pCurrent; pCurrent = pCurrent->m_pNext.load(std::memory_order_relaxed).data())
                    {
                        m_Pool.push(*pCurrent);
                    }
                }

                void push(user_data&& UserData ) noexcept
                {
                    auto pNode = m_Pool.pop();
                    assert(pNode != nullptr); // Ensure allocation succeeded
                    pNode->m_UserData = std::move(UserData);
                    pNode->m_pNext.store({nullptr}, std::memory_order_relaxed);

                    do
                    {
                        tagged_ptr  LocalTail           = m_Tail.load(std::memory_order_relaxed);
                        entry*      pLocalTail          = LocalTail.data();
                        tagged_ptr  LocalNext           = pLocalTail->m_pNext.load(std::memory_order_relaxed);
                        entry*      pLocalNext          = LocalNext.data();
                        tagged_ptr  LocalCurrentTail    = m_Tail.load(std::memory_order_relaxed);

                        if (LocalTail.m_Tag == LocalCurrentTail.m_Tag)
                        {
                            if (pLocalNext == nullptr)
                            {
                                tagged_ptr NewNext{ pNode, LocalNext.IncTagCopy() };
                                if (pLocalTail->m_pNext.compare_exchange_weak(LocalNext, NewNext, std::memory_order_release))
                                {
                                    tagged_ptr NewTail{ pNode, LocalTail.IncTagCopy() };
                                    m_Tail.compare_exchange_weak(LocalTail, NewTail, std::memory_order_release);
                                    ++m_Count;
                                    return;
                                }
                            }
                            else
                            {
                                tagged_ptr NewTail{ pLocalNext, LocalTail.IncTagCopy() };
                                m_Tail.compare_exchange_weak(LocalTail, NewTail, std::memory_order_release);
                            }
                        }
                    } while (true);
                }

                bool pop(user_data& UserData) noexcept
                {
                    do
                    {
                        tagged_ptr  LocalHead   = m_Head.load(std::memory_order_relaxed);
                        entry*      pLocalHead  = LocalHead.data();
                        assert(pLocalHead != nullptr); // Head should always be valid

                        tagged_ptr  LocalTail   = m_Tail.load(std::memory_order_relaxed);
                        entry*      pLocalTail  = LocalTail.data();
                        assert(pLocalTail != nullptr); // Tail should always be valid

                        tagged_ptr  LocalNext   = pLocalHead->m_pNext.load(std::memory_order_acquire);
                        entry*      pLocalNext  = LocalNext.data();
                        if(LocalHead.m_Tag != m_Head.load(std::memory_order_relaxed).m_Tag)
                            continue;

                        if (pLocalHead == pLocalTail)
                        {
                            if (pLocalNext == nullptr) return false;

                            tagged_ptr NewTail{ pLocalNext, LocalTail.IncTagCopy() };
                            m_Tail.compare_exchange_weak(LocalTail, NewTail, std::memory_order_release);
                        }
                        else
                        {
                            if (pLocalNext == nullptr) continue; // Rare race safeguard

                            tagged_ptr NewHead{ pLocalNext, LocalHead.IncTagCopy() };
                            if (m_Head.compare_exchange_weak(LocalHead, NewHead, std::memory_order_release))
                            {
                                UserData = std::move(pLocalNext->m_UserData);
                                m_Pool.push(*pLocalHead);
                                --m_Count;
                                return true;
                            }
                        }
                    } while (true);
                }

                std::size_t size(void) const noexcept
                {
                    return m_Count.load(std::memory_order_relaxed);
                }

                // Iterator support (const, forward-only; assumes no concurrent modifications)
                struct const_iterator
                {
                    using value_type        = const user_data;
                    using pointer           = value_type*;
                    using reference         = value_type&;
                    using difference_type   = std::ptrdiff_t;
                    using iterator_category = std::forward_iterator_tag;

                    const_iterator() noexcept : m_current(nullptr) {}
                    explicit const_iterator(entry* node) noexcept : m_current(node) {}

                    reference   operator*()     const noexcept { return m_current->m_UserData; }
                    pointer     operator->()    const noexcept { return &m_current->m_UserData; }

                    const_iterator& operator++() noexcept
                    {
                        m_current = m_current->m_pNext.load(std::memory_order_acquire).data();
                        return *this;
                    }

                    const_iterator operator++(int) noexcept
                    {
                        const_iterator tmp = *this;
                        ++(*this);
                        return tmp;
                    }

                    bool operator==(const const_iterator& other) const noexcept { return m_current == other.m_current; }
                    bool operator!=(const const_iterator& other) const noexcept { return !(*this == other); }

                    entry* m_current;
                };

                const_iterator begin() const noexcept
                {
                    tagged_ptr local_head = m_Head.load(std::memory_order_acquire);
                    return const_iterator{ local_head.data()->m_pNext.load(std::memory_order_acquire).data() };
                }

                const_iterator end() const noexcept
                {
                    return const_iterator{};
                }

                const_iterator cbegin() const noexcept { return begin(); }
                const_iterator cend() const noexcept { return end(); }


                T_MEMORY_POOL&          m_Pool;
                std::atomic<tagged_ptr> m_Head;
                std::atomic<tagged_ptr> m_Tail;
                std::atomic_int         m_Count;
            };
        }

        struct failed_container
        {
            mutable std::mutex                                          m_Mutex;
            std::unordered_map<xresource::full_guid, historical_entry>  m_Map;
        };

        struct compiling_list
        {
            mutable std::mutex                                          m_Mutex;
            std::vector<historical_entry>                               m_List;
        };

        struct historical
        {
            mutable std::mutex                                          m_Mutex;
            std::vector<historical_entry>                               m_List;
        };

        struct settings
        {
            enum class optimization : std::uint8_t
            { O0
            , O1
            , OZ
            };

            enum class debug : std::uint8_t
            { D0
            , D1
            , Dz
            };

            constexpr const wchar_t* getOptimizationLevelString() const noexcept { return m_OptimizationLevel == optimization::O0 ? L"O0" : m_OptimizationLevel == optimization::O1 ? L"O1" : L"Oz";}
            constexpr const wchar_t* getDebugLevelString()        const noexcept { return m_DebugLevel        == debug::D0        ? L"D0" : m_DebugLevel        == debug::D1        ? L"D1" : L"Dz"; }

            std::mutex                                      m_Mutex;
            optimization                                    m_OptimizationLevel     = { optimization::O1 };
            debug                                           m_DebugLevel            = { debug::D0 };
            bool                                            m_bUseDebugCompiler     = { false };
            bool                                            m_bOutputToConsole      = { false };
            int                                             m_MaxHistoricalEntries  = { 200 };
        };

        struct instance
        {
            struct compilation_job : xscheduler::job<0>
            {
                compilation_job(instance& Instance)
                    : xscheduler::job<0>{ xscheduler::universal_string{"COMPILATION_JOB"}, xscheduler::job_definition{ .m_WhenDone = xscheduler::when_done::DO_NOTHING}}
                    , m_Instance{ Instance }
                    {}

                bool ContinueCompiling() const
                {
                    return (m_Instance.m_ManualCompilationTemp.load(std::memory_order_relaxed) ^ m_Instance.m_AutoCompilation.load(std::memory_order_relaxed))
                        && (m_Instance.m_bRunning.load(std::memory_order_relaxed))
                        && !m_Instance.m_PauseCompilation.load(std::memory_order_relaxed);
                }

                void OnRun() noexcept override
                {
                    xscheduler::task_group Compilation({ "COMPILATION" });

                    // I may need to wait a little bit so that lower priority jobs and fill themselves....
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));

                    // Water level help us know until what level we can do jobs
                    std::size_t WaterLevel = 0;
                    std::as_const(m_Instance.m_Queue).lock();
                    const int nWorkers = xscheduler::g_System.getWorkerCount();
                    entry Entry;
                    do
                    {
                        for (auto& Q : m_Instance.m_Queue.get())
                        {
                            const auto iCurIndex = static_cast<std::size_t>( &Q - m_Instance.m_Queue.get().data() );

                            if (not ContinueCompiling())
                            {
                                std::as_const(m_Instance.m_Queue).unlock();
                                return;
                            }

                            // Let's not saturate the workers that way the editor can do other things...
                            if (Compilation.getJobsInQueue() > nWorkers)
                            {
                                std::as_const(m_Instance.m_Queue).unlock();
                                xscheduler::g_System.WorkerStartWorking([&]()
                                    {
                                        return Compilation.getJobsInQueue();
                                    });
                                std::as_const(m_Instance.m_Queue).lock();
                            }

                            // Try to pull a new job
                            if (Q.pop(Entry))
                            {
                                ++m_Instance.m_WorkersWorking;
                                Compilation.Submit([JobEntry = std::move(Entry), this]() mutable
                                    {
                                        CompilingThreadWorker(m_Instance.m_LibraryMgr, std::move(JobEntry));
                                        --m_Instance.m_WorkersWorking;
                                    });

                                break;
                            }

                            // Let us wait until all workers are done for this level
                            if (m_Instance.m_WorkersWorking)
                            {
                                std::as_const(m_Instance.m_Queue).unlock();
                                do
                                {
                                    std::this_thread::yield();
                                } while (m_Instance.m_WorkersWorking);
                                std::as_const(m_Instance.m_Queue).lock();
                            }

                            // We have up our level
                            WaterLevel = std::max(WaterLevel, iCurIndex);
                        }

                    } while (WaterLevel < (m_Instance.m_Queue.get().size()-1));

                    std::as_const(m_Instance.m_Queue).unlock();
                }

                void OnReset(void) noexcept override
                {
                    //Let the job reset itself first
                    xscheduler::job<0>::OnReset();

                    // Now we can let the system know we are done
                    m_Instance.m_isCompiling = false;
                }

                instance& m_Instance;
            };


            using entry_pool = xcontainer::pool::mpmc_bounded_dynamic_jitc<lockless::entry<entry>>;
            using queue      = lockless::queue<lockless::entry<entry>, entry_pool>;
            using lock_queue = xcontainer::lock::object<std::vector<queue>, xcontainer::lock::semaphore_reentrant >;

            library_mgr&                                    m_LibraryMgr;
            settings                                        m_Settings              = {};
            std::atomic_bool                                m_bRunning              = { true  };
            std::atomic_bool                                m_AutoCompilation       = { true  };
            std::atomic_bool                                m_ManualCompilationTemp = { false };
            std::atomic_bool                                m_PauseCompilation      = { false };
            std::atomic_bool                                m_isCompiling           = { false };
            std::atomic_int                                 m_WorkersWorking        = { 0 };
            entry_pool                                      m_EntryPool             = {};
            lock_queue                                      m_Queue                 = {};
            failed_container                                m_Failed                = {};
            compiling_list                                  m_Compiling             = {};
            historical                                      m_Historical            = {};
            compilation_job                                 m_CompilationJob        = {*this};


            instance(library_mgr& LibMgr ) : m_LibraryMgr{ LibMgr }
            {
                m_EntryPool.Init( 10000 );
            }

            void AllocateQueues( int Count )
            {
                xcontainer::lock::scope Lk(m_Queue);

                auto& QueueList = m_Queue.get();
                QueueList.reserve(Count);

                for (int i = 0; i < Count; ++i)
                {
                    QueueList.emplace_back(m_EntryPool);
                }
            }

            void PauseCompilation( bool bPause )
            {
                m_PauseCompilation.store(bPause);
            }

            bool ContinueCompiling() const
            {
                return (m_ManualCompilationTemp.load(std::memory_order_relaxed) ^ m_AutoCompilation.load(std::memory_order_relaxed))
                    && (m_bRunning.load(std::memory_order_relaxed))
                    && !m_PauseCompilation.load(std::memory_order_relaxed);
            }

            void StartCompilation()
            {
                if (not m_isCompiling)
                {
                    if ( not ContinueCompiling())
                        return;

                    m_isCompiling = true;

                    // Ok let us wake up the master (may or may not be sleeping)
                    if (m_AutoCompilation.load(std::memory_order_relaxed) ) m_ManualCompilationTemp.store(false); 
                    else m_ManualCompilationTemp.store(true);

                    xscheduler::g_System.SubmitJob(m_CompilationJob);
                }
            }
        };

        //==========================================================================================================

        inline void ConsoleCompliantString
            ( historical_entry::communication&  Communication
            , std::size_t&                      OldLength
            , std::size_t&                      LastLinePos
            , const std::string_view            NewChunk
            ) noexcept
        {
            std::string&    CurrentBuffer   = Communication.m_Log;
            auto            NewLength       = CurrentBuffer.length() + NewChunk.size();

            CurrentBuffer.append(NewChunk.data(), NewChunk.size());

            //
            // Check for "[warning]" in the tail of the buffer (last N characters)
            //
            constexpr std::string_view  keyword         = "[warning]";
            constexpr size_t            keyword_len     = keyword.length();

            if (NewLength >= keyword_len) 
            {
                auto search_start = CurrentBuffer.end() - std::min(NewLength, keyword_len * 2); // search a bit more than keyword length
                auto it = std::search
                    ( search_start
                    , CurrentBuffer.end()
                    , keyword.begin()
                    , keyword.end()
                    , [](char ch1, char ch2)
                    {
                       return std::tolower(ch1) == std::tolower(ch2);
                    });

                if (it != CurrentBuffer.end()) 
                {
                    Communication.m_Result = historical_entry::result::COMPILING_WARNINGS; 
                }
            }

            //
            // Handle carriage returns like a console
            //
            for (auto i = OldLength; i < NewLength; ++i)
            {
                if (CurrentBuffer[i] == '\r')
                {
                    if ((i + 1) != NewLength)
                    {
                        if (CurrentBuffer[i + 1] == '\n') continue;
                    }
                    else
                    {
                        NewLength -= 1;
                        break;
                    }

                    auto ToErase = i - LastLinePos + 1;
                    CurrentBuffer.erase(LastLinePos, ToErase);
                    NewLength -= ToErase;
                    i = LastLinePos;

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


        //------------------------------------------------------------------------------------------------

        static void RunCommandLine
            ( const std::wstring&     WStrCommanLine
            , historical_entry::log&  CompilerFeedback
            , const bool              bOutputInConsole
            )
        {
            SECURITY_ATTRIBUTES saAttr;
            saAttr.nLength              = sizeof(SECURITY_ATTRIBUTES);
            saAttr.bInheritHandle       = TRUE;
            saAttr.lpSecurityDescriptor = NULL;

            HANDLE hChildStd_OUT_Rd = NULL;
            HANDLE hChildStd_OUT_Wr = NULL;
            if (!CreatePipe(&hChildStd_OUT_Rd, &hChildStd_OUT_Wr, &saAttr, 0))
                throw std::runtime_error("Stdout pipe creation failed");

            if (!SetHandleInformation(hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0))
                throw std::runtime_error("Stdout SetHandleInformation failed");

            STARTUPINFO siStartInfo;
            ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
            siStartInfo.cb          = sizeof(STARTUPINFO);
            siStartInfo.hStdError   = hChildStd_OUT_Wr;
            siStartInfo.hStdOutput  = hChildStd_OUT_Wr;
            siStartInfo.dwFlags    |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
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
                xcontainer::lock::scope lock(CompilerFeedback);
                CompilerFeedback.get().m_Log.clear();
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
                        xcontainer::lock::scope lock(CompilerFeedback);
                        ConsoleCompliantString(CompilerFeedback.get(), OldLength, LastLine, { chBuf.data(), dwRead });
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
                xcontainer::lock::scope lock(CompilerFeedback);

                if (xstrtool::findI(CompilerFeedback.get().m_Log, "[COMPILATION_SUCCESS]") != std::string::npos)
                {
                    if (CompilerFeedback.get().m_Result == historical_entry::result::COMPILING_WARNINGS )
                    {
                        CompilerFeedback.get().m_Result = historical_entry::result::SUCCESS_WARNINGS;
                    }
                    else
                    {
                        // Nothing to report so let us just tell the user everything is cool...
                        CompilerFeedback.get().m_Result = historical_entry::result::SUCCESS;
                    }
                }
                else
                {
                    CompilerFeedback.get().m_Result = historical_entry::result::FAILURE;
                }
            }
        }
    }

    struct file_monitor_changes;
    struct library_db
    {
        struct resource_dep
        {
            bool                                m_bRecompileOnChange;                           // Whenever this resource changes I need to get rebuild...
            xresource::full_guid                m_gResource;
        };

        struct info_node
        {
            enum class state : std::uint8_t
            { IDLE
            , BEEN_EDITED
            , WAITING_TO_COMPILE
            , BEEN_EDITED_WAITING_TO_COMPILE
            , COMPILING
            , BEEN_EDITED_COMPILING
            , ERRORS
            , BEEN_EDITED_ERRORS
            };

            std::wstring                        m_Path              = {};           // Full path to the info.txt
            xresource_pipeline::info            m_Info              = {};           // Officially the info
            std::vector<xresource::full_guid>   m_lChildLinks       = {};           // Quick way to determine how many other resources have us as a dependency
            xresource_pipeline::dependencies    m_Dependencies      = {};           // List of the dependencies for the resource
            int                                 m_InfoChangeCount   = 0;            // This helped us know when an info has changed...
            std::filesystem::file_time_type     m_InfoReadTime      = {};           // When was the info read last 
            std::filesystem::file_time_type     m_InfoTime          = {};           // When was last time the info got modified
            std::filesystem::file_time_type     m_DescriptorTime    = {};           // When was last time the descriptor got modified
            std::filesystem::file_time_type     m_ResourceTime      = {};           // When was last time the descriptor got modified
            std::filesystem::file_time_type     m_NewestDependencyTime = {};        // From all its dependencies which is the newest of them all...
            bool                                m_bHasDescriptor    = {};           // tells the system if it has a descriptor.txt
            bool                                m_bHasResource      = {};           // tells if the resource has been compiled or not
            bool                                m_bHasDependencies  = {};           // Tells if it has a dependency file
            state                               m_State             = state::IDLE;  // The state of this resource

            void clear()
            {
                m_lChildLinks.clear();
                m_Dependencies.clear();
                m_State             = state::IDLE;
                m_InfoChangeCount   = 0;
                m_bHasDescriptor    = false;
                m_bHasResource      = false;
                m_bHasDependencies  = false;
                m_InfoReadTime      = {};
                m_InfoTime          = {};
                m_DescriptorTime    = {};
                m_ResourceTime      = {};
                m_NewestDependencyTime = {};
            }
        };

        struct asset
        {
            std::wstring                            m_Path;                         // Path to the asset relative to the library 
            std::filesystem::file_time_type         m_LastWriteTime;                // When was the info read last
            std::vector<xresource::full_guid>       m_lChildLinks;                  // Links of resources using this asset... (typically just one)
        };

        struct info_db
        {
            using map = xcontainer::unordered_lockless_map<xresource::instance_guid, library_db::info_node>;
            xresource::type_guid    m_TypeGUID              = {};
            map                     m_InfoDataBase          = {};
        };

        void clear()
        {
            m_InfoByTypeDataBase.clear();
        }

        library_db(compilation::instance& Compilation )
            : m_CompilationInstance{ Compilation }
            {}

        inline int getQueueIndexFromType(xresource::type_guid Type) const;
        
        bool AddToCompilationQueueIfNeeded( const info_db& InfoTypeDB, info_node& InfoNode) const
        {
            //NOTE: If a node is "deleted" should we let it compile???

            if (InfoNode.m_State != library_db::info_node::state::COMPILING && InfoNode.m_State != library_db::info_node::state::BEEN_EDITED_COMPILING )
            {
                if (InfoNode.m_NewestDependencyTime > InfoNode.m_ResourceTime || InfoNode.m_DescriptorTime > InfoNode.m_ResourceTime)
                {
                    if (InfoNode.m_State == library_db::info_node::state::BEEN_EDITED) InfoNode.m_State = library_db::info_node::state::BEEN_EDITED_WAITING_TO_COMPILE;
                    else if (InfoNode.m_State == library_db::info_node::state::IDLE) InfoNode.m_State = library_db::info_node::state::WAITING_TO_COMPILE;

                    compilation::entry QueueEntry;
                    QueueEntry.m_FullGuid = InfoNode.m_Info.m_Guid;
                    QueueEntry.m_gLibrary = m_Library.m_GUID;
                    QueueEntry.m_Priority = getQueueIndexFromType(InfoNode.m_Info.m_Guid.m_Type);

                    // Insert the entry in the queue
                    {
                        xcontainer::lock::scope Lk(std::as_const(m_CompilationInstance.m_Queue));
                        m_CompilationInstance.m_Queue.get()[QueueEntry.m_Priority].push(std::move(QueueEntry));
                    }
                    m_CompilationInstance.StartCompilation();

                    return true;
                }
            }

            return false;
        }

        using map_type_to_infodb        = xcontainer::unordered_lockless_map<xresource::type_guid, std::unique_ptr<library_db::info_db>>;
        using map_assetpath_to_asset    = xcontainer::unordered_lockless_map<std::wstring, asset>;

        compilation::instance&                  m_CompilationInstance;
        std::unique_ptr<process_info_job>       m_pProcessInfoJob                   = {};
        library                                 m_Library                           = {};
        map_type_to_infodb                      m_InfoByTypeDataBase                = {};
        map_assetpath_to_asset                  m_AssetDataBase                     = {};
        std::unique_ptr<file_monitor_changes>   m_FileMonitorChanges                = {};
    };

    //================================================================================================

    struct file_monitor_changes
    {
        asset_plugins_db&                                       m_AssetPluginsDB;
        library_db&                                             m_LibraryDB;
        std::atomic_bool                                        m_bRunning                   = true;
        std::vector<std::unique_ptr<std::thread>>               m_lMonitorAssetsThreads      = {};
        std::vector<std::unique_ptr<std::thread>>               m_lMonitorDescriptorsThreads = {};
        
        file_monitor_changes(asset_plugins_db& PlugInDB, library_db& LibDB )
            : m_AssetPluginsDB{ PlugInDB }
            , m_LibraryDB{ LibDB }
        {
            // NOTE: all paths for AssetFileChanges should be lower case because we do string compares, and it assumes all lower case
            // These are all the paths that an asset it allowed to be, from an asset perspective all other paths will be ignored.
            m_lMonitorAssetsThreads.emplace_back(std::make_unique<std::thread>(MonitorAssetFileChangesPerPath, std::ref(*this), std::wstring(L"cache/resources/logs")));
            m_lMonitorAssetsThreads.emplace_back(std::make_unique<std::thread>(MonitorAssetFileChangesPerPath, std::ref(*this), std::wstring(L"descriptors")));
            m_lMonitorAssetsThreads.emplace_back(std::make_unique<std::thread>(MonitorAssetFileChangesPerPath, std::ref(*this), std::wstring(L"cache/descriptors")));

            // This is the only asset path that users control all other ones are control by the system
            m_lMonitorAssetsThreads.emplace_back(std::make_unique<std::thread>(MonitorAssetFileChangesPerPath, std::ref(*this), std::wstring(L"assets")));

            // NOTE: Here casing does not matter...
            // These are all the paths where a descriptor can exist, all other paths will be ignored.
            m_lMonitorDescriptorsThreads.emplace_back(std::make_unique<std::thread>(MonitorResourceFileChangesPerPath, std::ref(*this), std::wstring(L"descriptors")));
            m_lMonitorDescriptorsThreads.emplace_back(std::make_unique<std::thread>(MonitorResourceFileChangesPerPath, std::ref(*this), std::wstring(L"cache/descriptors")));
        }

        ~file_monitor_changes()
        {
            if (m_bRunning)
            {
                m_bRunning.store(false);

                for( auto& E : m_lMonitorAssetsThreads)
                {
                    if( E )
                    {
                        if (BOOL cancel_result = CancelSynchronousIo(E->native_handle()); !cancel_result)
                        {
                            std::cerr << "Error canceling Asset watcher I/O: " << GetLastError() << '\n';
                        }
                        E->join();
                    }
                }

                for (auto& E : m_lMonitorDescriptorsThreads)
                {
                    if (BOOL cancel_result = CancelSynchronousIo(E->native_handle()); !cancel_result)
                    {
                        std::cerr << "Error canceling Resource Watcher I/O: " << GetLastError() << '\n';
                    }
                    E->join();
                }
            }
        }

        static void MonitorAssetFileChangesPerPath( file_monitor_changes& FileMonitorChanges, std::wstring_view AssetsPath )
        {
            const std::wstring Path = xstrtool::PathNormalizeCopy(std::format( L"{}/{}", FileMonitorChanges.m_LibraryDB.m_Library.m_Path, AssetsPath));

            const HANDLE dir_handle = CreateFileW
                ( Path.c_str()
                , FILE_LIST_DIRECTORY
                , FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE
                , nullptr
                , OPEN_EXISTING
                , FILE_FLAG_BACKUP_SEMANTICS
                , nullptr
                );

            if (dir_handle == INVALID_HANDLE_VALUE) 
            {
                std::cerr << "Error opening directory: " << GetLastError() << '\n';
                return;
            }

            // Larger buffer for recursive monitoring
            auto buffer = std::make_unique<std::array<int64_t, 4096>>();
            DWORD bytes_returned;

            while( FileMonitorChanges.m_bRunning.load() )
            {
                const BOOL result = ReadDirectoryChangesW
                    ( dir_handle
                    , buffer.get()
                    , static_cast<DWORD>(buffer->size() * sizeof(*(buffer->data())))
                    , TRUE                          // Watch subdirectories
                    , FILE_NOTIFY_CHANGE_LAST_WRITE
                    , &bytes_returned
                    , nullptr
                    , nullptr
                    );
                
                if (!FileMonitorChanges.m_bRunning.load()) break;

                // Check for errors
                if (!result) 
                {
                    DWORD error = GetLastError();
                    if (error == ERROR_OPERATION_ABORTED) 
                    {
                        std::wcout << L"Operation canceled.\n";
                        break;
                    }
                    std::cerr << "Error: " << error << '\n';
                    break;
                }

                FILE_NOTIFY_INFORMATION* notify_info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buffer->data());
                while( notify_info && FileMonitorChanges.m_bRunning.load() )
                {
                    // Extract filename from notification
                    std::wstring file_path = std::format( L"{}/{}", AssetsPath, std::wstring_view( notify_info->FileName, notify_info->FileNameLength / sizeof(wchar_t)) );

                    // Make it lower case
                    std::ranges::transform(file_path, file_path.begin(), std::towlower);

                    // replace all the forward-slashes with the right ones...
                    std::ranges::replace(file_path, L'/', L'\\');

                    // Check to see if file is in our asset list
                    if ( false == FileMonitorChanges.m_LibraryDB.m_AssetDataBase.FindAsWrite( file_path, [&](library_db::asset& Asset)
                    {
                        // Update the time...
                        std::wstring FullPath = std::format( L"{}/{}", FileMonitorChanges.m_LibraryDB.m_Library.m_Path, Asset.m_Path );

                        // Make sure the file still exists
                        if ( std::error_code Ec; std::filesystem::exists(FullPath, Ec) == false || Ec )
                        {
                            std::wcout << L"Unable to find file: " << FullPath << L"\n";
                        }
                        else
                        {
                            //
                            // Wait until we have access to read
                            //
                            do
                            {
                                HANDLE file_handle = CreateFileW(
                                    FullPath.c_str(),
                                    GENERIC_READ,
                                    0, // No sharing
                                    nullptr,
                                    OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL,
                                    nullptr
                                );

                                if (file_handle != INVALID_HANDLE_VALUE)
                                {
                                    CloseHandle(file_handle);
                                    break;
                                }
                                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            } while ( true );

                            std::wcout << L"File was Updated: " << FullPath << L"\n";
                            Asset.m_LastWriteTime = std::filesystem::last_write_time(FullPath);

                            for ( auto& E : Asset.m_lChildLinks )
                            {
                                if (false == FileMonitorChanges.m_LibraryDB.m_InfoByTypeDataBase.FindAsReadOnly(E.m_Type, [&](const std::unique_ptr<library_db::info_db>& TypeDB)
                                    {
                                        TypeDB->m_InfoDataBase.FindAsWrite(E.m_Instance, [&](library_db::info_node& InfoNode)
                                            {
                                                InfoNode.m_NewestDependencyTime = std::max(InfoNode.m_NewestDependencyTime, Asset.m_LastWriteTime);
                                                FileMonitorChanges.m_LibraryDB.AddToCompilationQueueIfNeeded(*TypeDB, InfoNode);
                                            });
                                    }))
                                {
                                    std::wcout << L"Unable to find one of the resource references: " << FullPath << L"\n";
                                }
                            }
                        }
                    }))
                    {
                        std::wcout << L"File was modified but is not part of our asset list: " << file_path << L" In library " << FileMonitorChanges.m_LibraryDB.m_Library.m_Path << L"\n";
                    }

                    //
                    // Get the next entry
                    //
                    if ( notify_info->NextEntryOffset == 0 ) 
                    {
                        notify_info = nullptr;
                    }
                    else
                    {
                        // Get the next notification (if any)
                        notify_info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>
                        (
                            reinterpret_cast<char*>(notify_info) + notify_info->NextEntryOffset
                        );
                    }
                }
            }

            CloseHandle(dir_handle);
        }

        static void MonitorResourceFileChangesPerPath(file_monitor_changes& FileMonitorChanges, std::wstring_view GivenPath )
        {
            const std::wstring Path = std::format(L"{}/{}", FileMonitorChanges.m_LibraryDB.m_Library.m_Path, GivenPath);

            const HANDLE dir_handle = CreateFileW
            ( Path.c_str()
            , FILE_LIST_DIRECTORY
            , FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE
            , nullptr
            , OPEN_EXISTING
            , FILE_FLAG_BACKUP_SEMANTICS
            , nullptr
            );

            if (dir_handle == INVALID_HANDLE_VALUE)
            {
                std::cerr << "Error opening directory: " << GetLastError() << '\n';
                return;
            }

            // Larger buffer for recursive monitoring
            auto buffer = std::make_unique<std::array<int64_t, 4096>>();
            DWORD bytes_returned;

            while (FileMonitorChanges.m_bRunning.load())
            {
                const BOOL result = ReadDirectoryChangesW
                (dir_handle
                    , buffer.get()
                    , static_cast<DWORD>(buffer->size() * sizeof(*(buffer->data())))
                    , TRUE                          // Watch subdirectories
                    , FILE_NOTIFY_CHANGE_LAST_WRITE
                    , &bytes_returned
                    , nullptr
                    , nullptr
                );

                if (!FileMonitorChanges.m_bRunning.load()) break;

                // Check for errors
                if (!result)
                {
                    DWORD error = GetLastError();
                    if (error == ERROR_OPERATION_ABORTED)
                    {
                        std::wcout << L"Operation canceled.\n";
                        break;
                    }
                    std::cerr << "Error: " << error << '\n';
                    break;
                }

                FILE_NOTIFY_INFORMATION* notify_info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buffer->data());
                while (notify_info && FileMonitorChanges.m_bRunning.load())
                {
                    constexpr static std::wstring_view  DescriptorName  = L"descriptor.txt";
                    auto                                FilePath        = std::wstring_view(notify_info->FileName, notify_info->FileNameLength / sizeof(wchar_t));
                    bool                                bSkipFile       = false;
                    xresource::full_guid                ResourceGuid    = {};

                    if (DescriptorName.size() > FilePath.size())
                    {
                        bSkipFile = true;
                    }
                    else
                    {
                        for (int i = 1; i < static_cast<int>(DescriptorName.size()); ++i)
                        {
                            if (towlower(FilePath[FilePath.size() - i]) != DescriptorName[DescriptorName.size() - i])
                            {
                                bSkipFile = true;
                            }
                        }

                        // Get the ResourceGuid...
                        if (bSkipFile==false)
                        {
                            auto pos = FilePath.find(L'\\');
                            if ( pos != std::wstring_view::npos )  
                            {
                                auto TypeName = xstrtool::To(std::wstring(std::wstring_view( FilePath.data(), pos )));
                                if ( auto E = FileMonitorChanges.m_AssetPluginsDB.m_mPluginsByTypeName.find(TypeName); E != FileMonitorChanges.m_AssetPluginsDB.m_mPluginsByTypeName.end() )
                                {
                                    auto& Plugin = FileMonitorChanges.m_AssetPluginsDB.m_lPlugins[E->second];
                                    ResourceGuid.m_Type = Plugin.m_TypeGUID;

                                    pos = FilePath.find(L'\\', pos + 1);
                                    assert(pos != std::wstring_view::npos);

                                    pos = FilePath.find(L'\\', pos + 1);
                                    assert(pos != std::wstring_view::npos);

                                    auto posEnd = FilePath.find(L'.', pos + 1);
                                    assert(posEnd != std::wstring_view::npos);

                                    auto        InstanceName = std::wstring_view(FilePath.data()+pos+1, posEnd - pos - 1);
                                    wchar_t*    end;
                                    ResourceGuid.m_Instance.m_Value = std::wcstoull(InstanceName.data(), &end, 16);

                                    //
                                    // Wait until we have access to read
                                    //
                                    const std::wstring FullPath = std::format(L"{}/{}/{}", FileMonitorChanges.m_LibraryDB.m_Library.m_Path, GivenPath, FilePath);
                                    do
                                    {
                                        HANDLE file_handle = CreateFileW(
                                            FullPath.data(),
                                            GENERIC_READ,
                                            0, // No sharing
                                            nullptr,
                                            OPEN_EXISTING,
                                            FILE_ATTRIBUTE_NORMAL,
                                            nullptr
                                        );

                                        if (file_handle != INVALID_HANDLE_VALUE) 
                                        {
                                            CloseHandle(file_handle);
                                            break;
                                        }
                                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                                    } while (true);

                                    // Check if we need to compile the file
                                    if (false == FileMonitorChanges.m_LibraryDB.m_InfoByTypeDataBase.FindAsReadOnly(ResourceGuid.m_Type, [&](const std::unique_ptr<library_db::info_db>& InfoDB)
                                    {
                                        if (false == InfoDB->m_InfoDataBase.FindAsWrite(ResourceGuid.m_Instance, [&](library_db::info_node& InfoNode)
                                            {
                                                InfoNode.m_bHasDescriptor = true;
                                                InfoNode.m_DescriptorTime = std::filesystem::last_write_time(FullPath);
                                                FileMonitorChanges.m_LibraryDB.AddToCompilationQueueIfNeeded(*InfoDB, InfoNode);
                                            }))
                                        {
                                            // Could not find the instance....
                                            std::wcout << L"Unable to find one of the resource instance for : " << FilePath << L"\n";
                                        }
                                    }))
                                    {
                                        // Could not find the type...
                                        std::wcout << L"Unable to find one of the resource type for : " << FilePath << L"\n";
                                    }
                                }
                            }
                        }
                    }

                    //
                    // Get the next entry
                    //
                    if (notify_info->NextEntryOffset == 0)
                    {
                        notify_info = nullptr;
                    }
                    else
                    {
                        // Get the next notification (if any)
                        notify_info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>
                        (
                            reinterpret_cast<char*>(notify_info) + notify_info->NextEntryOffset
                        );
                    }
                }
            }

            CloseHandle(dir_handle);
        }
    };

    //================================================================================================

    struct process_info_job final : xscheduler::job<0>
    {
        enum class state
        { IDLE
        , READY_TO_RUN
        , RUNNING
        , DONE
        };

        using err_list = xcontainer::lock::object<std::vector<std::wstring>, xcontainer::lock::spin>;

        library_mgr*                 m_pLibraryMgr   = nullptr;
        library_db*                  m_pLibraryDB    = nullptr;
        std::atomic<state>           m_State         = { state::IDLE };
        std::atomic<bool>            m_bCancel       = { false };
        err_list                     m_lErrors       = {};
        xproperty::settings::context m_Context       = {};

        //------------------------------------------------------------------------------------------

        void setup(library_mgr& LibMgr, library_db& LibraryDB, xproperty::settings::context&& Context )
        {
            m_pLibraryMgr = &LibMgr;
            m_pLibraryDB  = &LibraryDB;
            m_Context     = std::move(Context);
            m_State       = state::READY_TO_RUN;
        }

        //------------------------------------------------------------------------------------------

        process_info_job() : xscheduler::job<0>{ xscheduler::str_v<"Process_Infos">, xscheduler::job_definition::make<xscheduler::when_done::DO_NOTHING>() }
        {
        }

        //------------------------------------------------------------------------------------------

        xerr LoadInfo( const std::wstring_view LibraryPath, const std::wstring_view Path, const std::wstring_view ResourcePath, const std::wstring_view RelativePath )
        {
            // Load the file
            xresource_pipeline::info        Info                = {};
            std::filesystem::file_time_type InfoTime            = {};
            std::filesystem::file_time_type DescriptorTime      = {};
            std::filesystem::file_time_type ResourceTime        = {};
            std::filesystem::file_time_type NewestDependencyTime = {};
            std::error_code                 Ec                  = {};
            const bool                      bInfoFileExists     = std::filesystem::exists(Path, Ec) ? !Ec : false;
            const std::wstring              DescriptorPath      = std::wstring{ Path.substr(0, Path.find_last_of(L'\\') + 1) } + L"Descriptor.txt";
            const bool                      bDescriptorExists   = std::filesystem::exists(DescriptorPath, Ec) ? !Ec : false;
            const bool                      bResourceExists     = std::filesystem::exists(ResourcePath, Ec) ? !Ec : false;
            const std::wstring              DependencyPath      = std::format(L"{}/Cache/Resources/Logs{}.log/dependencies.txt", LibraryPath, RelativePath);
            bool                            bDependencyExists   = bResourceExists && std::filesystem::exists(DependencyPath, Ec) ? !Ec : false;
            xresource_pipeline::dependencies Dependencies;

            if (bInfoFileExists)
            {
                if (auto Err = Info.Serialize(true, Path, m_Context); Err)
                {
                    std::lock_guard Lk(m_lErrors);
                    m_lErrors.get().push_back(std::format(L"Fail to load info file[{}] with error[{}]", Path, xstrtool::To(Err.getMessage())));
                    printf("[INFO] %ls\n", m_lErrors.get().back().c_str());
                    return Err;
                }

                InfoTime = std::filesystem::last_write_time(Path);
            }
            else
            {
                std::lock_guard Lk(m_lErrors);
                m_lErrors.get().push_back(std::format(L"Fail to find the info file[{}] with error[{}]", Path, xstrtool::To(Ec.message()) ));
                return xerr::create_f<xerr::default_states,"Fail to find Info file">();
            }

            if (bDescriptorExists) DescriptorTime = std::filesystem::last_write_time(DescriptorPath);
            if (bResourceExists)   ResourceTime   = std::filesystem::last_write_time(ResourcePath);

            if (bDependencyExists)
            {
                xproperty::settings::context Context;
                if ( auto Err = Dependencies.Serialize(true, DependencyPath, Context); Err )
                {
                    std::lock_guard Lk(m_lErrors);
                    m_lErrors.get().push_back(std::format(L"Fail to read the dependency file [{}] with error[{}]", DependencyPath, xstrtool::To(Err.getMessage())));
                    bDependencyExists = false;
                }
                else
                {
                    for ( auto& E : Dependencies.m_Assets )
                    {
                        auto& AssetPath = E;
                        m_pLibraryDB->m_AssetDataBase.FindAsWriteOrCreate(AssetPath, [&](library_db::asset& Asset)
                        {
                            const auto FullPath = std::format(L"{}/{}", LibraryPath, AssetPath);
                            Asset.m_Path            = AssetPath;
                            Asset.m_LastWriteTime   = std::filesystem::last_write_time(FullPath);
                            NewestDependencyTime    = std::max(NewestDependencyTime, Asset.m_LastWriteTime);
                        }
                        , [&](library_db::asset& Asset)
                        {
                            bool bFound = false;
                            for (auto& X : Asset.m_lChildLinks)
                                if (X == Info.m_Guid)
                                {
                                    bFound = true;
                                    break;
                                }

                            if (bFound == false) Asset.m_lChildLinks.push_back(Info.m_Guid);
                        });
                    }
                }
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
                    InfoNode.m_Path                 = std::move(Path);
                    InfoNode.m_Info                 = std::move(Info);
                    InfoNode.m_InfoReadTime         = InfoTime;
                    InfoNode.m_InfoTime             = InfoTime;
                    InfoNode.m_DescriptorTime       = DescriptorTime;
                    InfoNode.m_ResourceTime         = ResourceTime;
                    InfoNode.m_bHasDescriptor       = bDescriptorExists;
                    InfoNode.m_bHasResource         = bResourceExists;
                    InfoNode.m_bHasDependencies     = bDependencyExists;
                    InfoNode.m_NewestDependencyTime = NewestDependencyTime;
                    InfoNode.m_Dependencies         = std::move(Dependencies);
                    InfoNode.m_State                = library_db::info_node::state::IDLE;

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

                    //
                    // Check if we should insert this entry in the compilation queue
                    //
                    m_pLibraryDB->AddToCompilationQueueIfNeeded(*Entry, InfoNode);
                });
            });

            /// Need to add the infos into the global array
            /// m_pLibraryMgr->m_RscToLibraryMap
            return {};
        }

        //------------------------------------------------------------------------------------------

        void ProcessInfos(const std::wstring_view& LibraryPath, const std::wstring_view& Path, const std::wstring_view& ResourcePath )
        {
            xscheduler::task_group TG({"Collect Infos from Path"});

            //
            // Read all the infos
            //
            const std::uint16_t ProjectPathLength = static_cast<std::uint16_t>(Path.length() );
            for (const auto& entry : std::filesystem::recursive_directory_iterator(Path))
            {
                if (m_bCancel) return;

                if (std::filesystem::is_directory(entry.path()))
                {
                    if (entry.path().extension() == L".desc")
                    {
                        struct data
                        {
                            std::wstring        m_DescriptorPath;
                            std::wstring_view   m_ResourcePath;
                            std::uint16_t       m_ProjectPathLength;
                            std::wstring_view   m_LibraryPath;
                        };

                        auto Data = std::make_unique<data>();
                        Data->m_DescriptorPath      = entry.path();
                        Data->m_ResourcePath        = ResourcePath;
                        Data->m_ProjectPathLength   = ProjectPathLength;
                        Data->m_LibraryPath         = LibraryPath;

                        TG.Submit( [this, pData = std::move(Data) ]
                        {
                            if (m_bCancel) return;

                            // Get the relative path of the resource from the descriptor
                            std::wstring_view RelativeResourcePath = pData->m_DescriptorPath;
                            RelativeResourcePath = RelativeResourcePath.substr(pData->m_ProjectPathLength, pData->m_DescriptorPath.length() - pData->m_ProjectPathLength - 5);

                            // Now we can compose the final path to the actual resource file
                            auto FileResourcePath = std::format(L"{}{}", pData->m_ResourcePath, RelativeResourcePath);

                            if (auto Err = LoadInfo(pData->m_LibraryPath, std::format(L"{}\\info.txt", pData->m_DescriptorPath), FileResourcePath, RelativeResourcePath); Err )
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

        void OnDone() noexcept override
        {
            m_State = state::DONE;
        }

        //------------------------------------------------------------------------------------------

        void OnRun() noexcept override
        {
            m_State = state::RUNNING;

            if ( m_bCancel == false ) ProcessInfos(m_pLibraryDB->m_Library.m_Path, m_pLibraryDB->m_Library.m_UserDescriptorPath, m_pLibraryDB->m_Library.m_ResourcePath);
            if ( m_bCancel == false ) ProcessInfos(m_pLibraryDB->m_Library.m_Path, m_pLibraryDB->m_Library.m_SysDescriptorPath, m_pLibraryDB->m_Library.m_ResourcePath);
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
            xscheduler::task_group&         m_Channel;
            library_db&                     m_Project;
            xproperty::settings::context&   m_Context;
            const bool                      m_isUser;
        };

        //------------------------------------------------------------------------------------------------

        xerr CreatePath( const std::wstring_view Path) const noexcept
        {
            std::error_code         ec;
            std::filesystem::path   path{ Path };

            std::filesystem::create_directories(path, ec);
            if (ec)
            {
                printf("Fail to create a directory [%s] with error [%s]", xstrtool::To(Path).c_str(), ec.message().c_str());
                return xerr::create_f<xerr::default_states, "Fail to create a directory">();
            }

            return {};
        }

        //------------------------------------------------------------------------------------------------

        e10::folder::guid getValidParentFolder(const library::guid glibraryGUID, const xresource::full_guid& gDescriptor) noexcept
        {
            e10::folder::guid   ParentFolder    = {};

            getInfo(glibraryGUID, gDescriptor, [&](xresource_pipeline::info& Info )
            {
                for ( auto& E : Info.m_RscLinks )
                {
                    if ( E.m_Type == e10::folder::type_guid_v && E != e10::folder::trash_guid_v )
                    {
                        // Make sure that the parent is not erased...
                        getInfo(glibraryGUID, E, [&](xresource_pipeline::info& Info)
                        {
                            if (Info.m_RscLinks.empty() || Info.m_RscLinks[0] != e10::folder::trash_guid_v)
                                ParentFolder.m_Instance = Info.m_Guid.m_Instance;
                        });
                        break;
                    }
                }
            });
            return ParentFolder;
        }

        //------------------------------------------------------------------------------------------

        template<typename T_CALLBACK = decltype([](int,int){}) >
        std::string EmptyTrashcan(const library::guid glibraryGUID, T_CALLBACK&& Callback = T_CALLBACK{} )
        {
            std::string Error;

            bool foundLibrary = m_mLibraryDB.FindAsReadOnly(glibraryGUID, [&](const std::unique_ptr<library_db>& Library)
            {
                bool bFoundFolderType = Library->m_InfoByTypeDataBase.FindAsReadOnly(e10::folder::trash_guid_v.m_Type, [&](const std::unique_ptr<library_db::info_db>& FolderInfoDB)
                {
                    bool bTrashInstanceFound = FolderInfoDB->m_InfoDataBase.FindAsWrite(e10::folder::trash_guid_v.m_Instance, [&](library_db::info_node& TrashNode)
                    {
                        const int  OriginalCount   = static_cast<int>(TrashNode.m_lChildLinks.size());
                        int        Index           = 0;
                        for ( auto& ItemToDelete : TrashNode.m_lChildLinks )
                        {
                            // Let the User know what is going on...
                            Callback( Index++, OriginalCount );

                            assert(ItemToDelete != e10::folder::trash_guid_v);

                            //
                            // Handle specific child
                            //
                            bool bFoundChildType = Library->m_InfoByTypeDataBase.FindAsReadOnly(ItemToDelete.m_Type, [&](const std::unique_ptr<library_db::info_db>& ChildTypeInfoDB)
                            {
                                // Get the child instance
                                bool bChildFound = ChildTypeInfoDB->m_InfoDataBase.FindForDelete(ItemToDelete.m_Instance, [&](library_db::info_node& ChildInfoNode)
                                {
                                    //
                                    // Remove itself from any parents
                                    //
                                    assert(ChildInfoNode.m_Info.m_RscLinks[0] == e10::folder::trash_guid_v);
                                    for ( auto& ItemParent : ChildInfoNode.m_Info.m_RscLinks )
                                    {
                                        if (ItemParent == e10::folder::trash_guid_v) continue;

                                        //
                                        // Detach from parent
                                        //
                                        bool bFoundParentType = Library->m_InfoByTypeDataBase.FindAsReadOnly(ItemParent.m_Type, [&](const std::unique_ptr<library_db::info_db>& ParentTypeInfoDB)
                                        {
                                            // Get the child instance
                                            bool bParentFound = ParentTypeInfoDB->m_InfoDataBase.FindAsWrite(ItemParent.m_Instance, [&](library_db::info_node& ParentInfoNode)
                                            {
                                                for ( auto& ChildRef : ParentInfoNode.m_lChildLinks)
                                                {
                                                    if (ChildRef == ItemToDelete)
                                                    {
                                                        ParentInfoNode.m_lChildLinks.erase(ParentInfoNode.m_lChildLinks.begin() + static_cast<int>(&ChildRef - ParentInfoNode.m_lChildLinks.data()));
                                                        // Should only be there ones...
                                                        return;
                                                    }
                                                }

                                                // We were not able to find our child in the parent references... something very bad happen...
                                                assert(false);
                                            });
                                        });
                                    }

                                    //
                                    // Remove all its dependencies
                                    //
                                    {
                                        for (auto& E : ChildInfoNode.m_Dependencies.m_Assets)
                                        {
                                            bool bEraseTheEntireThing = false;
                                            auto& WE = E;

                                            Library->m_AssetDataBase.FindAsWrite( WE, [&](library_db::asset& Asset )
                                            {
                                                for( auto& G : Asset.m_lChildLinks )
                                                {
                                                    if ( G == ChildInfoNode.m_Info.m_Guid )
                                                    {
                                                        Asset.m_lChildLinks.erase(Asset.m_lChildLinks.begin() + static_cast<int>(&G - Asset.m_lChildLinks.data()));
                                                    }
                                                }

                                                if(Asset.m_lChildLinks.empty()) bEraseTheEntireThing = true;
                                            });

                                            if (bEraseTheEntireThing)
                                            {
                                                Library->m_AssetDataBase.FindForDelete( WE, [](library_db::asset&){});
                                            }
                                        }
                                    }

                                    //
                                    // OK Now we should be able to delete the disk space from our entry
                                    //
                                    std::error_code ec;
                                    std::wstring    FolderPath = ChildInfoNode.m_Path.substr(0, ChildInfoNode.m_Path.find_last_of(L"\\"));
                                    if (std::filesystem::exists(FolderPath))
                                    {
                                        std::uintmax_t removedCount = std::filesystem::remove_all(FolderPath, ec);
                                        if (ec)
                                        {
                                            Error = std::format("Error removing {} path with error code: {}", xstrtool::To(FolderPath), ec.message());
                                            printf("Error: %s \n", Error.c_str());
                                            Error.clear();
                                        }
                                        else
                                        {
                                            // Clean up a bit... check if the paths are empty and if so remove the directories
                                            for (int i = 0; i < 2; ++i)
                                            {
                                                FolderPath = FolderPath.substr(0, FolderPath.find_last_of(L"\\"));
                                                if (auto Result = has_content(FolderPath); std::holds_alternative<bool>(Result))
                                                {
                                                    if (std::get<bool>(Result) == false)
                                                    {
                                                        std::filesystem::remove_all(FolderPath, ec);
                                                        if (ec)
                                                        {
                                                            Error = std::format("Error removing {} path with error code: {}", xstrtool::To(FolderPath), ec.message());
                                                            printf("Error: %s \n", Error.c_str());
                                                            Error.clear();
                                                            break;
                                                        }
                                                    }
                                                    else
                                                    {
                                                        break;
                                                    }
                                                }
                                                else
                                                {
                                                    printf("Error: %s \n", std::get<std::string>(Result).c_str());
                                                    break;
                                                }
                                            }
                                        }
                                    }
                                });

                                if (bChildFound == false)
                                {
                                    Error = "Unable to find the instance of the resource that we are trying to delete for the given Library";
                                }
                            });

                            if (bFoundChildType == false)
                            {
                                Error = "Unable to find one of the resource types that we are trying to delete for the given Library";
                            }
                        }

                        //
                        // OK So we have deleted all the entries at this point
                        //
                        TrashNode.m_lChildLinks.clear();
                    });

                    if (bTrashInstanceFound == false)
                    {
                        Error = "Unable to find the Transcan for the given Library";
                    }
                });

                if (bFoundFolderType == false)
                {
                    Error = "Unable to find the folder type for the given Library";
                }
            });
            if (foundLibrary==false)
            {
                Error = "Library not found";
            }

            return Error;
        }

        //------------------------------------------------------------------------------------------------

        std::string MoveFromTrashTo(const library::guid glibraryGUID, const xresource::full_guid& gDescriptor, const xresource::full_guid gNewParent ) noexcept
        {
            assert(gDescriptor != gNewParent);

            std::string             Error               = {};
            xresource::full_guid    gDescriptorParent   = {};

            bool foundLibrary = m_mLibraryDB.FindAsReadOnly(glibraryGUID, [&](const std::unique_ptr<library_db>& library)
            {
                bool bFoundFolderType = library->m_InfoByTypeDataBase.FindAsReadOnly(e10::folder::trash_guid_v.m_Type, [&](const std::unique_ptr<library_db::info_db>& FolderInfoDB)
                {
                    bool bTrashInstanceFound = FolderInfoDB->m_InfoDataBase.FindAsWrite(e10::folder::trash_guid_v.m_Instance, [&](library_db::info_node& TrashNode)
                    {
                        std::function<void(const xresource::full_guid&, bool)> ClearTrashTag = [&](const xresource::full_guid& gEntry, bool isRoot )
                        {
                            // Find the source descriptor in the library database
                            bool bFoundSourceType = library->m_InfoByTypeDataBase.FindAsReadOnly(gEntry.m_Type, [&](const std::unique_ptr<library_db::info_db>& sourceInfoDB)
                            {
                                bool bSourceIntanceFound = sourceInfoDB->m_InfoDataBase.FindAsWrite(gEntry.m_Instance, [&](library_db::info_node& sourceNode)
                                {
                                    //
                                    // Remove Trans Tag
                                    //
                                    assert(sourceNode.m_Info.m_RscLinks[0] == e10::folder::trash_guid_v);

                                    // Removed tag
                                    sourceNode.m_Info.m_RscLinks.erase(sourceNode.m_Info.m_RscLinks.begin());

                                    if (isRoot)
                                    {
                                        for ( auto& E : sourceNode.m_Info.m_RscLinks )
                                        {
                                            assert(E != e10::folder::trash_guid_v);
                                            if ( E.m_Type == e10::folder::type_guid_v)
                                            {
                                                gDescriptorParent = E;
                                                break;
                                            }
                                        }
                                    }

                                    //
                                    // Remove Entry reference from the trash
                                    //
                                    bool bFound = false;
                                    for (auto& E : TrashNode.m_lChildLinks)
                                    {
                                        if (E == gEntry)
                                        {
                                            bFound = true;
                                            TrashNode.m_lChildLinks.erase(TrashNode.m_lChildLinks.begin() + static_cast<int>( &E - &TrashNode.m_lChildLinks[0] ) );
                                            break;
                                        }
                                    }
                                    assert(bFound);

                                    // Mark this node as it has officially changed
                                    sourceNode.m_InfoChangeCount += 1;
                                    m_ModificationCounter++;

                                    //
                                    // If the entry happens to be a folder the children should come out from the trash as well
                                    //
                                    for ( auto& E : sourceNode.m_lChildLinks)
                                    {
                                        if (Error.empty()) ClearTrashTag(E, false);
                                    }
                                });
                                if (bSourceIntanceFound == false)
                                {
                                    Error = "The resource instance was not found in the database";
                                }
                            });
                            if (bFoundSourceType == false)
                            {
                                Error = "The resource instance type was not found in the library database";
                            }
                        };

                        // Move the item to the trash as well as all its children
                        ClearTrashTag(gDescriptor, true);
                    });
                    if (bTrashInstanceFound == false)
                    {
                        Error = "The trash instance was not found in the database";
                    }
                });
                if (bFoundFolderType == false)
                {
                    Error = "The folder type was not found in the library database";
                }
            });
            if (foundLibrary == false)
            {
                Error = "The library was not found in the library database";
            }

            if (Error.empty() == false ) return Error;

            //
            // Move to the new folder
            //
            if ( auto Err = MoveDescriptor(glibraryGUID, gDescriptor, gDescriptorParent, gNewParent); Err )
            {
                Err.clear();
            }

            return Error;
        }

        //------------------------------------------------------------------------------------------------

        std::string MoveToTrash(const library::guid glibraryGUID, const xresource::full_guid& gDescriptor) noexcept
        {
            std::string  Error;

            bool foundLibrary = m_mLibraryDB.FindAsReadOnly(glibraryGUID, [&](const std::unique_ptr<library_db>& library)
            {
                bool bFoundFolderType = library->m_InfoByTypeDataBase.FindAsReadOnly(e10::folder::trash_guid_v.m_Type, [&](const std::unique_ptr<library_db::info_db>& FolderInfoDB)
                {
                    bool bTrashInstanceFound = FolderInfoDB->m_InfoDataBase.FindAsWrite(e10::folder::trash_guid_v.m_Instance, [&](library_db::info_node& TrashNode)
                    {
                        std::function<void(const xresource::full_guid&)> MoveItemAnsChildrenToTrash = [&](const xresource::full_guid& gDescriptor )
                        {
                            // Find the source descriptor in the library database
                            bool bFoundSourceType = library->m_InfoByTypeDataBase.FindAsReadOnly(gDescriptor.m_Type, [&](const std::unique_ptr<library_db::info_db>& sourceInfoDB)
                            {
                                bool bSourceIntanceFound = sourceInfoDB->m_InfoDataBase.FindAsWrite(gDescriptor.m_Instance, [&](library_db::info_node& sourceNode)
                                {
                                    //
                                    // Double check that is not already in the trash
                                    //
                                    {
                                        int Index = 0;
                                        for (auto& E : sourceNode.m_Info.m_RscLinks)
                                        {
                                            if (E == e10::folder::trash_guid_v)
                                            {
                                                if (Index != 0)
                                                {
                                                    // Make sure that we move the trash folder to the front of the list
                                                    std::rotate(sourceNode.m_Info.m_RscLinks.begin(), sourceNode.m_Info.m_RscLinks.begin() + Index, sourceNode.m_Info.m_RscLinks.begin() + Index + 1);
                                                }

                                                // Already in the trash
                                                Error = "The descriptor is already in the trash";
                                                return;
                                            }
                                            Index++;
                                        }
                                    }

                                    //
                                    // OK Time to move the resource to the trash (We do this just to make sure we can restore it if we have to)
                                    // Note that we don't remove it from the existing folder... This allows us to restore it back to the original
                                    // folder if it still there as an option
                                    //
                                    sourceNode.m_Info.m_RscLinks.insert(sourceNode.m_Info.m_RscLinks.begin(), e10::folder::trash_guid_v);

                                    // Mark this node as it has officially changed
                                    sourceNode.m_InfoChangeCount += 1;
                                    m_ModificationCounter++;

                                    // Move all their children to the trash as well...
                                    for ( auto& E : sourceNode.m_lChildLinks)
                                    {
                                        MoveItemAnsChildrenToTrash(E);
                                    }

                                    //
                                    // Added as part of the trash links
                                    //
                                    TrashNode.m_lChildLinks.push_back(gDescriptor);
                                });
                                if (bSourceIntanceFound == false)
                                {
                                    Error = "The resource instance was not found in the database";
                                }
                            });
                            if (bFoundSourceType == false)
                            {
                                Error = "The resource instance type was not found in the library database";
                            }
                        };

                        // Move the item to the trash as well as all its children
                        MoveItemAnsChildrenToTrash(gDescriptor);

                    });
                    if (bTrashInstanceFound == false)
                    {
                        Error = "The trash instance was not found in the database";
                    }
                });
                if (bFoundFolderType == false)
                {
                    Error = "The folder type was not found in the library database";
                }
            });
            if (foundLibrary == false)
            {
                Error = "The library was not found in the library database";
            }

            return Error;
        }

        //------------------------------------------------------------------------------------------------
        /*
        std::string DeleteDescriptor(const library::guid glibraryGUID, const xresource::full_guid& gDescriptor) noexcept
        {
            std::string Error;
            bool foundLibrary = m_mLibraryDB.FindAsReadOnly(glibraryGUID, [&](const library_db& library)
            {
                // Find the source descriptor in the library database
                bool bFoundSourceType = library.m_InfoByTypeDataBase.FindAsReadOnly(gDescriptor.m_Type, [&](const std::unique_ptr<library_db::info_db>& sourceInfoDB)
                {
                    bool bSourceIntanceFound = sourceInfoDB->m_InfoDataBase.FindForDelete(gDescriptor.m_Instance, [&](library_db::info_node& sourceNode)
                    {
                        //
                        // OK Time to move the directory to the trash (We do this just to make sure we can restore it if we have to)
                        //
                        std::wstring    OriginalFolder = remove_substring(get_directory_path(sourceNode.m_Path), m_ProjectPath);
                        std::wstring    Destination    = std::format(L"{}\\Cache\\Temp\\Trash{}", m_ProjectPath, get_directory_path(OriginalFolder));
                        std::wstring    Source         = get_directory_path(sourceNode.m_Path);

                        Error = create_directory_path(Destination);
                        if (Error.empty() == false)
                            return;

                        Error = move_directory(std::format( L"{}\\{}", Destination, get_filename(Source) ), Source);
                        if (Error.empty() == false)
                            return;

                        // Clean up a bit... check if the paths are empty and if so remove the directories
                        for (int i=0; i<2; ++i)
                        {
                            Source = get_directory_path(Source);
                            if (auto Result = has_content(Source); std::holds_alternative<bool>(Result) )
                            {
                                if (std::get<bool>(Result) == false)
                                {
                                    std::error_code ec;
                                    std::filesystem::remove_all(Source, ec);
                                    if (ec)
                                    {
                                        Error = std::format("Error removing the source directory: {}", ec.message());
                                        printf("Error: %s \n", Error.c_str());
                                        Error.clear();
                                        break;
                                    }
                                }
                                else
                                {
                                    break;
                                }
                            }
                            else
                            {
                                printf("Error: %s \n", std::get<std::string>(Result).c_str() );
                                break;
                            }
                        }
                        
                        //
                        // Mark as things have changed
                        //
                        m_ModificationCounter += 1;

                        //
                        // Remove all the links from the source descriptor
                        //
                        for( auto& L : sourceNode.m_Info.m_RscLinks )
                        {
                            bool bFoundSourceType = library.m_InfoByTypeDataBase.FindAsReadOnly(L.m_Type, [&](const std::unique_ptr<library_db::info_db>& LinkInfoDB)
                            {
                                bool bSourceIntanceFound = LinkInfoDB->m_InfoDataBase.FindAsWrite(L.m_Instance, [&](library_db::info_node& LinkNode)
                                {
                                    // Delete the m_Info.m_Guid from the LinkNode.m_lChildLinks, should only be one
                                    bool bFound = false;
                                    LinkNode.m_lChildLinks.erase( std::remove_if(LinkNode.m_lChildLinks.begin(), LinkNode.m_lChildLinks.end(),[&](const xresource::full_guid& G)
                                    {
                                        if (G == sourceNode.m_Info.m_Guid)
                                        {
                                            bFound = true;
                                            return true;
                                        }
                                        return false;
                                    }), LinkNode.m_lChildLinks.end());

                                    assert(bFound);
                                });
                                assert(bSourceIntanceFound);
                            });
                            assert(bFoundSourceType);
                        }

                        //
                        // Remove the source descriptor from the database
                        //
                        for ( auto& D : sourceNode.m_lAssetDependencies )
                        {
                            // TODO: Delete all the dependencies
                        }
                    });
                    if (bSourceIntanceFound == false)
                    {
                        Error = "The source intance was not found in the database";
                    }
                });
                if (bFoundSourceType == false)
                {
                    Error = "The source type was not found in the library database";
                }
            });
            if (foundLibrary == false)
            {
                Error = "The library was not found in the library database";
            }
            return Error;
        }
        */

        //------------------------------------------------------------------------------------------------

        xerr RenameDescriptor(const library::guid glibraryGUID, const xresource::full_guid& gDescriptor, std::string_view NewName, bool bNewChange = true )
        {
            xerr Error;

            bool foundLibrary = m_mLibraryDB.FindAsReadOnly(glibraryGUID, [&](const std::unique_ptr<library_db>& library)
            {
                // Find the source descriptor in the library database
                bool bFoundSourceType = library->m_InfoByTypeDataBase.FindAsReadOnly(gDescriptor.m_Type, [&](const std::unique_ptr<library_db::info_db>& sourceInfoDB)
                {
                    bool bSourceIntanceFound = sourceInfoDB->m_InfoDataBase.FindAsWrite(gDescriptor.m_Instance, [&](library_db::info_node& sourceNode)
                    {
                        sourceNode.m_Info.m_Name = NewName;

                        int Count = bNewChange ? 1 : -1;
                        sourceNode.m_InfoChangeCount += Count;
                        m_ModificationCounter += Count;
                    });
                    if (bSourceIntanceFound == false)
                    {
                        Error = xerr::create_f<xerr::default_states, "The source instance was not found in the database">();
                    }
                });
                if (bFoundSourceType == false)
                {
                    Error = xerr::create_f<xerr::default_states, "The source type was not found in the library database" >();
                }
            });
            if (foundLibrary == false)
            {
                Error = xerr::create_f<xerr::default_states, "The library was not found in the library database" >();
            }

            return Error;
        }

        //------------------------------------------------------------------------------------------------

        std::string MakeDescriptorDirty(const library::guid glibraryGUID, const xresource::full_guid& gDescriptor, bool bNewChange = true )
        {
            std::string Error;

            bool foundLibrary = m_mLibraryDB.FindAsReadOnly(glibraryGUID, [&](const std::unique_ptr<library_db>& library)
            {
                // Find the source descriptor in the library database
                bool bFoundSourceType = library->m_InfoByTypeDataBase.FindAsReadOnly(gDescriptor.m_Type, [&](const std::unique_ptr<library_db::info_db>& sourceInfoDB)
                {
                    bool bSourceIntanceFound = sourceInfoDB->m_InfoDataBase.FindAsWrite(gDescriptor.m_Instance, [&](library_db::info_node& sourceNode)
                    {
                        int Count = bNewChange ? 1 : -1;
                        sourceNode.m_InfoChangeCount += Count;
                        m_ModificationCounter += Count;
                    });
                    if (bSourceIntanceFound == false)
                    {
                        Error = "The source instance was not found in the database";
                    }
                });
                if (bFoundSourceType == false)
                {
                    Error = "The source type was not found in the library database";
                }
            });
            if (foundLibrary == false)
            {
                Error = "The library was not found in the library database";
            }

            return Error;
        }

        //------------------------------------------------------------------------------------------------

        xerr MoveDescriptor(const library::guid& libraryGUID, const xresource::full_guid& sourceDescriptor, const xresource::full_guid& sourceParent, const xresource::full_guid& Target, bool bNewChange = true)
        {
            xerr Error = {};

            assert(libraryGUID.m_Type == e10::project::type_guid_v || libraryGUID.m_Type == e10::library::type_guid_v );
            assert(sourceDescriptor.empty()==false);
            assert(sourceParent.empty() == false);
            assert(Target.empty() == false);
            assert(libraryGUID.empty() == false);
            assert(sourceDescriptor != Target);
            assert(sourceDescriptor != sourceParent);
 //           assert(sourceParent != Target);

            bool foundLibrary = m_mLibraryDB.FindAsReadOnly(libraryGUID, [&](const std::unique_ptr<library_db>& library)
            {
                // Find the source descriptor in the library database
                bool bFoundSourceType = library->m_InfoByTypeDataBase.FindAsReadOnly(sourceDescriptor.m_Type, [&](const std::unique_ptr<library_db::info_db>& sourceInfoDB) 
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
                            Error = xerr::create_f<xerr::default_states, "The source parent was not found in the source descriptor">();
                            return;
                        }

                        bool bTargetType = library->m_InfoByTypeDataBase.FindAsReadOnly(Target.m_Type, [&](const std::unique_ptr<library_db::info_db>& targetInfoDB)
                        {
                            bool bTargetInstance = targetInfoDB->m_InfoDataBase.FindAsWrite(Target.m_Instance, [&](library_db::info_node& targetNode)
                            {
                                bool bParentType = library->m_InfoByTypeDataBase.FindAsReadOnly(sourceParent.m_Type, [&](const std::unique_ptr<library_db::info_db>& parentInfoDB)
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
                                            Error = xerr::create_f<xerr::default_states, "The source descriptor was not found in the parent">();
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

                                        int Count = bNewChange ? 1 : -1;
                                        sourceNode.m_InfoChangeCount += Count;
                                        m_ModificationCounter += Count;
                                    });

                                    if (bFoundParent==false)
                                    {
                                        Error = xerr::create_f<xerr::default_states, "The parent instance was not found in the library database">();
                                        return;
                                    }
                                });

                                if (bParentType == false)
                                {
                                    Error = xerr::create_f<xerr::default_states, "The parent type was not found in the library database">();
                                    return;
                                }
                            });

                            if (bTargetInstance == false )
                            {
                                Error = xerr::create_f<xerr::default_states, "Target instance not found in the database">();
                                return;
                            }
                        });

                        if (bTargetType == false)
                        {
                            Error = xerr::create_f<xerr::default_states, "The target type was not found in the library database">();
                            return;
                        }
                    });

                    if (bSourceIntanceFound == false )
                    {
                        Error = xerr::create_f<xerr::default_states, "The source intance was not found in the database">();
                        return;
                    }
                    });

                if (bFoundSourceType == false )
                {
                    Error = xerr::create_f<xerr::default_states, "The source type was not found in the library database">();
                    return;
                }
            });

            return Error;
        }

        //------------------------------------------------------------------------------------------------

        template<typename T_CALLBACK >
        bool getInfo(const library::guid LibraryGUID, xresource::full_guid ResourceGUID, T_CALLBACK&& CallBack ) const noexcept
        {
            bool bSuccess = false;
            m_mLibraryDB.FindAsReadOnly(LibraryGUID, [&](const std::unique_ptr<library_db>& Library)
            {
                Library->m_InfoByTypeDataBase.FindAsReadOnly(ResourceGUID.m_Type, [&](const std::unique_ptr<library_db::info_db>& InfoDB)
                {
                    using Arg0          = typename details::function_traits<T_CALLBACK>::template arg<0>::type;
                    using Arg0NoRef     = std::remove_reference_t<Arg0>;

                    if constexpr (std::is_lvalue_reference_v<Arg0> && std::is_const_v<Arg0NoRef>)
                    {
                        InfoDB->m_InfoDataBase.FindAsReadOnly(ResourceGUID.m_Instance, [&](const library_db::info_node& InfoNode)
                        {
                            CallBack(InfoNode.m_Info);
                            bSuccess = true;
                        });
                    }
                    else
                    {
                        InfoDB->m_InfoDataBase.FindAsWrite(ResourceGUID.m_Instance, [&](library_db::info_node& InfoNode)
                        {
                            CallBack(InfoNode.m_Info);
                            bSuccess = true;
                        });
                    }
                });
            });
            return bSuccess;
        }

        template<typename T_CALLBACK >
        bool getNodeInfo(const xresource::full_guid& ResourceGUID, T_CALLBACK&& CallBack) noexcept
        {
            bool bSuccess = false;
            for( auto& Lib : m_mLibraryDB )
            {
                Lib.second->m_InfoByTypeDataBase.FindAsReadOnly(ResourceGUID.m_Type, [&](const std::unique_ptr<library_db::info_db>& InfoDB)
                {
                    using Arg0          = typename details::function_traits<T_CALLBACK>::template arg<0>::type;
                    using Arg0NoRef     = std::remove_reference_t<Arg0>;

                    if constexpr (std::is_lvalue_reference_v<Arg0> && std::is_const_v<Arg0NoRef>)
                    {
                        InfoDB->m_InfoDataBase.FindAsReadOnly(ResourceGUID.m_Instance, [&](const library_db::info_node& InfoNode)
                        {
                            CallBack(InfoNode);
                            bSuccess = true;
                        });
                    }
                    else
                    {
                        InfoDB->m_InfoDataBase.FindAsWrite(ResourceGUID.m_Instance, [&](library_db::info_node& InfoNode)
                        {
                            CallBack(InfoNode);
                            bSuccess = true;
                        });
                    }
                });

                if (bSuccess) break;
            }

            return bSuccess;
        }

        //------------------------------------------------------------------------------------------------

        template<typename T_CALLBACK >
        bool getNodeInfo(const library::guid LibraryGUID, xresource::full_guid ResourceGUID, T_CALLBACK&& CallBack) const noexcept
        {
            bool bSuccess = false;
            m_mLibraryDB.FindAsReadOnly(LibraryGUID, [&](const std::unique_ptr<library_db>& Library)
            {
                Library->m_InfoByTypeDataBase.FindAsReadOnly(ResourceGUID.m_Type, [&](const std::unique_ptr<library_db::info_db>& InfoDB)
                {
                    using Arg0          = typename details::function_traits<T_CALLBACK>::template arg<0>::type;
                    using Arg0NoRef     = std::remove_reference_t<Arg0>;

                    if constexpr (std::is_lvalue_reference_v<Arg0> && std::is_const_v<Arg0NoRef>)
                    {
                        InfoDB->m_InfoDataBase.FindAsReadOnly(ResourceGUID.m_Instance, [&](const library_db::info_node& InfoNode)
                        {
                            CallBack(InfoNode);
                            bSuccess = true;
                        });
                    }
                    else
                    {
                        InfoDB->m_InfoDataBase.FindAsWrite(ResourceGUID.m_Instance, [&](library_db::info_node& InfoNode)
                        {
                            CallBack(InfoNode);
                            bSuccess = true;
                        });
                    }
                });
            });
            return bSuccess;
        }

        //------------------------------------------------------------------------------------------------

        void RecompileResource(const library::guid LibraryGUID, xresource::full_guid ResourceGUID)
        {
            m_mLibraryDB.FindAsReadOnly(LibraryGUID, [&](const std::unique_ptr<library_db>& Library)
            {
                Library->m_InfoByTypeDataBase.FindAsReadOnly(ResourceGUID.m_Type, [&](const std::unique_ptr<library_db::info_db>& InfoDB)
                {
                    InfoDB->m_InfoDataBase.FindAsWrite(ResourceGUID.m_Instance, [&](library_db::info_node& InfoNode)
                    {
                        // Let us clear the descriptor time to force the issue...
                        InfoNode.m_DescriptorTime = {};
                        InfoNode.m_ResourceTime   = {};
                        Library->AddToCompilationQueueIfNeeded(*InfoDB, InfoNode);
                    });
                });
            });
        }

        //------------------------------------------------------------------------------------------------

        xresource::full_guid NewAsset(const library::guid LibraryGUID, xresource::full_guid ResourceGUID, const xresource::full_guid& ParentGUID, const std::string_view Name = {} )
        {
            assert(m_mLibraryDB.empty() == false);
            assert(ResourceGUID.m_Type.empty() == false);

            if (ResourceGUID.m_Instance.empty())
            {
                ResourceGUID.m_Instance.GenerateGUID();
            }

            // Get the name of the type
            std::string TypeName;
            if ( auto P = m_AssetPluginsDB.find(ResourceGUID.m_Type); P )
            {
                TypeName = P->m_TypeName;
            }

            bool bFindLib = m_mLibraryDB.FindAsReadOnly(LibraryGUID, [&](const std::unique_ptr<library_db>& Library )
            {
                // TODO: The info path is completle incorrect
                std::wstring FinalPath = std::format(L"{}\\Descriptors\\{}\\{:02X}\\{:02X}\\{:X}.desc"
                    , Library->m_Library.m_Path
                    , xstrtool::To(TypeName)
                    , ResourceGUID.m_Instance.m_Value & 0xff
                    , (ResourceGUID.m_Instance.m_Value & 0xff00) >> 8
                    , ResourceGUID.m_Instance.m_Value
                );

                if (auto Err = CreatePath(FinalPath); Err)
                {
                    printf("Fail to create a directory [%s] with error [%s]", xstrtool::To(FinalPath).c_str(), std::string(Err.getMessage()).c_str() );
                    exit(1);
                }

                xresource_pipeline::info Info;
                Info.m_Name = Name;
                Info.m_Guid = ResourceGUID;
                if( ParentGUID.empty() == false ) Info.m_RscLinks.push_back(ParentGUID);


                FinalPath = std::format(L"{}\\info.txt", FinalPath);

                xproperty::settings::context Context;
                if ( auto Err = Info.Serialize( false, FinalPath, Context); Err)
                {
                    printf("Fail to serialize the info file [%s] with error [%s]", xstrtool::To(FinalPath).c_str(), std::string(Err.getMessage()).c_str());
                    exit(1);
                }

                // Create a new asset
                Library->m_InfoByTypeDataBase.FindAsReadOnlyOrCreate( ResourceGUID.m_Type, [&](std::unique_ptr<library_db::info_db>& InfoDB )
                {
                    InfoDB = std::make_unique<library_db::info_db>();
                }
                , [&](const std::unique_ptr<library_db::info_db>& InfoDB)
                {
                    InfoDB->m_InfoDataBase.Insert(ResourceGUID.m_Instance, [&](library_db::info_node& InfoNode)
                    {
                        InfoNode.m_Path   = std::move(FinalPath);
                        InfoNode.m_Info   = std::move(Info);
                        InfoNode.m_InfoChangeCount = 1;
                        m_ModificationCounter++;

                        //
                        // Insert all the child links
                        //
                        for (const auto& LinkGuid : InfoNode.m_Info.m_RscLinks)
                        {
                            Library->m_InfoByTypeDataBase.FindAsReadOnlyOrCreate
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

            return ResourceGUID;
        }

        //------------------------------------------------------------------------------------------------

        xresource::full_guid NewAsset(const xresource::type_guid ResourceType )
        {
            return NewAsset(m_ProjectGUID, xresource::full_guid{ {},ResourceType }, {0,0});
        }

        //------------------------------------------------------------------------------------------------

        xerr OpenProject( std::wstring_view ProjectPath )
        {
            assert(m_mLibraryDB.empty());

            m_ProjectPath = ProjectPath;

            //
            // Set this for the properties system... default path
            //
            xproperty::member_ui<std::wstring>::g_CurrentPath = std::format(L"{}\\Assets", m_ProjectPath );

            //
            // Set the current directory to the asset folder
            //
            SetCurrentDirectory((std::wstring(ProjectPath) + L"\\Assets").c_str());

            //
            // Setup the asset plugins
            //
            m_AssetPluginsDB.SetupProject(m_ProjectPath);

            //
            // Prepare compilation queues 
            //
            m_Compilation.AllocateQueues(m_AssetPluginsDB.RecomputePluginGroups());

            //
            // Load the config file for the project
            // TODO: Need to add the code load also libraries...
            library Library;
            {
                xtextfile::stream Stream;
                if (auto Err = Stream.Open(true, std::format(L"{}\\Project.config\\Library.config.txt", ProjectPath), {xtextfile::file_type::TEXT}); Err)
                    return Err;

                xproperty::settings::context Context;
                if ( auto Err = xproperty::sprop::serializer::Stream( Stream, Library, Context ); Err )
                    return Err;

                //
                // Fix to the proper path
                //
                Library.m_Path               = ProjectPath;
                Library.m_UserDescriptorPath = std::format(L"{}\\Descriptors", Library.m_Path);
                Library.m_SysDescriptorPath  = std::format(L"{}\\Cache\\Descriptors", Library.m_Path);
                Library.m_ResourcePath       = std::format(L"{}\\Cache\\Resources\\Platforms\\WINDOWS", Library.m_Path);

                // Make sure that those key paths are always created...
                if (false == std::filesystem::exists(Library.m_ResourcePath))       create_directory_path(Library.m_ResourcePath);
                if (false == std::filesystem::exists(Library.m_SysDescriptorPath))  create_directory_path(Library.m_SysDescriptorPath);
                if (false == std::filesystem::exists(Library.m_UserDescriptorPath)) create_directory_path(Library.m_UserDescriptorPath);
            }

            //
            // Set the project as the root project
            //
            m_ProjectGUID = Library.m_GUID;

            //
            // Load all the infos
            //
            m_mLibraryDB.Insert(Library.m_GUID, [&](std::unique_ptr<library_db>& ProjectDB)
            {
                ProjectDB = std::make_unique<library_db>(m_Compilation);

                ProjectDB->m_Library         = std::move(Library);
                ProjectDB->m_pProcessInfoJob = std::make_unique<process_info_job>();

                ProjectDB->m_pProcessInfoJob->setup(*this, *ProjectDB, {});

                xscheduler::g_System.SubmitJob(*ProjectDB->m_pProcessInfoJob);

                //
                // Wait for the job to finish
                //
                while (ProjectDB->m_pProcessInfoJob->m_State != process_info_job::state::DONE)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }

                //
                // Make sure to monitor the asset folder...
                //
                ProjectDB->m_FileMonitorChanges = std::make_unique<file_monitor_changes>( m_AssetPluginsDB, *ProjectDB);
            });

            //
            // Check to see if this is an entirely new project
            // If so we create the root + trash folder... 
            //
            if (m_mLibraryDB.size() == 1 )
            {
                //
                // Create the root if we have to
                //

                // Make sure to set the name of the root folder
                auto RootName = m_ProjectPath;
                RootName = RootName.substr(RootName.rfind(L'\\') + 1);
                RootName = RootName.substr(0, RootName.rfind(L'.'));

                bool bHasRootFolder = false;
                const xresource::full_guid RootGUID = { m_ProjectGUID.m_Instance, folder::type_guid_v };
                m_mLibraryDB.FindAsReadOnly(Library.m_GUID, [&](const std::unique_ptr<library_db>& ProjectDB)
                {
                    ProjectDB->m_InfoByTypeDataBase.FindAsReadOnly(RootGUID.m_Type, [&](const std::unique_ptr<library_db::info_db>& Info)
                    {
                        bHasRootFolder = Info->m_InfoDataBase.FindAsWrite(RootGUID.m_Instance, [&](library_db::info_node& Node )
                        {
                            // Make sure to set the upto date name of the root folder
                            Node.m_Info.m_Name = xstrtool::To(RootName);
                            bHasRootFolder     = true;
                        });
                    });
                });
                if (bHasRootFolder == false) NewAsset(m_ProjectGUID, RootGUID, { 0,0 }, xstrtool::To(RootName));

                //
                // Create the trash if we have to
                //
                bool bHasTrashcan = false;
                m_mLibraryDB.FindAsReadOnly(Library.m_GUID, [&](const std::unique_ptr<library_db>& ProjectDB)
                {
                    ProjectDB->m_InfoByTypeDataBase.FindAsReadOnly(e10::folder::trash_guid_v.m_Type, [&]( const std::unique_ptr<library_db::info_db>& Info)
                    {
                        bHasTrashcan = Info->m_InfoDataBase.FindAsReadOnly(e10::folder::trash_guid_v.m_Instance, [&](const library_db::info_node&){});
                    });
                });
                if (bHasTrashcan == false) NewAsset(m_ProjectGUID, e10::folder::trash_guid_v, RootGUID, "Trash");
            }

            //
            // Insert all the plugins found in the libraries
            //
            for(auto& L : m_mLibraryDB )
            {
                for ( auto& P : m_AssetPluginsDB.m_lPlugins )
                {
                    L.second->m_InfoByTypeDataBase.FindAsReadOnlyOrCreate
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

            //
            // Notify that we have open a new project
            //
            m_OnOpenProjectEvent.NotifyAll(*this);

            return {};
        }

        //------------------------------------------------------------------------------------------------

        void Save(xproperty::settings::context& Context )
        {
            if ( !isReadyToSave() ) return;

            for (auto& L : m_mLibraryDB)
            {
                for (auto& P : m_AssetPluginsDB.m_lPlugins)
                {
                    L.second->m_InfoByTypeDataBase.FindAsReadOnly
                    ( P.m_TypeGUID
                    , [&](const std::unique_ptr<library_db::info_db>& Entry)
                    {
                        for ( auto& I : Entry->m_InfoDataBase )
                        {
                            if (I.second.m_InfoChangeCount != 0)
                            {
                                auto& FileName = I.second.m_Path;
                                if ( auto Err = I.second.m_Info.Serialize(false, FileName.c_str(), Context); Err )
                                {
                                    printf("Fail to save info file [%s] with error[%s]", xstrtool::To(FileName).c_str(), std::string(Err.getMessage()).c_str() );
                                }
                                else
                                {
                                    I.second.m_InfoChangeCount = 0;
                                }
                            }
                        }
                    });
                }
            }

            m_ModificationCounter = 0;
        }

        //------------------------------------------------------------------------------------------------

        bool isReadyToSave() const noexcept
        {
            return m_ModificationCounter != 0;
        }

        //------------------------------------------------------------------------------------------------

        void CloseProject()
        {
            m_OnCloseProjectEvent.NotifyAll(*this);

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

        xdelegate::thread_safe<library_mgr&> m_OnOpenProjectEvent;
        xdelegate::thread_safe<library_mgr&> m_OnCloseProjectEvent;
        xdelegate::thread_safe<library_mgr&, library::guid, xresource::full_guid, std::shared_ptr<compilation::historical_entry::log>&> m_OnCompilationState;

        //------------------------------------------------------------------------------------------------

        using map                = xcontainer::unordered_lockless_map<library::guid,        std::unique_ptr<library_db>>;
        using map_rsc_to_library = xcontainer::unordered_lockless_map<xresource::full_guid, library::guid>;
        int                     m_ModificationCounter = 0;
        library::guid           m_ProjectGUID;
        std::wstring            m_ProjectPath;
        map                     m_mLibraryDB;
        asset_plugins_db        m_AssetPluginsDB;
        map_rsc_to_library      m_RscToLibraryMap;
        compilation::instance   m_Compilation       = {*this};
    };

    //------------------------------------------------------------------------------------------------
    //------------------------------------------------------------------------------------------------
    //------------------------------------------------------------------------------------------------

    inline void CompilingThreadWorker( library_mgr& LibMgr, compilation::entry&& Entry )
    {
        auto& Compilation = LibMgr.m_Compilation;

        // Let us start counting when they are awake and when they are sleeping
        ++Compilation.m_WorkersWorking;

        compilation::historical_entry   NewEntry;
        std::wstring                    DescriptorPath;
        {
            NewEntry.m_Entry  = std::move(Entry);
            NewEntry.m_Log    = std::make_shared<compilation::historical_entry::log>();

            // Change the entry state
            if ( false == LibMgr.getNodeInfo(NewEntry.m_Entry.m_gLibrary, NewEntry.m_Entry.m_FullGuid, [&](library_db::info_node& Node )
            {
                if ( static_cast<std::uint8_t>(Node.m_State)&1) Node.m_State = library_db::info_node::state::BEEN_EDITED_COMPILING;
                else                                            Node.m_State = library_db::info_node::state::COMPILING;

                // Set the actual string of the descriptor
                auto Skip = LibMgr.m_ProjectPath.size() + 1;
                DescriptorPath = Node.m_Path.substr(Skip, Node.m_Path.find_last_of(L'\\') - Skip);

            })) return;


            // Add the entry into the compilation list
            {
                std::scoped_lock lock(Compilation.m_Compiling.m_Mutex);
                Compilation.m_Compiling.m_List.push_back(NewEntry);
            }

            //
            // Now we can call the compiler
            //
            auto pPlugin = LibMgr.m_AssetPluginsDB.find(NewEntry.m_Entry.m_FullGuid.m_Type);

            // Make sure the path for the compiler is clean for the command line
            std::wstring CommandLine            = {};
            bool         OutputToConsole        = {};
            int          MaxHistoricalEntries   = {};
            {
                std::wstring        CompilerPath = {};
                std::scoped_lock    lk(Compilation.m_Settings.m_Mutex);

                OutputToConsole         = Compilation.m_Settings.m_bOutputToConsole;
                MaxHistoricalEntries    = Compilation.m_Settings.m_MaxHistoricalEntries;

                if (Compilation.m_Settings.m_bUseDebugCompiler)
                {
                    if ( pPlugin->m_DebugCompiler.empty() == false ) CompilerPath = std::format(L"{}\\{}", LibMgr.m_ProjectPath, pPlugin->m_DebugCompiler );
                    else
                    {
                        if (pPlugin->m_ReleaseCompiler.empty() == false) 
                        {
                            printf("ERROR: user requested the debug compiler but we have not path for it... will try the release compiler\n");
                            CompilerPath = std::format(L"{}\\{}", LibMgr.m_ProjectPath, pPlugin->m_ReleaseCompiler);
                        }
                        else
                        {
                            printf("ERROR: Both the release compiler and debug compiler has not paths\n");
                        }
                    }
                }
                else if (pPlugin->m_ReleaseCompiler.empty() == false)
                {
                    CompilerPath = std::format(L"{}\\{}", LibMgr.m_ProjectPath, pPlugin->m_ReleaseCompiler);
                }
                else
                {
                    if (pPlugin->m_DebugCompiler.empty() == false)
                    {
                        printf("ERROR: user requested the release compiler but we have not path for it... will try the debug compiler\n");
                        CompilerPath = std::format(L"{}\\{}", LibMgr.m_ProjectPath, pPlugin->m_DebugCompiler);
                    }
                    else
                    {
                        printf("ERROR: Both the release compiler and debug compiler has not paths\n");
                    }
                }

                if (CompilerPath.empty() == false)
                {
                    std::filesystem::path p(std::move(CompilerPath));
                    CompilerPath = p.lexically_normal();
                }

                // Generate the command line
                CommandLine = std::format(LR"("{}" -PROJECT "{}" -OPTIMIZATION {} -DEBUG {} -DESCRIPTOR "{}" -OUTPUT "{}\Cache\Resources\Platforms\WINDOWS")"
                    , CompilerPath
                    , LibMgr.m_ProjectPath
                    , LibMgr.m_Compilation.m_Settings.getOptimizationLevelString()
                    , LibMgr.m_Compilation.m_Settings.getDebugLevelString()
                    , DescriptorPath
                    , LibMgr.m_ProjectPath
                );
            }

            //
            // Start the actual compilation
            //
            {
                LibMgr.m_OnCompilationState.NotifyAll(LibMgr, NewEntry.m_Entry.m_gLibrary, NewEntry.m_Entry.m_FullGuid, NewEntry.m_Log );

                // Run the actual compiler
                try
                {
                    compilation::RunCommandLine
                    ( CommandLine
                    , *NewEntry.m_Log
                    , Compilation.m_Settings.m_bOutputToConsole
                    );
                }
                catch (const std::exception& e)
                {
                    std::cerr << "Error: " << e.what() << "\n" << xstrtool::To(CommandLine) << std::endl;
                }

                //
                // Put it in the final lists
                //

                // Remove our entry from the list and add it into the history
                {
                    std::scoped_lock lock(Compilation.m_Compiling.m_Mutex);
                    std::erase_if(Compilation.m_Compiling.m_List, [&](auto& E) { return E.m_Entry.m_FullGuid == NewEntry.m_Entry.m_FullGuid; });
                }

                // Let's collect the result of our entry...
                compilation::historical_entry::result Result;
                {
                    xcontainer::lock::scope lock( *NewEntry.m_Log );
                    Result = NewEntry.m_Log->get().m_Result;
                    if (Result == compilation::historical_entry::result::FAILURE ) NewEntry.m_Log->get().m_Log = xstrtool::To(CommandLine) + "\n==============================\n" + NewEntry.m_Log->get().m_Log;
                }

                // Added to the failure list if we failed
                if (Result == compilation::historical_entry::result::FAILURE )
                {
                    std::scoped_lock lock(Compilation.m_Failed.m_Mutex);
                    Compilation.m_Failed.m_Map[NewEntry.m_Entry.m_FullGuid] = NewEntry;
                }

                // move the NewEntry into the historical list
                {
                    std::scoped_lock lock(Compilation.m_Historical.m_Mutex);
                    Compilation.m_Historical.m_List.emplace_back(NewEntry);

                    if( Compilation.m_Historical.m_List.size() >= static_cast<std::size_t>(MaxHistoricalEntries+50))
                    {
                        Compilation.m_Historical.m_List.erase(Compilation.m_Historical.m_List.begin(), Compilation.m_Historical.m_List.begin() + 50);
                    }
                }

                //
                // Finally let us update our node_info state
                //
                if (LibMgr.m_mLibraryDB.FindAsReadOnly( NewEntry.m_Entry.m_gLibrary, [&]( const std::unique_ptr<library_db>& LibraryDB )
                {
                    if (LibraryDB->m_InfoByTypeDataBase.FindAsReadOnly(NewEntry.m_Entry.m_FullGuid.m_Type, [&]( const std::unique_ptr<library_db::info_db>& InfoDB )
                    {
                        if ( InfoDB->m_InfoDataBase.FindAsWrite(NewEntry.m_Entry.m_FullGuid.m_Instance, [&](library_db::info_node& Node )
                        {
                            if (static_cast<std::uint8_t>(Node.m_State) & 1) Node.m_State = Result == compilation::historical_entry::result::FAILURE ? library_db::info_node::state::BEEN_EDITED_ERRORS : library_db::info_node::state::BEEN_EDITED;
                            else                                             Node.m_State = Result == compilation::historical_entry::result::FAILURE ? library_db::info_node::state::ERRORS : library_db::info_node::state::IDLE;

                            if (Result != compilation::historical_entry::result::FAILURE)
                            {
                                // Update the basic dates
                                // Get the relative path of the resource from the descriptor
                                std::error_code     Ec                  = {};
                                auto                ResourceSubPath     = std::wstring_view(Node.m_Path.data() + 1 + LibMgr.m_ProjectPath.length() + sizeof("Descriptors"), Node.m_Path.find_last_of(L'\\') - sizeof("Descriptors\\.desc") - LibMgr.m_ProjectPath.length());
                                const std::wstring  ResourcePath        = std::format(L"{}\\Cache//Resources\\Platforms\\WINDOWS\\{}", LibMgr.m_ProjectPath, ResourceSubPath);
                                const std::wstring  DescriptorPath      = std::wstring{ Node.m_Path.substr(0, Node.m_Path.find_last_of(L'\\') + 1) } + L"Descriptor.txt";
                                const std::wstring  DependencyPath      = std::format(L"{}/Cache/Resources/Logs/{}.log/dependencies.txt", LibMgr.m_ProjectPath, ResourceSubPath);
                                const bool          bInfoFileExists     = std::filesystem::exists(Node.m_Path, Ec) ? !Ec : false;

                                Node.m_bHasDescriptor       = std::filesystem::exists(DescriptorPath, Ec) ? !Ec : false;
                                Node.m_bHasResource         = std::filesystem::exists(ResourcePath, Ec) ? !Ec : false;
                                Node.m_bHasDependencies     = Node.m_bHasResource && std::filesystem::exists(DependencyPath, Ec) ? !Ec : false;
                                Node.m_DescriptorTime       = Node.m_bHasDescriptor ? std::filesystem::last_write_time(DescriptorPath) : std::filesystem::file_time_type{};
                                Node.m_InfoTime             = bInfoFileExists ? std::filesystem::last_write_time(Node.m_Path) : std::filesystem::file_time_type{};
                                Node.m_ResourceTime         = Node.m_bHasResource ? std::filesystem::last_write_time(ResourcePath) : std::filesystem::file_time_type{};

                                //TODO: Update the dependencies of the info just in case they have changed....
                                if (Node.m_bHasDependencies)
                                {
                                    xproperty::settings::context        Context;
                                    xresource_pipeline::dependencies    Dependencies;

                                    if (auto Err = Dependencies.Serialize(true, DependencyPath, Context); Err)
                                    {
                                        //std::lock_guard Lk(m_lErrors);
                                        //m_lErrors.get().push_back(std::format(L"Fail to read the dependency file [{}] with error[{}]", DependencyPath.data(), strXstr(Err.getCode().m_pString).c_str()));
                                        //bDependencyExists = false;
                                        Node.m_bHasDependencies = false;
                                    }
                                    else
                                    {
                                        // Check to see if there is entries in our dependency list that we no longer care about
                                        for (auto& E : Node.m_Dependencies.m_Assets)
                                        {
                                            // Do we still have this old dependency? If not we need to remove it!
                                            if (auto it = std::find(Dependencies.m_Assets.begin(), Dependencies.m_Assets.end(), E); it == Dependencies.m_Assets.end())
                                            {
                                                // We no longer has this dependency
                                                bool bRemoveEntry = false;
                                                if( false == LibraryDB->m_AssetDataBase.FindAsWrite( E, [&](library_db::asset& Asset)
                                                {
                                                    // Remove our reference
                                                    for ( auto& G : Asset.m_lChildLinks )
                                                    {
                                                        if ( G == Node.m_Info.m_Guid )
                                                        {
                                                            // Remove Entry Here
                                                            Asset.m_lChildLinks.erase(Asset.m_lChildLinks.begin() + static_cast<int>(&G - Asset.m_lChildLinks.data() ));
                                                            break;
                                                        }
                                                    }

                                                    // if there are not more entries we should delete ours
                                                    if (Asset.m_lChildLinks.empty())
                                                    {
                                                        bRemoveEntry = true;
                                                    }
                                                }))
                                                {
                                                    // we should always find our entry...
                                                    assert(false);
                                                }

                                                if (bRemoveEntry)
                                                {
                                                    //NOTE: There is a race condition here... From the moment we removed our entry someone else could have added a new one...
                                                    LibraryDB->m_AssetDataBase.FindForDelete(E, [&](library_db::asset&){} );
                                                }
                                            }
                                            else
                                            {
                                                // We still have the same asset
                                                int a = 0;
                                            }
                                        }

                                        // See if there are new dependencies that we now care about...
                                        for (auto& E : Dependencies.m_Assets)
                                        {
                                            // Is this a new dependency for us?
                                            if (auto it = std::find(Node.m_Dependencies.m_Assets.begin(), Node.m_Dependencies.m_Assets.end(), E); it == Node.m_Dependencies.m_Assets.end())
                                            {
                                                LibraryDB->m_AssetDataBase.FindAsWriteOrCreate
                                                ( E
                                                , [&](library_db::asset& Asset)
                                                {
                                                    Asset.m_Path          = E;
                                                    Asset.m_LastWriteTime = std::filesystem::last_write_time( std::format( L"{}//{}", LibraryDB->m_Library.m_Path, Asset.m_Path) );
                                                }
                                                , [&](library_db::asset& Asset)
                                                {
                                                    Asset.m_lChildLinks.push_back(Node.m_Info.m_Guid);
                                                }
                                                );
                                            }
                                            else
                                            {
                                                // We still have this asset
                                                int a = 0;
                                            }
                                        }

                                        //TODO: We should check more types of dependencies...

                                        // Set the new dependencies
                                        Node.m_Dependencies = std::move(Dependencies);
                                    }
                                }

                                //
                                // Check and see if this entry needs to be recompile for some reason...
                                //
                                if ( LibraryDB->AddToCompilationQueueIfNeeded(*InfoDB, Node) == false )
                                {
                                    LibMgr.m_OnCompilationState.NotifyAll( LibMgr, LibraryDB->m_Library.m_GUID, Node.m_Info.m_Guid, NewEntry.m_Log);
                                }
                            }
                            else
                            {
                                // Fail to compile... nonetheless is the end of the compilation process...
                                LibMgr.m_OnCompilationState.NotifyAll(LibMgr, LibraryDB->m_Library.m_GUID, Node.m_Info.m_Guid, NewEntry.m_Log);
                            }
                        }))
                        {
                            //TODO: ERROR unable to find the library
                        }
                    }))
                    {
                        //TODO: ERROR unable to find the library
                    }
                }))
                {
                    //TODO: ERROR unable to find the library
                }
            }
        }

        // Say good bye to the worker
        --Compilation.m_WorkersWorking;
    }

    //------------------------------------------------------------------------------------------------

    int library_db::getQueueIndexFromType(xresource::type_guid Type) const
    {
        if ( auto pPlugin = m_CompilationInstance.m_LibraryMgr.m_AssetPluginsDB.find(Type); pPlugin)
            return pPlugin->m_RunGroupIndex;

        assert(false);
        return 0;
    }


    //------------------------------------------------------------------------------------------------
    //------------------------------------------------------------------------------------------------
    //------------------------------------------------------------------------------------------------

    inline library_mgr g_LibMgr;
}

#endif