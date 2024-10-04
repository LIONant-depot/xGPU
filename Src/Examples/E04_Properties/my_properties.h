#ifndef MY_PROPERTIES_H
#define MY_PROPERTIES_H

/*
------------------------------------------------------------------------------
# WHAT IS THIS FILE FOR?
------------------------------------------------------------------------------
There are libraries that are used by including a header file. xproperty does not
work like that, stead you have to define your own custom header file and then
include xproperty there. The reason for this is that xproperty needs to be
configured. So stead of creating dependencies to other header files and just
much easier to do it this way. In other the only place that will include xproperty
is your config header file (this one), all your other header and sources will include
this file.

------------------------------------------------------------------------------
# CORE INCLUDES
------------------------------------------------------------------------------
Add here all your header files required to configure your own xproperty.
Note that xproperty itself doesn't require any header files.

cpp */
#pragma once
#include<string>
#include<vector>
#include<map>
#include<memory>
#include<array>

/* cpp
------------------------------------------------------------------------------
# USER PRE-CONFIGURATION
------------------------------------------------------------------------------
configure the memory requirements for the xproperty library.
The library itself does not allocate any memory, but it does require to know
some edge cases. We must specify our worse case memory usage for our basic types
as well as for the iterator. Note that it requires to be put in the **xproperty::settings**
namespace.
cpp */
namespace xproperty::settings
{
    // Here are a bunch of useful tools to help us configure xproperty. 
    namespace details
    {
        // For our basic property types we need to know our worse alignment.
        // We can choose our alignment. This is useful if you are using SIMD instructions
        // or other types strange alignment types. So you have to choose your worse case scenario.
        constexpr auto force_minimum_case_alignment = 16;

        // This is the structure which is align and as big as our worse alignment and worse size
        template< typename...T >
        struct alignas(force_minimum_case_alignment) atomic_type_worse_alignment_and_memory
        {
            alignas(std::max({ alignof(T)... })) std::uint8_t m_Data[std::max({ sizeof(T)... })];
        };
    }

    // xproperty requires us to define this type. This type represents
    // our universal basic data allocation which is the worst case scenario for both alignment and size.
    // Note that no memory will in fact be allocated in the heap. 
    using data_memory = details::atomic_type_worse_alignment_and_memory
    <                   // Here we guess our worse properties data sizes
      std::string       // std::string is typically 32 bytes on 32 bit and 64 bit builds
    , std::size_t       // std::size_t is 4 bytes on 32 bit and 8 bytes on 64 bit
    , std::uint64_t     // std::uint64_t is always 8 bytes
    >;

    // xproperty requires as to define this type. This is similar as above but this time for iterators.
    using iterator_memory = details::atomic_type_worse_alignment_and_memory
    < std::vector<data_memory>::iterator
    , std::map<std::string, data_memory>::iterator
    , std::uint64_t         // this is an integer iterator for C arrays for instance
    , std::array<char, 4>   // you could set a minimum size
    >;
}

/* cpp
------------------------------------------------------------------------------
# ADDING THE ACTUAL LIBRARY
------------------------------------------------------------------------------
Now that we have defined our memory requirements we can Include the actual library
cpp */

#include "..\..\dependencies\xproperty\source\xproperty.h"
#include "..\..\dependencies\xproperty\source\sprop\property_sprop_container.h"

/* cpp
------------------------------------------------------------------------------
# USER POST-CONFIGURATION
------------------------------------------------------------------------------
We add here the rest of the configuration for the xproperty library. Note that
the rest of the configuration needs to be inside the **xproperty::settings** namespace.

## CONTEXT
------------------------------------------------------------------------------
We could add here some context for our properties. This is useful if you
have a xproperty that requires some global context to be able to work.
Typical example is a resource handle. You can add here a resource manager.
cpp */
namespace xproperty::settings
{
    struct context
    {
        // No context is needed for this example...
    };
}

/* cpp

## ATOMIC TYPES
------------------------------------------------------------------------------
Atomic types are all the data types that the property system is going to deal with.
Some of the types typically include thing like ints, floats, strings, etc...
But you would not put things like classes and such.
cpp */
namespace xproperty::settings
{
    // Note the pattern here. We use the **var_defaults** template to define that
    // we are dealing with your typical atomic type. We also define the name is
    // going to be used to identify it. This name is used to generate the GUID
    // for this type. The GUID is used to identify the type at runtime.
    template<>
    struct var_type<std::int64_t> : var_defaults<"s64", std::int64_t >
    {
        // We put this here for convenience. Since later own we can use this to
        // identify our type base on a single character. Which is useful for
        // serialization and such. But this is not needed for xproperty to work.
        constexpr static inline char ctype_v = 'D';
    };
    
