#ifndef XEDITOR_DOCUMENT_EDITOR_H
#define XEDITOR_DOCUMENT_EDITOR_H
#pragma once

// What every "one resource, one window" editor shares, whatever the resource's file holds (a descriptor's properties, a node graph, ...):
// the document (identity, the paths of the descriptor, the compiled resource and the compiler's log, dirty flag, whole-file snapshots that
// undo uses), the Save / Compile / Undo / Redo commands, the compile feedback, and the window (toolbar, a dock space of panels isolated from
// every other editor). A resource type derives from document_editor with its own document type, adds panels and commands.
#include "source/Tools/Editor/xeditor_resource_editor.h"
#include "source/Tools/Editor/xeditor_inspector.h"
#include "source/Tools/Editor/xeditor_toolbar.h"
#include "source/Tools/Editor/xeditor_resource_tab.h"
#include "dependencies/xeditor/include/xeditor/full_editor_shell.h"
#include "dependencies/xeditor/include/xeditor/commands.h"
#include "dependencies/xeditor/include/xeditor/serialize.h"
#include "dependencies/xstrtool/source/xstrtool.h"
#include "source/tools/xgpu_imgui_breach.h"

#include <atomic>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <format>
#include <mutex>
#include <sstream>

namespace xeditor
{
    //--------------------------------------------------------------------------------------------
    // Document: a resource whose source is one file (Descriptor.txt)
    //--------------------------------------------------------------------------------------------
    struct file_document : IDocument
    {
        xresource::full_guid    m_Guid          = {};
        e10::library::guid      m_LibraryGuid   = {};
        std::wstring            m_DescriptorPath;       // <resource>.desc\Descriptor.txt
        std::wstring            m_ResourcePath;         // the compiled resource (may not exist yet)
        std::wstring            m_LogPath;              // the compiler's log folder (Details.txt, shader.txt, ... live there)
        std::wstring            m_TempDir;              // <project>\Cache\Temp: snapshots go here, inside the project, because reading some descriptors depends on where the file is
        bool                    m_bDirty        = false;
        std::function<void()>   m_OnReplaced;           // the content was rebuilt (an undo): rebind whatever points at it

        // What a resource type provides
        virtual bool    isLoaded()                                              const noexcept = 0;
        virtual bool    Reset()                                                       noexcept = 0;     // empty content of the right type (false: no such resource type)
        virtual bool    ReplaceFromFile(const std::wstring& Path)                     noexcept = 0;     // read the content again; the old one stays when reading fails
        virtual bool    WriteToFile(const std::wstring& Path)                         noexcept = 0;
        virtual void    Validate(std::vector<std::string>&)                     const noexcept {}

        xresource::full_guid getGuid() const noexcept override { return m_Guid; }
        std::string getDisplayName() const noexcept override { return ResolveResourceDisplayName(m_LibraryGuid, m_Guid, "<resource>"); }
        bool        isDirty() const noexcept override { return m_bDirty; }

        bool Load() noexcept override
        {
            std::wstring InfoPath;
            e10::g_LibMgr.getNodeInfo(m_LibraryGuid, m_Guid, [&](e10::library_db::info_node& Node) { InfoPath = Node.m_Path; });
            if (InfoPath.empty()) return false;
            GeneratePaths(InfoPath);

            if (!Reset()) return false;
            // A resource that was just created has no file yet: it starts empty.
            if (std::filesystem::exists(m_DescriptorPath) && !ReplaceFromFile(m_DescriptorPath)) return false;
            m_bDirty = false;
            return true;
        }

        std::string Save() noexcept override
        {
            if (!isLoaded()) return "nothing loaded";
            if (!WriteToFile(m_DescriptorPath)) return "could not write " + xstrtool::To(m_DescriptorPath);
            e10::g_LibMgr.MakeDescriptorDirty({ m_LibraryGuid.m_Instance }, m_Guid);     // this is what queues the compile
            m_bDirty = false;
            return {};
        }

        // The whole content as its own file text: the "before" state of any command that cannot say how to invert itself.
        std::string Snapshot() noexcept
        {
            if (!isLoaded()) return {};
            const std::wstring Tmp = SnapshotPath();
            if (!WriteToFile(Tmp)) return {};
            std::ifstream In(Tmp, std::ios::binary);
            std::stringstream Text; Text << In.rdbuf();
            In.close();
            std::filesystem::remove(Tmp);
            return Text.str();
        }

        // Puts a snapshot's state back. m_OnReplaced tells the editor to rebind.
        bool Restore(const std::string& Text) noexcept
        {
            if (Text.empty()) return false;
            const std::wstring Tmp = SnapshotPath();
            { std::ofstream Out(Tmp, std::ios::binary | std::ios::trunc); Out << Text; }
            const bool bOk = ReplaceFromFile(Tmp);
            std::filesystem::remove(Tmp);
            if (!bOk) return false;
            m_bDirty = true;
            if (m_OnReplaced) m_OnReplaced();
            return true;
        }

