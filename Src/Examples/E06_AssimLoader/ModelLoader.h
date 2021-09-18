#ifndef MODEL_LOADER_H
#define MODEL_LOADER_H

#include <vector>

#include "assimp\Importer.hpp"
#include "assimp\scene.h"
#include "assimp\postprocess.h"

#include "Mesh.h"
//#include "TextureLoader.h"

namespace xgpu::assimp
{
    class model_loader
    {
    public:

        bool Load( xgpu::device& Device, std::string Filename ) noexcept;
        void Draw( xgpu::cmd_buffer& CommandBuffer ) noexcept;

    private:

        mesh					ProcessMesh			( const aiMesh& Mesh, const aiScene& Scene ) noexcept;
        std::vector<texture>	LoadMaterialTextures( const aiMaterial& Material, aiTextureType Type, std::string TypeName, const aiScene& Scene ) noexcept;
        void					ProcessNode			( const aiNode& Node, const aiScene& scene ) noexcept;
        xgpu::texture			LoadEmbeddedTexture ( const aiTexture& embeddedTexture ) noexcept;

        xgpu::device			m_Device			{};
        std::string				m_Directory			{};
        std::vector<texture>	m_TexturesLoaded	{};
        std::vector<mesh>		m_Meshes			{};
    };
}

#endif // !MODEL_LOADER_H

