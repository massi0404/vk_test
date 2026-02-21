pushd %~dp0

%VK_SDK_PATH%/Bin/glslc.exe deferred/gbuffer.vert -o bin/vert_gbuffer.spv
%VK_SDK_PATH%/Bin/glslc.exe deferred/gbuffer.frag -o bin/frag_gbuffer.spv

%VK_SDK_PATH%/Bin/glslc.exe deferred/composite.vert -o bin/vert_composite.spv
%VK_SDK_PATH%/Bin/glslc.exe deferred/composite.frag -o bin/frag_composite.spv

%VK_SDK_PATH%/Bin/glslc.exe forward/forward.vert -o bin/vert_forward.spv
%VK_SDK_PATH%/Bin/glslc.exe forward/forward.frag -o bin/frag_forward.spv
%VK_SDK_PATH%/Bin/glslc.exe forward/forward_simple.frag -o bin/frag_forward_simple.spv

%VK_SDK_PATH%/Bin/glslc.exe shader.comp -o bin/comp.spv