    private:
        std::wstring SnapshotPath() const noexcept
        {
            std::filesystem::path Dir = m_TempDir.empty() ? std::filesystem::temp_directory_path() : std::filesystem::path(m_TempDir);
            std::error_code Ec;
            std::filesystem::create_directories(Dir, Ec);
            return (Dir / std::format(L"xeditor_snapshot_{:016X}.txt", m_Guid.m_Instance.m_Value)).wstring();
        }

        // Descriptors\<Type>\xx\yy\<guid>.desc\info.txt  ->  the descriptor, the compiled resource and the log folder. A resource under
        // <project>\Cache\Descriptors keeps everything below <project>\Cache.
        void GeneratePaths(const std::wstring& InfoPath) noexcept
        {
            m_DescriptorPath = InfoPath;
            for (size_t i = 0; i + 8 <= m_DescriptorPath.size(); ++i)
            {
                bool bEq = true;
                for (size_t j = 0; j < 8 && bEq; ++j) bEq = towlower(m_DescriptorPath[i + j]) == L"info.txt"[j];
                if (bEq) { m_DescriptorPath.replace(i, 8, L"Descriptor.txt"); break; }
            }

            m_ResourcePath.clear();
            m_LogPath.clear();
            m_TempDir.clear();
            const auto Slash = m_DescriptorPath.rfind(L'\\');
            if (Slash == std::wstring::npos) return;
            const std::wstring Base = m_DescriptorPath.substr(0, Slash - sizeof("desc"));          // drops ".desc"
            const auto Pos = Base.rfind(L"Descriptors");
            if (Pos == std::wstring::npos) return;

            const bool bInCache = Pos >= 7 && Base.compare(Pos - 7, 7, L"\\Cache\\") == 0;
            const std::wstring Root = bInCache ? L"" : L"Cache\\";
            m_ResourcePath = Base;  m_ResourcePath.replace(Pos, sizeof("Descriptors"), Root + L"Resources\\Platforms\\WINDOWS\\");
            m_LogPath      = Base;  m_LogPath.replace(Pos, sizeof("Descriptors"), Root + L"Resources\\Logs\\");
            m_LogPath += L".log";

            std::wstring Project = Base.substr(0, Pos);                                             // ends with a backslash
            if (bInCache) Project.resize(Project.size() - sizeof("Cache"));                         // drops "Cache\"
            m_TempDir = Project + L"Cache\\Temp";
        }
    };

    //--------------------------------------------------------------------------------------------
    // Property text helpers shared by the commands that set a reflected property from text
    //--------------------------------------------------------------------------------------------
    namespace cmd_util
    {
        inline bool GetArg(const xcmdline::parser& Parser, xcmdline::parser::handle H, std::string& Out) noexcept
        {
            if (H.m_Value < 0 || !Parser.hasOption(H)) return false;
            auto Arg = Parser.getOptionArgAs<std::string>(H, 0);
            if (std::holds_alternative<xerr>(Arg)) return false;
            Out = std::get<std::string>(Arg);
            return true;
        }

        // The atomic types xproperty can turn into text and back (AnyToString/StringToAny assert on anything else).
        inline bool IsAtomicType(std::uint32_t Guid) noexcept
        {
            return [&]<typename...T>(std::tuple<T...>*) { return ((Guid == xproperty::settings::var_type<T>::guid_v) || ...); }
                (static_cast<xproperty::settings::atomic_types_tuple*>(nullptr));
        }

        inline std::string FormatValue(const xproperty::any& Value) noexcept
        {
            if (Value.isEnum())
            {
                // An enum is written and read as the name of the item; the any alone can fail to find the name (an enum with no table of its own): then its number
                const char* pName = Value.getEnumString();
                return pName ? std::string(pName) : std::to_string(Value.getEnumValue());
            }
            if (Value.is<std::string>())  return Value.get<std::string>();
            if (Value.is<std::wstring>()) return xstrtool::To(Value.get<std::wstring>());
            std::array<char, 256> Buffer{};
            const int Len = xproperty::settings::AnyToString(Buffer, Value);
            return std::string(Buffer.data(), Len > 0 ? static_cast<std::size_t>(Len) : 0);
        }

