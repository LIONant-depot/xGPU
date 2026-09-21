#ifndef XEDITOR_DESCRIPTOR_EDITOR_H
#define XEDITOR_DESCRIPTOR_EDITOR_H
#pragma once

// What every "one resource descriptor, one window" editor shares: the document (the descriptor on disk), the undoable commands
// that edit it (also the way an AI or a script edits it), the compile feedback, and the window (toolbar, a dock space of panels
// isolated from every other editor, an inspector bound to the descriptor). A resource type derives from descriptor_editor and
// adds its panels (preview, extra inspectors) and its own commands.
#include "source/Tools/Editor/xeditor_resource_editor.h"
#include "source/Tools/Editor/xeditor_inspector.h"
#include "source/Tools/Editor/xeditor_toolbar.h"
#include "source/Tools/Editor/xeditor_resource_tab.h"
#include "dependencies/xeditor/include/xeditor/full_editor_shell.h"
#include "dependencies/xeditor/include/xeditor/commands.h"
#include "dependencies/xeditor/include/xeditor/serialize.h"
#include "dependencies/xstrtool/source/xstrtool.h"
#include "source/tools/xgpu_imgui_breach.h"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <format>
#include <sstream>

namespace xeditor
{
    //--------------------------------------------------------------------------------------------
    // Document
    //--------------------------------------------------------------------------------------------
    struct descriptor_document : IDocument
    {
        xresource::full_guid                                    m_Guid          = {};
        e10::library::guid                                      m_LibraryGuid   = {};
        std::wstring                                            m_DescriptorPath;       // <resource>.desc\Descriptor.txt
        std::wstring                                            m_ResourcePath;         // the compiled resource (may not exist yet)
        std::wstring                                            m_LogPath;              // the compiler's log folder (Details.txt lives there)
        std::unique_ptr<xresource_pipeline::descriptor::base>   m_pDescriptor;
        bool                                                    m_bDirty        = false;
        std::function<void()>                                   m_OnReplaced;           // the descriptor object was rebuilt (an undo): rebind whatever points at it

        xresource::full_guid getGuid() const noexcept override { return m_Guid; }

        std::string getDisplayName() const noexcept override
        {
            return ResolveResourceDisplayName(m_LibraryGuid, m_Guid, "<resource>");
        }

        bool Load() noexcept override
        {
            std::wstring InfoPath;
            e10::g_LibMgr.getNodeInfo(m_LibraryGuid, m_Guid, [&](e10::library_db::info_node& Node) { InfoPath = Node.m_Path; });
            if (InfoPath.empty()) return false;
            GeneratePaths(InfoPath);

            auto* pFactory = xresource_pipeline::factory_base::Find(m_Guid.m_Type);
            if (!pFactory) return false;
            m_pDescriptor = pFactory->CreateDescriptor();

            // A resource that was just created has no descriptor file yet: start from the defaults.
            if (std::filesystem::exists(m_DescriptorPath))
            {
                xproperty::settings::context Context;
                if (auto Err = m_pDescriptor->Serialize(true, m_DescriptorPath, Context); Err) { m_pDescriptor.reset(); return false; }
            }
            m_bDirty = false;
            return true;
        }

        std::string Save() noexcept override
        {
            if (!m_pDescriptor) return "no descriptor loaded";
            xproperty::settings::context Context;
            if (auto Err = m_pDescriptor->Serialize(false, m_DescriptorPath, Context); Err) return std::string(Err.getMessage());
            e10::g_LibMgr.MakeDescriptorDirty({ m_LibraryGuid.m_Instance }, m_Guid);     // this is what queues the compile
            m_bDirty = false;
            return {};
        }

        bool isDirty() const noexcept override { return m_bDirty; }

        // The whole descriptor as its own file text: the "before" state of any command that cannot say how to invert itself.
        std::string Snapshot() noexcept
        {
            if (!m_pDescriptor) return {};
            const std::wstring Tmp = SnapshotPath();
            xproperty::settings::context Context;
            if (m_pDescriptor->Serialize(false, Tmp, Context)) return {};
            std::ifstream In(Tmp, std::ios::binary);
            std::stringstream Text; Text << In.rdbuf();
            In.close();
            std::filesystem::remove(Tmp);
            return Text.str();
        }