    template<>
    struct var_type<std::uint64_t> : var_defaults<"u64", std::uint64_t >
    {
        constexpr static inline char ctype_v = 'U';
    };

    template<>
    struct var_type<std::int32_t> : var_defaults<"s32", std::int32_t >
    {
        constexpr static inline char ctype_v = 'd';
    };

    template<>
    struct var_type<std::uint32_t> : var_defaults<"u32", std::uint32_t >
    {
        constexpr static inline char ctype_v = 'u';
    };

    template<>
    struct var_type<std::int16_t> : var_defaults<"s16", std::int16_t > 
    {
        constexpr static inline char ctype_v = 'C';
    };

    template<>
    struct var_type<std::uint16_t> : var_defaults<"u16", std::uint16_t >
    {
        constexpr static inline char ctype_v = 'V';
    };

    template<>
    struct var_type<std::int8_t> : var_defaults<"s8", std::int8_t >
    {
        constexpr static inline char ctype_v = 'c';
    };

    template<>
    struct var_type<std::uint8_t> : var_defaults<"u8", std::uint8_t >
    {
        constexpr static inline char ctype_v = 'v';
    };

    template<>
    struct var_type<char> : var_type<std::int8_t> {};

    template<>
    struct var_type<bool> : var_defaults<"bool", bool >
    {
        constexpr static inline char ctype_v = 'b';
    };

    template<>
    struct var_type<float> : var_defaults<"f32", float >
    {
        constexpr static inline char ctype_v = 'f';
    };

    template<>
    struct var_type<double> : var_defaults<"f64", double >
    {
        constexpr static inline char ctype_v = 'F';
    };

#if __has_include(<string>)
    template<>
    struct var_type<std::string> : var_defaults<"string", std::string >
    {
        constexpr static inline char ctype_v = 's';
    };
#endif

}

namespace xproperty::settings
{
    using vector3_group = obj_group<"Vector3">;
    using vector2_group = obj_group<"Vector2">;
}

/* cpp

## SUPPORTING C-POINTERS TYPES
------------------------------------------------------------------------------
We can support C-Pointers. In this configuration we limit to
maximum 2 levels of pointers (T* and T**). If you need more you can add them later.
cpp */
namespace xproperty::settings
{
    template<typename T>
    requires(std::is_function_v<T> == false)
    struct var_type<T*> : var_ref_defaults<"pointer", T*> {};

    template<typename T>
    requires(std::is_function_v<T> == false)
    struct var_type<T**> : var_ref_defaults<"ppointer", T**> {};
}
/* cpp
 
## SUPPORTING CPP POINTERS TYPES
------------------------------------------------------------------------------
We can support C++ Pointers. This is not limited to std library we could use
are own concept of pointers such resource handles, etc...
cpp */
namespace xproperty::settings
{
#if __has_include(<memory>)
    template< typename T>
    requires(std::is_function_v<T> == false)
    struct var_type<std::unique_ptr<T>> : var_ref_defaults<"unique_ptr", std::unique_ptr<T>> {};