        // Text -> value of the given atomic type. False when the text does not parse. StringToAny is not used for the numbers: it is
        // noexcept and calls std::stoi and friends, so garbage text ends the process instead of being refused.
        inline bool ParseValue(xproperty::any& Out, std::uint32_t TypeGuid, std::string Text) noexcept
        {
            using namespace xproperty::settings;
            auto Number = [&]<typename T>(std::type_identity<T>) -> bool
            {
                T Value{};
                const auto Result = std::from_chars(Text.data(), Text.data() + Text.size(), Value);
                if (Result.ec != std::errc() || Result.ptr != Text.data() + Text.size()) return false;
                Out.set<T>(Value);
                return true;
            };

            if (TypeGuid == var_type<std::int32_t>::guid_v)  return Number(std::type_identity<std::int32_t>{});
            if (TypeGuid == var_type<std::uint32_t>::guid_v) return Number(std::type_identity<std::uint32_t>{});
            if (TypeGuid == var_type<std::int16_t>::guid_v)  return Number(std::type_identity<std::int16_t>{});
            if (TypeGuid == var_type<std::uint16_t>::guid_v) return Number(std::type_identity<std::uint16_t>{});
            if (TypeGuid == var_type<std::int8_t>::guid_v)   return Number(std::type_identity<std::int8_t>{});
            if (TypeGuid == var_type<std::uint8_t>::guid_v)  return Number(std::type_identity<std::uint8_t>{});
            if (TypeGuid == var_type<std::int64_t>::guid_v)  return Number(std::type_identity<std::int64_t>{});
            if (TypeGuid == var_type<std::uint64_t>::guid_v) return Number(std::type_identity<std::uint64_t>{});
            if (TypeGuid == var_type<float>::guid_v)         return Number(std::type_identity<float>{});
            if (TypeGuid == var_type<double>::guid_v)        return Number(std::type_identity<double>{});
            if (TypeGuid == var_type<bool>::guid_v)
            {
                if (Text != "true" && Text != "false" && Text != "1" && Text != "0") return false;
                Out.set<bool>(Text == "true" || Text == "1");
                return true;
            }
            if (TypeGuid == var_type<std::string>::guid_v)   { Out.set<std::string>(Text);                  return true; }
            if (TypeGuid == var_type<std::wstring>::guid_v)  { Out.set<std::wstring>(xstrtool::To(Text));   return true; }
            if (TypeGuid == var_type<xresource::full_guid>::guid_v)
            {
                // "<instance hex>, <type hex>", exactly what AnyToString writes (StringToAny would take garbage for an empty guid)
                const auto Comma = Text.find(',');
                if (Comma == std::string::npos) return false;
                auto Hex = [](std::string_view Part, std::uint64_t& Value) noexcept
                {
                    while (!Part.empty() && Part.front() == ' ') Part.remove_prefix(1);
                    while (!Part.empty() && Part.back()  == ' ') Part.remove_suffix(1);
                    const auto Result = std::from_chars(Part.data(), Part.data() + Part.size(), Value, 16);
                    return !Part.empty() && Result.ec == std::errc() && Result.ptr == Part.data() + Part.size();
                };
                std::uint64_t Instance = 0, Type = 0;
                if (!Hex(std::string_view(Text).substr(0, Comma), Instance) || !Hex(std::string_view(Text).substr(Comma + 1), Type)) return false;
                Out.set<xresource::full_guid>(xresource::full_guid{ { Instance }, { Type } });
                return true;
            }
            // Anything else is an enum, which xproperty takes as the name of the item; a property of another kind refuses it when it is set
            Out.set<std::string>(Text);
            return true;
        }

        // A reflected object and where it lives
        struct property_target
        {
            const xproperty::type::object*  m_pObject   = nullptr;
            void*                           m_pInstance = nullptr;
        };

        inline bool FindProperty(const property_target& Target, const std::string& Path, xproperty::any& Out) noexcept
        {
            bool bFound = false;
            xproperty::settings::context Context;
            xproperty::sprop::collector(Target.m_pInstance, *Target.m_pObject, Context, [&](const char* pName, xproperty::any&& Data, const xproperty::type::members&, bool, const void*) noexcept
            {
                if (!bFound && Path == pName) { Out = std::move(Data); bFound = true; }
            });
            return bFound;
        }

        // "path = value" for every atomic property of the object whose path contains Filter, one per line.
        inline std::string ListProperties(const property_target& Target, const std::string& Filter) noexcept
        {
            std::string Out;
            xproperty::settings::context Context;
            xproperty::sprop::collector(Target.m_pInstance, *Target.m_pObject, Context, [&](const char* pName, xproperty::any&& Data, const xproperty::type::members& Member, bool, const void*) noexcept
            {
                if (!Data.m_pType || !(IsAtomicType(Data.getTypeGuid()) || Data.isEnum()) || std::string_view(pName).find(Filter) == std::string_view::npos) return;
                if (std::holds_alternative<xproperty::type::members::scope>(Member.m_Variant) || std::holds_alternative<xproperty::type::members::props>(Member.m_Variant)) return;
                Out += std::format("{} = {}\n", pName, FormatValue(Data));
            });
            return Out.empty() ? "(no properties)" : Out;
        }

