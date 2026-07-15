struct EngineInstanceData
{
    mat4 Model;
    mat4 PreviousModel;
};

#if defined(ENGINE_OPENGL)
layout(std430, binding = 3) readonly buffer EngineInstanceBuffer
{
    EngineInstanceData engineInstances[];
};
#define ENGINE_INSTANCE_DATA engineInstances[uint(gl_BaseInstance) + uint(gl_InstanceID)]
#elif defined(ENGINE_VULKAN)
layout(set = 2, binding = 0, std430) readonly buffer EngineInstanceBuffer
{
    EngineInstanceData engineInstances[];
};
#define ENGINE_INSTANCE_DATA engineInstances[uint(gl_InstanceIndex)]
#else
#error Backend instance-buffer declaration is missing
#endif
