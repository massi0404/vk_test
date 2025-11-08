#include "Resource.h"

#include "fastgltf/glm_element_traits.hpp"
#include "fastgltf/core.hpp"
#include "fastgltf/tools.hpp"

// sbocco...
MeshData LoadMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

void Resource::LoadMesh(const std::filesystem::path& path, std::vector<MeshResource>& outMeshes)
{
    constexpr auto gltfOptions = /*fastgltf::Options::LoadGLBBuffers | */ fastgltf::Options::LoadExternalBuffers;
    
    fastgltf::Expected<fastgltf::GltfDataBuffer> data = fastgltf::GltfDataBuffer::FromPath(path);
    check(data);

    fastgltf::Parser parser;
    fastgltf::Expected<fastgltf::Asset> gltf = parser.loadGltfBinary(data.get(), path.parent_path(), gltfOptions);
    check(gltf);

    std::vector<uint32_t> indices;
    std::vector<Vertex> vertices;

    for (fastgltf::Mesh& mesh : gltf->meshes)
    {
        size_t meshIndex = outMeshes.size();
        MeshResource& newMesh = outMeshes.emplace_back();
        
        // primitive (triangoli, quad, etc..)
        for (const auto& primitive : mesh.primitives)
        {
            GeoSurface newSurface;
            newSurface.startIndex = (uint32_t)indices.size();
            newSurface.count = gltf->accessors[primitive.indicesAccessor.value()].count;
            // newSurface.type = primitive.type;

            size_t startVertex = vertices.size();

            // load indexes
            {
                fastgltf::Accessor& indexAccessor = gltf->accessors[primitive.indicesAccessor.value()];
                indices.reserve(indices.size() + indexAccessor.count);

                fastgltf::iterateAccessor<std::uint32_t>(gltf.get(), indexAccessor, 
                    [&](std::uint32_t idx) {
                        indices.push_back(idx + startVertex);
                    });
            }

            // load vertices
            {
                fastgltf::Accessor& posAccessor = gltf->accessors[primitive.findAttribute("POSITION")->accessorIndex]; 
                vertices.resize(vertices.size() + posAccessor.count);

                fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf.get(), posAccessor, 
                    [&](glm::vec3 v, size_t index) {
                        Vertex newvtx;
                        newvtx.position = v;
                        newvtx.normal = { 1, 0, 0 };
                        newvtx.color = glm::vec4{ 1.f };
                        newvtx.uv_x = 0;
                        newvtx.uv_y = 0;
                        vertices[startVertex + index] = newvtx;
                    });
            }

            // load vertex normals
            {
                auto normals = primitive.findAttribute("NORMAL");
                if (normals != primitive.attributes.end()) 
                {
                    fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf.get(), gltf->accessors[(*normals).accessorIndex],
                        [&](glm::vec3 v, size_t index) {
                            vertices[startVertex + index].normal = v;
                        });
                }
            }
            
            // load UVs
            {
                auto uv = primitive.findAttribute("TEXCOORD_0");
                if (uv != primitive.attributes.end()) 
                {
                    fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf.get(), gltf->accessors[(*uv).accessorIndex],
                        [&](glm::vec2 v, size_t index) {
                            vertices[startVertex + index].uv_x = v.x;
                            vertices[startVertex + index].uv_y = v.y;
                        });
                }
            }

            // load vertex colors
            auto colors = primitive.findAttribute("COLOR_0");
            if (colors != primitive.attributes.end()) 
            {
                fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf.get(), gltf->accessors[(*colors).accessorIndex],
                    [&](glm::vec4 v, size_t index) {
                        vertices[startVertex + index].color = v;
                    });
            }

            constexpr bool kOverrideColors = true;
            if constexpr (kOverrideColors)
            {
                for (Vertex& v : vertices)
                    v.color = glm::vec4(v.normal, 1.0f);
            }

            newMesh.surfaces.push_back(newSurface);
        }

        newMesh.data = ::LoadMesh(vertices, indices);
        newMesh.name = std::format("{}_{}", path.string(), meshIndex);

        indices.clear();
        vertices.clear();
    }

}