        inline bool SetValue(const property_target& Target, const std::string& Path, std::uint32_t TypeGuid, const std::string& Text) noexcept
        {
            xproperty::any Value;
            if (!ParseValue(Value, TypeGuid, Text)) return false;
            // An enum item name that does not exist is ignored by xproperty without an error: a value that neither changed nor is the one asked for
            const bool bEnum = !IsAtomicType(TypeGuid);
            xproperty::any Before;
            if (bEnum && !FindProperty(Target, Path, Before)) return false;

            std::string Error;
            xproperty::settings::context Context;
            xproperty::sprop::setProperty(Error, Target.m_pInstance, *Target.m_pObject, xproperty::sprop::container::prop{ Path, Value }, Context);
            if (!Error.empty()) return false;
            if (!bEnum) return true;

            xproperty::any After;
            if (!FindProperty(Target, Path, After)) return false;
            const std::string Now = FormatValue(After);
            return Now == Text || Now != FormatValue(Before);
        }
    }

    //--------------------------------------------------------------------------------------------
    // Commands every document editor has: queries (xundo keeps them apart from edits, and none is itself undoable)
    //--------------------------------------------------------------------------------------------
    // The state of the last compile of a resource, its validation errors and the tail of the compiler's log: what CompileStatus answers
    inline std::string DescribeCompile(const std::shared_ptr<e10::compilation::historical_entry::log>& Log, bool bDirty, const std::vector<std::string>& Errors, int nLines) noexcept
    {
        using result = e10::compilation::historical_entry::result;
        std::string Text;
        std::string Tail;
        result Result = result::SUCCESS;
        if (Log)
        {
            xcontainer::lock::scope Lock(*Log);
            Result = Log->get().m_Result;
            const std::string& Full = Log->get().m_Log;
            std::size_t Start = Full.size();
            for (int i = 0; i < nLines && Start > 0; ++i)
            {
                const auto Newline = Full.rfind('\n', Start > 1 ? Start - 2 : 0);
                Start = Newline == std::string::npos ? 0 : Newline + 1;
            }
            Tail = Full.substr(Start);
        }
        const char* pState = "success";
        switch (Result)
        {
        case result::COMPILING:          pState = "compiling";              break;
        case result::COMPILING_WARNINGS: pState = "compiling (warnings)";   break;
        case result::SUCCESS:            pState = "success";                break;
        case result::SUCCESS_WARNINGS:   pState = "success (warnings)";     break;
        case result::FAILURE:            pState = "failed";                 break;
        }
        Text = std::format("state: {}\nunsaved changes: {}\nvalidation errors: {}\n", pState, bDirty ? "yes" : "no", Errors.size());
        for (auto& E : Errors) Text += "  " + E + "\n";
        if (!Tail.empty()) Text += "log (last lines):\n" + Tail;
        return Text;
    }

    namespace document_cmds
    {
        struct compile_status_cmd : xundo::query_command_base
        {
            file_document&                                              m_Doc;
            std::shared_ptr<e10::compilation::historical_entry::log>&   m_Log;
            std::vector<std::string>&                                   m_Errors;
            compile_status_cmd(xundo::system& System, file_document& Doc, std::shared_ptr<e10::compilation::historical_entry::log>& Log, std::vector<std::string>& Errors) noexcept
                : query_command_base(System, "CompileStatus", nullptr), m_Doc(Doc), m_Log(Log), m_Errors(Errors) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "How the last compile of the resource went: state (compiling, success, failed), unsaved changes, validation errors, the compiler's log. Usage: CompileStatus [-Lines n]"; }
            void RegisterArguments() noexcept override { m_hLines = m_Parser.addOption("Lines", "How many lines of the log (default 15)", false, 1); }
            std::string Query() noexcept override
            {
                std::string Lines;
                int n = 15;
                if (cmd_util::GetArg(m_Parser, m_hLines, Lines) && std::from_chars(Lines.data(), Lines.data() + Lines.size(), n).ec != std::errc()) return "CompileStatus: Lines takes a number";
                std::vector<std::string> Errors = m_Errors;
                if (Errors.empty() && m_Doc.isLoaded()) m_Doc.Validate(Errors);
                return DescribeCompile(m_Log, m_Doc.isDirty(), Errors, n);
            }
            xcmdline::parser::handle m_hLines;
        };

        struct save_cmd : xundo::query_command_base
        {
            file_document& m_Doc;
            save_cmd(xundo::system& System, file_document& Doc) noexcept : query_command_base(System, "Save", nullptr), m_Doc(Doc) {}
            const char* getCommandHelp() const noexcept override { return "Saves the resource's file (which queues its compile). Usage: Save"; }
            void RegisterArguments() noexcept override {}
            std::string Query() noexcept override { auto Err = m_Doc.Save(); return Err.empty() ? "Save: saved" : "Save: " + Err; }
        };

