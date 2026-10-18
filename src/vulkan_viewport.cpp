#include "vulkan_viewport.h"
#include "vulkan_renderer.h"

#include <QtGui/qevent.h>
#include <QtCore/qmath.h>
#include <QtCore/qfileinfo.h>
#include <cmath>

VulkanViewport::VulkanViewport(QWindow* parent)
    : QVulkanWindow(parent),
      m_renderer(nullptr),
      m_model(nullptr),
      m_currentHeight(1.448f),
      m_wireHeight(1.448f),
      m_wireVisible(true),
      m_yaw(45.0f),
      m_pitch(25.0f),
      m_distance(3.0f),
      m_target(0.0f, 0.7f, 0.0f),
      m_invertCamera(true),
      m_isLeftDragging(false),
      m_isMiddleDragging(false),
      m_animTimer(nullptr) {

    // 60 FPS viewport render loop so newly loaded models & kinematic adjustments appear instantly
    m_animTimer = new QTimer(this);
    connect(m_animTimer, &QTimer::timeout, this, [this]() {
        if (isExposed()) {
            requestUpdate();
        }
    });
    m_animTimer->start(16);
}

VulkanViewport::~VulkanViewport() {
    if (m_animTimer) {
        m_animTimer->stop();
    }
    if (m_model) {
        edpc_model_free(m_model);
        m_model = nullptr;
    }
}

QVulkanWindowRenderer* VulkanViewport::createRenderer() {
    m_renderer = new VulkanRenderer(this);
    if (!m_cachedTexture.isNull()) {
        m_renderer->updateTexture(m_cachedTexture);
    }
    if (!m_cachedVehicleTexture.isNull()) {
        m_renderer->updateVehicleTexture(m_cachedVehicleTexture);
    }
    if (!m_cachedVehicleMesh.empty()) {
        m_renderer->updateVehicleMeshData(m_cachedVehicleMesh);
    }
    if (!m_cachedCustomMesh.empty()) {
        m_renderer->updateMeshData(m_cachedCustomMesh);
    }
    m_renderer->setShowWire(m_wireVisible);
    m_renderer->updateWireHeight(m_wireHeight);
    applyModelKinematics();
    return m_renderer;
}

QMatrix4x4 VulkanViewport::getViewMatrix() const {
    QMatrix4x4 view;
    float radYaw = qDegreesToRadians(m_yaw);
    float radPitch = qDegreesToRadians(m_pitch);

    float eyeX = m_target.x() + m_distance * std::cos(radPitch) * std::sin(radYaw);
    float eyeY = m_target.y() + m_distance * std::sin(radPitch);
    float eyeZ = m_target.z() + m_distance * std::cos(radPitch) * std::cos(radYaw);

    view.lookAt(QVector3D(eyeX, eyeY, eyeZ), m_target, QVector3D(0.0f, 1.0f, 0.0f));
    return view;
}

bool VulkanViewport::loadModel(const QString& objPath) {
    if (m_model) {
        edpc_model_free(m_model);
        m_model = nullptr;
    }

    m_model = edpc_model_load_file(objPath.toUtf8().constData());
    if (!m_model) {
        emit modelLoaded(false, "Failed to load OBJ model. Ensure p_base, p1, p2, p3 parts are present.");
        return false;
    }

    EdpcPivots pivots;
    if (edpc_model_get_pivots(m_model, &pivots)) {
        m_currentHeight = pivots.tip.y;
        m_wireHeight = pivots.tip.y;
        m_target = QVector3D(0.0f, pivots.tip.y * 0.5f, 0.0f);
        if (m_renderer) {
            m_renderer->updateWireHeight(m_wireHeight);
        }
    }

    applyModelKinematics();
    requestUpdate();

    emit modelLoaded(true, QString("Pantograph OBJ loaded successfully. Initial tip height: %1 m")
                               .arg(m_currentHeight, 0, 'f', 3));
    return true;
}

bool VulkanViewport::loadTexture(const QString& texturePath) {
    if (texturePath.isEmpty() || !QFileInfo::exists(texturePath)) {
        return false;
    }

    QImage img(texturePath);
    if (img.isNull()) {
        return false;
    }

    return setTextureImage(img);
}

bool VulkanViewport::setTextureImage(const QImage& img) {
    if (img.isNull()) return false;
    m_cachedTexture = img;
    if (m_renderer) {
        m_renderer->updateTexture(m_cachedTexture);
    }
    requestUpdate();
    return true;
}

void VulkanViewport::updateKinematics(float height) {
    m_currentHeight = height;
    applyModelKinematics();
}

void VulkanViewport::updateMeshData(const std::vector<EdpcVertex>& vertices) {
    m_cachedCustomMesh = vertices;
    if (m_renderer) {
        m_renderer->updateMeshData(vertices);
    }
    requestUpdate();
}

void VulkanViewport::setVehicleMesh(const std::vector<EdpcVertex>& vertices) {
    m_cachedVehicleMesh = vertices;
    if (m_renderer) {
        m_renderer->updateVehicleMeshData(vertices);
    }
    requestUpdate();
}

void VulkanViewport::setVehicleTexture(const QImage& image) {
    m_cachedVehicleTexture = image;
    if (m_renderer) {
        m_renderer->updateVehicleTexture(image);
    }
    requestUpdate();
}

