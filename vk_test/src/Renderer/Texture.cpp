#include "Texture.h"
#include "stb/stb_image.h"
#include "Engine.h"
#include "Renderer/ResourceFactory.h"

void Texture::Load(const std::filesystem::path& path)
{
    int width, height;
    int channels;

    FILE* file;
    errno_t err = _wfopen_s(&file, path.c_str(), L"rb");
    if (err)
    {
        LOG_ERR("Unable to load texture file: %ls", path.c_str());
        return;
    }

    stbi_uc* data = stbi_load_from_file(file, &width, &height, &channels, 4);
    check(data);

    u64 sizeBytes = width * height * 4;

    m_Data.resize(sizeBytes);
    memcpy(m_Data.data(), data, sizeBytes);

    m_Desc.format = EImageFormat::RGBA8;
    m_Desc.width = (u32)width;
    m_Desc.height = (u32)height;

    g_ResourceFactory.CreateTexture(this);

    DebugName = path.string();
}

static u32 GetPixelSize(EImageFormat format)
{
    switch (format)
    {
        case EImageFormat::Undefined: return 0;
        case EImageFormat::RGBA8:     return 4;
    }

    check(0);
    return 0;
}

void Texture::SetData(const void* data, u64 size, const TextureDesc& desc)
{
    check(data);
    check(size == desc.width * desc.height * GetPixelSize(desc.format));

    m_Data.resize(size);
    memcpy(m_Data.data(), data, size);

    m_Desc = desc;

    g_ResourceFactory.CreateTexture(this);
}

void Texture::ClearData()
{
    m_Data.clear();
    m_Data.shrink_to_fit(); // free actual memory

    m_Desc = {};
}
