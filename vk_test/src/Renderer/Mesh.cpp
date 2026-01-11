#include "Mesh.h"

#include "fastgltf/glm_element_traits.hpp"
#include "fastgltf/core.hpp"
#include "fastgltf/tools.hpp"

#include "Engine.h"
#include "ResourceFactory.h"

void Mesh::Load(const std::filesystem::path& path)
{
    fastgltf::Expected<fastgltf::GltfDataBuffer> data = fastgltf::GltfDataBuffer::FromPath(path);
    if (!data)
    {
        LOG_ERR("Unable to load mesh file: %ls", path.c_str());
        return;
    }
    
    constexpr auto gltfOptions = /*fastgltf::Options::LoadGLBBuffers | */ fastgltf::Options::LoadExternalBuffers;
    fastgltf::Parser parser;
    fastgltf::Expected<fastgltf::Asset> gltf = parser.loadGltfBinary(data.get(), path.parent_path(), gltfOptions);
    check(gltf);

    u64 totalVertexCount = 0;
    u64 totalIndexCount = 0;

    // megatodo: iterate gltf->nodes to fix transform and stuff of meshes

    m_Submeshes.reserve(gltf->meshes.size());
    for (fastgltf::Mesh& mesh : gltf->meshes)
    {
        Submesh submesh = {};
        submesh.indexOffset = totalIndexCount;
        for (const auto& primitive : mesh.primitives)
        {
            u64 primitiveVertexCount = gltf->accessors[primitive.findAttribute("POSITION")->accessorIndex].count;
            u64 primitiveIndexCount = gltf->accessors[primitive.indicesAccessor.value()].count;

            totalVertexCount += primitiveVertexCount;
            totalIndexCount += primitiveIndexCount;

            submesh.indexCount += primitiveIndexCount;
        }
        m_Submeshes.push_back(submesh);
    }

    m_Vertices.resize(totalVertexCount);
    m_Indices.reserve(totalIndexCount);

    u64 loadedVertexCount = 0;

    for (u32 i = 0; i < (u32)gltf->meshes.size(); i++)
    {
        const fastgltf::Mesh& mesh = gltf->meshes[i];
        const Submesh& meshDesc = m_Submeshes[i];
        
        // primitive (triangoli, quad, etc..)
        for (const auto& primitive : mesh.primitives)
        {
            check(primitive.type == fastgltf::PrimitiveType::Triangles);

            // load indexes
            fastgltf::Accessor& indexAccessor = gltf->accessors[primitive.indicesAccessor.value()];
            fastgltf::iterateAccessor<std::uint32_t>(gltf.get(), indexAccessor,
                [&](std::uint32_t idx) {
                    m_Indices.push_back(idx + loadedVertexCount);
                });

            // load vertices
            fastgltf::Accessor& posAccessor = gltf->accessors[primitive.findAttribute("POSITION")->accessorIndex];
            fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf.get(), posAccessor,
                [&](glm::vec3 v, size_t index) {
                    Vertex newvtx;
                    newvtx.position = v;
                    newvtx.normal = { 1, 0, 0 };
                    newvtx.color = glm::vec4{ 1.f };
                    newvtx.uv_x = 0;
                    newvtx.uv_y = 0;

                    m_Vertices[loadedVertexCount + index] = newvtx;
                });

            // load vertex normals
            auto normals = primitive.findAttribute("NORMAL");
            if (normals != primitive.attributes.end())
            {
                fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf.get(), gltf->accessors[(*normals).accessorIndex],
                    [&](glm::vec3 v, size_t index) {
                        m_Vertices[loadedVertexCount + index].normal = v;
                    });
            }

            // load UVs
            auto uv = primitive.findAttribute("TEXCOORD_0");
            if (uv != primitive.attributes.end())
            {
                fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf.get(), gltf->accessors[(*uv).accessorIndex],
                    [&](glm::vec2 v, size_t index) {
                        m_Vertices[loadedVertexCount + index].uv_x = v.x;
                        m_Vertices[loadedVertexCount + index].uv_y = v.y;
                    });
            }

            // load vertex colors
            auto colors = primitive.findAttribute("COLOR_0");
            if (colors != primitive.attributes.end())
            {
                fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf.get(), gltf->accessors[(*colors).accessorIndex],
                    [&](glm::vec4 v, size_t index) {
                        m_Vertices[loadedVertexCount + index].color = v;
                    });
            }

            loadedVertexCount += posAccessor.count;
        }
    }
    
    constexpr bool kOverrideColors = true;
    if constexpr (kOverrideColors)
    {
        for (Vertex& v : m_Vertices)
            v.color = glm::vec4(v.normal, 1.0f);
    }

    // create gpu vertex and index buffer (only create, no upload)
    g_ResourceFactory.CreateMesh(this);
    
    DebugName = path.string();
}

void Mesh::ClearData()
{
    m_Vertices.clear();
    m_Vertices.shrink_to_fit(); // free actual memory

    m_Indices.clear();
    m_Indices.shrink_to_fit(); // same
}