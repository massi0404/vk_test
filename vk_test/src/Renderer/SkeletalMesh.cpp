#include "SkeletalMesh.h"

#include "assimp/scene.h"
#include "assimp/postprocess.h"
#include "assimp/Importer.hpp"

#include "Engine.h"
#include "ResourceFactory.h"

#include <map>

static void AssimpMatToGlmMat(const aiMatrix4x4& assimpMat, glm::mat4& outGlmMat)
{
    outGlmMat[0] = glm::vec4(assimpMat.a1, assimpMat.b1, assimpMat.c1, assimpMat.d1);
    outGlmMat[1] = glm::vec4(assimpMat.a2, assimpMat.b2, assimpMat.c2, assimpMat.d2);
    outGlmMat[2] = glm::vec4(assimpMat.a3, assimpMat.b3, assimpMat.c3, assimpMat.d3);
    outGlmMat[3] = glm::vec4(assimpMat.a4, assimpMat.b4, assimpMat.c4, assimpMat.d4);
}

static void AssimpToGlmKey(const aiVectorKey& aiVecKey, PositionKey& outVecKey)
{
    auto& [outPos, outTime] = outVecKey;
    outPos = glm::vec3(aiVecKey.mValue.x, aiVecKey.mValue.y, aiVecKey.mValue.z);
    outTime = (float)aiVecKey.mTime;
}

static void AssimpToGlmKey(const aiQuatKey& aiQuatKey, RotationKey& outQuatKey)
{
    auto& [outRot, outTime] = outQuatKey;
    outRot = glm::quat(aiQuatKey.mValue.w, aiQuatKey.mValue.x, aiQuatKey.mValue.y, aiQuatKey.mValue.z);
    outTime = (float)aiQuatKey.mTime;
}

void PrintNodes(aiNode* node, u32 itLevel)
{
    std::string tabs;
    tabs.resize(itLevel);
    memset(tabs.data(), '\t', itLevel);

    LOG_INFO("%s %s", tabs.c_str(), node->mName.C_Str());

    for (u32 i = 0; i < node->mNumChildren; i++)
    {
        aiNode* child = node->mChildren[i];
        PrintNodes(child, itLevel + 1);
    }
}

void PrintNodes(Node& node, u32 itLevel)
{
    std::string tabs;
    tabs.resize(itLevel);
    memset(tabs.data(), ' ', itLevel);

    LOG_INFO("%s %s", tabs.c_str(), node.debugName.c_str());

    for (u32 i = 0; i < node.children.size(); i++)
    {
        Node& child = node.children[i];
        PrintNodes(child, itLevel + 1);
    }
}

static void CreateNodeStructure(aiNode* node, Node& outNode, std::map<std::string, Node*>& outNodeMap, int& nodeID)
{
    outNode.debugName = node->mName.C_Str();
    outNode.nodeID = nodeID;
    AssimpMatToGlmMat(node->mTransformation, outNode.localTransform);
    outNodeMap[outNode.debugName] = &outNode;
    nodeID++;

    outNode.children.resize(node->mNumChildren);
    for (u32 i = 0; i < node->mNumChildren; i++)
        CreateNodeStructure(node->mChildren[i], outNode.children[i], outNodeMap, nodeID);
}

