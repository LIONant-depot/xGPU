#ifndef E19_MESH_MANAGER_H
#define E19_MESH_MANAGER_H
#pragma once

#include "dependencies/xprim_geom/source/xprim_geom.h"

namespace e19
{
    struct draw_vert
    {
        float           m_X, m_Y, m_Z;
        float           m_U, m_V;
        std::uint32_t   m_Color;
    };

    struct vert_2d
    {
        float           m_X, m_Y;
        float           m_U, m_V;
        std::uint32_t   m_Color;
    };

    struct mesh_manager
    {
        struct mesh
        {
            xgpu::buffer m_VertexBuffer {};
            xgpu::buffer m_IndexBuffer  {};
            int          m_IndexCount   {};
        };

        enum class model
        { CUBE
        , SPHERE
        , CAPSULE
        , CYLINDER
        , PLANE2D
        , ENUM_COUNT
        };

        void Init(xgpu::device& Device)
        {
            CreateCube(Device);
            CreateSphere(Device);
            CreateCapsule(Device);
            CreateCylinder(Device);
            Create2DPlane(Device);
        }

        void Rendering(xgpu::cmd_buffer& CmdBuffer, model Model)
        {
            auto& Mesh = m_Meshes[static_cast<int>(Model)];
            CmdBuffer.setBuffer(Mesh.m_VertexBuffer);
            CmdBuffer.setBuffer(Mesh.m_IndexBuffer);
            CmdBuffer.Draw(Mesh.m_IndexCount);
        }

        //CUBE
        void CreateCube(xgpu::device& Device)
        {
            const auto  Primitive   = xprim_geom::cube::Generate(4, 4, 4, 4, xprim_geom::float3{ 0.7f,0.7f,0.7f });
            mesh&       Mesh        = m_Meshes[static_cast<int>(model::CUBE)];

            Mesh.m_IndexCount = static_cast<int>(Primitive.m_Indices.size());

            if (auto Err = Device.Create(Mesh.m_VertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = sizeof(draw_vert), .m_EntryCount = static_cast<int>(Primitive.m_Vertices.size()) }); Err)
            {
                assert(false);
                exit(xgpu::getErrorInt(Err));
            }

            (void)Mesh.m_VertexBuffer.MemoryMap(0, static_cast<int>(Primitive.m_Vertices.size()), [&](void* pData)
            {
                auto pVertex = static_cast<draw_vert*>(pData);
                for (int i = 0; i < static_cast<int>(Primitive.m_Vertices.size()); ++i)
                {
                    auto&       V = pVertex[i];
                    const auto& v = Primitive.m_Vertices[i];
                    V.m_X = v.m_Position.m_X;
                    V.m_Y = v.m_Position.m_Y;
                    V.m_Z = v.m_Position.m_Z;
                    V.m_U = v.m_Texcoord.m_X;
                    V.m_V = v.m_Texcoord.m_Y;
                    V.m_Color = ~0;
                }
            });

            if (auto Err = Device.Create(Mesh.m_IndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = static_cast<int>(Primitive.m_Indices.size()) }); Err)
            {
                assert(false);
                exit(xgpu::getErrorInt(Err));
            }

