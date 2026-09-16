// sc_git_lfs_smoke.cpp
//
// Manual smoke test for GitLfsWorkspaceSession, run directly against a real
// Git repository. Not wired into the Visual Studio solution (yet) -- Phase 1
// verification only needs a standalone compile+run, not full build
// integration; that's Phase 2's concern if this ever needs to be permanent.
//
// Usage:
//   sc_git_lfs_smoke <path-to-git-repo> [file-to-prepare-edit-on]
//
// Build (from this directory):
//   g++ -std=c++20 -Wall -Wextra -Wpedantic sc_git_lfs_smoke.cpp -o sc_git_lfs_smoke.exe

#include "../sc_git_lfs_provider.hpp"

#include <cstdio>

using namespace sc;
using namespace sc::git_lfs;

namespace
{
    const char* ToString(ErrorCode code)
    {
        switch (code)
        {
            case ErrorCode::None: return "None";
            case ErrorCode::InvalidArgument: return "InvalidArgument";
            case ErrorCode::WorkspaceNotFound: return "WorkspaceNotFound";
            case ErrorCode::PathNotFound: return "PathNotFound";
            case ErrorCode::LockedByOther: return "LockedByOther";
            case ErrorCode::AuthenticationRequired: return "AuthenticationRequired";
            case ErrorCode::Offline: return "Offline";
            case ErrorCode::OutOfDate: return "OutOfDate";
            case ErrorCode::Conflict: return "Conflict";
            case ErrorCode::Unsupported: return "Unsupported";
            case ErrorCode::ProviderProtocolError: return "ProviderProtocolError";
            case ErrorCode::ProviderInternalError: return "ProviderInternalError";
        }
        return "Unknown";
    }

    const char* ToString(SubmitOutcome outcome)
    {
        switch (outcome)
        {
            case SubmitOutcome::Published: return "Published";
            case SubmitOutcome::CompletedLocally: return "CompletedLocally";
            case SubmitOutcome::RemoteStateUnknown: return "RemoteStateUnknown";
            case SubmitOutcome::Failed: return "Failed";
        }
        return "Unknown";
    }

    void PrintCapabilities(const SessionCapabilities& caps)
    {
        std::printf("  sourceControlAvailable=%s lockingBackendAvailable=%s lockingAvailable=%s\n",
            caps.sourceControlAvailable ? "true" : "false",
            caps.lockingBackendAvailable ? "true" : "false",
            caps.lockingAvailable ? "true" : "false");
    }

    void PrintStatus(const StatusResult& status)
    {
        if (status.error)
        {
            std::printf("  GetStatus error [%s]: %s\n",
                ToString(status.error->code), status.error->message.c_str());
            return;
        }
        if (status.files.empty())
        {
            std::printf("  (no files reported)\n");
        }
        for (const auto& f : status.files)
        {
            std::printf("  %s%s%s%s %s%s\n",
                f.staged ? "[staged]" : "",
                f.modified ? "[modified]" : "",
                f.untracked ? "[untracked]" : "",
                f.conflicted ? "[conflicted]" : "",
                f.path.relative.string().c_str(),
                f.lfsTracked ? " (LFS)" : "");
        }
    }

    void PrintPrepareEdit(const PrepareEditResult& result)
    {
        for (const auto& f : result.files)
        {
            if (!f.OperationSucceeded())
            {
                std::printf("  PrepareEdit FAILED for %s: [%s] %s\n",
                    f.path.relative.string().c_str(),
                    ToString(f.error->code), f.error->message.c_str());
                continue;
            }

            std::printf("  PrepareEdit OK for %s -- actions: %s%s%s\n",
                f.path.relative.string().c_str(),
                HasFlag(f.actions, EditActionFlags::LocalIntentRecorded) ? "LocalIntentRecorded " : "",
                HasFlag(f.actions, EditActionFlags::MadeWritable) ? "MadeWritable " : "",
                HasFlag(f.actions, EditActionFlags::LockAcquired) ? "LockAcquired " : "");

            std::printf("    coordination: %s\n",
                f.coordination.effects == CoordinationEffectFlags::None
                    ? "None (expected for non-LFS files, or when locking was skipped)"
                    : "VisibleToOtherUsers+PreventsOtherPublish (server-enforced LFS lock)");

            for (const auto& w : f.warnings)
            {
                std::printf("    Warning: %s\n", w.c_str());
            }
        }
    }