    template< typename T>
    requires(std::is_function_v<T> == false)
    struct var_type<std::shared_ptr<T>> : var_ref_defaults<"shared_ptr", std::shared_ptr<T>> {};
#endif
}
/* cpp

## SUPPORTING CPP LISTS TYPES
------------------------------------------------------------------------------
We can support C++ List types as well. A list is a container that has a begin and end iterator.
This includes things like std::span, std::vector, std::array, etc...

cpp */
namespace xproperty::settings
{
#if __has_include(<span>)
    template< typename T >
    struct var_type<std::span<T>> : var_list_defaults< "span", std::span<T>, T > {};
#endif

#if __has_include(<array>)
    template< typename T, auto N >
    struct var_type<std::array<T, N>> : var_list_defaults< "array", std::array<T, N>, T> {};
#endif

#if __has_include(<vector>)
    namespace details
    {
        template <typename...> struct is_std_unique_ptr final : std::false_type {};
        template<class T, typename... Args>
        struct is_std_unique_ptr<std::unique_ptr<T, Args...>> final : std::true_type {};
    }
    template< typename T >
    struct var_type<std::vector<T>> : var_list_defaults< "vector", std::vector<T>, T>
    {
        using type = typename var_list_defaults< "vector", std::vector<T>, T>::type;
        constexpr static void setSize( type& MemberVar, const std::size_t Size, context&) noexcept
        {
            MemberVar.resize(Size);

            // If it is a vector of a unique pointers we can initialize them right away...
            // Otherwise, when we try to access the nullptr pointer, and it will blow up... 
            if constexpr ( details::is_std_unique_ptr<T>::value )
            {
                for (auto& E : MemberVar)
                    E = std::make_unique<typename T::element_type>();
            }
        }
    };
#endif
}
/* cpp

## SUPPORTING C LISTS TYPES
------------------------------------------------------------------------------
We can support native C-List types as well. Such int a[1], int a[2][3], etc...
We added up to 3D arrays,but you can add more if you need.
cpp */
namespace xproperty::settings
{
    template< typename T, std::size_t N >
    struct var_type<T[N]> : var_list_native_defaults< "C-Array1D", N, T[N], T, std::size_t > {};

    template< typename T, std::size_t N1, std::size_t N2 >
    struct var_type<T[N1][N2]> : var_list_native_defaults< "C-Array2D", N1, T[N1][N2], T[N2], std::size_t > {};

    template< typename T, std::size_t N1, std::size_t N2, std::size_t N3>
    struct var_type<T[N1][N2][N3]> : var_list_native_defaults< "C-Array3D", N1, T[N1][N2][N3], T[N2][N3], std::size_t > {};
}

/* cpp

## SUPPORTING ADVANCED CPP LISTS TYPES
------------------------------------------------------------------------------
We can also add more advance type of containers such (std::map)

cpp */
namespace xproperty::settings
{
    template< typename T_KEY, typename T_VALUE_TYPE >
    struct var_type<std::map<T_KEY, T_VALUE_TYPE>> : var_list_defaults< "map", std::map<T_KEY, T_VALUE_TYPE>, T_VALUE_TYPE, typename std::map<T_KEY, T_VALUE_TYPE>::iterator, T_KEY >
    {
        using base           = var_list_defaults< "map", std::map<T_KEY, T_VALUE_TYPE>, T_VALUE_TYPE, typename std::map<T_KEY, T_VALUE_TYPE>::iterator, T_KEY >;
        using type           = typename base::type;
        using specializing_t = typename base::specializing_t;
        using begin_iterator = typename base::begin_iterator;
        using atomic_key     = typename base::atomic_key;

        constexpr static void             IteratorToKey   ( const type& MemberVar,       any&               Key, const begin_iterator&    I,   context& ) noexcept { Key.set<atomic_key>( I->first ); }
        constexpr static specializing_t*  getObject       (       type& MemberVar, const any&               Key, context& ) noexcept
        {
            auto p = MemberVar.find(Key.get<atomic_key>());

            // if we don't find it then we are going to add it!
            if(p == MemberVar.end()) 
            {
                MemberVar[ Key.get<atomic_key>() ] = T_VALUE_TYPE{};
                p = MemberVar.find(Key.get<atomic_key>());
            }

            return &p->second;
        }
        constexpr static specializing_t*  IteratorToObject(       type& MemberVar,       begin_iterator&    I,   context& ) noexcept { return &(*I).second; }
    };
}

/* cpp

## SUPPORTING CUSTOM LISTS TYPES
------------------------------------------------------------------------------
We add some random stupid custom list here that we build from scratch to serve as an example
on how to do containers that work with properties.

cpp */
namespace xproperty::settings
{
    template< typename T>
    struct llist
    {
        struct node
        {
            std::unique_ptr<node>  m_pNext;
            T                      m_Data;
        };

        // You are required to implement a simple iterator
        // Which includes the following functions
        struct iterator
        {
            node*       m_pCurrent;
            std::size_t m_Index;

            iterator    operator ++ ()                        noexcept { m_pCurrent = m_pCurrent->m_pNext.get(); ++m_Index; return *this; }
            bool        operator != (const iterator& I) const noexcept { return m_pCurrent != I.m_pCurrent; }
            T&          operator *()                          noexcept { return m_pCurrent->m_Data; }
        };

        // Your container should support the following functions
        // begin, end, and size are require...
        std::size_t     size        ()                  const noexcept { return m_Count;              }
        iterator        begin       ()                  const noexcept { return { m_pHead.get(), 0};  }
        iterator        end         ()                  const noexcept { return { nullptr, m_Count }; }

