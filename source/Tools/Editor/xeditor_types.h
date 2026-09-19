#ifndef XEDITOR_TYPES_H
#define XEDITOR_TYPES_H
#pragma once

// Core, author-facing types for the shared editor/viewer/headless framework - see
// Build/EDITOR_VIEWER_FRAMEWORK_PROBLEM_STATEMENT.md for the full design rationale. Deliberately
// minimal per direct user instruction: identity is just a resource's own xresource::full_guid (no
// separate "session id"), and a resource-type author only ever implements two things - IDocument
// (data/load/save/commands, its own private xundo::system) and IUI (rendering, reused unmodified
// across a full editor's main view, an embedded preview, or a thumbnail render target). The
// framework supplies everything else: open-instance tracking (lives on the Asset Manager, not
// here), dock isolation, the command console, and the headless host.
#include "dependencies/xundo/source/xundo_system.h"
#include "dependencies/xresource_guid/source/xresource_guid.h"
#include <memory>
#include <functional>

namespace xeditor
{
    // One xundo::system per open resource - undo/command scope is per-document-instance, never
    // global, never shared across two open resources (direct user requirement). The Asset
    // Browser, Source Control, and every other cross-cutting service stay exactly as globally
    // shared as they are today; only the editing side gets this per-instance treatment.
    struct IDocument
    {
        virtual ~IDocument() = default;

        virtual xresource::full_guid getGuid() const noexcept = 0;

        // Loads from the resource's own on-disk descriptor via the existing asset pipeline.
        virtual bool Load() noexcept = 0;

        // Persists the current (possibly dirty) state back through the existing asset pipeline.
        // Returns false with a human-readable reason on failure (matches this codebase's own
        // command-return-string convention).
        virtual std::string Save() noexcept = 0;

        virtual bool isDirty() const noexcept = 0;

        // Every mutating/query operation for this document goes through here - the SAME command
        // implementation serves the graphical UI, the headless console, and automated tests; a
        // mutation reachable only from an ImGui callback is a framework violation, not a shortcut.
        xundo::system m_Undo;
    };

    // Optional - zero or more per IDocument. Renders a document into whatever target the host
    // hands it (a dock panel, a small ImGui child region, or - once the offscreen-readback gap is
    // closed, a later pass - an offscreen texture for thumbnails). Same implementation every
    // time; a headless session never constructs one at all.
    struct IUI
    {
        virtual ~IUI() = default;

        // Called once per frame by whichever host owns the target region. `bEmbedded` lets a
        // single IUI implementation know it's being hosted inside someone else's window (reduced
        // chrome, read-only by default per the problem statement's §7.3) versus owning its own
        // top-level dock panel.
        virtual void Render(IDocument& Doc, bool bEmbedded) noexcept = 0;
    };

    // Per-type registration record. Deliberately code-based (a plugin's own header calls
    // Register() at static-init time) rather than a new text-config format for this first slice -
    // a persisted "HasEditor" declarative flag in resource_pipeline.config.txt, matching how
    // DebugCompiler/IconPaths are already plugin-owned config, is real follow-up work, explicitly
    // deferred for now rather than risking getting pipeline_plugin's text-serialization format
    // wrong under time/token pressure.
    struct editor_descriptor
    {
        xresource::type_guid                                   m_TypeGuid;
        std::function<std::unique_ptr<IDocument>(xresource::full_guid)> m_CreateDocument;
        std::function<std::unique_ptr<IUI>()>                  m_CreateUI;          // optional - may be empty
        bool                                                   m_bSupportsHeadless = true;
    };
}

#endif // XEDITOR_TYPES_H