    void PrintSubmit(const SubmitResult& result)
    {
        std::printf("  Submit outcome: %s\n", ToString(result.Summary()));
        if (result.localRevision) std::printf("  Local revision:  %s\n", result.localRevision->c_str());
        if (result.remoteRevision) std::printf("  Remote revision: %s\n", result.remoteRevision->c_str());
        for (const auto& w : result.warnings) std::printf("  Warning: %s\n", w.c_str());
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "Usage: %s <path-to-git-repo> [file-to-prepare-edit-on]\n", argv[0]);
        return 1;
    }

    const std::filesystem::path repoRoot = argv[1];
    GitLfsWorkspaceSession workspace(repoRoot);

    std::printf("== Connect ==\n");
    if (auto err = workspace.Connect())
    {
        std::printf("  Connect reported: [%s] %s\n", ToString(err->code), err->message.c_str());
        if (err->code == ErrorCode::WorkspaceNotFound)
        {
            return 1; // fatal: not a git repo at all
        }
        // Unsupported (LFS missing) is a soft warning; continue the demo.
    }
    std::printf("  Session capabilities:\n");
    PrintCapabilities(workspace.Capabilities());

    std::printf("\n== GetStatus (all paths) ==\n");
    PrintStatus(workspace.GetStatus(StatusRequest{}));

    if (argc >= 3)
    {
        const WorkspacePath target{ std::filesystem::path(argv[2]) };

        std::printf("\n== PrepareEdit(%s), lockRequirement=Try ==\n", argv[2]);
        PrepareEditRequest prepareRequest;
        prepareRequest.paths = { target };
        prepareRequest.policy.lockRequirement = LockRequirement::Try;
        PrintPrepareEdit(workspace.PrepareEdit(prepareRequest));

        std::printf("\n== Add(%s) ==\n", argv[2]);
        AddRequest addRequest;
        addRequest.paths = { target };
        const auto addResult = workspace.Add(addRequest);
        std::printf("  Add %s\n", addResult.AllSucceeded() ? "succeeded" : "reported failures");

        std::printf("\n== ListLocks (all) ==\n");
        const auto listResult = workspace.ListLocks(ListLocksRequest{});
        if (listResult.error) std::printf("  ListLocks error: [%s] %s\n", ToString(listResult.error->code), listResult.error->message.c_str());
        else if (listResult.locks.empty()) std::printf("  (no locks)\n");
        else for (const auto& L : listResult.locks)
            std::printf("  %s owner=%s ownership=%s\n", L.path.relative.string().c_str(), L.ownerDisplayName.c_str(),
                L.ownership == LockOwnership::CurrentUser ? "CurrentUser" : L.ownership == LockOwnership::OtherUser ? "OtherUser" : "Unknown");

        std::printf("\n== Unlock(%s) [expected: nothing to release if PrepareEdit didn't acquire one] ==\n", argv[2]);
        UnlockRequest unlockRequest;
        unlockRequest.paths = { target };
        const auto unlockResult = workspace.Unlock(unlockRequest);
        for (const auto& item : unlockResult.items)
        {
            if (item.Succeeded()) std::printf("  Unlocked %s\n", item.item.relative.string().c_str());
            else std::printf("  Unlock FAILED for %s: [%s] %s\n",
                item.item.relative.string().c_str(), ToString(item.error->code), item.error->message.c_str());
        }

        std::printf("\n== Submit ==\n");
        SubmitRequest submitRequest;
        submitRequest.paths = { target };
        submitRequest.description = "sc_git_lfs_smoke: manual smoke test commit";
        PrintSubmit(workspace.Submit(submitRequest));

        std::printf("\n== Push (nothing new -- exercises the standalone path) ==\n");
        const auto pushResult = workspace.Push(PushRequest{});
        std::printf("  Push %s\n", pushResult.succeeded ? "succeeded" : "failed");
        if (pushResult.error) std::printf("  Error: [%s] %s\n", ToString(pushResult.error->code), pushResult.error->message.c_str());
        if (!pushResult.summary.empty()) std::printf("  Summary: %s\n", pushResult.summary.c_str());
    }
    else
    {
        std::printf("\n(No file argument given -- skipping PrepareEdit/Add/Submit demo. "
                     "Pass a relative path to a tracked file to exercise those.)\n");
    }

    return 0;
}
