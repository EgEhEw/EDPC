#pragma once

#include <QtGui/qvulkanwindow.h>
#include <QtGui/qmatrix4x4.h>
#include <QtGui/qvector3d.h>
#include <QtGui/qimage.h>
#include <QtCore/qpoint.h>
#include <QtCore/qtimer.h>
#include <vector>

#include "edpc_core.h"

class VulkanRenderer;

class VulkanViewport : public QVulkanWindow {
    Q_OBJECT

public:
    explicit VulkanViewport(QWindow* parent = nullptr);
    ~VulkanViewport() override;

    QVulkanWindowRenderer* createRenderer() override;

    QMatrix4x4 getViewMatrix() const;

    bool loadModel(const QString& objPath);
    bool loadTexture(const QString& texturePath);
    bool setTextureImage(const QImage& img);
    void updateKinematics(float height);
    void updateMeshData(const std::vector<EdpcVertex>& vertices);
    void setVehicleMesh(const std::vector<EdpcVertex>& vertices);
    void setVehicleTexture(const QImage& image);
    void reapplyTextures();
    void updateWireHeight(float wireHeight);
    void setWireVisible(bool show);
    void setShowPivots(bool show);
    bool showPivots() const { return m_showPivots; }
    void setInvertCamera(bool invert);
    bool invertCamera() const { return m_invertCamera; }
    void resetCamera();
    void setCameraParams(float yaw, float pitch, float distance, QVector3D target);

    bool hasModel() const { return m_model != nullptr; }
    EdpcModelHandle* modelHandle() const { return m_model; }
    const QImage& cachedTexture() const { return m_cachedTexture; }
    const QImage& cachedVehicleTexture() const { return m_cachedVehicleTexture; }
    bool getPivots(EdpcPivots* outPivots) const;
    bool setPivots(const EdpcPivots& pivots);
    bool solveKinematics(float height, EdpcAngles* outAngles) const;

signals:
    void modelLoaded(bool success, const QString& message);
    void kinematicsUpdated(float height, float angle1, float angle2, float angle4, bool reachable);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void applyModelKinematics();

    VulkanRenderer* m_renderer;
    EdpcModelHandle* m_model;

    float m_currentHeight;
    float m_wireHeight;
    bool m_wireVisible;
    bool m_showPivots;

    // Orbit Camera Parameters
    float m_yaw;
    float m_pitch;
    float m_distance;
    QVector3D m_target;
    bool m_invertCamera;

    QPoint m_lastMousePos;
    bool m_isLeftDragging;
    bool m_isMiddleDragging;

    QTimer* m_animTimer;
    QImage m_cachedTexture;
    QImage m_cachedVehicleTexture;
    std::vector<EdpcVertex> m_cachedVehicleMesh;
    std::vector<EdpcVertex> m_cachedCustomMesh;
};