        struct compile_cmd : xundo::query_command_base
        {
            file_document&            m_Doc;
            std::vector<std::string>& m_Errors;
            compile_cmd(xundo::system& System, file_document& Doc, std::vector<std::string>& Errors) noexcept : query_command_base(System, "Compile", nullptr), m_Doc(Doc), m_Errors(Errors) {}
            const char* getCommandHelp() const noexcept override { return "Validates the resource, saves it and queues the compile. Usage: Compile"; }
            void RegisterArguments() noexcept override {}
            std::string Query() noexcept override
            {
                m_Errors.clear();
                if (!m_Doc.isLoaded()) return "Compile: nothing loaded";
                m_Doc.Validate(m_Errors);
                if (!m_Errors.empty())
                {
                    std::string Text = std::format("Compile: {} validation error(s)", m_Errors.size());
                    for (auto& E : m_Errors) Text += "\n  " + E;
                    return Text;
                }
                auto Err = m_Doc.Save();
                return Err.empty() ? "Compile: saved, compile queued" : "Compile: " + Err;
            }
        };

        struct undo_cmd : xundo::query_command_base
        {
            undo_cmd(xundo::system& System) noexcept : query_command_base(System, "Undo", nullptr) {}
            const char* getCommandHelp() const noexcept override { return "Undoes the last edit. Usage: Undo"; }
            void RegisterArguments() noexcept override {}
            std::string Query() noexcept override { m_System.Undo(); return "Undo: done"; }
        };