void SkeletalMesh::Load(const std::filesystem::path& path)
{
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path.string(),
        aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_ConvertToLeftHanded);

    // submeshes
    m_Submeshes.resize(scene->mNumMeshes);

    u32 totalVertexCount = 0;
    u32 totalIndexCount = 0;
    for (u32 i = 0; i < scene->mNumMeshes; i++)
    {
        aiMesh* mesh = scene->mMeshes[i]; // submesh per noi

        // le faces sono i poligoni (nel nostro caso sempre triangoli abbiamo il flag abbiamo aiProcess_Triangulate)
        u32 indexCount = mesh->mNumFaces * 3;

        Submesh& sub = m_Submeshes.emplace_back();
        sub.indexOffset = totalIndexCount;
        sub.indexCount = indexCount;

        // update total vertex and index count
        totalVertexCount += mesh->mNumVertices;
        totalIndexCount += indexCount;
    }

    m_Vertices.resize(totalVertexCount);
    m_Indices.resize(totalIndexCount);

    // nodes
    int nodeID = 0;
    std::map<std::string, Node*> nodeMap;
    CreateNodeStructure(scene->mRootNode, m_RootNode, nodeMap, nodeID);

    m_NodeCount = nodeID;

    //PrintNodes(m_RootNode, 0);

    // vertex data
    u32 currentVertex = 0;
    u32 currentIndex = 0;
    u32 currentBone = 0;
    
    u32 currentBoneDataIndex = 0;

    for (u32 i = 0; i < scene->mNumMeshes; i++)
    {
        aiMesh* mesh = scene->mMeshes[i]; // submesh per noi
        Submesh& submesh = m_Submeshes[i];

        u32 submeshVertexOffset = currentVertex;

        // vertex data
        for (u32 j = 0; j < mesh->mNumVertices; j++)
        {
            check(mesh->mNormals);

            SkeletalVertex& vec = m_Vertices[currentVertex];

            vec.position = { mesh->mVertices[j].x, mesh->mVertices[j].y, mesh->mVertices[j].z };
            vec.normal = { mesh->mNormals[j].x, mesh->mNormals[j].y, mesh->mNormals[j].z };
            vec.bones[0] = vec.bones[1] = vec.bones[2] = vec.bones[3] = -1;
            vec.bonesWeight[0] = vec.bonesWeight[1] = vec.bonesWeight[2] = vec.bonesWeight[3] = 0.0f;

            if (aiVector3D* texCoords = mesh->mTextureCoords[0]) // canali [0, 8] ovvero tipo dei layer, noi usiamo il primo 0
            {
                vec.uv_x = texCoords[j].x;
                vec.uv_y = texCoords[j].y;
            }
            else
            {
                vec.uv_x = vec.uv_y = 0;
            }

#if 0
            if (aiColor4D* colors = mesh->mColors[0]) // anche qui stessa roba
                vec.color = { colors[j].r, colors[j].g, colors[j].b, colors[j].a };
            else
                vec.color = glm::vec4(1.0);
#else

            constexpr glm::vec4 defaultColors[] = {
                glm::vec4(0.78f, 0.48f, 0.01f, 1.0f),
                glm::vec4(0.78f, 0.237f, 0.01f, 1.0f)
            };

            vec.color = defaultColors[i % scene->mNumMeshes];
#endif

            currentVertex++;
        }

        // index data
        for (u32 j = 0; j < mesh->mNumFaces; j++)
        {
            aiFace& face = mesh->mFaces[j];

            // sono sempre triangoli, quindi 3 indici x faccia
            m_Indices[currentIndex + 0] = submeshVertexOffset + (u32)face.mIndices[0];
            m_Indices[currentIndex + 1] = submeshVertexOffset + (u32)face.mIndices[1];
            m_Indices[currentIndex + 2] = submeshVertexOffset + (u32)face.mIndices[2];

            currentIndex += 3;
        }

        // bone data
        for (u32 j = 0; j < mesh->mNumBones; j++)
        {
            aiBone* bone = mesh->mBones[j];

            std::string nodeName = bone->mName.C_Str();
            Node* node = nodeMap[nodeName];

            if (node->boneDataIndex == -1)
            {
                AssimpMatToGlmMat(bone->mOffsetMatrix, node->offsetMatrix);

                node->boneDataIndex = currentBoneDataIndex;
                currentBoneDataIndex++;
            }

            for (u32 weightIndex = 0; weightIndex < bone->mNumWeights; weightIndex++)
            {
                u32 vertexIndex = submeshVertexOffset + bone->mWeights[weightIndex].mVertexId;
                check(vertexIndex >= 0 && vertexIndex < m_Vertices.size());

                SkeletalVertex& vertex = m_Vertices[vertexIndex];
                
                // set weights (max 4)
                for (u32 vertexWeight = 0; vertexWeight < MAX_BONE_WEIGHTS; vertexWeight++)
                {
                    float boneWeight = bone->mWeights[weightIndex].mWeight;
                    if (vertex.bones[vertexWeight] == -1 || vertex.bonesWeight[vertexWeight] < boneWeight)
                    {
                        vertex.bones[vertexWeight] = node->boneDataIndex;
                        vertex.bonesWeight[vertexWeight] = boneWeight;
                    }
                }
            }

            currentBone++;
        }
    }

    m_BoneCount = currentBoneDataIndex;

    // animation data
    m_Animations.resize(scene->mNumAnimations);

    for (u32 i = 0; i < scene->mNumAnimations; i++)
    {
        aiAnimation* aiAnimation = scene->mAnimations[i];
        Animation& anim = m_Animations[i];
        
        anim.name = aiAnimation->mName.C_Str();

        anim.nodeKeyframes.resize(aiAnimation->mNumChannels);
        
        anim.keyFrameMap.resize(nodeID);
        for (u32& keyFrameIndex : anim.keyFrameMap)
            keyFrameIndex = -1;

        anim.frameCount = (u32)aiAnimation->mDuration;
        anim.frameRate = (u32)aiAnimation->mTicksPerSecond;

        float totalDuration = aiAnimation->mDuration / aiAnimation->mTicksPerSecond;
        //LOG_INFO("Anim: '%s', %f FPS, %f frames, duration: %f seconds, channels: %d", aiAnimation->mName.C_Str(), aiAnimation->mTicksPerSecond, aiAnimation->mDuration, totalDuration, aiAnimation->mNumChannels);

        for (u32 channelIndex = 0; channelIndex < aiAnimation->mNumChannels; channelIndex++)
        {
            aiNodeAnim* channel = aiAnimation->mChannels[channelIndex];
            NodeAnim& nodeAnim = anim.nodeKeyframes[channelIndex];

            std::string nodeName = channel->mNodeName.C_Str();

            check(nodeMap.contains(nodeName));
            Node* node = nodeMap[nodeName];

            nodeAnim.keyFramesPos.resize(channel->mNumPositionKeys);
            nodeAnim.keyFramesRot.resize(channel->mNumRotationKeys);
            nodeAnim.keyFramesScale.resize(channel->mNumScalingKeys);

            for (u32 keyIndex = 0; keyIndex < channel->mNumPositionKeys; keyIndex++)
                AssimpToGlmKey(channel->mPositionKeys[keyIndex], nodeAnim.keyFramesPos[keyIndex]);

            for (u32 keyIndex = 0; keyIndex < channel->mNumRotationKeys; keyIndex++)
                AssimpToGlmKey(channel->mRotationKeys[keyIndex], nodeAnim.keyFramesRot[keyIndex]);

            for (u32 keyIndex = 0; keyIndex < channel->mNumScalingKeys; keyIndex++)
                AssimpToGlmKey(channel->mScalingKeys[keyIndex], nodeAnim.keyFramesScale[keyIndex]);
                
            check(anim.keyFrameMap[node->nodeID] == -1);
            anim.keyFrameMap[node->nodeID] = channelIndex;
        }

        Anim::BakeAnimation(this, &anim, anim.bakedAnimData);
    }

    DebugName = path.string();
}

void SkeletalMesh::ClearData()
{
    m_Vertices.clear();
    m_Vertices.shrink_to_fit(); // free actual memory

    m_Indices.clear();
    m_Indices.shrink_to_fit(); // same
}

void SkeletalMesh::CreateOnGPU()
{
    g_ResourceFactory.CreateSkeletalMesh(this);
}