        // Replaces the descriptor with a snapshot's state. The object is rebuilt, so m_OnReplaced tells the editor to rebind.
        bool Restore(const std::string& Text) noexcept
        {
            auto* pFactory = xresource_pipeline::factory_base::Find(m_Guid.m_Type);
            if (!pFactory || Text.empty()) return false;
            const std::wstring Tmp = SnapshotPath();
            { std::ofstream Out(Tmp, std::ios::binary | std::ios::trunc); Out << Text; }
            auto pNew = pFactory->CreateDescriptor();
            xproperty::settings::context Context;
            const bool bOk = !static_cast<bool>(pNew->Serialize(true, Tmp, Context));
            std::filesystem::remove(Tmp);
            if (!bOk) return false;
            m_pDescriptor = std::move(pNew);
            m_bDirty = true;
            if (m_OnReplaced) m_OnReplaced();
            return true;
        }

    private:
        std::wstring SnapshotPath() const noexcept
        {
            return (std::filesystem::temp_directory_path() / std::format(L"xeditor_snapshot_{:016X}.txt", m_Guid.m_Instance.m_Value)).wstring();
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
        }
    };

    //--------------------------------------------------------------------------------------------
    // Commands - all on the editor's own undo system, so `Name\Command` reaches them from the console.
    //--------------------------------------------------------------------------------------------
    namespace descriptor_cmds
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
                return StringToAny(Out, TypeGuid, std::span<char>(Text.data(), Text.size()));      // parses hex without throwing
            return false;
        }

        inline bool FindProperty(xproperty::base& Object, const std::string& Path, xproperty::any& Out) noexcept
        {
            bool bFound = false;
            xproperty::settings::context Context;
            xproperty::sprop::collector(&Object, *Object.getProperties(), Context, [&](const char* pName, xproperty::any&& Data, const xproperty::type::members&, bool, const void*) noexcept
            {
                if (!bFound && Path == pName) { Out = std::move(Data); bFound = true; }
            });
            return bFound;
        }

        inline bool SetValue(descriptor_document& Doc, const std::string& Path, std::uint32_t TypeGuid, const std::string& Text) noexcept
        {
            xproperty::any Value;
            if (!ParseValue(Value, TypeGuid, Text)) return false;
            std::string Error;
            xproperty::settings::context Context;
            xproperty::sprop::setProperty(Error, Doc.m_pDescriptor.get(), *Doc.m_pDescriptor->getProperties(), xproperty::sprop::container::prop{ Path, Value }, Context);
            Doc.m_bDirty = true;
            return Error.empty();
        }

        //---- SetProperty: one property to a new value. Before is what an inspector edit passes (the edit is already applied when the
        // command runs); without it the current value is the before.
        struct set_property_cmd : xundo::command_base
        {
            descriptor_document& m_Doc;
            set_property_cmd(xundo::system& System, descriptor_document& Doc) noexcept : command_base(System, "SetProperty", nullptr), m_Doc(Doc) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Sets one descriptor property (undoable). Usage: SetProperty -Path base64 -Value base64 [-Before base64]. Lists resize with a path ending in []."; }
            void RegisterArguments() noexcept override
            {
                m_hPath   = m_Parser.addOption("Path",   "Property path, base64",         true,  1);
                m_hValue  = m_Parser.addOption("Value",  "New value, base64",             true,  1);
                m_hBefore = m_Parser.addOption("Before", "Previous value, base64",        false, 1);
            }
            std::string Redo() noexcept override
            {
                std::string Path, Value;
                if (!GetArg(m_Parser, m_hPath, Path) || !GetArg(m_Parser, m_hValue, Value)) return "SetProperty: bad arguments";
                if (!m_Doc.m_pDescriptor) return "SetProperty: nothing loaded";
                Path  = Base64Decode(Path);
                Value = Base64Decode(Value);

                xproperty::any Current;
                if (!FindProperty(*m_Doc.m_pDescriptor, Path, Current)) return std::format("SetProperty: no property '{}'", Path);
                if (!SetValue(m_Doc, Path, Current.getTypeGuid(), Value)) return std::format("SetProperty: '{}' does not take '{}'", Path, Value);
                return {};
            }
            void BackupCurrenState(xundo::undo_file& File) noexcept override
            {
                std::string Path, Before;
                std::uint32_t TypeGuid = 0;
                if (GetArg(m_Parser, m_hPath, Path) && m_Doc.m_pDescriptor)
                {
                    Path = Base64Decode(Path);
                    xproperty::any Current;
                    if (FindProperty(*m_Doc.m_pDescriptor, Path, Current))
                    {
                        TypeGuid = Current.getTypeGuid();
                        Before   = FormatValue(Current);
                    }
                }
                if (std::string Given; GetArg(m_Parser, m_hBefore, Given)) Before = Base64Decode(Given);
                File.Write(TypeGuid);
                WriteString(File, Path);
                WriteString(File, Before);
            }
            void Undo(xundo::undo_file& File) noexcept override
            {
                std::uint32_t TypeGuid = 0; File.Read(TypeGuid);
                const std::string Path   = ReadString(File);
                const std::string Before = ReadString(File);
                if (TypeGuid && m_Doc.m_pDescriptor) SetValue(m_Doc, Path, TypeGuid, Before);
            }
            xcmdline::parser::handle m_hPath, m_hValue, m_hBefore;
        };

        //---- SnapshotEdit: an inspector edit that touched several properties at once (an array insert, delete or reorder). The
        // inspector reports it as two text snapshots of the whole object; this replays them.
        struct snapshot_edit_cmd : xundo::command_base
        {
            descriptor_document& m_Doc;
            snapshot_edit_cmd(xundo::system& System, descriptor_document& Doc) noexcept : command_base(System, "SnapshotEdit", nullptr), m_Doc(Doc) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Applies an inspector snapshot edit (undoable). Usage: SnapshotEdit -Label base64 -Before base64 -After base64"; }
            void RegisterArguments() noexcept override
            {
                m_hLabel  = m_Parser.addOption("Label",  "What the edit did, base64",   true, 1);
                m_hBefore = m_Parser.addOption("Before", "State before, base64",        true, 1);
                m_hAfter  = m_Parser.addOption("After",  "State after, base64",         true, 1);
            }
            void Apply(const std::string& Snapshot) noexcept
            {
                xproperty::settings::context Context;
                xproperty::ui::undo::ApplySnapshotFromString(*m_Doc.m_pDescriptor->getProperties(), m_Doc.m_pDescriptor.get(), Snapshot, Context);
                m_Doc.m_bDirty = true;
            }
            std::string Redo() noexcept override
            {
                std::string After;
                if (!GetArg(m_Parser, m_hAfter, After)) return "SnapshotEdit: bad arguments";
                if (!m_Doc.m_pDescriptor) return "SnapshotEdit: nothing loaded";
                Apply(Base64Decode(After));
                return {};
            }
            void BackupCurrenState(xundo::undo_file& File) noexcept override
            {
                std::string Before;
                GetArg(m_Parser, m_hBefore, Before);
                WriteString(File, Base64Decode(Before));
            }
            void Undo(xundo::undo_file& File) noexcept override
            {
                const std::string Before = ReadString(File);
                if (m_Doc.m_pDescriptor) Apply(Before);
            }
            xcmdline::parser::handle m_hLabel, m_hBefore, m_hAfter;
        };

        //---- Queries: save, compile, undo, redo (xundo keeps queries apart from edits, and neither is itself undoable)
        // Every property of the descriptor with its value, one per line: the paths SetProperty takes.
        struct list_properties_cmd : xundo::query_command_base
        {
            descriptor_document& m_Doc;
            list_properties_cmd(xundo::system& System, descriptor_document& Doc) noexcept : query_command_base(System, "ListProperties", nullptr), m_Doc(Doc) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Lists the descriptor's properties (path = value), the paths SetProperty takes. Usage: ListProperties [-Filter text]"; }
            void RegisterArguments() noexcept override { m_hFilter = m_Parser.addOption("Filter", "Only paths containing this text", false, 1); }
            std::string Query() noexcept override
            {
                if (!m_Doc.m_pDescriptor) return "ListProperties: nothing loaded";
                std::string Filter;
                GetArg(m_Parser, m_hFilter, Filter);

                std::string Out;
                xproperty::settings::context Context;
                xproperty::sprop::collector(m_Doc.m_pDescriptor.get(), *m_Doc.m_pDescriptor->getProperties(), Context, [&](const char* pName, xproperty::any&& Data, const xproperty::type::members& Member, bool, const void*) noexcept
                {
                    if (!Data.m_pType || !IsAtomicType(Data.getTypeGuid()) || std::string_view(pName).find(Filter) == std::string_view::npos) return;
                    if (std::holds_alternative<xproperty::type::members::scope>(Member.m_Variant) || std::holds_alternative<xproperty::type::members::props>(Member.m_Variant)) return;
                    Out += std::format("{} = {}\n", pName, FormatValue(Data));
                });
                return Out.empty() ? "(no properties)" : Out;
            }
            xcmdline::parser::handle m_hFilter;
        };

        struct save_cmd : xundo::query_command_base
        {
            descriptor_document& m_Doc;
            save_cmd(xundo::system& System, descriptor_document& Doc) noexcept : query_command_base(System, "Save", nullptr), m_Doc(Doc) {}
            const char* getCommandHelp() const noexcept override { return "Saves the descriptor (which queues its compile). Usage: Save"; }
            void RegisterArguments() noexcept override {}
            std::string Query() noexcept override { auto Err = m_Doc.Save(); return Err.empty() ? "Save: saved" : "Save: " + Err; }
        };

        struct compile_cmd : xundo::query_command_base
        {
            descriptor_document&      m_Doc;
            std::vector<std::string>& m_Errors;
            compile_cmd(xundo::system& System, descriptor_document& Doc, std::vector<std::string>& Errors) noexcept : query_command_base(System, "Compile", nullptr), m_Doc(Doc), m_Errors(Errors) {}
            const char* getCommandHelp() const noexcept override { return "Validates the descriptor, saves it and queues the compile. Usage: Compile"; }
            void RegisterArguments() noexcept override {}
            std::string Query() noexcept override
            {
                m_Errors.clear();
                if (!m_Doc.m_pDescriptor) return "Compile: nothing loaded";
                m_Doc.m_pDescriptor->Validate(m_Errors);
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

    //--------------------------------------------------------------------------------------------
    // The editor
    //--------------------------------------------------------------------------------------------
    struct descriptor_editor : resource_editor
    {
        enum class dock { left, center, right };

        struct panel
        {
            std::string             m_Title;        // window title with the resource's id, unique per editor
            dock                    m_Where;
            std::function<void()>   m_Render;       // the window's content
        };

        descriptor_document                     m_Document;
        xundo::system                           m_Undo;
        descriptor_cmds::set_property_cmd       m_SetProperty;
        descriptor_cmds::snapshot_edit_cmd      m_SnapshotEdit;
        descriptor_cmds::list_properties_cmd    m_ListProperties;
        descriptor_cmds::save_cmd               m_Save;
        std::vector<std::string>                m_ValidationErrors;
        descriptor_cmds::compile_cmd            m_Compile;
        descriptor_cmds::undo_cmd               m_UndoCmd;
        descriptor_cmds::redo_cmd               m_RedoCmd;

        xgpu::device*                           m_pDevice = nullptr;
        std::string                             m_TypeName;
        std::string                             m_IdSuffix;                 // "##<instance><type>"
        std::vector<panel>                      m_Panels;
        inspector_panel                         m_DescriptorInspector{ "Description" };
        bool                                    m_bCompiled = false;        // a compile finished OK: OnCompiled runs on the next frame
        std::shared_ptr<e10::compilation::historical_entry::log> m_CompilationLog = std::make_shared<e10::compilation::historical_entry::log>(
            e10::compilation::historical_entry::communication{ .m_Result = e10::compilation::historical_entry::result::SUCCESS });

        descriptor_editor(const char* pTypeName, xresource::full_guid Guid, e10::library::guid LibraryGuid, xgpu::device* pDevice) noexcept
            : m_SetProperty(m_Undo, m_Document), m_SnapshotEdit(m_Undo, m_Document), m_ListProperties(m_Undo, m_Document), m_Save(m_Undo, m_Document)
            , m_Compile(m_Undo, m_Document, m_ValidationErrors), m_UndoCmd(m_Undo), m_RedoCmd(m_Undo)
            , m_pDevice(pDevice), m_TypeName(pTypeName)
        {
            m_Document.m_Guid        = Guid;
            m_Document.m_LibraryGuid = LibraryGuid;
            if (auto Err = m_Undo.Init({}, false); !Err.empty()) printf("%s editor undo Init: %s\n", pTypeName, Err.c_str());
            m_Document.Load();
            m_Document.m_OnReplaced = [this] { BindDescriptorInspector(); OnDescriptorReplaced(); };

            m_IdSuffix = std::format("##{:016X}{:016X}", Guid.m_Instance.m_Value, Guid.m_Type.m_Value);
            e10::g_LibMgr.m_OnCompilationState.Register<&descriptor_editor::OnCompilationState>(*this);
            BindDescriptorInspector();
        }

        ~descriptor_editor() noexcept override { e10::g_LibMgr.m_OnCompilationState.RemoveDelegates(this); }

        // ---- what a resource type overrides
        virtual void OnDescriptorReplaced() noexcept {}         // an undo rebuilt the descriptor object
        virtual void OnCompiled()           noexcept {}         // a compile of this resource finished: reload what shows it

        IDocument&     getDocument() noexcept override { return m_Document; }
        xundo::system& getUndo()     noexcept override { return m_Undo; }
        bool           isLoaded() const noexcept override { return m_Document.m_pDescriptor != nullptr; }

        // A panel's title; the id suffix keeps two open editors' panels apart.
        void AddPanel(const char* pName, dock Where, std::function<void()> Render) noexcept
        {
            m_Panels.push_back({ std::string(pName) + m_IdSuffix, Where, std::move(Render) });
        }

        void BindDescriptorInspector() noexcept
        {
            m_DescriptorInspector.Clear();
            if (!m_Document.m_pDescriptor) return;
            m_DescriptorInspector.m_Inspector.AppendEntity();
            m_DescriptorInspector.AppendComponent(*m_Document.m_pDescriptor->getProperties(), m_Document.m_pDescriptor.get());
            m_DescriptorInspector.m_Inspector.m_OnChangeEvent.m_Delegates.clear();
            m_DescriptorInspector.m_Inspector.m_OnChangeEvent.Register<&descriptor_editor::OnDescriptorChange>(*this);
        }

        // The inspector has already applied the edit: report it as a command so it is undoable, logged and the same as a typed one.
        void OnDescriptorChange(xproperty::inspector&, const xproperty::ui::undo::cmd& Cmd) noexcept
        {
            m_Document.m_bDirty = true;
            using descriptor_cmds::FormatValue;
            using descriptor_cmds::IsAtomicType;

            if (Cmd.m_NewValue.is<std::string>() && Cmd.m_Original.is<std::string>() && !Cmd.m_Name.empty() && Cmd.m_Name.find('/') == std::string::npos)
            {
                // An edit bracket (array insert / delete / reorder): both values are whole-object snapshots.
                const std::string Line = std::format("SnapshotEdit -Label {} -Before {} -After {}", Base64Encode(Cmd.m_Name)
                    , Base64Encode(Cmd.m_Original.get<std::string>()), Base64Encode(Cmd.m_NewValue.get<std::string>()));
                LogConsole(std::format("SnapshotEdit \"{}\"", Cmd.m_Name), log_source::User);
                if (auto Err = m_Undo.Execute(Line); !Err.empty()) NotifyError(std::format("edit failed: {}", Err));
                return;
            }

            if (!Cmd.m_NewValue.m_pType || !Cmd.m_Original.m_pType || !IsAtomicType(Cmd.m_NewValue.getTypeGuid())) return;   // not something a command can carry
            Run(m_Undo, std::format("SetProperty -Path {} -Value {} -Before {}", Base64Encode(Cmd.m_Name)
                , Base64Encode(FormatValue(Cmd.m_NewValue)), Base64Encode(FormatValue(Cmd.m_Original))));
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
            if (Result == e10::compilation::historical_entry::result::SUCCESS || Result == e10::compilation::historical_entry::result::SUCCESS_WARNINGS)
                m_bCompiled = true;
        }

        static void ToolbarSave(void* pUser) noexcept    { auto R = static_cast<descriptor_editor*>(pUser)->m_Undo.Query("Save");    (void)R; }
        static void ToolbarCompile(void* pUser) noexcept { auto R = static_cast<descriptor_editor*>(pUser)->m_Undo.Query("Compile"); (void)R; }

        void RenderToolbar() noexcept
        {
            toolbar_model Bar{};
            Bar.m_pUndo             = &m_Undo;
            Bar.m_bDirty            = m_Document.isDirty();
            Bar.m_bCanCompile       = m_Document.m_pDescriptor != nullptr;
            Bar.m_Log               = m_CompilationLog;
            Bar.m_pValidationErrors = &m_ValidationErrors;
            Bar.m_OnSave            = &descriptor_editor::ToolbarSave;
            Bar.m_OnCompile         = &descriptor_editor::ToolbarCompile;
            Bar.m_pUser             = this;
            RenderEditorToolbar(Bar);
        }

        void RenderPanels() noexcept
        {
            const auto WindowClass = DockClassForResource(m_Document.m_Guid);
            const ImGuiID DockId = ImGui::GetID((m_TypeName + "EditorDock").c_str());
            if (ImGui::DockBuilderGetNode(DockId) == nullptr)
            {
                ImGui::DockBuilderAddNode(DockId, ImGuiDockNodeFlags_DockSpace);
                ImGui::DockBuilderSetNodeSize(DockId, ImGui::GetContentRegionAvail());
                ImGuiID Left = 0, Right = 0, Center = 0;
                ImGui::DockBuilderSplitNode(DockId, ImGuiDir_Left, 0.25f, &Left, &Center);
                ImGui::DockBuilderSplitNode(Center, ImGuiDir_Right, 0.35f, &Right, &Center);
                for (auto& P : m_Panels)
                    ImGui::DockBuilderDockWindow(P.m_Title.c_str(), P.m_Where == dock::left ? Left : P.m_Where == dock::right ? Right : Center);
                ImGui::DockBuilderFinish(DockId);
            }
            ImGui::DockSpace(DockId, ImGui::GetContentRegionAvail(), ImGuiDockNodeFlags_None, &WindowClass);
            FinishFullEditorDockspace(DockId, m_Document.m_Guid);

            for (auto& P : m_Panels)
            {
                ImGui::SetNextWindowClass(&WindowClass);
                if (ImGui::Begin(P.m_Title.c_str())) P.m_Render();
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
            const bool bVisible = ImGui::Begin(Title, &m_bOpen, Flags);
            DrawEditorRootTabIcon(m_pDevice, m_Document.m_Guid.m_Type);          // every frame, even when the tab is not selected
            if (bVisible)
            {
                if (m_Document.m_pDescriptor)
                {
                    m_ValidationErrors.clear();
                    m_Document.m_pDescriptor->Validate(m_ValidationErrors);
                }
                if (m_bCompiled) { m_bCompiled = false; OnCompiled(); }
                RenderToolbar();
                if (m_Document.m_pDescriptor) RenderPanels();
                else ImGui::Text("Failed to load the %s descriptor.", m_TypeName.c_str());
            }
            ImGui::End();
            PopFullEditorRootStyle();
        }
    };
}

#endif // XEDITOR_DESCRIPTOR_EDITOR_H
