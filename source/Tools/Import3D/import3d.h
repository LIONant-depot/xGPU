
#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"

#include "import3d_geom.h"
#include "import3d_animation.h"
#include "import3d_skeleton.h"

#include "dependencies/xstrtool/source/xstrtool.h"

#pragma message("***NOTE*** Import3d.h is adding to the program the assimp-vc143-mt.lib library")
#pragma comment( lib, "dependencies/assimp/BINARIES/Win32/lib/Release/assimp-vc143-mt.lib")

#include <functional>

namespace import3d
{
    template< typename T>
    struct scope_exit
    {
        explicit scope_exit(T&& func) : m_Func(std::forward<decltype(func)>(func)) {}
        ~scope_exit() { m_Func(); }

        T m_Func;
    };

    class importer
    {
    public:

        bool Import( std::wstring FileName, import3d::geom* pGeom, import3d::skeleton* pSkeleton, import3d::anim_package* pAnimPackage ) noexcept
        {
            scope_exit CleanUp([&]
            {
                //
                // Set everything back to null
                //
                m_pAnimPackage = nullptr;
                m_pGeom        = nullptr;
                m_pSkeleton    = nullptr;
                m_pScene       = nullptr;
            });

            auto Importer = std::make_unique<Assimp::Importer>();
            
            m_pAnimPackage = pAnimPackage;
            m_pGeom        = pGeom;
            m_pSkeleton    = pSkeleton;

            if(m_pGeom) m_pGeom->m_FileName = FileName;

            m_pScene = Importer->ReadFile( xstrtool::To(FileName).c_str()
                , aiProcess_Triangulate                // Make sure we get triangles rather than nvert polygons
                | aiProcess_LimitBoneWeights           // 4 weights for skin model max
                | aiProcess_GenUVCoords                // Convert any type of mapping to uv mapping
                | aiProcess_TransformUVCoords          // preprocess UV transformations (scaling, translation ...)
                | aiProcess_FindInstances              // search for instanced meshes and remove them by references to one master
                | aiProcess_GenNormals                 // if it does not have normals generate them... (this may not be a good option as it may hide issues from artist)
                | aiProcess_CalcTangentSpace           // calculate tangents and bitangents if possible (definetly you will meed UVs)
//                | aiProcess_JoinIdenticalVertices      // join identical vertices/ optimize indexing (It seems to be creating cracks in the mesh... some bug?)
                | aiProcess_RemoveRedundantMaterials   // remove redundant materials
                | aiProcess_FindInvalidData            // detect invalid model data, such as invalid normal vectors
                | aiProcess_FlipUVs                    // flip the V to match the Vulkans way of doing UVs
            );
            if( m_pScene == nullptr ) return true;

            if( SanityCheck() ) return true;

            if(m_pSkeleton)
            {
                ImportSkeleton();
            }

            if(m_pAnimPackage) 
            {
                // to deal with animations we must be able to load the skeleton
                assert(m_pSkeleton);
                ImportAnimations();
            }

            if(m_pGeom)
            {
                ImportGeometry();
                ImportMaterials();
            }

            return false;
        }

        //------------------------------------------------------------------------------------------------------
        // TYPES
        //------------------------------------------------------------------------------------------------------
    protected:

        struct refs
        {
            std::vector<const aiNode*>      m_Nodes;
        };

        struct myMeshPart
        {
            std::string                     m_MeshName;
            std::string                     m_Name;
            std::vector<import3d::vertex>   m_Vertices;
            std::vector<int>                m_Indices;
            int                             m_iMaterialInstance;
        };

        //------------------------------------------------------------------------------------------------------
        // PRIVATE FUNCTIONS
        //------------------------------------------------------------------------------------------------------
    protected:

        bool SanityCheck() noexcept
        {
            m_MeshReferences.resize(m_pScene->mNumMeshes);

            std::function<void(const aiNode& Node)> ProcessNode = [&](const aiNode& Node) noexcept
            {
                for (auto i = 0u, end = Node.mNumMeshes; i < end; ++i)
                {
                    aiMesh* pMesh = m_pScene->mMeshes[Node.mMeshes[i]];

                    m_MeshReferences[Node.mMeshes[i]].m_Nodes.push_back(&Node);
                }

                for (auto i = 0u; i < Node.mNumChildren; ++i)
                {
                    ProcessNode(*Node.mChildren[i]);
                }
            };

            ProcessNode( *m_pScene->mRootNode );

            for (auto iMesh = 0u; iMesh < m_pScene->mNumMeshes; ++iMesh)
            {
                const aiMesh& AssimpMesh = *m_pScene->mMeshes[iMesh];
                const auto&   Refs       = m_MeshReferences[iMesh].m_Nodes;

                if (Refs.size() == 0u)
                {
                    printf("ERROR: I had a mesh but no reference to it in the scene... very strange\n");
                    return true;
                }

                if(AssimpMesh.HasBones())
                {
                    if (Refs.size() > 1 )
                    {
                        printf("ERROR: I had a skin mesh (%s) that is reference in the scene %zd times. We don't support this feature.\n", AssimpMesh.mName.C_Str(), Refs.size() );
                        return true;
                    }
                }
                else
                {
                    if (Refs.size() > 1)
                    {
                        printf("INFO: I will be duplicating mesh %s, %zd times\n", AssimpMesh.mName.C_Str(), Refs.size());
                    }
                }
            }

            return false;
        }

        //------------------------------------------------------------------------------------------------------

        std::string GetMeshNameFromNode(const aiNode& Node)
        {
            for( auto pNode = &Node; pNode; pNode = pNode->mParent )
            {
                // Using the naming convention to group meshes...
                if (xstrtool::findI(pNode->mName.C_Str(), "MESH_") != std::string::npos)
                {
                    return pNode->mName.C_Str();
                }
            }

            return {};
        }

        //------------------------------------------------------------------------------------------------------