        // The following functions are not required but are useful
        T*              find        (const T& Key)      const noexcept
        {
            for( auto& E : *this )
                if (E == Key)
                    return &E;
            return nullptr;
        }

        llist() = default;
        llist(llist&& List ) noexcept
            : m_pHead{ std::move(List.m_pHead) }
            , m_Count{ List.m_Count }
        {
            List.m_Count = 0;
        }

        void push_front( T&& Data ) noexcept
        {
            auto pNewNode = std::make_unique<node>();
            pNewNode->m_Data     = std::move(Data);
            pNewNode->m_pNext    = std::move(m_pHead);
            m_pHead = std::move(pNewNode);
            ++m_Count;
        }

        std::unique_ptr<node>   m_pHead = {};
        std::size_t             m_Count = 0;
    };

    //
    // We add the registration/definition of our stupid container here...
    //
    template< typename T >
    struct var_type<llist<T>> : var_list_defaults< "llist", llist<T>, T, typename llist<T>::iterator, T >
    {
        using base           = var_list_defaults< "llist", llist<T>, T, typename llist<T>::iterator, T >;
        using type           = typename base::type;
        using atomic_key     = typename base::atomic_key;
        using specializing_t = typename base::specializing_t;
        using begin_iterator = typename base::begin_iterator;

        constexpr static void IteratorToKey(const type& MemberVar, xproperty::any& Key, const begin_iterator& I, context&) noexcept
        {
            Key.set<atomic_key>( I.m_pCurrent->m_Data );
        }

        constexpr static specializing_t* getObject(type& MemberVar, const any& Key, context&) noexcept
        {
            auto p = MemberVar.find(Key.get<atomic_key>());

            // If we can't find it then we are going to add it!
            if( p == nullptr )
            {
                MemberVar.push_front( T{Key.get<atomic_key>()} );
                p = MemberVar.find(Key.get<atomic_key>());
            }

            return p;
        }
    };
}
/* cpp
------------------------------------------------------------------------------
# CUSTOM USER DATA PER PROPERTY
------------------------------------------------------------------------------
We can create custom data for our properties. This is useful for instance if you
wish to add help for each property so that in an editor you can display it for the user.

cpp */
namespace xproperty::settings
{
    struct member_help_t : xproperty::member_user_data<"help">
    {
        const char* m_pHelp;
    };
}

//
// We put what the user interface in a different namespace
// to make it less verbose for the user.
//
namespace xproperty
{
    template< xproperty::details::fixed_string HELP >
    struct member_help : settings::member_help_t
    {
        constexpr  member_help() noexcept
            : member_help_t{ .m_pHelp = HELP.m_Value } {}
    };
}
/* cpp
------------------------------------------------------------------------------
# USEFUL FUNCTIONS
------------------------------------------------------------------------------
We can always add helper functions to help us with the xproperty library.
This is application dependent. The ones here are for our example application.

cpp */
namespace xproperty::settings
{
    //
    // Example on how to dump the content of xproperty::any to a string
    //
    inline int AnyToString( std::span<char> String, const xproperty::any& Value ) noexcept
    {
        if( Value.isEnum() )
        {
            const auto pString = Value.getEnumString();
            return sprintf_s(String.data(), String.size(), "%s", pString ? pString : "<unknown>" );
        }

        switch (Value.getTypeGuid())
        {
        case xproperty::settings::var_type<std::int32_t>::guid_v:    return sprintf_s( String.data(), String.size(), "%d",   Value.get<std::int32_t>());  
        case xproperty::settings::var_type<std::uint32_t>::guid_v:   return sprintf_s( String.data(), String.size(), "%u",   Value.get<std::uint32_t>()); 
        case xproperty::settings::var_type<std::int16_t>::guid_v:    return sprintf_s( String.data(), String.size(), "%d",   Value.get<std::int16_t>());  
        case xproperty::settings::var_type<std::uint16_t>::guid_v:   return sprintf_s( String.data(), String.size(), "%u",   Value.get<std::uint16_t>()); 
        case xproperty::settings::var_type<std::int8_t>::guid_v:     return sprintf_s( String.data(), String.size(), "%d",   Value.get<std::int8_t>());   
        case xproperty::settings::var_type<std::uint8_t>::guid_v:    return sprintf_s( String.data(), String.size(), "%u",   Value.get<std::uint8_t>());  
        case xproperty::settings::var_type<float>::guid_v:           return sprintf_s( String.data(), String.size(), "%f",   Value.get<float>());         
        case xproperty::settings::var_type<double>::guid_v:          return sprintf_s( String.data(), String.size(), "%f",   Value.get<double>());        
        case xproperty::settings::var_type<std::string>::guid_v:     return sprintf_s( String.data(), String.size(), "%s",   Value.get<std::string>().c_str()); 
        case xproperty::settings::var_type<std::uint64_t>::guid_v:   return sprintf_s( String.data(), String.size(), "%llu", Value.get<std::uint64_t>());
        case xproperty::settings::var_type<std::int64_t>::guid_v:    return sprintf_s( String.data(), String.size(), "%lld", Value.get<std::int64_t>());
        case xproperty::settings::var_type<bool>::guid_v:            return sprintf_s( String.data(), String.size(), "%s",   Value.get<bool>() ? "true" : "false");
        default: assert(false); break;
        }
        return 0;
    }

