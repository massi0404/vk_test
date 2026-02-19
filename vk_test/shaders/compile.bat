pushd %~dp0

%VK_SDK_PATH%/Bin/glslc.exe gbuffer.vert -o bin/vert_gbuffer.spv
%VK_SDK_PATH%/Bin/glslc.exe gbuffer.frag -o bin/frag_gbuffer.spv

%VK_SDK_PATH%/Bin/glslc.exe composite.vert -o bin/vert_composite.spv
%VK_SDK_PATH%/Bin/glslc.exe composite.frag -o bin/frag_composite.spv

%VK_SDK_PATH%/Bin/glslc.exe forward.vert -o bin/vert_forward.spv
%VK_SDK_PATH%/Bin/glslc.exe forward.frag -o bin/frag_forward.spv
%VK_SDK_PATH%/Bin/glslc.exe forward_simple.frag -o bin/frag_forward_simple.spv

%VK_SDK_PATH%/Bin/glslc.exe shader.comp -o bin/comp.spv