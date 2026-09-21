#ifndef XEDITOR_DESCRIPTOR_EDITOR_H
#define XEDITOR_DESCRIPTOR_EDITOR_H
#pragma once

// A resource editor whose document is a descriptor (an xresource_pipeline descriptor: reflected properties). On top of document_editor it adds the
// undoable commands that edit a descriptor (also the way an AI or a script edits it) and an inspector bound to it. A resource type derives from
// descriptor_editor and adds its panels (preview, extra inspectors) and its own commands.
#include "source/Tools/Editor/xeditor_document_editor.h"

namespace xeditor
{
    //--------------------------------------------------------------------------------------------
    // Document
    //--------------------------------------------------------------------------------------------
    struct descriptor_document : file_document
    {
        std::unique_ptr<xresource_pipeline::descriptor::base>   m_pDescriptor;

        bool isLoaded() const noexcept override { return m_pDescriptor != nullptr; }

        bool Reset() noexcept override
        {
            auto* pFactory = xresource_pipeline::factory_base::Find(m_Guid.m_Type);
            if (!pFactory) return false;
            m_pDescriptor = pFactory->CreateDescriptor();
            return true;
        }

        bool ReplaceFromFile(const std::wstring& Path) noexcept override
        {
            auto* pFactory = xresource_pipeline::factory_base::Find(m_Guid.m_Type);
            if (!pFactory) return false;
            auto pNew = pFactory->CreateDescriptor();
            xproperty::settings::context Context;
            if (pNew->Serialize(true, Path, Context)) return false;
            m_pDescriptor = std::move(pNew);
            return true;
        }

        bool WriteToFile(const std::wstring& Path) noexcept override
        {
            xproperty::settings::context Context;
            return m_pDescriptor && !m_pDescriptor->Serialize(false, Path, Context);
        }

        void Validate(std::vector<std::string>& Errors) const noexcept override { if (m_pDescriptor) m_pDescriptor->Validate(Errors); }

        cmd_util::property_target target() noexcept { return { m_pDescriptor->getProperties(), m_pDescriptor.get() }; }
    };

    //--------------------------------------------------------------------------------------------
    // Commands
    //--------------------------------------------------------------------------------------------
    namespace descriptor_cmds
    {
        using namespace cmd_util;

        // SetProperty: one property to a new value. Before is what an inspector edit passes (the edit is already applied when the command runs);
        // without it the current value is the before.
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
                if (!m_Doc.isLoaded()) return "SetProperty: nothing loaded";
                Path  = Base64Decode(Path);
                Value = Base64Decode(Value);

                xproperty::any Current;
                if (!FindProperty(m_Doc.target(), Path, Current)) return std::format("SetProperty: no property '{}'", Path);
                if (!SetValue(m_Doc.target(), Path, Current.getTypeGuid(), Value)) return std::format("SetProperty: '{}' does not take '{}'", Path, Value);
                m_Doc.m_bDirty = true;
                return {};
            }
            void BackupCurrenState(xundo::undo_file& File) noexcept override
            {
                std::string Path, Before;
                std::uint32_t TypeGuid = 0;
                if (GetArg(m_Parser, m_hPath, Path) && m_Doc.isLoaded())
                {
                    Path = Base64Decode(Path);
                    xproperty::any Current;
                    if (FindProperty(m_Doc.target(), Path, Current))
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
                if (TypeGuid && m_Doc.isLoaded()) { SetValue(m_Doc.target(), Path, TypeGuid, Before); m_Doc.m_bDirty = true; }
            }
            xcmdline::parser::handle m_hPath, m_hValue, m_hBefore;
        };

        // SnapshotEdit: an inspector edit that touched several properties at once (an array insert, delete or reorder). The inspector reports it
        // as two text snapshots of the whole object; this replays them.
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
                if (!m_Doc.isLoaded()) return "SnapshotEdit: nothing loaded";
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
                if (m_Doc.isLoaded()) Apply(Before);
            }
            xcmdline::parser::handle m_hLabel, m_hBefore, m_hAfter;
        };

        // Every property of the descriptor with its value, one per line: the paths SetProperty takes.
        struct list_properties_cmd : xundo::query_command_base
        {
            descriptor_document& m_Doc;
            list_properties_cmd(xundo::system& System, descriptor_document& Doc) noexcept : query_command_base(System, "ListProperties", nullptr), m_Doc(Doc) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Lists the descriptor's properties (path = value), the paths SetProperty takes. Usage: ListProperties [-Filter text]"; }
            void RegisterArguments() noexcept override { m_hFilter = m_Parser.addOption("Filter", "Only paths containing this text", false, 1); }
            std::string Query() noexcept override
            {
                if (!m_Doc.isLoaded()) return "ListProperties: nothing loaded";
                std::string Filter;
                GetArg(m_Parser, m_hFilter, Filter);
                return ListProperties(m_Doc.target(), Filter);
            }
            xcmdline::parser::handle m_hFilter;
        };
    }

    //--------------------------------------------------------------------------------------------
    // The editor
    //--------------------------------------------------------------------------------------------
    struct descriptor_editor : document_editor<descriptor_document>
    {
        descriptor_cmds::set_property_cmd       m_SetProperty;
        descriptor_cmds::snapshot_edit_cmd      m_SnapshotEdit;
        descriptor_cmds::list_properties_cmd    m_ListProperties;
        inspector_panel                         m_DescriptorInspector{ "Description" };

        descriptor_editor(const char* pTypeName, xresource::full_guid Guid, e10::library::guid LibraryGuid, xgpu::device* pDevice) noexcept
            : document_editor(pTypeName, Guid, LibraryGuid, pDevice)
            , m_SetProperty(m_Undo, m_Document), m_SnapshotEdit(m_Undo, m_Document), m_ListProperties(m_Undo, m_Document)
        {
            BindDescriptorInspector();
        }

        virtual void OnDescriptorReplaced() noexcept {}         // an undo rebuilt the descriptor object
        virtual bool OnCustomChange(const xproperty::ui::undo::cmd&) noexcept { return false; }     // an edit that needs its own command: true when it took it

        void OnDocumentReplaced() noexcept override { BindDescriptorInspector(); OnDescriptorReplaced(); }

        void BindDescriptorInspector() noexcept
        {
            m_DescriptorInspector.Clear();
            if (!m_Document.isLoaded()) return;
            m_DescriptorInspector.m_Inspector.AppendEntity();
            m_DescriptorInspector.AppendComponent(*m_Document.m_pDescriptor->getProperties(), m_Document.m_pDescriptor.get());
            m_DescriptorInspector.m_Inspector.m_OnChangeEvent.m_Delegates.clear();
            m_DescriptorInspector.m_Inspector.m_OnChangeEvent.Register<&descriptor_editor::OnDescriptorChange>(*this);
        }

        // The inspector has already applied the edit: report it as a command so it is undoable, logged and the same as a typed one.
        void OnDescriptorChange(xproperty::inspector&, const xproperty::ui::undo::cmd& Cmd) noexcept
        {
            m_Document.m_bDirty = true;
            if (OnCustomChange(Cmd)) return;
            using cmd_util::FormatValue;
            using cmd_util::IsAtomicType;

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
    };
}

#endif // XEDITOR_DESCRIPTOR_EDITOR_H