        struct redo_cmd : xundo::query_command_base
        {
            redo_cmd(xundo::system& System) noexcept : query_command_base(System, "Redo", nullptr) {}
            const char* getCommandHelp() const noexcept override { return "Redoes the last undone edit. Usage: Redo"; }
            void RegisterArguments() noexcept override {}
            std::string Query() noexcept override { m_System.Redo(); return "Redo: done"; }
        };
    }

    // A query whose answer an editor computes itself, for the few that do not need a class of their own
    struct lambda_query_cmd : xundo::query_command_base
    {
        const char*                 m_pHelp;
        std::function<std::string()> m_Fn;
        lambda_query_cmd(xundo::system& System, const char* pName, const char* pHelp, std::function<std::string()> Fn) noexcept : query_command_base(System, pName, nullptr), m_pHelp(pHelp), m_Fn(std::move(Fn)) {}
        const char* getCommandHelp() const noexcept override { return m_pHelp; }
        void RegisterArguments() noexcept override {}
        std::string Query() noexcept override { return m_Fn(); }
    };

    //--------------------------------------------------------------------------------------------
    // Commands for the settings an editor's preview is drawn with (an inspector over a reflected settings struct). Those are view state, not part
    // of the resource, so they are queries: not undoable, and they never dirty the document. Path and value are base64 like SetProperty's.
    //--------------------------------------------------------------------------------------------
    template<class T_SETTINGS>
    struct set_preview_cmd : xundo::query_command_base
    {
        T_SETTINGS& m_Settings;

        set_preview_cmd(xundo::system& System, T_SETTINGS& Settings) noexcept : query_command_base(System, "SetPreview", nullptr), m_Settings(Settings) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Sets one preview setting. Usage: SetPreview -Path base64 -Value base64 (paths as ListPreview shows them)"; }
        void RegisterArguments() noexcept override
        {
            m_hPath  = m_Parser.addOption("Path",  "Setting path, base64", true, 1);
            m_hValue = m_Parser.addOption("Value", "New value, base64",    true, 1);
        }
        std::string Query() noexcept override
        {
            std::string Path, Value;
            if (!cmd_util::GetArg(m_Parser, m_hPath, Path) || !cmd_util::GetArg(m_Parser, m_hValue, Value)) return "SetPreview: bad arguments";
            Path  = Base64Decode(Path);
            Value = Base64Decode(Value);

            const cmd_util::property_target Target{ xproperty::getObjectByType<T_SETTINGS>(), &m_Settings };
            xproperty::any Current;
            if (!cmd_util::FindProperty(Target, Path, Current)) return std::format("SetPreview: no setting '{}'", Path);
            if (!cmd_util::SetValue(Target, Path, Current.getTypeGuid(), Value)) return std::format("SetPreview: '{}' does not take '{}'", Path, Value);
            return "SetPreview: done";
        }
        xcmdline::parser::handle m_hPath, m_hValue;
    };

    template<class T_SETTINGS>
    struct list_preview_cmd : xundo::query_command_base
    {
        T_SETTINGS& m_Settings;

        list_preview_cmd(xundo::system& System, T_SETTINGS& Settings) noexcept : query_command_base(System, "ListPreview", nullptr), m_Settings(Settings) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Lists the preview settings (path = value), the paths SetPreview takes. Usage: ListPreview [-Filter text]"; }
        void RegisterArguments() noexcept override { m_hFilter = m_Parser.addOption("Filter", "Only paths containing this text", false, 1); }
        std::string Query() noexcept override
        {
            std::string Filter;
            cmd_util::GetArg(m_Parser, m_hFilter, Filter);
            return cmd_util::ListProperties({ xproperty::getObjectByType<T_SETTINGS>(), &m_Settings }, Filter);
        }
        xcmdline::parser::handle m_hFilter;
    };

    // The same for an editor whose preview has settings in more than one object
    using preview_targets = std::function<std::vector<cmd_util::property_target>()>;

    struct set_preview_multi_cmd : xundo::query_command_base
    {
        preview_targets m_Targets;
        set_preview_multi_cmd(xundo::system& System, preview_targets Targets) noexcept : query_command_base(System, "SetPreview", nullptr), m_Targets(std::move(Targets)) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Sets one preview setting. Usage: SetPreview -Path base64 -Value base64 (paths as ListPreview shows them)"; }
        void RegisterArguments() noexcept override
        {
            m_hPath  = m_Parser.addOption("Path",  "Setting path, base64", true, 1);
            m_hValue = m_Parser.addOption("Value", "New value, base64",    true, 1);
        }
        std::string Query() noexcept override
        {
            std::string Path, Value;
            if (!cmd_util::GetArg(m_Parser, m_hPath, Path) || !cmd_util::GetArg(m_Parser, m_hValue, Value)) return "SetPreview: bad arguments";
            Path  = Base64Decode(Path);
            Value = Base64Decode(Value);
            for (auto& Target : m_Targets())
            {
                xproperty::any Current;
                if (!cmd_util::FindProperty(Target, Path, Current)) continue;
                if (!cmd_util::SetValue(Target, Path, Current.getTypeGuid(), Value)) return std::format("SetPreview: '{}' does not take '{}'", Path, Value);
                return "SetPreview: done";
            }
            return std::format("SetPreview: no setting '{}'", Path);
        }
        xcmdline::parser::handle m_hPath, m_hValue;
    };

    struct list_preview_multi_cmd : xundo::query_command_base
    {
        preview_targets m_Targets;
        list_preview_multi_cmd(xundo::system& System, preview_targets Targets) noexcept : query_command_base(System, "ListPreview", nullptr), m_Targets(std::move(Targets)) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Lists the preview settings (path = value), the paths SetPreview takes. Usage: ListPreview [-Filter text]"; }
        void RegisterArguments() noexcept override { m_hFilter = m_Parser.addOption("Filter", "Only paths containing this text", false, 1); }
        std::string Query() noexcept override
        {
            std::string Filter, Text;
            cmd_util::GetArg(m_Parser, m_hFilter, Filter);
            for (auto& Target : m_Targets())
            {
                auto Part = cmd_util::ListProperties(Target, Filter);
                if (Part != "(no properties)") Text += Part;
            }
            return Text.empty() ? "(no properties)" : Text;
        }
        xcmdline::parser::handle m_hFilter;
    };

    //--------------------------------------------------------------------------------------------
    // The editor window
    //--------------------------------------------------------------------------------------------
    template<class T_DOC>
    struct document_editor : resource_editor
    {
        // Where a panel docks: left column (a second panel there docks below the first), the middle (a second one docks below), right column.
        enum class dock { left, left_bottom, center, bottom, right };

        struct panel
        {
            std::string             m_Title;        // window title with the resource's id, unique per editor
            dock                    m_Where;
            std::function<void()>   m_Render;       // the window's content
            ImGuiWindowFlags        m_Flags = 0;
        };

        T_DOC                                   m_Document;
        xundo::system                           m_Undo;
        std::vector<std::string>                m_ValidationErrors;
        document_cmds::save_cmd                 m_Save;
        document_cmds::compile_cmd              m_Compile;
        document_cmds::undo_cmd                 m_UndoCmd;
        document_cmds::redo_cmd                 m_RedoCmd;
        document_cmds::compile_status_cmd       m_CompileStatus;

        xgpu::device*                           m_pDevice = nullptr;
        std::string                             m_TypeName;
        std::string                             m_IdSuffix;                 // "##<instance><type>"
        std::vector<panel>                      m_Panels;
        std::atomic<bool>                       m_bCompileStarted = false;  // a compile began: OnCompileStarted runs on the next frame
        bool                                    m_bCompiled = false;        // a compile finished OK: OnCompiled runs on the next frame
        bool                                    m_bCompileFailed = false;   // one failed: OnCompileFailed runs on the next frame
        mutable std::mutex                      m_CompileStartMutex;
        std::filesystem::file_time_type         m_CompileStart{};
        std::shared_ptr<e10::compilation::historical_entry::log> m_CompilationLog = std::make_shared<e10::compilation::historical_entry::log>(
            e10::compilation::historical_entry::communication{ .m_Result = e10::compilation::historical_entry::result::SUCCESS });

        document_editor(const char* pTypeName, xresource::full_guid Guid, e10::library::guid LibraryGuid, xgpu::device* pDevice) noexcept
            : m_Save(m_Undo, m_Document), m_Compile(m_Undo, m_Document, m_ValidationErrors), m_UndoCmd(m_Undo), m_RedoCmd(m_Undo), m_CompileStatus(m_Undo, m_Document, m_CompilationLog, m_ValidationErrors)
            , m_pDevice(pDevice), m_TypeName(pTypeName)
        {
            m_Document.m_Guid        = Guid;
            m_Document.m_LibraryGuid = LibraryGuid;
            if (auto Err = m_Undo.Init({}, false); !Err.empty()) printf("%s editor undo Init: %s\n", pTypeName, Err.c_str());
            m_Document.Load();
            m_Document.m_OnReplaced = [this] { OnDocumentReplaced(); };

            m_IdSuffix = std::format("##{:016X}{:016X}", Guid.m_Instance.m_Value, Guid.m_Type.m_Value);
            e10::g_LibMgr.m_OnCompilationState.Register<&document_editor::OnCompilationState>(*this);
        }

        ~document_editor() noexcept override { e10::g_LibMgr.m_OnCompilationState.RemoveDelegates(this); }

        // ---- what a resource type overrides
        virtual void OnDocumentReplaced() noexcept {}           // an undo rebuilt the content
        virtual void OnCompileStarted()   noexcept {}           // a compile of this resource began: the compiled files are about to be replaced, let go of them
        virtual void OnCompiled()         noexcept {}           // a compile of this resource finished: reload what shows it
        virtual void OnCompileFailed()    noexcept {}           // a compile of this resource failed: the log says why

        IDocument&     getDocument() noexcept override { return m_Document; }
        xundo::system& getUndo()     noexcept override { return m_Undo; }
        bool           isLoaded() const noexcept override { return m_Document.isLoaded(); }

        // A panel's title; the id suffix keeps two open editors' panels apart.
        void AddPanel(const char* pName, dock Where, std::function<void()> Render, ImGuiWindowFlags Flags = 0) noexcept
        {
            m_Panels.push_back({ std::string(pName) + m_IdSuffix, Where, std::move(Render), Flags });
        }

        void OnCompilationState(e10::library_mgr&, e10::library::guid, xresource::full_guid Compiling, std::shared_ptr<e10::compilation::historical_entry::log>& Log) noexcept
        {
            if (Compiling != m_Document.m_Guid) return;
            if (m_CompilationLog.get() != Log.get()) m_CompilationLog = Log;
            if (!m_CompilationLog) return;
            e10::compilation::historical_entry::result Result;
            {
                xcontainer::lock::scope Lock(*m_CompilationLog);
                Result = m_CompilationLog->get().m_Result;
            }
            if (Result == e10::compilation::historical_entry::result::COMPILING || Result == e10::compilation::historical_entry::result::COMPILING_WARNINGS)
            {
                // Stamped here, on the compile thread, the moment the compile is seen: any compiled file older than this is stale. Stamping when the
                // main thread gets to the flag can be after a fast compile already wrote its files.
                SetCompileStartTime(std::filesystem::file_time_type::clock::now());
                m_bCompileStarted = true;
            }
            else if (Result == e10::compilation::historical_entry::result::SUCCESS || Result == e10::compilation::historical_entry::result::SUCCESS_WARNINGS)
                m_bCompiled = true;
            else if (Result == e10::compilation::historical_entry::result::FAILURE)
                m_bCompileFailed = true;
        }

        std::filesystem::file_time_type CompileStartTime() const noexcept { std::lock_guard Lock(m_CompileStartMutex); return m_CompileStart; }

        static void ToolbarSave(void* pUser) noexcept    { auto R = static_cast<document_editor*>(pUser)->m_Undo.Query("Save");    (void)R; }
        static void ToolbarCompile(void* pUser) noexcept { auto R = static_cast<document_editor*>(pUser)->m_Undo.Query("Compile"); (void)R; }

        void RenderToolbar() noexcept
        {
            toolbar_model Bar{};
            Bar.m_pUndo             = &m_Undo;
            Bar.m_bDirty            = m_Document.isDirty();
            Bar.m_bCanCompile       = m_Document.isLoaded();
            Bar.m_Log               = m_CompilationLog;
            Bar.m_pValidationErrors = &m_ValidationErrors;
            Bar.m_OnSave            = &document_editor::ToolbarSave;
            Bar.m_OnCompile         = &document_editor::ToolbarCompile;
            Bar.m_pUser             = this;
            RenderEditorToolbar(Bar);
        }

        void SetCompileStartTime(std::filesystem::file_time_type Time) noexcept { std::lock_guard Lock(m_CompileStartMutex); m_CompileStart = Time; }

        bool UsesDock(dock Where) const noexcept
        {
            return std::ranges::any_of(m_Panels, [&](const panel& P) { return P.m_Where == Where; });
        }

        void RenderPanels() noexcept
        {
            const auto WindowClass = DockClassForResource(m_Document.m_Guid);
            const ImGuiID DockId = ImGui::GetID((m_TypeName + "EditorDock").c_str());
            if (ImGui::DockBuilderGetNode(DockId) == nullptr)
            {
                ImGui::DockBuilderAddNode(DockId, ImGuiDockNodeFlags_DockSpace);
                ImGui::DockBuilderSetNodeSize(DockId, ImGui::GetContentRegionAvail());
                ImGuiID Left = 0, LeftBottom = 0, Right = 0, Center = DockId, Bottom = 0;
                if (UsesDock(dock::left))  { ImGuiID Rest; ImGui::DockBuilderSplitNode(Center, ImGuiDir_Left, 0.25f, &Left, &Rest); Center = Rest; }
                if (UsesDock(dock::right)) { ImGuiID Rest; ImGui::DockBuilderSplitNode(Center, ImGuiDir_Right, 0.35f, &Right, &Rest); Center = Rest; }
                if (UsesDock(dock::bottom)) { ImGuiID Rest; ImGui::DockBuilderSplitNode(Center, ImGuiDir_Down, 0.3f, &Bottom, &Rest); Center = Rest; }
                if (UsesDock(dock::left_bottom)) { ImGuiID Rest; ImGui::DockBuilderSplitNode(Left, ImGuiDir_Down, 0.5f, &LeftBottom, &Rest); Left = Rest; }
                for (auto& P : m_Panels)
                {
                    ImGuiID Id = Center;
                    switch (P.m_Where)
                    {
                    case dock::left:        Id = Left;       break;
                    case dock::left_bottom: Id = LeftBottom; break;
                    case dock::right:       Id = Right;      break;
                    case dock::bottom:      Id = Bottom;     break;
                    case dock::center:      break;
                    }
                    ImGui::DockBuilderDockWindow(P.m_Title.c_str(), Id);
                }
                ImGui::DockBuilderFinish(DockId);
            }
            ImGui::DockSpace(DockId, ImGui::GetContentRegionAvail(), ImGuiDockNodeFlags_None, &WindowClass);
            FinishFullEditorDockspace(DockId, m_Document.m_Guid);

            for (auto& P : m_Panels)
            {
                ImGui::SetNextWindowClass(&WindowClass);
                if (ImGui::Begin(P.m_Title.c_str(), nullptr, P.m_Flags)) P.m_Render();
                ImGui::End();
            }
        }

        // The whole editor window: a tab in the host's main dock space (icon + resource name) with the toolbar over a dock space of panels.
        void Render() noexcept override
        {
            char StableId[40];
            std::snprintf(StableId, sizeof(StableId), "%016llX%016llX", (unsigned long long)m_Document.m_Guid.m_Instance.m_Value, (unsigned long long)m_Document.m_Guid.m_Type.m_Value);
            char Title[256];
            FormatEditorRootTabTitle(Title, sizeof(Title), ResolveResourceDisplayName(m_Document.m_LibraryGuid, m_Document.m_Guid, m_TypeName.c_str()).c_str(), StableId);

            SetNextPeerEditorDockedInMainHost(ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(520, 640), ImGuiCond_FirstUseEver);
            PushFullEditorRootStyle();
            if (m_bRequestFocus) { ImGui::SetNextWindowFocus(); m_bRequestFocus = false; }

            ImGuiWindowFlags Flags = ImGuiWindowFlags_MenuBar;
#ifdef ImGuiWindowFlags_DockingAlwaysTabBar
            Flags |= ImGuiWindowFlags_DockingAlwaysTabBar;
#endif
            // What the compile did is handled every frame, whether or not this tab is the one showing: commands see the resource as it is now
            if (m_bCompileStarted.exchange(false)) OnCompileStarted();
            if (m_bCompiled) { m_bCompiled = false; OnCompiled(); }
            if (m_bCompileFailed) { m_bCompileFailed = false; OnCompileFailed(); }

            const bool bVisible = ImGui::Begin(Title, &m_bOpen, Flags);
            DrawEditorRootTabIcon(m_pDevice, m_Document.m_Guid.m_Type);          // every frame, even when the tab is not selected
            if (bVisible)
            {
                m_ValidationErrors.clear();
                if (m_Document.isLoaded()) m_Document.Validate(m_ValidationErrors);
                RenderToolbar();
                if (m_Document.isLoaded()) RenderPanels();
                else ImGui::Text("Failed to load the %s.", m_TypeName.c_str());
            }
            ImGui::End();
            PopFullEditorRootStyle();
        }
    };
}

#endif // XEDITOR_DOCUMENT_EDITOR_H