    constexpr std::uint32_t CTypeToGUID(const char Type) noexcept
    {
        switch (Type)
        {
        case xproperty::settings::var_type<std::int32_t>::ctype_v:    return xproperty::settings::var_type<std::int32_t>::guid_v;
        case xproperty::settings::var_type<std::uint32_t>::ctype_v:   return xproperty::settings::var_type<std::uint32_t>::guid_v;
        case xproperty::settings::var_type<std::int16_t>::ctype_v:    return xproperty::settings::var_type<std::int16_t>::guid_v;
        case xproperty::settings::var_type<std::uint16_t>::ctype_v:   return xproperty::settings::var_type<std::uint16_t>::guid_v;
        case xproperty::settings::var_type<std::int8_t>::ctype_v:     return xproperty::settings::var_type<std::int8_t>::guid_v;
        case xproperty::settings::var_type<std::uint8_t>::ctype_v:    return xproperty::settings::var_type<std::uint8_t>::guid_v;
        case xproperty::settings::var_type<float>::ctype_v:           return xproperty::settings::var_type<float>::guid_v;
        case xproperty::settings::var_type<double>::ctype_v:          return xproperty::settings::var_type<double>::guid_v;
        case xproperty::settings::var_type<std::string>::ctype_v:     return xproperty::settings::var_type<std::string>::guid_v; 
        case xproperty::settings::var_type<std::uint64_t>::ctype_v:   return xproperty::settings::var_type<std::uint64_t>::guid_v;
        case xproperty::settings::var_type<std::int64_t>::ctype_v:    return xproperty::settings::var_type<std::int64_t>::guid_v;
        case xproperty::settings::var_type<bool>::ctype_v:            return xproperty::settings::var_type<bool>::guid_v;
        default: assert(false); break;
        }
        return 0;
    }

    constexpr char GUIDToCType(std::uint32_t GUID) noexcept
    {
        switch (GUID)
        {
        case xproperty::settings::var_type<std::int32_t>::guid_v:    return xproperty::settings::var_type<std::int32_t>::ctype_v;
        case xproperty::settings::var_type<std::uint32_t>::guid_v:   return xproperty::settings::var_type<std::uint32_t>::ctype_v;
        case xproperty::settings::var_type<std::int16_t>::guid_v:    return xproperty::settings::var_type<std::int16_t>::ctype_v;
        case xproperty::settings::var_type<std::uint16_t>::guid_v:   return xproperty::settings::var_type<std::uint16_t>::ctype_v;
        case xproperty::settings::var_type<std::int8_t>::guid_v:     return xproperty::settings::var_type<std::int8_t>::ctype_v;
        case xproperty::settings::var_type<std::uint8_t>::guid_v:    return xproperty::settings::var_type<std::uint8_t>::ctype_v;
        case xproperty::settings::var_type<float>::guid_v:           return xproperty::settings::var_type<float>::ctype_v;
        case xproperty::settings::var_type<double>::guid_v:          return xproperty::settings::var_type<double>::ctype_v;
        case xproperty::settings::var_type<std::string>::guid_v:     return xproperty::settings::var_type<std::string>::ctype_v;
        case xproperty::settings::var_type<std::uint64_t>::guid_v:   return xproperty::settings::var_type<std::uint64_t>::ctype_v;
        case xproperty::settings::var_type<std::int64_t>::guid_v:    return xproperty::settings::var_type<std::int64_t>::ctype_v;
        case xproperty::settings::var_type<bool>::guid_v:            return xproperty::settings::var_type<bool>::ctype_v;
        default: assert(false); break;
        }
        return 0;
    }

