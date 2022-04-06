#include "ModelLoader.h"
#include "../../tools/xgpu_xcore_bitmap_helpers.h"
#pragma comment( lib, "../../Dependencies/assimp/BINARIES/Win32/lib/Release/assimp-vc142-mt.lib") //Dependencies/assimp/BINARIES/Win32/lib/Debug/assimp-vc142-mtd.lib" )

namespace xgpu::assimp {


    //----------------------------------------------------------------------------------------------------

    bool model_loader::Load( xgpu::device& Device, std::string Filename ) noexcept
    {
        auto Importer = std::make_unique<Assimp::Importer>();

        const aiScene* pScene = Importer->ReadFile( Filename
            , aiProcess_Triangulate                // Make sure we get triangles rather than nvert polygons
            | aiProcess_LimitBoneWeights           // 4 weights for skin model max
            | aiProcess_GenUVCoords                // Convert any type of mapping to uv mapping
            | aiProcess_TransformUVCoords          // preprocess UV transformations (scaling, translation ...)
            | aiProcess_FindInstances              // search for instanced meshes and remove them by references to one master
            | aiProcess_CalcTangentSpace           // calculate tangents and bitangents if possible
            | aiProcess_JoinIdenticalVertices      // join identical vertices/ optimize indexing
            | aiProcess_RemoveRedundantMaterials   // remove redundant materials
            | aiProcess_FindInvalidData            // detect invalid model data, such as invalid normal vectors
            | aiProcess_PreTransformVertices       // pre-transform all vertices
        );

        if (pScene == nullptr)
            return true;

        m_Directory = Filename.substr(0, Filename.find_last_of("/\\"));
        m_Device    = Device;

        ProcessNode( *pScene->mRootNode, *pScene );

        return false;
    }

    //----------------------------------------------------------------------------------------------------

    mesh model_loader::ProcessMesh( const aiMesh& Mesh, const aiScene& Scene ) noexcept
    {
        // Data to fill
        std::vector<vertex>			Vertices;
        std::vector<std::uint32_t>	Indices;
        std::vector<texture>		Textures;

        // Walk through each of the mesh's vertices
        for( auto i = 0u; i < Mesh.mNumVertices; ++i ) 
        {
            vertex Vertex;

            Vertex.m_Position = xcore::vector3
            ( static_cast<float>(Mesh.mVertices[i].x)
            , static_cast<float>(Mesh.mVertices[i].y)
            , static_cast<float>(Mesh.mVertices[i].z)
            );

            if( Mesh.mTextureCoords[0] ) 
            {
                Vertex.m_Texcoord = xcore::vector2
                ( static_cast<float>(Mesh.mTextureCoords[0][i].x)
                , static_cast<float>(Mesh.mTextureCoords[0][i].y)
                );
            }

            if( Mesh.mColors[0] )
            {
                Vertex.m_Color = xcore::vector4
                ( static_cast<float>(Mesh.mColors[0][i].r)
                , static_cast<float>(Mesh.mColors[0][i].g)
                , static_cast<float>(Mesh.mColors[0][i].b)
                , static_cast<float>(Mesh.mColors[0][i].a)
                );
            }

            Vertices.push_back(Vertex);
        }

        // Walk thourgh the faces
        for( auto i = 0u; i < Mesh.mNumFaces; ++i ) 
        {
            const auto& Face = Mesh.mFaces[i];

            for( auto j = 0u; j < Face.mNumIndices; ++j )
                Indices.push_back(Face.mIndices[j]);
        }

        // Handle materials
        if( Mesh.mMaterialIndex >= 0 ) 
        {
            const aiMaterial& Material = *Scene.mMaterials[ Mesh.mMaterialIndex ];

            std::vector<texture> DiffuseMaps = LoadMaterialTextures(Material, aiTextureType_DIFFUSE, "texture_diffuse", Scene );

            Textures.insert( Textures.end(), DiffuseMaps.begin(), DiffuseMaps.end() );
        }

        return mesh( m_Device, std::move(Vertices), std::move(Indices), std::move(Textures) );
    }

    //----------------------------------------------------------------------------------------------------

