#pragma once
// The frame map (measurement): one frame's work as the GPU ran it - each render pass, dispatch and copy in order, its GPU time, its draws
// and the buffers and textures it read and wrote, by name. XGPU_FRAME_MAP=<file>: when <file>.trigger appears, every window records its
// next frame and appends it to <file> as one JSON line (tools: frame_map.py draws the passes and their dependencies). Nothing is recorded
// otherwise: the hooks test one bool.
#include <cstdio>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include "../xgpu_env.h"

namespace xgpu::vulkan
{
    struct frame_map
    {
        struct entry
        {
            const char*                 m_pKind     = "";
            std::string                 m_Name      {};
            std::vector<std::string>    m_Reads     {};
            std::vector<std::string>    m_Writes    {};
            std::vector<std::string>    m_RW        {};         // storage buffers: read or written, not known
            int                         m_Draws     = 0;
            int                         m_Indirect  = 0;
            std::uint32_t               m_Q0        = ~0u, m_Q1 = ~0u;
        };
        static constexpr std::uint32_t max_queries_v = 8192;

        bool                m_bOn       = false;        // this frame records
        std::uint64_t       m_Seen      = 0;            // the device's arm count this window has captured
        VkQueryPool         m_Pool      = VK_NULL_HANDLE;
        std::uint32_t       m_Q         = 0;
        std::vector<entry>  m_Entries   {};
        int                 m_Open      = -1;           // the render pass entry open (draws and binds go there)

        static const char* Path( void ) noexcept { static const char* p = xgpu::details::Env("XGPU_FRAME_MAP"); return p; }

        static std::string Name( const char* p ) { return p && *p ? std::string(p) : std::string("(no name)"); }

        entry* Current( void ) noexcept { return m_bOn && m_Open >= 0 ? &m_Entries[std::size_t(m_Open)] : nullptr; }

        std::uint32_t Stamp( VkCommandBuffer Cmd, VkPipelineStageFlagBits Stage ) noexcept
        {
            if( m_Q >= max_queries_v ) return ~0u;
            vkCmdWriteTimestamp(Cmd, Stage, m_Pool, m_Q);
            return m_Q++;
        }

        // At the start of a window's command buffer: armed (the trigger file seen by any window) and not yet captured here: record this frame
        void Begin( VkDevice Device, const VkAllocationCallbacks* pAlloc, VkCommandBuffer Cmd, std::uint64_t& DeviceArm ) noexcept
        {
            m_bOn = false;
            const char* pPath = Path();
            if( pPath == nullptr ) return;
            std::error_code Ec;
            const std::string Trigger = std::string(pPath) + ".trigger";
            if( std::filesystem::exists(Trigger, Ec) && std::filesystem::remove(Trigger, Ec) ) ++DeviceArm;
            if( DeviceArm == m_Seen ) return;
            m_Seen = DeviceArm;
            if( m_Pool == VK_NULL_HANDLE )
            {
                const VkQueryPoolCreateInfo Info{ .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = max_queries_v };
                if( vkCreateQueryPool(Device, &Info, pAlloc, &m_Pool) != VK_SUCCESS ) return;
            }
            vkCmdResetQueryPool(Cmd, m_Pool, 0, max_queries_v);
            m_Q = 0; m_Entries.clear(); m_Open = -1; m_bOn = true;
        }

        entry& Add( const char* pKind ) { m_Entries.push_back({ .m_pKind = pKind }); return m_Entries.back(); }

        // After the frame's submission: wait for it, read the times, append the frame as one JSON line
        void End( VkDevice Device, int WindowId, float TimestampPeriodNs ) noexcept
        {
            if( !m_bOn ) return;
            m_bOn = false;
            vkDeviceWaitIdle(Device);
            std::vector<std::uint64_t> T(m_Q ? m_Q : 1, 0);
            if( m_Q ) vkGetQueryPoolResults(Device, m_Pool, 0, m_Q, T.size() * sizeof(std::uint64_t), T.data(), sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
            const std::uint64_t T0 = m_Q ? T[0] : 0;
            const auto Ms = [&](std::uint32_t q) { return q < m_Q ? double(T[q] - T0) * double(TimestampPeriodNs) * 1e-6 : -1.0; };
            const auto Esc = [](const std::string& s) { std::string o; for( char c : s ) { if( c == '"' || c == '\\' ) o += '\\'; o += c; } return o; };
            for( auto& E : m_Entries ) for( auto* pV : { &E.m_Reads, &E.m_Writes, &E.m_RW } ) { std::sort(pV->begin(), pV->end()); pV->erase(std::unique(pV->begin(), pV->end()), pV->end()); }
            FILE* f = nullptr;
            if( fopen_s(&f, Path(), "a") != 0 || f == nullptr ) return;
            std::fprintf(f, "{\"window\":%d,\"entries\":[", WindowId);
            for( std::size_t i = 0; i < m_Entries.size(); ++i )
            {
                const auto& E = m_Entries[i];
                std::fprintf(f, "%s{\"kind\":\"%s\",\"name\":\"%s\",\"t0\":%.4f,\"t1\":%.4f,\"draws\":%d,\"indirect\":%d,\"reads\":[", i ? "," : "", E.m_pKind, Esc(E.m_Name).c_str(), Ms(E.m_Q0), Ms(E.m_Q1), E.m_Draws, E.m_Indirect);
                for( std::size_t r = 0; r < E.m_Reads.size(); ++r ) std::fprintf(f, "%s\"%s\"", r ? "," : "", Esc(E.m_Reads[r]).c_str());
                std::fprintf(f, "],\"writes\":[");
                for( std::size_t w = 0; w < E.m_Writes.size(); ++w ) std::fprintf(f, "%s\"%s\"", w ? "," : "", Esc(E.m_Writes[w]).c_str());
                std::fprintf(f, "],\"rw\":[");
                for( std::size_t w = 0; w < E.m_RW.size(); ++w ) std::fprintf(f, "%s\"%s\"", w ? "," : "", Esc(E.m_RW[w]).c_str());
                std::fprintf(f, "]}");
            }
            std::fprintf(f, "]}\n");
            std::fclose(f);
            m_Entries.clear();
        }
    };
}