    inline bool StringToAny( xproperty::any& Value, std::uint32_t TypeGUID, const std::span<char> String) noexcept
    {
        switch (TypeGUID)
        {
        case xproperty::settings::var_type<std::int32_t>::guid_v:    Value.set<std::int32_t>    (static_cast<std::int32_t>(std::stol(String.data())));  return true;
        case xproperty::settings::var_type<std::uint32_t>::guid_v:   Value.set<std::uint32_t>   (static_cast<std::uint32_t>(std::stoul(String.data())));  return true;
        case xproperty::settings::var_type<std::int16_t>::guid_v:    Value.set<std::int16_t>    (static_cast<std::int16_t>(std::atoi(String.data())));  return true;
        case xproperty::settings::var_type<std::uint16_t>::guid_v:   Value.set<std::uint16_t>   (static_cast<std::uint16_t>(std::atoi(String.data())));  return true;
        case xproperty::settings::var_type<std::int8_t>::guid_v:     Value.set<std::int8_t>     (static_cast<std::int8_t>(std::atoi(String.data())));  return true;
        case xproperty::settings::var_type<std::uint8_t>::guid_v:    Value.set<std::uint8_t>    (static_cast<std::uint8_t>(std::atoi(String.data())));  return true;
        case xproperty::settings::var_type<float>::guid_v:           Value.set<float>           (static_cast<float>(std::stof(String.data())));  return true;
        case xproperty::settings::var_type<double>::guid_v:          Value.set<double>          (static_cast<float>(std::stof(String.data())));  return true;
        case xproperty::settings::var_type<std::string>::guid_v:     Value.set<std::string>     (String.data());  return true;
        case xproperty::settings::var_type<std::uint64_t>::guid_v:   Value.set<std::uint64_t>   (static_cast<std::uint64_t>(std::stoull(String.data())));  return true;
        case xproperty::settings::var_type<std::int64_t>::guid_v:    Value.set<std::int64_t>    (static_cast<std::int64_t>(std::stoll(String.data())));  return true;
        case xproperty::settings::var_type<bool>::guid_v:            Value.set<bool>            ((String[0]=='t' || String[0]=='1' || String[0]=='T')?true:false);  return true;
        default: assert(false); break;
        }
        return false; 
    }
}

/* cpp
------------------------------------------------------------------------------
# ADD SUPPORT FOR UI/EDITOR FOR PROPERTIES
------------------------------------------------------------------------------
cpp */

namespace xproperty::flags
{
    struct type
    {
        bool m_isShowReadOnly : 1
            , m_isDontSave    : 1
            , m_isDontShow    : 1
            , m_isScope       : 1
            ;
    };
}

namespace xproperty::ui::details
{
    struct member_ui_base;
}

namespace xproperty::settings
{
    struct member_ui_t : xproperty::member_user_data<"UI">
    {
        const xproperty::ui::details::member_ui_base* m_pUIBase;
    };
}

namespace xproperty
{
    template< typename T>
    struct member_ui;

    namespace ui::undo
    {
        struct cmd;
    }

    namespace ui::details
    {
        struct member_ui_base
        {
            void*           m_pDrawFn;
            std::uint32_t   m_TypeGUID;
        };

        struct style
        {
            struct scroll_bar;
            struct drag_bar;
            struct enumeration;
            struct defaulted;
            struct button;
        };

        template< typename T_TYPE, typename T_STYLE>
        struct draw
        {
            static void Render(ui::undo::cmd& Cmd, const T_TYPE& Value, const member_ui_base& I, xproperty::flags::type Flags) noexcept;
        };

        template<typename T, xproperty::details::fixed_string T_FORMAT_MAIN>
        struct member_ui_numbers : ui::details::member_ui_base
        {
            inline static constexpr auto type_guid_v = xproperty::settings::var_type<T>::guid_v;

            member_ui_numbers() = delete;

            struct data : member_ui_base
            {
                T               m_Min;
                T               m_Max;
                const char*     m_pFormat;
                float           m_Speed;
            };