        void ImportMaterials() noexcept
        {
            std::unordered_map<int,int>             AssimMaterialToGeomMaterial;
            std::unordered_map<std::wstring, int>   TextureToIndex;

            // Handle the samplers and textures
            auto HandleSampler = [&](import3d::material_instance::sampler& Sampler, aiString& TexPath, aiTextureMapMode UMap, aiTextureMapMode VMap)
            {
                if(std::wstring FilePath = xstrtool::To(TexPath.C_Str());  FilePath.empty() )
                {
                    Sampler.m_iTexture = -1;
                }
                else
                {
                    if (auto I = TextureToIndex.find(FilePath); I == TextureToIndex.end())
                    {
                        Sampler.m_iTexture = static_cast<int>(m_pGeom->m_TexturePaths.size());
                        TextureToIndex[FilePath] = Sampler.m_iTexture;
                        m_pGeom->m_TexturePaths.emplace_back( std::move(FilePath) );
                        xstrtool::PathNormalize(m_pGeom->m_TexturePaths.back());
                    }
                    else
                    {
                        Sampler.m_iTexture = I->second;
                    }
                }

                static constexpr auto Address = [](aiTextureMapMode A) constexpr
                {
                    switch (A)
                    {
                    case aiTextureMapMode_Decal: 
                    case aiTextureMapMode_Clamp:    return import3d::material_instance::address_mode::CLAMP;
                    case aiTextureMapMode_Mirror:   return import3d::material_instance::address_mode::MIRROR;
                    }

                    return import3d::material_instance::address_mode::TILE;
                };

                Sampler.m_UMode = Address(UMap);
                Sampler.m_VMode = Address(VMap);
            };

            // Go through all the submeshes
            for( auto& Mesh    : m_pGeom->m_Mesh )
            for (auto& Submesh : Mesh.m_Submeshes )
            {
                if (auto I = AssimMaterialToGeomMaterial.find(Submesh.m_iMaterial); I != AssimMaterialToGeomMaterial.end())
                {
                    Submesh.m_iMaterial = I->second;
                    continue;
                }

                AssimMaterialToGeomMaterial[Submesh.m_iMaterial] = static_cast<int>(m_pGeom->m_MaterialInstance.size());

                auto  pcMat = m_pScene->mMaterials[Submesh.m_iMaterial];
                auto& MatI  = m_pGeom->m_MaterialInstance.emplace_back();

                MatI.m_Name = pcMat->GetName().C_Str();

                // Shading model
                {
                    int ShadingModel = -1;
                    aiGetMaterialInteger(pcMat, AI_MATKEY_SHADING_MODEL, (int*)&ShadingModel);
                    switch (ShadingModel)
                    {
                        case aiShadingMode_Gouraud: 
                        case aiShadingMode_Flat:
                        case aiShadingMode_Phong:
                        case aiShadingMode_Blinn:       MatI.m_ShadingModel = import3d::material_instance::shading_model::GOURAUD; break;
                        case aiShadingMode_Toon:        MatI.m_ShadingModel = import3d::material_instance::shading_model::TOON; break;
                        case aiShadingMode_NoShading:   MatI.m_ShadingModel = import3d::material_instance::shading_model::UNLIGHT; break;
                        case aiShadingMode_OrenNayar:
                        case aiShadingMode_Minnaert:
                        case aiShadingMode_Fresnel:
                        case aiShadingMode_CookTorrance:
                        case aiShadingMode_PBR_BRDF:    MatI.m_ShadingModel = import3d::material_instance::shading_model::PBR; break;
                        default:                        MatI.m_ShadingModel = import3d::material_instance::shading_model::UNKOWN; break;
                    }
                }

                // Diffuse Color
                {
                    aiColor4D C(1, 1, 1, 1);
                    aiGetMaterialColor(pcMat, AI_MATKEY_COLOR_DIFFUSE, (aiColor4D*)&C);
                    MatI.m_DiffuseColor.setupFromRGBA(C.r, C.g, C.b, C.a);
                }

                // Specular Color
                {
                    aiColor4D C(0, 0, 0, 1);
                    aiGetMaterialColor(pcMat, AI_MATKEY_COLOR_SPECULAR, (aiColor4D*)&C);
                    MatI.m_SpecularColor.setupFromRGBA(C.r, C.g, C.b, C.a);
                }

                // Ambient Color
                {
                    aiColor4D C(0, 0, 0, 1);
                    aiGetMaterialColor(pcMat, AI_MATKEY_COLOR_AMBIENT, (aiColor4D*)&C);
                    MatI.m_AmbientColor.setupFromRGBA(C.r, C.g, C.b, C.a);
                }

                // Emissive Color
                {
                    aiColor4D C(0, 0, 0, 1);
                    aiGetMaterialColor(pcMat, AI_MATKEY_COLOR_EMISSIVE, (aiColor4D*)&C);
                    MatI.m_EmmisiveColor.setupFromRGBA(C.r, C.g, C.b, C.a);
                }

                // Opacity float
                {
                    MatI.m_OpacityFactor = 1;
                    aiGetMaterialFloat(pcMat, AI_MATKEY_OPACITY, &MatI.m_OpacityFactor);
                }

                // Shininess float
                {
                    MatI.m_ShininessFactor = 0;
                    aiGetMaterialFloat(pcMat, AI_MATKEY_SHININESS, &MatI.m_ShininessFactor);
                }

                // Shininess strength float
                {
                    MatI.m_ShininessStreanthFactor = 0;
                    aiGetMaterialFloat(pcMat, AI_MATKEY_SHININESS_STRENGTH, &MatI.m_ShininessStreanthFactor);
                }

                // Diffuse Texture
                {
                    aiString         szPath;
                    aiTextureMapMode mapU(aiTextureMapMode_Wrap), mapV(aiTextureMapMode_Wrap);
                    if (AI_SUCCESS != aiGetMaterialString(pcMat, AI_MATKEY_TEXTURE_DIFFUSE(0), &szPath))
                    {
                        for (std::uint32_t i = 0; i < pcMat->mNumProperties; ++i)
                        {
                            const auto& Props = *pcMat->mProperties[i];
                            if (Props.mType == aiPTI_String)
                            {
                                if (Props.mSemantic != aiTextureType_NONE)
                                {
                                    if(Props.mSemantic == aiTextureType_BASE_COLOR)
                                    {
                                        szPath = *(aiString*)Props.mData;
                                        break;
                                    }
                                    else if (Props.mSemantic == aiTextureType_UNKNOWN)
                                    {
                                        if (xstrtool::findI(((aiString*)Props.mData)->C_Str(), "_Base_Color") != std::string::npos)
                                        {
                                            szPath = *(aiString*)Props.mData;
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    aiGetMaterialInteger(pcMat, AI_MATKEY_MAPPINGMODE_U_DIFFUSE(0), (int*)&mapU);
                    aiGetMaterialInteger(pcMat, AI_MATKEY_MAPPINGMODE_V_DIFFUSE(0), (int*)&mapV);
                    HandleSampler(MatI.m_DiffuseSampler, szPath, mapU, mapV);
                }

                // Specular Texture
                {
                    aiString         szPath;
                    aiTextureMapMode mapU(aiTextureMapMode_Wrap), mapV(aiTextureMapMode_Wrap);
                    aiGetMaterialString(pcMat, AI_MATKEY_TEXTURE_SPECULAR(0), &szPath);
                    aiGetMaterialInteger(pcMat, AI_MATKEY_MAPPINGMODE_U_SPECULAR(0), (int*)&mapU);
                    aiGetMaterialInteger(pcMat, AI_MATKEY_MAPPINGMODE_V_SPECULAR(0), (int*)&mapV);
                    HandleSampler(MatI.m_SpecularSampler, szPath, mapU, mapV);
                }

                // Opacity Texture
                {
                    aiString         szPath;
                    aiTextureMapMode mapU(aiTextureMapMode_Wrap), mapV(aiTextureMapMode_Wrap);
                    if (AI_SUCCESS == aiGetMaterialString(pcMat, AI_MATKEY_TEXTURE_OPACITY(0), &szPath))
                    {
                        aiGetMaterialInteger(pcMat, AI_MATKEY_MAPPINGMODE_U_OPACITY(0), (int*)&mapU);
                        aiGetMaterialInteger(pcMat, AI_MATKEY_MAPPINGMODE_V_OPACITY(0), (int*)&mapV);
                        HandleSampler(MatI.m_OpacitySampler, szPath, mapU, mapV);
                    }
                    else
                    {
                        int flags = 0;
                        aiGetMaterialInteger(pcMat, AI_MATKEY_TEXFLAGS_DIFFUSE(0), &flags);

                        if( MatI.m_DiffuseSampler.m_iTexture != -1 
                           && !(flags & aiTextureFlags_IgnoreAlpha)
                           && true // HasAlphaPixels(pcMesh->piDiffuseTexture)
                          )
                          {
                              MatI.m_OpacitySampler = MatI.m_DiffuseSampler;
                          }
                    }
                }

                // Ambient Texture
                {
                    aiString         szPath;
                    aiTextureMapMode mapU(aiTextureMapMode_Wrap), mapV(aiTextureMapMode_Wrap);
                    aiGetMaterialString(pcMat, AI_MATKEY_TEXTURE_AMBIENT(0), &szPath);
                    aiGetMaterialInteger(pcMat, AI_MATKEY_MAPPINGMODE_U_AMBIENT(0), (int*)&mapU);
                    aiGetMaterialInteger(pcMat, AI_MATKEY_MAPPINGMODE_V_AMBIENT(0), (int*)&mapV);
                    HandleSampler(MatI.m_AmbientSampler, szPath, mapU, mapV);
                }

                // Emmisive Texture
                {
                    aiString         szPath;
                    aiTextureMapMode mapU(aiTextureMapMode_Wrap), mapV(aiTextureMapMode_Wrap);
                    aiGetMaterialString(pcMat, AI_MATKEY_TEXTURE_EMISSIVE(0), &szPath);
                    aiGetMaterialInteger(pcMat, AI_MATKEY_MAPPINGMODE_U_EMISSIVE(0), (int*)&mapU);
                    aiGetMaterialInteger(pcMat, AI_MATKEY_MAPPINGMODE_V_EMISSIVE(0), (int*)&mapV);
                    HandleSampler(MatI.m_EmissiveSampler, szPath, mapU, mapV);
                }

                // Shininess Texture
                {
                    aiString         szPath;
                    aiTextureMapMode mapU(aiTextureMapMode_Wrap), mapV(aiTextureMapMode_Wrap);
                    aiGetMaterialString(pcMat, AI_MATKEY_TEXTURE_SHININESS(0), &szPath);
                    aiGetMaterialInteger(pcMat, AI_MATKEY_MAPPINGMODE_U_SHININESS(0), (int*)&mapU);
                    aiGetMaterialInteger(pcMat, AI_MATKEY_MAPPINGMODE_V_SHININESS(0), (int*)&mapV);
                    HandleSampler(MatI.m_ShininessSampler, szPath, mapU, mapV);
                }

                // Lightmap Texture
                {
                    aiString         szPath;
                    aiTextureMapMode mapU(aiTextureMapMode_Wrap), mapV(aiTextureMapMode_Wrap);
                    aiGetMaterialString(pcMat, AI_MATKEY_TEXTURE_LIGHTMAP(0), &szPath);
                    aiGetMaterialInteger(pcMat, AI_MATKEY_MAPPINGMODE_U_LIGHTMAP(0), (int*)&mapU);
                    aiGetMaterialInteger(pcMat, AI_MATKEY_MAPPINGMODE_V_LIGHTMAP(0), (int*)&mapV);
                    HandleSampler(MatI.m_LightmapSampler, szPath, mapU, mapV);
                }

                // Normal Texture
                {
                    aiString         szPath;
                    aiTextureMapMode mapU(aiTextureMapMode_Wrap), mapV(aiTextureMapMode_Wrap);
                    aiGetMaterialString(pcMat, AI_MATKEY_TEXTURE_NORMALS(0), &szPath);
                    aiGetMaterialInteger(pcMat, AI_MATKEY_MAPPINGMODE_U_NORMALS(0), (int*)&mapU);
                    aiGetMaterialInteger(pcMat, AI_MATKEY_MAPPINGMODE_V_NORMALS(0), (int*)&mapV);
                    HandleSampler(MatI.m_NormalSampler, szPath, mapU, mapV);
                }

                // Height Texture
                {
                    aiString         szPath;
                    aiTextureMapMode mapU(aiTextureMapMode_Wrap), mapV(aiTextureMapMode_Wrap);
                    aiGetMaterialString(pcMat, AI_MATKEY_TEXTURE_HEIGHT(0), &szPath);
                    aiGetMaterialInteger(pcMat, AI_MATKEY_MAPPINGMODE_U_HEIGHT(0), (int*)&mapU);
                    aiGetMaterialInteger(pcMat, AI_MATKEY_MAPPINGMODE_V_HEIGHT(0), (int*)&mapV);
                    HandleSampler(MatI.m_HeightSampler, szPath, mapU, mapV);
                }
            }
        }

        //------------------------------------------------------------------------------------------------------

        bool ImportGeometryValidateMesh(const aiMesh& AssimpMesh, int& iTexture )
        {
            if (AssimpMesh.HasPositions() == false)
            {
                printf("WARNING: Found a mesh (%s) without position! mesh will be removed\n", AssimpMesh.mName.C_Str());
                return true;
            }

            if (AssimpMesh.HasFaces() == false)
            {
                printf("WARNING: Found a mesh (%s) without position! mesh will be removed\n", AssimpMesh.mName.C_Str());
                return true;
            }

            if (AssimpMesh.HasNormals() == false)
            {
                printf("WARNING: Found a mesh (%s) without normals! mesh will be removed\n", AssimpMesh.mName.C_Str());
                return true;
            }

            if (AssimpMesh.HasTangentsAndBitangents() == false)
            {
                printf("WARNING: Found a mesh (%s) without Tangets! We will create fake tangets.. but it will look bad!\n", AssimpMesh.mName.C_Str());
            }

            if (AssimpMesh.GetNumUVChannels() != 1)
            {
                if (AssimpMesh.GetNumUVChannels() == 0)
                {
                    printf("WARNING: Found a mesh (%s) without UVs we will assign 0,0 to all uvs\n", AssimpMesh.mName.C_Str());
                }
                else
                {
                    printf("WARNING: Found a mesh (%s) without too many UV chanels we will use only one...\n", AssimpMesh.mName.C_Str());
                }
            }

            iTexture = [&]()->int
            {
                for (auto i = 0u; i < AssimpMesh.GetNumUVChannels(); ++i)
                    if (AssimpMesh.HasTextureCoords(i)) return i;
                return -1;
            }();

            /*
            if (AssimpMesh.GetNumColorChannels() > 1)
            {
                printf("WARNING: Found a mesh with too many color channels we will use only one...");
            }

            const int iColors = [&]
            {
                for (int i = 0; i < AssimpMesh.GetNumColorChannels(); ++i)
                    if (AssimpMesh.HasVertexColors(i)) return i;
                return -1;
            }();
            */

            return false;
        }

        //------------------------------------------------------------------------------------------------------

        void ImportGeometryStatic(std::vector<myMeshPart>& MyNodes)
        {
            auto ProcessMesh = [&]( const aiMesh& AssimpMesh, const aiMatrix4x4& Transform, myMeshPart& MeshPart, const int iTexCordinates )
            {
                // get the rotation for the normals
                aiQuaternion presentRotation;
                {
                    aiVector3D p;
                    Transform.DecomposeNoScaling(presentRotation, p);
                }

                MeshPart.m_Name              = AssimpMesh.mName.C_Str();
                MeshPart.m_iMaterialInstance = AssimpMesh.mMaterialIndex;

                MeshPart.m_Vertices.resize(AssimpMesh.mNumVertices);
                for (auto i = 0u; i < AssimpMesh.mNumVertices; ++i)
                {
                    import3d::vertex& Vertex = MeshPart.m_Vertices[i];

                    auto L = Transform * AssimpMesh.mVertices[i];

                    Vertex.m_Position = xmath::fvec3d
                    ( static_cast<float>(L.x)
                    , static_cast<float>(L.y)
                    , static_cast<float>(L.z)
                    );

                    if (iTexCordinates == -1)
                    {
                        Vertex.m_UV.setup(0, 0);
                    }
                    else
                    {
                        Vertex.m_UV.setup(static_cast<float>(AssimpMesh.mTextureCoords[iTexCordinates][i].x)
                                        , static_cast<float>(AssimpMesh.mTextureCoords[iTexCordinates][i].y));
                    }

                    /*
                    if (iColors == -1)
                    {
                        Vertex.m_Color.setup(255,255,255,255);
                    }
                    else
                    {
                        xcore::vector4 RGBA(static_cast<float>(AssimpMesh.mColors[iColors][i].r)
                                          , static_cast<float>(AssimpMesh.mColors[iColors][i].g)
                                          , static_cast<float>(AssimpMesh.mColors[iColors][i].b)
                                          , static_cast<float>(AssimpMesh.mColors[iColors][i].a)
                        );

                        Vertex.m_Color.setupFromRGBA(RGBA);
                    }
                    */

                    if (AssimpMesh.HasTangentsAndBitangents())
                    {
                        assert(AssimpMesh.HasNormals());

                        const auto T = presentRotation.Rotate(AssimpMesh.mTangents[i]);
                        const auto B = presentRotation.Rotate(AssimpMesh.mBitangents[i]);
                        const auto N = presentRotation.Rotate(AssimpMesh.mNormals[i]);

                        Vertex.m_Normal.setup(N.x, N.y, N.z);
                        Vertex.m_Tangent.setup(T.x, T.y, T.z);
                        Vertex.m_Bitangent.setup(B.x, B.y, B.z);

                        Vertex.m_Normal.NormalizeSafe();
                        Vertex.m_Tangent.NormalizeSafe();
                        Vertex.m_Bitangent.NormalizeSafe();
                    }
                    else
                    {
                        const auto N = presentRotation.Rotate(AssimpMesh.mNormals[i]);

                        Vertex.m_Normal.setup(N.x, N.y, N.z);
                        Vertex.m_Tangent.setup(1, 0, 0);
                        Vertex.m_Bitangent.setup(1, 0, 0);

                        Vertex.m_Normal.NormalizeSafe();
                    }

                    // This is a static geometry so this is kind of meaning less
                    Vertex.m_BoneIndex.m_R   = Vertex.m_BoneIndex.m_G = Vertex.m_BoneIndex.m_B = Vertex.m_BoneIndex.m_A = 0;
                    Vertex.m_BoneWeights.m_R = Vertex.m_BoneWeights.m_G = Vertex.m_BoneWeights.m_B = Vertex.m_BoneWeights.m_A = 0;
                    Vertex.m_BoneWeights.m_R = 0xff;
                }

                //
                // Copy the indices
                //
                for (auto i = 0u; i < AssimpMesh.mNumFaces; ++i)
                {
                    const auto& Face = AssimpMesh.mFaces[i];
                    for (auto j = 0u; j < Face.mNumIndices; ++j)
                        MeshPart.m_Indices.push_back(Face.mIndices[j]);
                }
            };

            std::function<void(const aiNode&, const aiMatrix4x4&)> RecurseScene = [&]( const aiNode& Node, const aiMatrix4x4& ParentTransform)
            {
                const aiMatrix4x4 Transform = ParentTransform * Node.mTransformation;
                auto        iBase     = MyNodes.size();

                // Collect all the meshes
                MyNodes.resize(iBase + m_pScene->mNumMeshes);
                for (auto i = 0u, end = Node.mNumMeshes; i < end; ++i)
                {
                    aiMesh& AssimpMesh = *m_pScene->mMeshes[Node.mMeshes[i]];

                    int iTexCordinates;
                    if( ImportGeometryValidateMesh(AssimpMesh,iTexCordinates) ) continue;

                    ProcessMesh(AssimpMesh, Transform, MyNodes[iBase++], iTexCordinates );
                }

                // Make sure the base matches what should be in the vector
                if ( iBase != MyNodes.size() )
                {
                    MyNodes.erase(MyNodes.begin() + iBase, MyNodes.end());
                }

                // Do the children
                for (auto i = 0u; i < Node.mNumChildren; ++i)
                {
                    RecurseScene(*Node.mChildren[i], Transform);
                }
            };

            aiMatrix4x4 L2W;
            RecurseScene( *m_pScene->mRootNode, L2W );
        }

        //------------------------------------------------------------------------------------------------------

        void ImportGeometrySkin(std::vector<myMeshPart>& MyNodes) noexcept
        {
            //
            // Add bones base on bone associated by meshes
            // 
            MyNodes.resize(m_pScene->mNumMeshes);
            for (auto iMesh = 0u; iMesh < m_pScene->mNumMeshes; ++iMesh)
            {
                const aiMesh& AssimpMesh = *m_pScene->mMeshes[iMesh];

                int iTexCordinates;
                if( ImportGeometryValidateMesh(AssimpMesh, iTexCordinates) ) continue;

                //
                // Copy mesh name and Material Index
                //
                MyNodes[iMesh].m_Name               = AssimpMesh.mName.C_Str();
                MyNodes[iMesh].m_iMaterialInstance  = AssimpMesh.mMaterialIndex;

                // get the rotation for the normals
                aiQuaternion presentRotation;
                {
                    aiVector3D p;
                    m_MeshReferences[iMesh].m_Nodes[0]->mTransformation.DecomposeNoScaling(presentRotation, p);
                }

                //
                // Copy Vertices
                //
                MyNodes[iMesh].m_Vertices.resize(AssimpMesh.mNumVertices);
                for (auto i = 0u; i < AssimpMesh.mNumVertices; ++i)
                {
                    import3d::vertex& Vertex = MyNodes[iMesh].m_Vertices[i];

                    auto L = AssimpMesh.mVertices[i];
                    L = m_MeshReferences[iMesh].m_Nodes[0]->mTransformation * L;

                    Vertex.m_Position = xmath::fvec3d
                    ( static_cast<float>(L.x)
                    , static_cast<float>(L.y)
                    , static_cast<float>(L.z)
                    );

                    if(iTexCordinates == -1)
                    {
                        Vertex.m_UV.setup(0,0);
                    }
                    else
                    {
                        Vertex.m_UV.setup(static_cast<float>(AssimpMesh.mTextureCoords[iTexCordinates][i].x)
                                         ,static_cast<float>(AssimpMesh.mTextureCoords[iTexCordinates][i].y) );
                    }

                    /*
                    if (iColors == -1)
                    {
                        Vertex.m_Color.setup(255,255,255,255);
                    }
                    else
                    {
                        xcore::vector4 RGBA(static_cast<float>(AssimpMesh.mColors[iColors][i].r)
                                          , static_cast<float>(AssimpMesh.mColors[iColors][i].g)
                                          , static_cast<float>(AssimpMesh.mColors[iColors][i].b)
                                          , static_cast<float>(AssimpMesh.mColors[iColors][i].a)
                        );

                        Vertex.m_Color.setupFromRGBA(RGBA);
                    }
                    */

                    if (AssimpMesh.HasTangentsAndBitangents())
                    {
                        assert(AssimpMesh.HasNormals());

                        const auto T = presentRotation.Rotate( AssimpMesh.mTangents[i]);
                        const auto B = presentRotation.Rotate(AssimpMesh.mBitangents[i]);
                        const auto N = presentRotation.Rotate(AssimpMesh.mNormals[i]);

                        Vertex.m_Normal.setup(N.x, N.y, N.z);
                        Vertex.m_Tangent.setup(T.x, T.y, T.z);
                        Vertex.m_Bitangent.setup(B.x, B.y, B.z);

                        Vertex.m_Normal.NormalizeSafe();
                        Vertex.m_Tangent.NormalizeSafe();
                        Vertex.m_Bitangent.NormalizeSafe();
                    }
                    else
                    {
                        const auto N = presentRotation.Rotate(AssimpMesh.mNormals[i]);
                        Vertex.m_Normal.setup(N.x, N.y, N.z);
                        Vertex.m_Tangent.setup(1, 0, 0);
                        Vertex.m_Bitangent.setup(1, 0, 0);

                        Vertex.m_Normal.NormalizeSafe();
                    }

                    // Mark the weights as uninitialized we will be setting them later
                    Vertex.m_BoneIndex.m_R   = Vertex.m_BoneIndex.m_G   = Vertex.m_BoneIndex.m_B   = Vertex.m_BoneIndex.m_A   = 0;
                    Vertex.m_BoneWeights.m_R = Vertex.m_BoneWeights.m_G = Vertex.m_BoneWeights.m_B = Vertex.m_BoneWeights.m_A = 0;
                }

                //
                // Copy the indices
                //
                for (auto i = 0u; i < AssimpMesh.mNumFaces; ++i)
                {
                    const auto& Face = AssimpMesh.mFaces[i];
                    for (auto j = 0u; j < Face.mNumIndices; ++j)
                        MyNodes[iMesh].m_Indices.push_back(Face.mIndices[j]);
                }

                //
                // Add the bone weights
                //
                if (AssimpMesh.mNumBones > 0)
                {
                    struct tmp_weight
                    {
                        std::uint8_t m_iBone;
                        float        m_Weight{0};
                    };

                    struct my_weights
                    {
                        int                         m_Count {0};
                        std::array<tmp_weight,4>    m_Weights;
                    };

                    std::vector<my_weights> MyWeights;
                    MyWeights.resize(AssimpMesh.mNumVertices);

                    //
                    // Collect bones indices and weights
                    // 
                    assert(m_MeshReferences[iMesh].m_Nodes.size() == 1);

                    MyNodes[iMesh].m_MeshName = GetMeshNameFromNode( *m_MeshReferences[iMesh].m_Nodes[0] );
                    for( auto iBone = 0u; iBone< AssimpMesh.mNumBones; iBone++ )
                    {
                        const auto&          AssimpBone    = *AssimpMesh.mBones[iBone];
                        const std::uint8_t   iSkeletonBone = m_pSkeleton->findBone(AssimpBone.mName.C_Str());
                        assert( m_pSkeleton->findBone(AssimpBone.mName.C_Str()) != -1 );

                        for( auto iWeight = 0u; iWeight < AssimpBone.mNumWeights; ++iWeight )
                        {
                            const auto& AssimpWeight = AssimpBone.mWeights[iWeight];
                            auto&       MyWeight     = MyWeights[AssimpWeight.mVertexId];

                            MyWeight.m_Weights[MyWeight.m_Count].m_iBone  = iSkeletonBone;
                            MyWeight.m_Weights[MyWeight.m_Count].m_Weight = AssimpWeight.mWeight;

                            // get ready for the next one
                            MyWeight.m_Count++;
                        }
                    }

                    //
                    // Sort weights, normalize and set to the final vert
                    //
                    for( int iVertex=0u; iVertex< MyWeights.size(); ++iVertex )
                    {
                        auto& E = MyWeights[iVertex];

                        // Short from bigger to smaller
                        std::qsort( E.m_Weights.data(), E.m_Weights.size(), sizeof(tmp_weight), []( const void* pA, const void* pB ) ->int
                        {
                            auto& A = *reinterpret_cast<const tmp_weight*>(pA);
                            auto& B = *reinterpret_cast<const tmp_weight*>(pB);
                            if( B.m_Weight < A.m_Weight ) return -1;
                            return B.m_Weight > A.m_Weight;
                        });

                        assert(E.m_Weights[0].m_Weight >= E.m_Weights[1].m_Weight);

                        // Normalize the weights
                        float Total=0;
                        for (int i = 0; i < E.m_Count; ++i)
                        {
                            Total += E.m_Weights[i].m_Weight;
                        }

                        for (int i = 0; i < E.m_Count; ++i)
                        {
                            E.m_Weights[i].m_Weight /= Total;
                        }

                        // Copy Weight To the Vert
                        auto& V = MyNodes[iMesh].m_Vertices[iVertex];
                        for (int i = 0; i < E.m_Count; ++i)
                        {
                            const auto& BW = E.m_Weights[i];

                            switch (i)
                            {
                            case 0: V.m_BoneIndex.m_R   = static_cast<std::uint8_t>(BW.m_iBone);
                                    V.m_BoneWeights.m_R = static_cast<std::uint8_t>(BW.m_Weight * 0xff);
                                    break;
                            case 1: V.m_BoneIndex.m_G   = static_cast<std::uint8_t>(BW.m_iBone);
                                    V.m_BoneWeights.m_G = static_cast<std::uint8_t>(BW.m_Weight * 0xff);
                                    break;
                            case 2: V.m_BoneIndex.m_B   = static_cast<std::uint8_t>(BW.m_iBone);
                                    V.m_BoneWeights.m_B = static_cast<std::uint8_t>(BW.m_Weight * 0xff);
                                    break;
                            case 3: V.m_BoneIndex.m_A   = static_cast<std::uint8_t>(BW.m_iBone);
                                    V.m_BoneWeights.m_A = static_cast<std::uint8_t>(BW.m_Weight * 0xff);
                                    break;
                            }
                        }
                    }

                    //
                    // Sanity check (make sure that all the vertices have bone and weights
                    //
                    for( auto& V : MyNodes[iMesh].m_Vertices )
                    {
                        assert( V.m_BoneWeights.m_R > 0 );
                    }
                }
                else
                {
                    //
                    // Set the weights and duplicate mesh if needed
                    //

                    // Remember where was the base
                    int iBase = static_cast<int>(m_MeshReferences[iMesh].m_Nodes.size());

                    // Grow the total number of meshes if we have to...
                    if(iBase > 1 ) MyNodes.resize(MyNodes.size() + m_MeshReferences[iMesh].m_Nodes.size() - 1 );

                    auto pMyNode = &MyNodes[iMesh];                    
                    for (const auto pN : m_MeshReferences[iMesh].m_Nodes)
                    {
                        pMyNode->m_MeshName = GetMeshNameFromNode(*pN);
                        const std::uint8_t   iSkeletonBone = m_pSkeleton->findBone(pN->mName.C_Str());
                        for (auto iVertex = 0u; iVertex < AssimpMesh.mNumVertices; ++iVertex)
                        {
                            auto& V = pMyNode->m_Vertices[iVertex];
                            V.m_BoneIndex.m_R   = iSkeletonBone;
                            V.m_BoneWeights.m_R = 0xff;
                        }

                        if (iBase < MyNodes.size())
                        {
                            pMyNode = &MyNodes[iBase++];

                            // Deep copy the mesh...
                            *pMyNode = MyNodes[iMesh];
                        }
                    }
                }
            }
        }

        //------------------------------------------------------------------------------------------------------

        void ImportGeometry()
        {
            std::vector<myMeshPart> MyNodes;

            //
            // Import from scene
            //
            if(m_pSkeleton && m_pSkeleton->m_Bones.size() )
            {
                ImportGeometrySkin(MyNodes);
            }
            else
            {
                ImportGeometryStatic(MyNodes);
            }

            //
            // Remove Mesh parts with zero vertices
            //
            for (auto i = 0u; i < MyNodes.size(); ++i)
            {
                if (MyNodes[i].m_Vertices.size() == 0 || MyNodes[i].m_Indices.size() == 0)
                {
                    MyNodes.erase(MyNodes.begin() + i);
                    --i;
                }
            }

            //
            // Merge any mesh part based on Mesh and iMaterial...
            //
            for (auto i = 0u; i < MyNodes.size(); ++i)
            {
                for (auto j = i + 1; j < MyNodes.size(); ++j)
                {
                    // Lets find a candidate to merge...
                    if (MyNodes[i].m_iMaterialInstance == MyNodes[j].m_iMaterialInstance
                        && MyNodes[i].m_MeshName == MyNodes[j].m_MeshName)
                    {
                        const int  iBaseVertex = static_cast<int>(MyNodes[i].m_Vertices.size());
                        const auto iBaseIndex = MyNodes[i].m_Indices.size();
                        MyNodes[i].m_Vertices.insert(MyNodes[i].m_Vertices.end(), MyNodes[j].m_Vertices.begin(), MyNodes[j].m_Vertices.end());
                        MyNodes[i].m_Indices.insert(MyNodes[i].m_Indices.end(), MyNodes[j].m_Indices.begin(), MyNodes[j].m_Indices.end());

                        // Fix the indices
                        for (auto I = iBaseIndex; I < MyNodes[i].m_Indices.size(); ++I)
                        {
                            MyNodes[i].m_Indices[I] += iBaseVertex;
                        }

                        MyNodes.erase(MyNodes.begin() + j);
                        --j;
                    }
                }
            }

            //
            // Create final structure
            //
            for (auto& E : MyNodes)
            {
                int iFinalMesh = -1;
                for (auto i = 0u; i < m_pGeom->m_Mesh.size(); ++i)
                {
                    if (m_pGeom->m_Mesh[i].m_Name == E.m_MeshName)
                    {
                        iFinalMesh = i;
                        break;
                    }
                }

                if (iFinalMesh == -1)
                {
                    iFinalMesh = static_cast<int>(m_pGeom->m_Mesh.size());
                    m_pGeom->m_Mesh.emplace_back();
                    m_pGeom->m_Mesh.back().m_Name = E.m_MeshName;
                }

                auto& FinalMesh = m_pGeom->m_Mesh[iFinalMesh];
                auto& SubMesh   = FinalMesh.m_Submeshes.emplace_back();

                SubMesh.m_Vertices      = std::move(E.m_Vertices);
                SubMesh.m_Indices       = std::move(E.m_Indices);
                SubMesh.m_iMaterial     = E.m_iMaterialInstance;
            }
        }

        //------------------------------------------------------------------------------------------------------

        void ImportAnimations( int MaxSamplingFPS = 60 ) noexcept
        {
            struct indices
            {
                std::uint32_t m_iPositions{ 0 };
                std::uint32_t m_iRotations{ 0 };
                std::uint32_t m_iScales   { 0 };
            };

            m_pAnimPackage->m_Animations.resize(m_pScene->mNumAnimations);
            for( auto i = 0ul; i < m_pScene->mNumAnimations; ++i )
            {
                const aiAnimation&      AssimpAnim        = *m_pScene->mAnimations[i];
                const int               SamplingFPS       = static_cast<int>(MaxSamplingFPS > AssimpAnim.mTicksPerSecond ? AssimpAnim.mTicksPerSecond : MaxSamplingFPS);
                const double            AnimationDuration = AssimpAnim.mDuration / AssimpAnim.mTicksPerSecond;
                const double            DeltaTime         = (AssimpAnim.mTicksPerSecond / SamplingFPS);
                const int               FrameCount        = (int)std::ceil( AssimpAnim.mDuration / DeltaTime);
                assert(FrameCount>0);
                std::vector<indices>    LastPositions;

                // Allocate all the bones for this animation
              //  assert( AssimpAnim.mNumChannels <= m_pSkeleton->m_Bones.size() );
                auto& MyAnim = m_pAnimPackage->m_Animations[i];
                MyAnim.m_BoneKeyFrames.resize(m_pSkeleton->m_Bones.size());
                MyAnim.m_FPS        = SamplingFPS;
                MyAnim.m_Name       = AssimpAnim.mName.C_Str();
                MyAnim.m_TimeLength = static_cast<float>(AnimationDuration);

                // To cache the last positions for a given frame for each bone
                LastPositions.resize( AssimpAnim.mNumChannels );

                // Create/Sample all the frames            
                for( int iFrame = 0; iFrame < FrameCount; iFrame++ )
                {
                    const auto t = iFrame * DeltaTime;
                    for( auto b = 0ul; b < AssimpAnim.mNumChannels; ++b )
                    {
                        const aiNodeAnim& Channel = *AssimpAnim.mChannels[b];
                        auto&             LastPos = LastPositions[b];

                        // Sample the position key
                        aiVector3D presentPosition(0, 0, 0);
                        if( Channel.mNumPositionKeys > 0 ) 
                        {
                            // Update the Position Index for the given bone
                            while( LastPos.m_iPositions < Channel.mNumPositionKeys - 1 ) 
                            {
                                if( t < Channel.mPositionKeys[LastPos.m_iPositions + 1].mTime ) break;
                                ++LastPos.m_iPositions;
                            }

                            // interpolate between this frame's value and next frame's value
                            unsigned int        NextFrame   = (LastPos.m_iPositions + 1) % Channel.mNumPositionKeys;
                            const aiVectorKey&  Key         = Channel.mPositionKeys[ LastPos.m_iPositions ];
                            const aiVectorKey&  NextKey     = Channel.mPositionKeys[ NextFrame ];
                            double              diffTime    = NextKey.mTime - Key.mTime;

                            if ( diffTime < 0.0 ) diffTime += AssimpAnim.mDuration;
                            if ( diffTime > 0   ) 
                            {
                                float factor = float((t - Key.mTime) / diffTime);
                                presentPosition = Key.mValue + (NextKey.mValue - Key.mValue) * factor;
                            }
                            else 
                            {
                                presentPosition = Key.mValue;
                            }
                        }

                        // Sample the Rotation key
                        aiQuaternion presentRotation(1, 0, 0, 0);
                        if( Channel.mNumRotationKeys > 0 ) 
                        {
                            // Update the Rotation Index for the given bone
                            while (LastPos.m_iRotations < Channel.mNumRotationKeys - 1)
                            {
                                if (t < Channel.mRotationKeys[LastPos.m_iRotations + 1].mTime) break;
                                ++LastPos.m_iRotations;
                            }

                            // interpolate between this frame's value and next frame's value
                            unsigned int        NextFrame   = (LastPos.m_iRotations + 1) % Channel.mNumRotationKeys;
                            const aiQuatKey&    Key         = Channel.mRotationKeys[LastPos.m_iRotations];
                            const aiQuatKey&    NextKey     = Channel.mRotationKeys[NextFrame];
                            double              diffTime    = NextKey.mTime - Key.mTime;

                            if( diffTime < 0.0 ) diffTime += AssimpAnim.mDuration;
                            if( diffTime > 0   ) 
                            {
                                float factor = float((t - Key.mTime) / diffTime);
                                aiQuaternion::Interpolate(presentRotation, Key.mValue, NextKey.mValue, factor);
                            }
                            else 
                            {
                                presentRotation = Key.mValue;
                            }
                        }

                        // Sample the Scale key
                        aiVector3D presentScaling( 1, 1, 1 );
                        if( Channel.mNumScalingKeys > 0 ) 
                        {
                            // Update the Rotation Index for the given bone
                            while( LastPos.m_iScales < Channel.mNumScalingKeys - 1)
                            {
                                if (t < Channel.mScalingKeys[LastPos.m_iScales + 1].mTime) break;
                                ++LastPos.m_iScales;
                            }

                            // TODO: interpolation maybe? This time maybe even logarithmic, not linear!
                            // interpolate between this frame's value and next frame's value
                            unsigned int        NextFrame   = (LastPos.m_iScales + 1) % Channel.mNumScalingKeys;
                            const aiVectorKey&  Key         = Channel.mScalingKeys[ LastPos.m_iScales];
                            const aiVectorKey&  NextKey     = Channel.mScalingKeys[ NextFrame ];
                            double              diffTime    = NextKey.mTime - Key.mTime;

                            if ( diffTime < 0.0 ) diffTime += AssimpAnim.mDuration;
                            if ( diffTime > 0   ) 
                            {
                                float factor = float((t - Key.mTime) / diffTime);
                                presentScaling = Key.mValue + (NextKey.mValue - Key.mValue) * factor;
                            }
                            else 
                            {
                                presentScaling = Key.mValue;
                            }
                        }

                        //
                        // Set all the computer components into our frame
                        //

                        // make sure that we can find the bone                         
                        const int iBone = m_pSkeleton->findBone(Channel.mNodeName.C_Str());
                        if (-1 == iBone)
                        {
                            continue;
                        }
                            
                        if (MyAnim.m_BoneKeyFrames[iBone].m_Transfoms.size() == 0) MyAnim.m_BoneKeyFrames[iBone].m_Transfoms.resize(FrameCount);

                        auto& MyBoneKeyFrame = MyAnim.m_BoneKeyFrames[iBone].m_Transfoms[iFrame];

                        MyBoneKeyFrame.m_Position.setup( presentPosition.x, presentPosition.y, presentPosition.z );
                        MyBoneKeyFrame.m_Rotation .setup( presentRotation.x, presentRotation.y, presentRotation.z, presentRotation.w ).NormalizeSafe();
                        MyBoneKeyFrame.m_Scale.setup    ( presentScaling.x,  presentScaling.y,  presentScaling.z );
                    }
                }

                //
                // Add transforms without animations
                //
                for (int i = 0; i < m_pSkeleton->m_Bones.size(); ++i)
                {
                    if (MyAnim.m_BoneKeyFrames[i].m_Transfoms.size() == 0)
                    {
                        MyAnim.m_BoneKeyFrames[i].m_Transfoms.resize(FrameCount);
                        auto pNode = m_pScene->mRootNode->FindNode(m_pSkeleton->m_Bones[i].m_Name.c_str());
                        
                        aiQuaternion Q(0,0,0,1);
                        aiVector3D   S(1,1,1);
                        aiVector3D   T(0,0,0);
                        pNode->mTransformation.Decompose(S, Q, T);

                        for (int f = 0; f < FrameCount; ++f)
                        {
                            auto& MyBoneKeyFrame = MyAnim.m_BoneKeyFrames[i].m_Transfoms[f];

                            MyBoneKeyFrame.m_Position.setup(T.x, T.y, T.z);
                            MyBoneKeyFrame.m_Rotation.setup(Q.x, Q.y, Q.z, Q.w);
                            MyBoneKeyFrame.m_Scale.setup(S.x, S.y, S.z);
                        }
                    }
                }
            }
        }

        //------------------------------------------------------------------------------------------------------

        void ImportSkeleton( void ) noexcept
        {
            std::unordered_map<std::string, const aiNode*> NameToNode;
            std::unordered_map<std::string, const aiBone*> NameToBone;

            //
            // Add bones base on bone associated by meshes
            // 
            for (auto iMesh = 0u; iMesh < m_pScene->mNumMeshes; ++iMesh)
            {
                const aiMesh& Mesh = *m_pScene->mMeshes[iMesh];
                for (auto iBone = 0u; iBone < Mesh.mNumBones; ++iBone)
                {
                    const aiBone& Bone = *Mesh.mBones[iBone];
                    if (auto E = NameToBone.find(Bone.mName.data); E == NameToBone.end())
                    {
                        auto pNode = m_pScene->mRootNode->FindNode(Bone.mName);
                        NameToBone[Bone.mName.data] = &Bone;
                        NameToNode[Bone.mName.data] = pNode;
                    }
                }
            }

            //
            // Add bones base on the animation streams
            // This should be an option really...
            // We can throw away any node that is animated but not used by any mesh and it is not a parent to a nodes containing meshes
            if (false)
            {
                for (auto iAnim = 0u; iAnim < m_pScene->mNumAnimations; ++iAnim)
                {
                    const aiAnimation& CurrentAnim = *m_pScene->mAnimations[iAnim];
                    for (auto iStream = 0u; iStream < CurrentAnim.mNumChannels; iStream++)
                    {
                        auto& Channel = *CurrentAnim.mChannels[iStream];

                        if (auto E = NameToNode.find(Channel.mNodeName.data); E == NameToNode.end())
                        {
                            NameToNode.insert({ Channel.mNodeName.data, m_pScene->mRootNode->FindNode(Channel.mNodeName) });
                        }
                    }
                }
            }

            //
            // Make sure all the parent nodes are inserted in the hash table
            // This algotithum is a bit overkill but is ok... 
            //
            for (auto itr1 : NameToNode)
            {
                for (auto pParentNode = NameToNode.find(itr1.first)->second->mParent; pParentNode != nullptr; pParentNode = pParentNode->mParent)
                {
                    if (auto e = NameToNode.find(pParentNode->mName.C_Str()); e == NameToNode.end())
                    {
                        NameToNode[pParentNode->mName.C_Str()] = pParentNode;
                    }
                }
            }

            //
            // Check to see if we readed too many bones!
            //
            if (NameToNode.size() > 0xff)
            {
                printf("ERROR: This mesh has %zd Bones we can only handle up to 256\n", NameToNode.size());
            }

            //
            // Organize build the skeleton 
            // We want the parents to be first then the children
            // Ideally we also want to have the bones that have more children higher
            //
            struct proto
            {
                const aiNode*   m_pAssimpNode   { nullptr };
                int             m_Depth         { 0 };
                int             m_nTotalChildren{ 0 };
                int             m_nChildren     { 0 };
            };
            std::vector<proto> Proto;

            // Set the Assimp Node
            Proto.resize(NameToNode.size());
            {
                int i = 0;
                for (auto itr = NameToNode.begin(); itr != NameToNode.end(); ++itr)
                {
                    auto& P = Proto[i++];
                    P.m_pAssimpNode = itr->second;
                }
            }

            // Set the Depth, m_nTotalChildren and nChildren
            for (auto i = 0u; i < Proto.size(); ++i)
            {
                auto& P = Proto[i];
                bool  bFoundParent = false;

                for (aiNode* pNode = P.m_pAssimpNode->mParent; pNode; pNode = pNode->mParent)
                {
                    P.m_Depth++;

                    // If we can find the parent lets keep a count of how many total children it has
                    for (auto j = 0; j < Proto.size(); ++j)
                    {
                        auto& ParentProto = Proto[j];
                        if (pNode == ParentProto.m_pAssimpNode)
                        {
                            ParentProto.m_nTotalChildren++;
                            if (bFoundParent == false) ParentProto.m_nChildren++;
                            bFoundParent = true;
                            break;
                        }
                    }
                }
            }

            // Put all the Proto bones in the right order
            std::qsort(Proto.data(), Proto.size(), sizeof(proto), [](const void* pA, const void* pB) -> int
            {
                const auto& A = *reinterpret_cast<const proto*>(pA);
                const auto& B = *reinterpret_cast<const proto*>(pB);

                if (A.m_Depth < B.m_Depth) return -1;
                if (A.m_Depth > B.m_Depth) return  1;
                if (A.m_nTotalChildren < B.m_nTotalChildren) return  -1;
                return (A.m_nTotalChildren > B.m_nTotalChildren);
            });

            //
            // Create all the real bones
            //
            m_pSkeleton->m_Bones.resize(Proto.size());
            {
                int i = 0;
                for( auto& ACBone : m_pSkeleton->m_Bones )
                {
                    auto& ProtoBone     = Proto[i++];
                    ACBone.m_Name       = ProtoBone.m_pAssimpNode->mName.data;
                    ACBone.m_iParent    = -1;

                    // Potentially we may not have all parent nodes in our skeleton
                    // so we must search by each of the potential assimp nodes
                    for( aiNode* pNode = ProtoBone.m_pAssimpNode->mParent; ACBone.m_iParent == -1 && pNode; pNode = pNode->mParent)
                    {
                        for (auto j = 0; j < i; ++j)
                        {
                            if (Proto[j].m_pAssimpNode == ProtoBone.m_pAssimpNode->mParent)
                            {
                                ACBone.m_iParent = j;
                                break;
                            }
                        }
                    }

                    // Check if we have a binding matrix
                    if( auto B = NameToBone.find(ProtoBone.m_pAssimpNode->mName.data); B != NameToBone.end() )
                    {
                        // Inverse bind matrix
                        auto OffsetMatrix = B->second->mOffsetMatrix;
                        auto NodeMatrix   = m_MeshReferences[0].m_Nodes[0]->mTransformation;
                        NodeMatrix = OffsetMatrix * NodeMatrix.Inverse();

                        std::memcpy(&ACBone.m_InvBind, &NodeMatrix, sizeof(xmath::fmat4));
                        ACBone.m_InvBind = ACBone.m_InvBind.Transpose();
                    }
                    else
                    {
                        ACBone.m_InvBind = xmath::fmat4::fromIdentity();
                    }

                    // Neutral pose
                    {
                        const auto  C = NameToNode.find(ProtoBone.m_pAssimpNode->mName.data);
                        auto        NodeMatrix = C->second->mTransformation;
                        for (auto p = C->second->mParent; p; p = p->mParent) NodeMatrix = p->mTransformation * NodeMatrix;
                        std::memcpy(&ACBone.m_NeutalPose, &NodeMatrix, sizeof(xmath::fmat4));
                        ACBone.m_NeutalPose = ACBone.m_NeutalPose.Transpose();

                        ACBone.m_NeutalPose *= ACBone.m_InvBind;
                    }

                }
            }
        }

    protected:

        std::vector<refs>                   m_MeshReferences;
        import3d::anim_package*             m_pAnimPackage  = nullptr;
        import3d::geom*                     m_pGeom         = nullptr;
        import3d::skeleton*                 m_pSkeleton     = nullptr;
        const aiScene*                      m_pScene        = nullptr;
    };
}