void VulkanViewport::reapplyTextures() {
    if (m_renderer) {
        if (!m_cachedTexture.isNull()) {
            m_renderer->updateTexture(m_cachedTexture);
        }
        if (!m_cachedVehicleTexture.isNull()) {
            m_renderer->updateVehicleTexture(m_cachedVehicleTexture);
        }
    }
    requestUpdate();
}

void VulkanViewport::setCameraParams(float yaw, float pitch, float distance, QVector3D target) {
    m_yaw = yaw;
    m_pitch = pitch;
    m_distance = distance;
    m_target = target;
    requestUpdate();
}

void VulkanViewport::updateWireHeight(float wireHeight) {
    m_wireHeight = wireHeight;
    if (m_renderer) {
        m_renderer->updateWireHeight(wireHeight);
    }
    requestUpdate();
}

void VulkanViewport::setWireVisible(bool show) {
    m_wireVisible = show;
    if (m_renderer) {
        m_renderer->setShowWire(show);
    }
    requestUpdate();
}

void VulkanViewport::setInvertCamera(bool invert) {
    m_invertCamera = invert;
}

void VulkanViewport::resetCamera() {
    m_yaw = 45.0f;
    m_pitch = 25.0f;
    m_distance = 3.0f;
    if (m_model) {
        EdpcPivots pivots;
        if (edpc_model_get_pivots(m_model, &pivots)) {
            m_target = QVector3D(0.0f, pivots.tip.y * 0.5f, 0.0f);
        }
    } else {
        m_target = QVector3D(0.0f, 0.7f, 0.0f);
    }
    requestUpdate();
}

bool VulkanViewport::getPivots(EdpcPivots* outPivots) const {
    if (!m_model || !outPivots) return false;
    return edpc_model_get_pivots(m_model, outPivots);
}

bool VulkanViewport::solveKinematics(float height, EdpcAngles* outAngles) const {
    if (!m_model || !outAngles) return false;
    return edpc_model_solve_kinematics(m_model, height, outAngles);
}

void VulkanViewport::applyModelKinematics() {
    if (!m_model) return;

    const EdpcVertex* verts = nullptr;
    uint32_t count = 0;
    if (edpc_model_update_transformed_mesh(m_model, m_currentHeight, &verts, &count)) {
        if (m_renderer && verts && count > 0) {
            std::vector<EdpcVertex> meshVec(verts, verts + count);
            m_renderer->updateMeshData(meshVec);
        }
    }

    EdpcAngles angles;
    if (edpc_model_solve_kinematics(m_model, m_currentHeight, &angles)) {
        emit kinematicsUpdated(m_currentHeight, angles.angle1, angles.angle2, angles.angle4, angles.reachable);
    }

    requestUpdate();
}

void VulkanViewport::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_isLeftDragging = true;
        m_lastMousePos = event->pos();
    } else if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton) {
        m_isMiddleDragging = true;
        m_lastMousePos = event->pos();
    }
}

void VulkanViewport::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_isLeftDragging = false;
    } else if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton) {
        m_isMiddleDragging = false;
    }
}

void VulkanViewport::mouseMoveEvent(QMouseEvent* event) {
    int dx = event->pos().x() - m_lastMousePos.x();
    int dy = event->pos().y() - m_lastMousePos.y();
    m_lastMousePos = event->pos();

    if (m_isLeftDragging) {
        if (event->modifiers() & Qt::ShiftModifier) {
            QMatrix4x4 view = getViewMatrix();
            QVector3D camRight(view(0, 0), view(0, 1), view(0, 2));
            QVector3D camUp(view(1, 0), view(1, 1), view(1, 2));
            float panSpeed = m_distance * 0.002f;
            m_target -= camRight * (dx * panSpeed);
            m_target += camUp * (dy * panSpeed);
        } else {
            if (m_invertCamera) {
                m_yaw -= dx * 0.5f;
                m_pitch += dy * 0.5f;
            } else {
                m_yaw += dx * 0.5f;
                m_pitch -= dy * 0.5f;
            }
            if (m_pitch > 89.0f) m_pitch = 89.0f;
            if (m_pitch < -89.0f) m_pitch = -89.0f;
            while (m_yaw < 0.0f) m_yaw += 360.0f;
            while (m_yaw >= 360.0f) m_yaw -= 360.0f;
        }
        requestUpdate();
    } else if (m_isMiddleDragging) {
        QMatrix4x4 view = getViewMatrix();
        QVector3D camRight(view(0, 0), view(0, 1), view(0, 2));
        QVector3D camUp(view(1, 0), view(1, 1), view(1, 2));
        float panSpeed = m_distance * 0.002f;
        m_target -= camRight * (dx * panSpeed);
        m_target += camUp * (dy * panSpeed);
        requestUpdate();
    }
}

void VulkanViewport::wheelEvent(QWheelEvent* event) {
    float numDegrees = event->angleDelta().y() / 8.0f;
    float numSteps = numDegrees / 15.0f;
    m_distance *= std::pow(0.9f, numSteps);
    if (m_distance < 0.15f) m_distance = 0.15f;
    if (m_distance > 80.0f) m_distance = 80.0f;
    requestUpdate();
}