            (void)Mesh.m_IndexBuffer.MemoryMap(0, static_cast<int>(Primitive.m_Indices.size()), [&](void* pData)
            {
                auto pIndex = static_cast<std::uint32_t*>(pData);
                for (int i = 0; i < static_cast<int>(Primitive.m_Indices.size()); ++i)
                {
                    pIndex[i] = Primitive.m_Indices[i];
                }
            });
        }

        //SPHERE
        void CreateSphere(xgpu::device& Device)
        {
            const auto  Primitive   = xprim_geom::uvsphere::Generate(70, 70, 1, 0.5f);
            mesh&       Mesh        = m_Meshes[static_cast<int>(model::SPHERE)];

            Mesh.m_IndexCount = static_cast<int>(Primitive.m_Indices.size());

            if (auto Err = Device.Create(Mesh.m_VertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = sizeof(draw_vert), .m_EntryCount = static_cast<int>(Primitive.m_Vertices.size()) }); Err)
            {
                assert(false);
                exit(xgpu::getErrorInt(Err));
            }

            (void)Mesh.m_VertexBuffer.MemoryMap(0, static_cast<int>(Primitive.m_Vertices.size()), [&](void* pData)
            {
                auto pVertex = static_cast<draw_vert*>(pData);
                for (int i = 0; i < static_cast<int>(Primitive.m_Vertices.size()); ++i)
                {
                    auto&       V = pVertex[i];
                    const auto& v = Primitive.m_Vertices[i];
                    V.m_X = v.m_Position.m_X;
                    V.m_Y = v.m_Position.m_Y;
                    V.m_Z = v.m_Position.m_Z;
                    V.m_U = v.m_Texcoord.m_X;
                    V.m_V = v.m_Texcoord.m_Y;
                    V.m_Color = ~0;
                }
            });

            if (auto Err = Device.Create(Mesh.m_IndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = static_cast<int>(Primitive.m_Indices.size()) }); Err)
            {
                assert(false);
                exit(xgpu::getErrorInt(Err));
            }

            (void)Mesh.m_IndexBuffer.MemoryMap(0, static_cast<int>(Primitive.m_Indices.size()), [&](void* pData)
            {
                auto pIndex = static_cast<std::uint32_t*>(pData);
                for (int i = 0; i < static_cast<int>(Primitive.m_Indices.size()); ++i)
                {
                    pIndex[i] = Primitive.m_Indices[i];
                }
            });
        }

        //CAPSULE
        void CreateCapsule(xgpu::device& Device)
        {
            const auto  Primitive   = xprim_geom::capsule::Generate(32, 32, 0.4f, 1.2f);
            mesh&       Mesh        = m_Meshes[static_cast<int>(model::CAPSULE)];

            Mesh.m_IndexCount = static_cast<int>(Primitive.m_Indices.size());

            if (auto Err = Device.Create(Mesh.m_VertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = sizeof(draw_vert), .m_EntryCount = static_cast<int>(Primitive.m_Vertices.size()) }); Err)
            {
                assert(false);
                exit(xgpu::getErrorInt(Err));
            }

            (void)Mesh.m_VertexBuffer.MemoryMap(0, static_cast<int>(Primitive.m_Vertices.size()), [&](void* pData)
            {
                auto pVertex = static_cast<draw_vert*>(pData);
                for (int i = 0; i < static_cast<int>(Primitive.m_Vertices.size()); ++i)
                {
                    auto&       V = pVertex[i];
                    const auto& v = Primitive.m_Vertices[i];
                    V.m_X = v.m_Position.m_X;
                    V.m_Y = v.m_Position.m_Y;
                    V.m_Z = v.m_Position.m_Z;
                    V.m_U = v.m_Texcoord.m_X;
                    V.m_V = v.m_Texcoord.m_Y;
                    V.m_Color = ~0;
                }
            });

            if (auto Err = Device.Create(Mesh.m_IndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = static_cast<int>(Primitive.m_Indices.size()) }); Err)
            {
                assert(false);
                exit(xgpu::getErrorInt(Err));
            }

            (void)Mesh.m_IndexBuffer.MemoryMap(0, static_cast<int>(Primitive.m_Indices.size()), [&](void* pData)
            {
                auto pIndex = static_cast<std::uint32_t*>(pData);
                for (int i = 0; i < static_cast<int>(Primitive.m_Indices.size()); ++i)
                {
                    pIndex[i] = Primitive.m_Indices[i];
                }
            });
        }

        //CYLINDER
        void CreateCylinder(xgpu::device& Device)
        {
            const auto  Primitive   = xprim_geom::cylinder::Generate(1, 64, 1.f, 0.3f, 0.3f);;
            mesh&       Mesh        = m_Meshes[static_cast<int>(model::CYLINDER)];

            Mesh.m_IndexCount = static_cast<int>(Primitive.m_Indices.size());

            if (auto Err = Device.Create(Mesh.m_VertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = sizeof(draw_vert), .m_EntryCount = static_cast<int>(Primitive.m_Vertices.size()) }); Err)
            {
                assert(false);
                exit(xgpu::getErrorInt(Err));
            }

            (void)Mesh.m_VertexBuffer.MemoryMap(0, static_cast<int>(Primitive.m_Vertices.size()), [&](void* pData)
            {
                auto pVertex = static_cast<draw_vert*>(pData);
                for (int i = 0; i < static_cast<int>(Primitive.m_Vertices.size()); ++i)
                {
                    auto&       V = pVertex[i];
                    const auto& v = Primitive.m_Vertices[i];
                    V.m_X = v.m_Position.m_X;
                    V.m_Y = v.m_Position.m_Y;
                    V.m_Z = v.m_Position.m_Z;
                    V.m_U = v.m_Texcoord.m_X;
                    V.m_V = v.m_Texcoord.m_Y;
                    V.m_Color = ~0;
                }
            });

            if (auto Err = Device.Create(Mesh.m_IndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = static_cast<int>(Primitive.m_Indices.size()) }); Err)
            {
                assert(false);
                exit(xgpu::getErrorInt(Err));
            }

            (void)Mesh.m_IndexBuffer.MemoryMap(0, static_cast<int>(Primitive.m_Indices.size()), [&](void* pData)
            {
                auto pIndex = static_cast<std::uint32_t*>(pData);
                for (int i = 0; i < static_cast<int>(Primitive.m_Indices.size()); ++i)
                {
                    pIndex[i] = Primitive.m_Indices[i];
                }
            });
        }

        void Create2DPlane(xgpu::device& Device)
        {
            mesh& Mesh = m_Meshes[static_cast<int>(model::PLANE2D)];
            Mesh.m_IndexCount = 6;

            if (auto Err = Device.Create(Mesh.m_VertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = sizeof(vert_2d), .m_EntryCount = 4 }); Err)
            {
                assert(false);
                exit(xgpu::getErrorInt(Err));
            }

            (void)Mesh.m_VertexBuffer.MemoryMap(0, 4, [&](void* pData)
            {
                auto pVertex = static_cast<vert_2d*>(pData);
                pVertex[0] = { -100.0f, -100.0f,  0.0f, 0.0f, 0xffffffff };
                pVertex[1] = {  100.0f, -100.0f,  1.0f, 0.0f, 0xffffffff };
                pVertex[2] = {  100.0f,  100.0f,  1.0f, 1.0f, 0xffffffff };
                pVertex[3] = { -100.0f,  100.0f,  0.0f, 1.0f, 0xffffffff };
            });

            if (auto Err = Device.Create(Mesh.m_IndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = Mesh.m_IndexCount }); Err)
            {
                assert(false);
                exit(xgpu::getErrorInt(Err));
            }

            (void)Mesh.m_IndexBuffer.MemoryMap(0, Mesh.m_IndexCount, [&](void* pData)
            {
                auto            pIndex = static_cast<std::uint32_t*>(pData);
                constexpr auto  StaticIndex = std::array
                {
                    2u,  1u,  0u,      3u,  2u,  0u,    // front
                };
                static_assert(StaticIndex.size() == 6);
                for (auto i : StaticIndex)
                {
                    *pIndex = i;
                    pIndex++;
                }
            });
        }

        std::array<mesh, static_cast<int>(mesh_manager::model::ENUM_COUNT)> m_Meshes;
    };
}

#endif