            template< T                                 T_MIN       = std::numeric_limits<T>::lowest()
                    , T                                 T_MAX       = std::numeric_limits<T>::max()
                    , xproperty::details::fixed_string  T_FORMAT    = T_FORMAT_MAIN
                    >
            struct scroll_bar : settings::member_ui_t
            {
                inline static constexpr data data_v
                { {.m_pDrawFn = &ui::details::draw<T, ui::details::style::scroll_bar>::Render, .m_TypeGUID = type_guid_v }
                , T_MIN
                , T_MAX
                , T_FORMAT
                , 0
                };
                constexpr scroll_bar() : settings::member_ui_t{ .m_pUIBase = &data_v }{}
            };

            template< float                             T_SPEED     = 0.5f
                    , T                                 T_MIN       = std::numeric_limits<T>::lowest()
                    , T                                 T_MAX       = std::numeric_limits<T>::max()
                    , xproperty::details::fixed_string  T_FORMAT    = T_FORMAT_MAIN
                    >
            struct drag_bar : settings::member_ui_t
            {
                inline static constexpr data data_v
                { { .m_pDrawFn = &ui::details::draw< T, ui::details::style::drag_bar>::Render, .m_TypeGUID = type_guid_v }
                , T_MIN
                , T_MAX
                , T_FORMAT
                , T_SPEED
                };
                constexpr drag_bar() : settings::member_ui_t{ .m_pUIBase  = &data_v }{}
            };

            using defaults = drag_bar<>;
        };

    }

    template<> struct member_ui<std::int64_t>   : ui::details::member_ui_numbers<std::int64_t,  "%d">   {};
    template<> struct member_ui<std::uint64_t>  : ui::details::member_ui_numbers<std::uint64_t, "%d">   {};
    template<> struct member_ui<std::int32_t>   : ui::details::member_ui_numbers<std::int32_t,  "%d">   {};
    template<> struct member_ui<std::uint32_t>  : ui::details::member_ui_numbers<std::uint32_t, "%d">   {};
    template<> struct member_ui<std::int16_t>   : ui::details::member_ui_numbers<std::int16_t,  "%d">   {};
    template<> struct member_ui<std::uint16_t>  : ui::details::member_ui_numbers<std::uint16_t, "%d">   {};
    template<> struct member_ui<std::int8_t>    : ui::details::member_ui_numbers<std::int8_t,   "%d">   {};
    template<> struct member_ui<std::uint8_t>   : ui::details::member_ui_numbers<std::uint8_t,  "%d">   {};
    template<> struct member_ui<char>           : ui::details::member_ui_numbers<int8_t,        "%d">   {};
    template<> struct member_ui<float>          : ui::details::member_ui_numbers<float,         "%.3f"> {};
    template<> struct member_ui<double>         : ui::details::member_ui_numbers<double,        "%.3f"> {};

    template<> struct member_ui<std::string>
    {
        member_ui() = delete;

        using data = ui::details::member_ui_base;

        inline static constexpr auto type_guid_v = xproperty::settings::var_type<std::string>::guid_v;

        template< typename T = ui::details::style::defaulted >
        struct button : settings::member_ui_t
        {
            inline static constexpr data data_v
            { .m_pDrawFn = &ui::details::draw<std::string, ui::details::style::button>::Render, .m_TypeGUID = type_guid_v };

            constexpr button() : settings::member_ui_t{ .m_pUIBase = &data_v } {}
        };

        struct defaults : settings::member_ui_t
        {
            inline static constexpr data data_v
            { .m_pDrawFn = &ui::details::draw<std::string, ui::details::style::defaulted>::Render, .m_TypeGUID = type_guid_v };

            constexpr defaults() : settings::member_ui_t{.m_pUIBase  = &data_v} {}
        };
    };

    template<> struct member_ui<bool>
    {
        member_ui() = delete;

        using data = ui::details::member_ui_base;
        inline static constexpr auto type_guid_v = xproperty::settings::var_type<bool>::guid_v;

        struct defaults : settings::member_ui_t
        {
            inline static constexpr data data_v
            { .m_pDrawFn = &ui::details::draw<bool, ui::details::style::defaulted>::Render, .m_TypeGUID = type_guid_v };

            constexpr defaults() : settings::member_ui_t{ .m_pUIBase = &data_v }
            {
                static_assert(type_guid_v == data_v.m_TypeGUID, "What the hells..." );

            }
        };
    };
}

/////////////////////////////////////////////////////////////////
// DONE
/////////////////////////////////////////////////////////////////
#endif