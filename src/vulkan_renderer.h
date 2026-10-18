#pragma once

#include <QtGui/qvulkanwindow.h>
#include <QtGui/qvulkanfunctions.h>
#include <QtGui/qmatrix4x4.h>
#include <QtGui/qvector3d.h>
#include <QtGui/qimage.h>
#include <vector>
#include <mutex>

#include "edpc_core.h"

class VulkanViewport;

struct UniformBufferObject {
    float mvp[16];
    float model[16];
    float lightDir[4];
    float wireColor[4];
    float wireHeight;
    float padding[3];
};

class VulkanRenderer : public QVulkanWindowRenderer {
public:
    explicit VulkanRenderer(VulkanViewport* window);
    ~VulkanRenderer() override;

    void initResources() override;
    void initSwapChainResources() override;
    void releaseSwapChainResources() override;
    void releaseResources() override;
    void startNextFrame() override;

    void updateMeshData(const std::vector<EdpcVertex>& vertices);
    void updateVehicleMeshData(const std::vector<EdpcVertex>& vertices);
    void updateWireHeight(float height);
    void setShowWire(bool show);
    void setShowPivots(bool show);
    void updateTransformedPivots(const EdpcTransformedPivots& pivots);
    void updateTexture(const QImage& image);
    void updateVehicleTexture(const QImage& image);

private:
    void createPipeline();
    void createUniformBuffer();
    void createVertexBuffer();
    void createDefaultTextures();
    void uploadImageToTexture(const QImage& inputImage, uint32_t dstBinding,
                              VkImage& outImg, VkDeviceMemory& outMem,
                              VkImageView& outView, VkSampler& outSampler);
    void updateUbo(const QMatrix4x4& projection, const QMatrix4x4& view);
    void generateCatenaryAndGrid(std::vector<EdpcVertex>& outVerts);

    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer cb);

    VulkanViewport* m_window;
    QVulkanDeviceFunctions* m_devFuncs;

    VkDevice m_device;
    VkDescriptorPool m_descPool;
    VkDescriptorSetLayout m_descSetLayout;
    VkDescriptorSet m_descSet;
    VkPipelineLayout m_pipelineLayout;
    VkPipeline m_pipeline;

    VkBuffer m_ubo;
    VkDeviceMemory m_uboMem;

    VkBuffer m_vertexBuffer;
    VkDeviceMemory m_vertexMem;
    uint32_t m_vertexBufferSize;
    uint32_t m_vertexCount;

    // Pantograph Texture resources (Binding 1)
    VkImage m_textureImage;
    VkDeviceMemory m_textureMem;
    VkImageView m_textureView;
    VkSampler m_textureSampler;
    QImage m_pendingTexture;
    bool m_textureNeedsUpload;

    // Vehicle Texture resources (Binding 2)
    VkImage m_vehicleTextureImage;
    VkDeviceMemory m_vehicleTextureMem;
    VkImageView m_vehicleTextureView;
    VkSampler m_vehicleTextureSampler;
    QImage m_pendingVehicleTexture;
    bool m_vehicleTextureNeedsUpload;

    std::mutex m_textureMutex;

    // Mesh geometry
    std::vector<EdpcVertex> m_currentMesh;
    std::vector<EdpcVertex> m_vehicleMesh;
    float m_wireHeight;
    bool m_showWire;
    bool m_showPivots;
    bool m_hasPivots;
    EdpcTransformedPivots m_transformedPivots;
    void generatePivotGizmos(std::vector<EdpcVertex>& outVerts);
    bool m_meshNeedsUpload;
    std::mutex m_meshMutex;
};
