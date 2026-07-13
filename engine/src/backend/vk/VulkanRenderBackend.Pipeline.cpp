#include "engine/backend/vk/VulkanRenderBackend.h"

#include <cstddef>
#include <iterator>
#include <stdexcept>

namespace engine {

namespace {
constexpr VkFormat kHdrColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
}

void VulkanRenderBackend::CreateGraphicsPipeline()
{
    VkPipelineShaderStageCreateInfo vertexStage{};
    vertexStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertexStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertexStage.module = m_pbrVertexShader;
    vertexStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragmentStage{};
    fragmentStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragmentStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragmentStage.module = m_pbrFragmentShader;
    fragmentStage.pName = "main";
    const VkPipelineShaderStageCreateInfo stages[] = {vertexStage, fragmentStage};

    VkVertexInputBindingDescription vertexBinding{};
    vertexBinding.binding = 0;
    vertexBinding.stride = sizeof(Vertex);
    vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    const VkVertexInputAttributeDescription attributes[] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, Position))},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, Normal))},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, Tangent))},
        {3, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, UV))},
    };
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &vertexBinding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(std::size(attributes));
    vertexInput.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterization{};
    rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = m_msaaSamples;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    std::array<VkPipelineColorBlendAttachmentState, 2> blendAttachments{};
    for (VkPipelineColorBlendAttachmentState& attachment : blendAttachments)
        attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                  | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blending{};
    blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blending.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
    blending.pAttachments = blendAttachments.data();

    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(std::size(dynamicStates));
    dynamicState.pDynamicStates = dynamicStates;

    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    const VkFormat mainColorFormats[] = {kHdrColorFormat, VK_FORMAT_R16G16_SFLOAT};
    rendering.colorAttachmentCount = static_cast<uint32_t>(std::size(mainColorFormats));
    rendering.pColorAttachmentFormats = mainColorFormats;
    rendering.depthAttachmentFormat = m_depthFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &rendering;
    pipelineInfo.stageCount = static_cast<uint32_t>(std::size(stages));
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterization;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &blending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pbrPipelineLayout;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pbrPipeline) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create PBR graphics pipeline");
    SetDebugName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(m_pbrPipeline),
                 "Main HDR PBR Pipeline");

    VkPipelineShaderStageCreateInfo shadowVertexStage = vertexStage;
    shadowVertexStage.module = m_shadowVertexShader;
    VkPipelineShaderStageCreateInfo shadowFragmentStage = fragmentStage;
    shadowFragmentStage.module = m_shadowFragmentShader;
    const VkPipelineShaderStageCreateInfo shadowStages[] = {shadowVertexStage, shadowFragmentStage};
    const VkVertexInputAttributeDescription shadowAttributes[] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, Position))},
        {3, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, UV))},
    };
    VkPipelineVertexInputStateCreateInfo shadowVertexInput = vertexInput;
    shadowVertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(std::size(shadowAttributes));
    shadowVertexInput.pVertexAttributeDescriptions = shadowAttributes;
    VkPipelineRasterizationStateCreateInfo shadowRasterization = rasterization;
    shadowRasterization.cullMode = VK_CULL_MODE_FRONT_BIT;
    VkPipelineColorBlendStateCreateInfo shadowBlending{};
    shadowBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    VkPipelineRenderingCreateInfo shadowRendering{};
    shadowRendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    shadowRendering.depthAttachmentFormat = m_depthFormat;
    VkPipelineMultisampleStateCreateInfo shadowMultisampling = multisampling;
    shadowMultisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    pipelineInfo.pNext = &shadowRendering;
    pipelineInfo.stageCount = static_cast<uint32_t>(std::size(shadowStages));
    pipelineInfo.pStages = shadowStages;
    pipelineInfo.pVertexInputState = &shadowVertexInput;
    pipelineInfo.pRasterizationState = &shadowRasterization;
    pipelineInfo.pColorBlendState = &shadowBlending;
    pipelineInfo.pMultisampleState = &shadowMultisampling;
    pipelineInfo.layout = m_shadowPipelineLayout;
    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_shadowPipeline) != VK_SUCCESS)
    {
        DestroyGraphicsPipeline();
        throw std::runtime_error("Vulkan: failed to create cascade shadow pipeline");
    }
    SetDebugName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(m_shadowPipeline),
                 "Directional Shadow Pipeline");

    VkPipelineShaderStageCreateInfo skyVertexStage = vertexStage;
    skyVertexStage.module = m_skyVertexShader;
    VkPipelineShaderStageCreateInfo skyFragmentStage = fragmentStage;
    skyFragmentStage.module = m_skyFragmentShader;
    const VkPipelineShaderStageCreateInfo skyStages[] = {skyVertexStage, skyFragmentStage};
    VkPipelineVertexInputStateCreateInfo skyVertexInput{};
    skyVertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineDepthStencilStateCreateInfo skyDepth{};
    skyDepth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    VkPipelineRasterizationStateCreateInfo skyRasterization = rasterization;
    skyRasterization.cullMode = VK_CULL_MODE_NONE;
    pipelineInfo.pStages = skyStages;
    pipelineInfo.stageCount = static_cast<uint32_t>(std::size(skyStages));
    pipelineInfo.pVertexInputState = &skyVertexInput;
    pipelineInfo.pRasterizationState = &skyRasterization;
    pipelineInfo.pDepthStencilState = &skyDepth;
    pipelineInfo.pColorBlendState = &blending;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pNext = &rendering;
    pipelineInfo.layout = m_pbrPipelineLayout;
    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_skyPipeline) != VK_SUCCESS)
    {
        DestroyGraphicsPipeline();
        throw std::runtime_error("Vulkan: failed to create sky graphics pipeline");
    }
    SetDebugName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(m_skyPipeline),
                 "Main HDR Sky Pipeline");

    CreatePostPipelines();
}

void VulkanRenderBackend::DestroyGraphicsPipeline()
{
    DestroyPostPipelines();
    if (m_shadowPipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(m_device, m_shadowPipeline, nullptr);
    if (m_skyPipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(m_device, m_skyPipeline, nullptr);
    if (m_pbrPipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(m_device, m_pbrPipeline, nullptr);
    m_skyPipeline = VK_NULL_HANDLE;
    m_pbrPipeline = VK_NULL_HANDLE;
    m_shadowPipeline = VK_NULL_HANDLE;
}

} // namespace engine