    std::vector<texture> model_loader::LoadMaterialTextures(const aiMaterial& Material, aiTextureType Type, std::string TypeName, const aiScene& Scene) noexcept
    {
        std::vector<texture> Textures;

        for( auto i = 0u; i < Material.GetTextureCount(Type); ++i )
        {
            aiString str;
            Material.GetTexture( Type, i, &str );

            // Check if texture was loaded before and if so, continue to next iteration: skip loading a new texture
            bool bSkip = false;
            for( auto j = 0u; j < m_TexturesLoaded.size(); ++j )
            {
                if( std::strcmp(m_TexturesLoaded[j].m_Path.c_str(), str.C_Str() ) == 0 )
                {
                    Textures.push_back( m_TexturesLoaded[j] );

                    // A texture with the same filepath has already been loaded, continue to next one. (optimization)
                    bSkip = true; 
                    break;
                }
            }

            // Skip loading the texture if we already did so
            if( bSkip ) continue;

            // If texture hasn't been loaded already, load it
            {   
                texture Texture;

                const aiTexture* pEmbeddedTexture = Scene.GetEmbeddedTexture(str.C_Str());

                if( pEmbeddedTexture )
                {
                    Texture.m_XGPUTexture = LoadEmbeddedTexture( *pEmbeddedTexture );
                } 
                else 
                {
                    std::string Filename = std::string(str.C_Str());
                    Filename = m_Directory + '/' + Filename;

                    std::wstring filenamews = std::wstring( Filename.begin(), Filename.end() );
                    
                    // This is temporary untill we know we can load proper textures...
                    if (auto Err = xgpu::tools::bitmap::Create(Texture.m_XGPUTexture, m_Device, xcore::bitmap::getDefaultBitmap()); Err)
                    {
                        assert(false);
                    }

                    /*
                    hr = CreateWICTextureFromFile(dev_, devcon_, filenamews.c_str(), nullptr, &texture.texture);
                    if (FAILED(hr))
                        MessageBox(hwnd_, "Texture couldn't be loaded", "Error!", MB_ICONERROR | MB_OK);
                        */
                }

                Texture.m_Type = TypeName;
                Texture.m_Path = str.C_Str();
                Textures.push_back(Texture);

                // Store it as texture loaded for entire model, to ensure we won't unnecesery load duplicate textures.
                m_TexturesLoaded.push_back(Texture);  
            }
        }
        return Textures;
    }

    //----------------------------------------------------------------------------------------------------

    void model_loader::Draw( xgpu::cmd_buffer& CommandBuffer ) noexcept
    {
        for( auto& Mesh : m_Meshes )
        {
            Mesh.Draw(CommandBuffer);
        }
    }

    //----------------------------------------------------------------------------------------------------

    void model_loader::ProcessNode( const aiNode& Node, const aiScene& Scene ) noexcept 
    {
        for( auto i = 0u, end = Node.mNumMeshes; i < end; ++i )
        {
            aiMesh* pMesh = Scene.mMeshes[ Node.mMeshes[i] ];
            m_Meshes.push_back( ProcessMesh( *pMesh, Scene ) );
        }

        for( auto i = 0u; i < Node.mNumChildren; ++i ) 
        {
            ProcessNode( *Node.mChildren[i], Scene );
        }
    }

    //----------------------------------------------------------------------------------------------------

    xgpu::texture model_loader::LoadEmbeddedTexture( const aiTexture& EmbeddedTexture ) noexcept
    {
        // Set the default texture for now...
        xgpu::texture Texture;
        if( auto Err = xgpu::tools::bitmap::Create( Texture, m_Device, xcore::bitmap::getDefaultBitmap() ); Err )
        {
            assert(false);
        }

        return Texture;

        /*
        if( EmbeddedTexture.mHeight != 0 )
        {
            xgpu::texture			Texture;
            xgpu::texture::setup	Setup
            { .m_Format = xgpu::texture::format::B8G8R8A8
            , .m_Type   = xgpu::texture::type::GAMMA
            , .m_Width  = EmbeddedTexture.mWidth
            , .m_Height = EmbeddedTexture.mHeight
            , .m_Data   = { reinterpret_cast<const std::byte*>(EmbeddedTexture.pcData), EmbeddedTexture.mWidth * 4ull }
            };

            if( auto Err = m_Device.Create(Texture, Setup); Err )
            {
                return {};
            }

            return texture;
        }
        else
        {
            // mHeight is 0, so try to load a compressed texture of mWidth bytes
            xgpu::texture	Texture;
            const size_t	Size	= EmbeddedTexture.mWidth;
            xgpu::texture::setup	Setup
            { .m_Format = xgpu::texture::format::
            , .m_Type   = xgpu::texture::type::GAMMA
            , .m_Width  = EmbeddedTexture.mWidth
            , .m_Height = EmbeddedTexture.mHeight
            , .m_Data   = { reinterpret_cast<const std::byte*>(EmbeddedTexture.pcData), EmbeddedTexture.mWidth }
            };

            if( strcmp( "dds", EmbeddedTexture.achFormatHint );


            if( auto Err = m_Device.Create(Texture, Setup); Err )
            {
                return {};
            }



            hr = CreateWICTextureFromMemory(dev_, devcon_, reinterpret_cast<const unsigned char*>(embeddedTexture->pcData), size, nullptr, &texture);
            if (FAILED(hr))
                MessageBox(hwnd_, "Texture couldn't be created from memory!", "Error!", MB_ICONERROR | MB_OK);

            return texture;
        }
            */
    }

    //----------------------------------------------------------------------------------------------------
    // END
    //----------------------------------------------------------------------------------------------------

} // namespace xgpu::assimp