#include "vulkan_renderer.h"
#include "vulkan_viewport.h"
#include "shaders_embedded.h"

#include <QtCore/qdebug.h>
#include <cstring>
#include <cmath>

static uint32_t findMemoryType(VkPhysicalDevice physDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties, QVulkanInstance* inst) {
    VkPhysicalDeviceMemoryProperties memProperties;
    inst->functions()->vkGetPhysicalDeviceMemoryProperties(physDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return 0;
}

VulkanRenderer::VulkanRenderer(VulkanViewport* window)
    : m_window(window),
      m_devFuncs(nullptr),
      m_device(VK_NULL_HANDLE),
      m_descPool(VK_NULL_HANDLE),
      m_descSetLayout(VK_NULL_HANDLE),
      m_descSet(VK_NULL_HANDLE),
      m_pipelineLayout(VK_NULL_HANDLE),
      m_pipeline(VK_NULL_HANDLE),
      m_ubo(VK_NULL_HANDLE),
      m_uboMem(VK_NULL_HANDLE),
      m_vertexBuffer(VK_NULL_HANDLE),
      m_vertexMem(VK_NULL_HANDLE),
      m_vertexBufferSize(0),
      m_vertexCount(0),
      m_textureImage(VK_NULL_HANDLE),
      m_textureMem(VK_NULL_HANDLE),
      m_textureView(VK_NULL_HANDLE),
      m_textureSampler(VK_NULL_HANDLE),
      m_textureNeedsUpload(false),
      m_vehicleTextureImage(VK_NULL_HANDLE),
      m_vehicleTextureMem(VK_NULL_HANDLE),
      m_vehicleTextureView(VK_NULL_HANDLE),
      m_vehicleTextureSampler(VK_NULL_HANDLE),
      m_vehicleTextureNeedsUpload(false),
      m_wireHeight(1.448f),
      m_showWire(true),
      m_meshNeedsUpload(false) {
}

VulkanRenderer::~VulkanRenderer() {
}

void VulkanRenderer::initResources() {
    m_device = m_window->device();
    m_devFuncs = m_window->vulkanInstance()->deviceFunctions(m_device);

    createUniformBuffer();
    createPipeline();
    createVertexBuffer();
    createDefaultTextures();

    const QImage& cached = m_window->cachedTexture();
    if (!cached.isNull()) {
        uploadImageToTexture(cached, 1, m_textureImage, m_textureMem, m_textureView, m_textureSampler);
    }
    const QImage& cachedVehicle = m_window->cachedVehicleTexture();
    if (!cachedVehicle.isNull()) {
        uploadImageToTexture(cachedVehicle, 2, m_vehicleTextureImage, m_vehicleTextureMem, m_vehicleTextureView, m_vehicleTextureSampler);
    }
    m_meshNeedsUpload = true;
}

void VulkanRenderer::initSwapChainResources() {
    m_meshNeedsUpload = true;
}

void VulkanRenderer::releaseSwapChainResources() {
}

void VulkanRenderer::releaseResources() {
    if (m_textureSampler != VK_NULL_HANDLE) {
        m_devFuncs->vkDestroySampler(m_device, m_textureSampler, nullptr);
        m_textureSampler = VK_NULL_HANDLE;
    }
    if (m_textureView != VK_NULL_HANDLE) {
        m_devFuncs->vkDestroyImageView(m_device, m_textureView, nullptr);
        m_textureView = VK_NULL_HANDLE;
    }
    if (m_textureImage != VK_NULL_HANDLE) {
        m_devFuncs->vkDestroyImage(m_device, m_textureImage, nullptr);
        m_textureImage = VK_NULL_HANDLE;
    }
    if (m_textureMem != VK_NULL_HANDLE) {
        m_devFuncs->vkFreeMemory(m_device, m_textureMem, nullptr);
        m_textureMem = VK_NULL_HANDLE;
    }

    if (m_vehicleTextureSampler != VK_NULL_HANDLE) {
        m_devFuncs->vkDestroySampler(m_device, m_vehicleTextureSampler, nullptr);
        m_vehicleTextureSampler = VK_NULL_HANDLE;
    }
    if (m_vehicleTextureView != VK_NULL_HANDLE) {
        m_devFuncs->vkDestroyImageView(m_device, m_vehicleTextureView, nullptr);
        m_vehicleTextureView = VK_NULL_HANDLE;
    }
    if (m_vehicleTextureImage != VK_NULL_HANDLE) {
        m_devFuncs->vkDestroyImage(m_device, m_vehicleTextureImage, nullptr);
        m_vehicleTextureImage = VK_NULL_HANDLE;
    }
    if (m_vehicleTextureMem != VK_NULL_HANDLE) {
        m_devFuncs->vkFreeMemory(m_device, m_vehicleTextureMem, nullptr);
        m_vehicleTextureMem = VK_NULL_HANDLE;
    }

    if (m_pipeline != VK_NULL_HANDLE) {
        m_devFuncs->vkDestroyPipeline(m_device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        m_devFuncs->vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
    if (m_descSetLayout != VK_NULL_HANDLE) {
        m_devFuncs->vkDestroyDescriptorSetLayout(m_device, m_descSetLayout, nullptr);
        m_descSetLayout = VK_NULL_HANDLE;
    }
    if (m_descPool != VK_NULL_HANDLE) {
        m_devFuncs->vkDestroyDescriptorPool(m_device, m_descPool, nullptr);
        m_descPool = VK_NULL_HANDLE;
    }
    if (m_ubo != VK_NULL_HANDLE) {
        m_devFuncs->vkDestroyBuffer(m_device, m_ubo, nullptr);
        m_devFuncs->vkFreeMemory(m_device, m_uboMem, nullptr);
        m_ubo = VK_NULL_HANDLE;
    }
    if (m_vertexBuffer != VK_NULL_HANDLE) {
        m_devFuncs->vkDestroyBuffer(m_device, m_vertexBuffer, nullptr);
        m_devFuncs->vkFreeMemory(m_device, m_vertexMem, nullptr);
        m_vertexBuffer = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::createUniformBuffer() {
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = sizeof(UniformBufferObject);
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    m_devFuncs->vkCreateBuffer(m_device, &bufferInfo, nullptr, &m_ubo);

    VkMemoryRequirements memReq;
    m_devFuncs->vkGetBufferMemoryRequirements(m_device, m_ubo, &memReq);

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(
        m_window->physicalDevice(),
        memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_window->vulkanInstance()
    );

    m_devFuncs->vkAllocateMemory(m_device, &allocInfo, nullptr, &m_uboMem);
    m_devFuncs->vkBindBufferMemory(m_device, m_ubo, m_uboMem, 0);
}

void VulkanRenderer::createPipeline() {
    // Descriptor Set Layout:
    // Binding 0: UBO
    // Binding 1: Pantograph Texture Sampler
    // Binding 2: Vehicle Texture Sampler
    VkDescriptorSetLayoutBinding bindings[3] = {};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 3;
    layoutInfo.pBindings = bindings;

    m_devFuncs->vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_descSetLayout);

    // Descriptor Pool
    VkDescriptorPoolSize poolSizes[2] = {};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 2; // Binding 1 and Binding 2

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 1;

    m_devFuncs->vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descPool);

    // Descriptor Set
    VkDescriptorSetAllocateInfo setAllocInfo = {};
    setAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAllocInfo.descriptorPool = m_descPool;
    setAllocInfo.descriptorSetCount = 1;
    setAllocInfo.pSetLayouts = &m_descSetLayout;

    m_devFuncs->vkAllocateDescriptorSets(m_device, &setAllocInfo, &m_descSet);

    // Bind UBO to binding 0
    VkDescriptorBufferInfo dbi = {};
    dbi.buffer = m_ubo;
    dbi.offset = 0;
    dbi.range = sizeof(UniformBufferObject);

    VkWriteDescriptorSet descriptorWrite = {};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_descSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &dbi;

    m_devFuncs->vkUpdateDescriptorSets(m_device, 1, &descriptorWrite, 0, nullptr);

    // Shaders
    VkShaderModuleCreateInfo vertInfo = {};
    vertInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vertInfo.codeSize = SPV_MESH_VERT_size;
    vertInfo.pCode = SPV_MESH_VERT;

    VkShaderModule vertShader;
    m_devFuncs->vkCreateShaderModule(m_device, &vertInfo, nullptr, &vertShader);

    VkShaderModuleCreateInfo fragInfo = {};
    fragInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fragInfo.codeSize = SPV_MESH_FRAG_size;
    fragInfo.pCode = SPV_MESH_FRAG;

    VkShaderModule fragShader;
    m_devFuncs->vkCreateShaderModule(m_device, &fragInfo, nullptr, &fragShader);

    VkPipelineShaderStageCreateInfo shaderStages[2] = {};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertShader;
    shaderStages[0].pName = "main";

    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragShader;
    shaderStages[1].pName = "main";

    // Vertex Input: pos(0), normal(1), uv(2), part_id(3)
    VkVertexInputBindingDescription bindingDesc = {};
    bindingDesc.binding = 0;
    bindingDesc.stride = sizeof(EdpcVertex);
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrDesc[4] = {};
    // pos
    attrDesc[0].location = 0;
    attrDesc[0].binding = 0;
    attrDesc[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrDesc[0].offset = offsetof(EdpcVertex, pos);
    // normal
    attrDesc[1].location = 1;
    attrDesc[1].binding = 0;
    attrDesc[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrDesc[1].offset = offsetof(EdpcVertex, normal);
    // uv
    attrDesc[2].location = 2;
    attrDesc[2].binding = 0;
    attrDesc[2].format = VK_FORMAT_R32G32_SFLOAT;
    attrDesc[2].offset = offsetof(EdpcVertex, uv);
    // part_id
    attrDesc[3].location = 3;
    attrDesc[3].binding = 0;
    attrDesc[3].format = VK_FORMAT_R32_UINT;
    attrDesc[3].offset = offsetof(EdpcVertex, part_id);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = 4;
    vertexInputInfo.pVertexAttributeDescriptions = attrDesc;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Dynamic Viewport / Scissor
    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE; // Double sided
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = m_window->sampleCountFlagBits();

    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_descSetLayout;

    m_devFuncs->vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout);

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = m_window->defaultRenderPass();
    pipelineInfo.subpass = 0;

    m_devFuncs->vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline);

    m_devFuncs->vkDestroyShaderModule(m_device, vertShader, nullptr);
    m_devFuncs->vkDestroyShaderModule(m_device, fragShader, nullptr);
}

void VulkanRenderer::createVertexBuffer() {
    // 300,000 vertices capacity for vehicle + pantograph + catenary + grid
    m_vertexBufferSize = 300000 * sizeof(EdpcVertex);

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = m_vertexBufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    m_devFuncs->vkCreateBuffer(m_device, &bufferInfo, nullptr, &m_vertexBuffer);

    VkMemoryRequirements memReq;
    m_devFuncs->vkGetBufferMemoryRequirements(m_device, m_vertexBuffer, &memReq);

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(
        m_window->physicalDevice(),
        memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_window->vulkanInstance()
    );

    m_devFuncs->vkAllocateMemory(m_device, &allocInfo, nullptr, &m_vertexMem);
    m_devFuncs->vkBindBufferMemory(m_device, m_vertexBuffer, m_vertexMem, 0);

    m_meshNeedsUpload = true;
}

VkCommandBuffer VulkanRenderer::beginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_window->graphicsCommandPool();
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cb;
    m_devFuncs->vkAllocateCommandBuffers(m_device, &allocInfo, &cb);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    m_devFuncs->vkBeginCommandBuffer(cb, &beginInfo);
    return cb;
}

void VulkanRenderer::endSingleTimeCommands(VkCommandBuffer cb) {
    m_devFuncs->vkEndCommandBuffer(cb);

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cb;

    m_devFuncs->vkQueueSubmit(m_window->graphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    m_devFuncs->vkQueueWaitIdle(m_window->graphicsQueue());

    m_devFuncs->vkFreeCommandBuffers(m_device, m_window->graphicsCommandPool(), 1, &cb);
}

void VulkanRenderer::createDefaultTextures() {
    QImage defaultPantoImg(2, 2, QImage::Format_RGBA8888);
    defaultPantoImg.fill(QColor(220, 225, 230, 255));
    uploadImageToTexture(defaultPantoImg, 1, m_textureImage, m_textureMem, m_textureView, m_textureSampler);

    QImage defaultVehicleImg(2, 2, QImage::Format_RGBA8888);
    defaultVehicleImg.fill(QColor(180, 190, 205, 255));
    uploadImageToTexture(defaultVehicleImg, 2, m_vehicleTextureImage, m_vehicleTextureMem, m_vehicleTextureView, m_vehicleTextureSampler);
}

void VulkanRenderer::uploadImageToTexture(const QImage& inputImage, uint32_t dstBinding,
                                          VkImage& outImg, VkDeviceMemory& outMem,
                                          VkImageView& outView, VkSampler& outSampler) {
    QImage img = inputImage.convertToFormat(QImage::Format_RGBA8888);
    uint32_t width = static_cast<uint32_t>(img.width());
    uint32_t height = static_cast<uint32_t>(img.height());
    if (width == 0 || height == 0) return;
    VkDeviceSize imageSize = width * height * 4;

    // 1. Create Staging Buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMem;
    VkBufferCreateInfo bufInfo = {};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = imageSize;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    m_devFuncs->vkCreateBuffer(m_device, &bufInfo, nullptr, &stagingBuffer);

    VkMemoryRequirements memReq;
    m_devFuncs->vkGetBufferMemoryRequirements(m_device, stagingBuffer, &memReq);

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(
        m_window->physicalDevice(),
        memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_window->vulkanInstance()
    );
    m_devFuncs->vkAllocateMemory(m_device, &allocInfo, nullptr, &stagingBufferMem);
    m_devFuncs->vkBindBufferMemory(m_device, stagingBuffer, stagingBufferMem, 0);

    void* data;
    m_devFuncs->vkMapMemory(m_device, stagingBufferMem, 0, imageSize, 0, &data);
    std::memcpy(data, img.constBits(), imageSize);
    m_devFuncs->vkUnmapMemory(m_device, stagingBufferMem);

    // 2. Clean up previous texture if any
    if (m_device != VK_NULL_HANDLE) {
        m_devFuncs->vkDeviceWaitIdle(m_device);
    }
    if (outSampler != VK_NULL_HANDLE) {
        m_devFuncs->vkDestroySampler(m_device, outSampler, nullptr);
        outSampler = VK_NULL_HANDLE;
    }
    if (outView != VK_NULL_HANDLE) {
        m_devFuncs->vkDestroyImageView(m_device, outView, nullptr);
        outView = VK_NULL_HANDLE;
    }
    if (outImg != VK_NULL_HANDLE) {
        m_devFuncs->vkDestroyImage(m_device, outImg, nullptr);
        outImg = VK_NULL_HANDLE;
    }
    if (outMem != VK_NULL_HANDLE) {
        m_devFuncs->vkFreeMemory(m_device, outMem, nullptr);
        outMem = VK_NULL_HANDLE;
    }

    // 3. Create Optimal VkImage
    VkImageCreateInfo imgInfo = {};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.extent.width = width;
    imgInfo.extent.height = height;
    imgInfo.extent.depth = 1;
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imgInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    m_devFuncs->vkCreateImage(m_device, &imgInfo, nullptr, &outImg);

    m_devFuncs->vkGetImageMemoryRequirements(m_device, outImg, &memReq);
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(
        m_window->physicalDevice(),
        memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_window->vulkanInstance()
    );
    m_devFuncs->vkAllocateMemory(m_device, &allocInfo, nullptr, &outMem);
    m_devFuncs->vkBindImageMemory(m_device, outImg, outMem, 0);

    // 4. Copy via command buffer
    VkCommandBuffer cb = beginSingleTimeCommands();

    // Transition to TRANSFER_DST_OPTIMAL
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = outImg;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    m_devFuncs->vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    m_devFuncs->vkCmdCopyBufferToImage(cb, stagingBuffer, outImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition to SHADER_READ_ONLY_OPTIMAL
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    m_devFuncs->vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    endSingleTimeCommands(cb);

    m_devFuncs->vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    m_devFuncs->vkFreeMemory(m_device, stagingBufferMem, nullptr);

    // 5. Create ImageView
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = outImg;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    m_devFuncs->vkCreateImageView(m_device, &viewInfo, nullptr, &outView);

    // 6. Create Sampler (NEAREST filter for crisp Minecraft Blockbench texture aesthetic)
    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    m_devFuncs->vkCreateSampler(m_device, &samplerInfo, nullptr, &outSampler);

    // 7. Update Descriptor Set at dstBinding
    VkDescriptorImageInfo descImgInfo = {};
    descImgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descImgInfo.imageView = outView;
    descImgInfo.sampler = outSampler;

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_descSet;
    write.dstBinding = dstBinding;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &descImgInfo;

    m_devFuncs->vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

void VulkanRenderer::updateTexture(const QImage& image) {
    std::lock_guard<std::mutex> lock(m_textureMutex);
    m_pendingTexture = image;
    m_textureNeedsUpload = true;
}

void VulkanRenderer::updateVehicleTexture(const QImage& image) {
    std::lock_guard<std::mutex> lock(m_textureMutex);
    m_pendingVehicleTexture = image;
    m_vehicleTextureNeedsUpload = true;
}

void VulkanRenderer::updateMeshData(const std::vector<EdpcVertex>& vertices) {
    std::lock_guard<std::mutex> lock(m_meshMutex);
    m_currentMesh = vertices;
    m_meshNeedsUpload = true;
}

void VulkanRenderer::updateVehicleMeshData(const std::vector<EdpcVertex>& vertices) {
    std::lock_guard<std::mutex> lock(m_meshMutex);
    m_vehicleMesh = vertices;
    m_meshNeedsUpload = true;
}

void VulkanRenderer::updateWireHeight(float height) {
    std::lock_guard<std::mutex> lock(m_meshMutex);
    m_wireHeight = height;
    m_meshNeedsUpload = true;
}

void VulkanRenderer::setShowWire(bool show) {
    std::lock_guard<std::mutex> lock(m_meshMutex);
    m_showWire = show;
    m_meshNeedsUpload = true;
}

void VulkanRenderer::generateCatenaryAndGrid(std::vector<EdpcVertex>& outVerts) {
    EdpcVec2 zeroUv{0.0f, 0.0f};

    // 1. Reference Catenary Wire
    if (m_showWire) {
        float r = 0.012f;
        float zStart = -15.0f;
        float zEnd = 15.0f;
        // Wire center so that the bottom of the wire box is at exactly m_wireHeight
        float y = m_wireHeight + r;

        EdpcVec3 p0{-r, y - r, zStart};
        EdpcVec3 p1{ r, y - r, zStart};
        EdpcVec3 p2{ r, y + r, zStart};
        EdpcVec3 p3{-r, y + r, zStart};
        EdpcVec3 p4{-r, y - r, zEnd};
        EdpcVec3 p5{ r, y - r, zEnd};
        EdpcVec3 p6{ r, y + r, zEnd};
        EdpcVec3 p7{-r, y + r, zEnd};

        auto addQuad = [&](EdpcVec3 a, EdpcVec3 b, EdpcVec3 c, EdpcVec3 d, EdpcVec3 n) {
            outVerts.push_back({a, n, zeroUv, 6});
            outVerts.push_back({b, n, zeroUv, 6});
            outVerts.push_back({c, n, zeroUv, 6});
            outVerts.push_back({a, n, zeroUv, 6});
            outVerts.push_back({c, n, zeroUv, 6});
            outVerts.push_back({d, n, zeroUv, 6});
        };

        addQuad(p4, p5, p6, p7, {0, 0, 1});
        addQuad(p1, p0, p3, p2, {0, 0, -1});
        addQuad(p3, p2, p6, p7, {0, 1, 0});
        addQuad(p0, p4, p5, p1, {0, -1, 0});
        addQuad(p1, p5, p6, p2, {1, 0, 0});
        addQuad(p4, p0, p3, p7, {-1, 0, 0});
    }

    // 2. Grid on ground level (Y = 0)
    float gridExt = 6.0f;
    float step = 0.50f;
    float thick = 0.005f;
    EdpcVec3 nUp{0, 1, 0};

    auto addGridStripe = [&](float x0, float z0, float x1, float z1) {
        EdpcVec3 a{x0, 0.0f, z0};
        EdpcVec3 b{x1, 0.0f, z0};
        EdpcVec3 c{x1, 0.0f, z1};
        EdpcVec3 d{x0, 0.0f, z1};
        outVerts.push_back({a, nUp, zeroUv, 7});
        outVerts.push_back({b, nUp, zeroUv, 7});
        outVerts.push_back({c, nUp, zeroUv, 7});
        outVerts.push_back({a, nUp, zeroUv, 7});
        outVerts.push_back({c, nUp, zeroUv, 7});
        outVerts.push_back({d, nUp, zeroUv, 7});
    };

    for (float x = -gridExt; x <= gridExt + 1e-4f; x += step) {
        addGridStripe(x - thick, -gridExt, x + thick, gridExt);
    }
    for (float z = -gridExt; z <= gridExt + 1e-4f; z += step) {
        addGridStripe(-gridExt, z - thick, gridExt, z + thick);
    }
}

void VulkanRenderer::updateUbo(const QMatrix4x4& projection, const QMatrix4x4& view) {
    UniformBufferObject ubo = {};
    QMatrix4x4 model;
    model.setToIdentity();

    QMatrix4x4 mvp = projection * view * model;
    std::memcpy(ubo.mvp, mvp.constData(), 16 * sizeof(float));
    std::memcpy(ubo.model, model.constData(), 16 * sizeof(float));

    // Directional light from upper-front
    ubo.lightDir[0] = 0.4f;
    ubo.lightDir[1] = 0.85f;
    ubo.lightDir[2] = 0.35f;
    ubo.lightDir[3] = 0.0f;

    ubo.wireColor[0] = 1.0f;
    ubo.wireColor[1] = 0.82f;
    ubo.wireColor[2] = 0.25f;
    ubo.wireColor[3] = 1.0f;

    ubo.wireHeight = m_wireHeight;

    void* data;
    m_devFuncs->vkMapMemory(m_device, m_uboMem, 0, sizeof(UniformBufferObject), 0, &data);
    std::memcpy(data, &ubo, sizeof(UniformBufferObject));
    m_devFuncs->vkUnmapMemory(m_device, m_uboMem);
}

void VulkanRenderer::startNextFrame() {
    VkCommandBuffer cb = m_window->currentCommandBuffer();
    const QSize sz = m_window->swapChainImageSize();

    // Check if textures need upload
    {
        std::lock_guard<std::mutex> lock(m_textureMutex);
        if (m_textureNeedsUpload && !m_pendingTexture.isNull()) {
            uploadImageToTexture(m_pendingTexture, 1, m_textureImage, m_textureMem, m_textureView, m_textureSampler);
            m_textureNeedsUpload = false;
        } else if (m_textureImage == VK_NULL_HANDLE) {
            const QImage& cached = m_window->cachedTexture();
            if (!cached.isNull()) {
                uploadImageToTexture(cached, 1, m_textureImage, m_textureMem, m_textureView, m_textureSampler);
            } else {
                QImage defaultPantoImg(2, 2, QImage::Format_RGBA8888);
                defaultPantoImg.fill(QColor(220, 225, 230, 255));
                uploadImageToTexture(defaultPantoImg, 1, m_textureImage, m_textureMem, m_textureView, m_textureSampler);
            }
        }

        if (m_vehicleTextureNeedsUpload && !m_pendingVehicleTexture.isNull()) {
            uploadImageToTexture(m_pendingVehicleTexture, 2, m_vehicleTextureImage, m_vehicleTextureMem, m_vehicleTextureView, m_vehicleTextureSampler);
            m_vehicleTextureNeedsUpload = false;
        } else if (m_vehicleTextureImage == VK_NULL_HANDLE) {
            const QImage& cachedVehicle = m_window->cachedVehicleTexture();
            if (!cachedVehicle.isNull()) {
                uploadImageToTexture(cachedVehicle, 2, m_vehicleTextureImage, m_vehicleTextureMem, m_vehicleTextureView, m_vehicleTextureSampler);
            } else {
                QImage defaultVehicleImg(2, 2, QImage::Format_RGBA8888);
                defaultVehicleImg.fill(QColor(180, 190, 205, 255));
                uploadImageToTexture(defaultVehicleImg, 2, m_vehicleTextureImage, m_vehicleTextureMem, m_vehicleTextureView, m_vehicleTextureSampler);
            }
        }
    }

    // Rebuild full vertex list if needed
    std::vector<EdpcVertex> fullBuffer;
    bool needsUpload = false;
    {
        std::lock_guard<std::mutex> lock(m_meshMutex);
        if (m_meshNeedsUpload && m_vertexBuffer != VK_NULL_HANDLE) {
            fullBuffer = m_currentMesh;
            // Append vehicle mesh if present
            if (!m_vehicleMesh.empty()) {
                fullBuffer.insert(fullBuffer.end(), m_vehicleMesh.begin(), m_vehicleMesh.end());
            }
            generateCatenaryAndGrid(fullBuffer);
            m_meshNeedsUpload = false;
            needsUpload = true;
        }
    }

    if (needsUpload) {
        m_vertexCount = static_cast<uint32_t>(fullBuffer.size() * sizeof(EdpcVertex) <= m_vertexBufferSize ? fullBuffer.size() : 0);
        if (m_vertexCount > 0) {
            void* data;
            m_devFuncs->vkMapMemory(m_device, m_vertexMem, 0, fullBuffer.size() * sizeof(EdpcVertex), 0, &data);
            std::memcpy(data, fullBuffer.data(), fullBuffer.size() * sizeof(EdpcVertex));
            m_devFuncs->vkUnmapMemory(m_device, m_vertexMem);
        }
    }

    // Camera setup
    QMatrix4x4 projection;
    float aspect = sz.width() / (float)(sz.height() > 0 ? sz.height() : 1);
    projection.perspective(45.0f, aspect, 0.1f, 100.0f);
    // Vulkan Y is inverted compared to OpenGL
    projection.scale(1.0f, -1.0f, 1.0f);

    QMatrix4x4 view = m_window->getViewMatrix();
    updateUbo(projection, view);

    // Record Commands
    VkClearColorValue clearColor = {{0.08f, 0.09f, 0.11f, 1.0f}};
    VkClearDepthStencilValue clearDS = {1.0f, 0};
    VkClearValue clearValues[2];
    clearValues[0].color = clearColor;
    clearValues[1].depthStencil = clearDS;

    VkRenderPassBeginInfo rpBeginInfo = {};
    rpBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBeginInfo.renderPass = m_window->defaultRenderPass();
    rpBeginInfo.framebuffer = m_window->currentFramebuffer();
    rpBeginInfo.renderArea.extent.width = sz.width();
    rpBeginInfo.renderArea.extent.height = sz.height();
    rpBeginInfo.clearValueCount = 2;
    rpBeginInfo.pClearValues = clearValues;

    m_devFuncs->vkCmdBeginRenderPass(cb, &rpBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = {};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = sz.width();
    viewport.height = sz.height();
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    m_devFuncs->vkCmdSetViewport(cb, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent.width = sz.width();
    scissor.extent.height = sz.height();
    m_devFuncs->vkCmdSetScissor(cb, 0, 1, &scissor);

    m_devFuncs->vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    m_devFuncs->vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descSet, 0, nullptr);

    if (m_vertexCount > 0) {
        VkDeviceSize offsets[] = {0};
        m_devFuncs->vkCmdBindVertexBuffers(cb, 0, 1, &m_vertexBuffer, offsets);
        m_devFuncs->vkCmdDraw(cb, m_vertexCount, 1, 0, 0);
    }

    m_devFuncs->vkCmdEndRenderPass(cb);

    m_window->frameReady();
    m_window->requestUpdate();
}
