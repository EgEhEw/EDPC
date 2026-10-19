#pragma once

#include <QtWidgets/qmainwindow.h>
#include <QtWidgets/qtabwidget.h>
#include <QtWidgets/qtablewidget.h>
#include <QtWidgets/qspinbox.h>
#include <QtWidgets/qcheckbox.h>
#include <QtWidgets/qslider.h>
#include <QtWidgets/qcombobox.h>
#include <QtWidgets/qlineedit.h>
#include <QtWidgets/qplaintextedit.h>
#include <QtWidgets/qlabel.h>
#include <QtWidgets/qgroupbox.h>
#include <QtWidgets/qpushbutton.h>
#include <QtCore/qlist.h>
#include <QtCore/qjsonarray.h>
#include <QtCore/qjsonobject.h>
#include <QtCore/qjsondocument.h>
#include <QtNetwork/qnetworkaccessmanager.h>
#include <QtNetwork/qnetworkreply.h>
#include <vector>

#include "vulkan_viewport.h"
#include "edpc_core.h"

struct PantographProfile {
    int index = 0;
    QString id;               // e.g. "panto_0", "panto_1"
    QString name;             // e.g. "Pantograph 1"
    QString randomScriptId;   // 8-char random e.g. "x7wtnf3k"
    QString randomModelName;  // 5-char random e.g. "msutm"
    QString objPath;
    QString mtlPath;
    QString texturePath;
    double wireRef = 1.4480;
    double noWirePark = 1.3300;
    bool enableDepotPark = false;
    double depotPark = 0.3800;
    double upperThresh = 3.0;
    double lowerThresh = -3.0;
    float nominalTipHeight = 1.4480f;
    float shoeOffset = 0.1048f;
    bool customPivotsSet = false;
    EdpcPivots customPivots{};
    EdpcModelHandle* modelHandle = nullptr;
    QImage textureImage;

    QString displayName(bool isTurkish = false) const {
        QString base = isTurkish ? QString("Pantograf %1").arg(index + 1) : QString("Pantograph %1").arg(index + 1);
        if (!randomModelName.isEmpty()) {
            return QString("%1 [%2]").arg(base, randomModelName);
        }
        return base;
    }
};

struct VehicleMountItem {
    QString id;
    QString name;
    QString description;
    bool hasPantograph = false;
    int pantographIndex = 0;
    double mountHeight = 3.450;
    double mountX = 0.000;
    double mountZ = 5.200;
    double rotationYDeg = 0.0;
    double wireDetectZ = 0.000;
    QString modelResource;
    QString textureResource;
    QString modelPropertiesResource;
    QString positionDefinitionsResource;
    QString bogie1ModelResource;
    QString bogie1PropertiesResource;
    QString bogie1DefinitionsResource;
    double bogie1Position = 0.0;
    QString bogie2ModelResource;
    QString bogie2PropertiesResource;
    QString bogie2DefinitionsResource;
    double bogie2Position = 0.0;
    bool hasGangway1 = false;
    bool hasGangway2 = false;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QVulkanInstance* vulkanInst = nullptr, QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    // Tab 1 Slots
    void onAddPantograph();
    void onRemovePantograph();
    void onPantographSelectionChanged(int index);
    void onBrowseModel();
    void onKinematicsSliderChanged(int value);
    void onKinematicsSpinChanged(double value);
    void onWireRefChanged(double value);
    void onWireHeightChanged(double value);
    void onTestWireRef();
    void onTestNoWirePark();
    void onTestDepotPark();
    void onShowWireToggled(bool checked);
    void onDepotParkToggled(bool checked);
    void onKinematicsUpdated(float height, float angle1, float angle2, float angle4, bool reachable);
    void onPivotSpinboxChanged();
    void onResetPivots();

    // Tab 2 Slots
    void onBrowsePackZip();
    void onBrowsePackFolder();
    void onVehicleSelectionChanged();
    void onVehicleCellChanged(int row, int column);
    void onVehiclePantoChanged(int index);
    void onVehicleMountParamChanged();
    void onPresetFrontRoof();
    void onPresetRearRoof();
    void onPresetCenterRoof();
    void onSelectAllVehicles(bool select);

    // Tab 3 Slots
    void onGenerateNewRandomId();
    void onBrowseOutputPath();
    void onRefreshScriptPreview();
    void onPackageAndExport();
    void onOpenOutputFolder();

    // Language & Viewport Slots
    void onLanguageChanged(int index);

    // Update Checker Slots
    void onCheckForUpdates();
    void onUpdateReplyFinished(QNetworkReply* reply);

public:
    enum class AppLanguage {
        English = 0,
        Turkish = 1
    };

private:
    void setupUi();
    void retranslateUi();
    QWidget* createTab1Pantograph();
    QWidget* createTab2TrainPack();
    QWidget* createTab3Export();
    void applyDarkTheme();

    void loadPackPath(const QString& path);
    void updateVehicleMountUi(int row);
    void saveCurrentVehicleMountUi();
    QJsonObject buildPackageRequestJson() const;
    void updateViewportKinematics();
    void updateVehicleViewportMesh();
    void loadVehicleModelForPreview(const VehicleMountItem& v);

    PantographProfile createNewPantographProfile(int index);
    QString generateScriptId() const;
    QString generateDisplayName() const;
    void syncCurrentPantoUiToProfile();
    void loadPantoProfileToUi(int index);
    void updatePantographCombos();
    void populatePivotSpinboxes(const EdpcPivots& p);

    QVulkanInstance* m_vulkanInst;
    VulkanViewport* m_viewport;
    QWidget* m_viewportContainer;

    QTabWidget* m_tabs;
    QComboBox* m_comboLang;
    AppLanguage m_currentLang;

    // --- Tab 1 Widgets ---
    QGroupBox* m_grpPantoProfile;
    QLabel* m_lblProfileSelect;
    QComboBox* m_comboPantographs;
    QPushButton* m_btnAddPanto;
    QPushButton* m_btnRemovePanto;
    QLabel* m_lblProfileInfo;

    QGroupBox* m_grpModel;
    QLineEdit* m_txtObjPath;
    QPushButton* m_btnBrowseObj;
    QLabel* m_lblModelInfo;
    QLabel* m_lblPivotsInfo;

    QGroupBox* m_grpPivots;
    QLabel* m_lblPivotHelp;
    QLabel* m_lblP1;
    QLabel* m_lblP2;
    QLabel* m_lblP3;
    QLabel* m_lblP4;
    QLabel* m_lblP4Elbow;
    QDoubleSpinBox* m_spinP1Y;
    QDoubleSpinBox* m_spinP1Z;
    QDoubleSpinBox* m_spinP2Y;
    QDoubleSpinBox* m_spinP2Z;
    QDoubleSpinBox* m_spinP3Y;
    QDoubleSpinBox* m_spinP3Z;
    QCheckBox* m_chkP4Enabled;
    QDoubleSpinBox* m_spinP4BaseY;
    QDoubleSpinBox* m_spinP4BaseZ;
    QDoubleSpinBox* m_spinP4ElbowY;
    QDoubleSpinBox* m_spinP4ElbowZ;
    QPushButton* m_btnResetPivots;
    bool m_syncingPivots = false;

    QGroupBox* m_grpParams;
    QLabel* m_lblWireRef;
    QDoubleSpinBox* m_spinWireRef;
    QPushButton* m_btnTestWireRef;
    QLabel* m_lblNoWirePark;
    QDoubleSpinBox* m_spinNoWirePark;
    QPushButton* m_btnTestNoWire;
    QCheckBox* m_chkDepotPark;
    QLabel* m_lblDepotPark;
    QDoubleSpinBox* m_spinDepotPark;
    QPushButton* m_btnTestDepot;
    QLabel* m_lblUpperThresh;
    QDoubleSpinBox* m_spinUpperThresh;
    QLabel* m_lblLowerThresh;
    QDoubleSpinBox* m_spinLowerThresh;

    QGroupBox* m_grpLive;
    QLabel* m_lblTestHeight;
    QSlider* m_sliderTestHeight;
    QDoubleSpinBox* m_spinTestHeight;
    QCheckBox* m_chkShowWire;
    QCheckBox* m_chkShowPivots;
    QLabel* m_lblArmAngles;
    QLabel* m_lblReachability;
    QPushButton* m_btnResetCam;
    QCheckBox* m_chkInvertCam;
    QLabel* m_lblTab1Help;
    QPushButton* m_btnNextToPack;
    QLabel* m_lblTab1VpTitle;

    // --- Tab 2 Widgets ---
    QGroupBox* m_grpPack;
    QLineEdit* m_txtPackPath;
    QPushButton* m_btnBrowseZip;
    QPushButton* m_btnBrowseFolder;
    QLabel* m_lblPackStatus;

    QGroupBox* m_grpVehicles;
    QTableWidget* m_tableVehicles;
    QPushButton* m_btnSelectAll;
    QPushButton* m_btnDeselectAll;

    QGroupBox* m_groupVehicleMount;
    QLabel* m_lblSelectedVehicleTitle;
    QLabel* m_lblVehiclePanto;
    QComboBox* m_comboVehiclePanto;
    QLabel* m_lblMountHeight;
    QDoubleSpinBox* m_spinMountHeight;
    QLabel* m_lblMountX;
    QDoubleSpinBox* m_spinMountX;
    QLabel* m_lblMountZ;
    QDoubleSpinBox* m_spinMountZ;
    QLabel* m_lblRotationY;
    QComboBox* m_comboRotationY;
    QLabel* m_lblWireDetectZ;
    QDoubleSpinBox* m_spinWireDetectZ;
    QLabel* m_lblPresets;
    QPushButton* m_btnPreFront;
    QPushButton* m_btnPreRear;
    QPushButton* m_btnPreCenter;

    QLabel* m_lblTab2VpTitle;
    VulkanViewport* m_vehicleViewport;
    QWidget* m_vehicleViewportContainer;
    QLabel* m_lblVehicleVpInfo;
    QPushButton* m_btnResetVehCam;
    QPushButton* m_btnFrontVehCam;
    QPushButton* m_btnRearVehCam;
    QCheckBox* m_chkInvertVehCam;
    QLabel* m_lblTab2Help;
    QPushButton* m_btnNextToExport;

    // --- Tab 3 Widgets ---
    QGroupBox* m_grpConfig;
    QLabel* m_lblOutputPath;
    QLineEdit* m_txtOutputPath;
    QPushButton* m_btnBrowseOut;

    QGroupBox* m_grpPreview;
    QLabel* m_lblScriptPreviewHint;
    QPushButton* m_btnRefreshPreview;
    QPlainTextEdit* m_txtScriptPreview;

    QGroupBox* m_grpAction;
    QPushButton* m_btnPackage;
    QLabel* m_lblExportStatus;
    QPushButton* m_btnOpenOutputFolder;

    // --- Application State ---
    QList<PantographProfile> m_pantographs;
    int m_currentPantoIndex;
    QString m_objPath;
    QString m_mtlPath;
    QString m_texturePath;
    QString m_packInputPath;
    QString m_customResourcesRelPath;
    bool m_packIsZip;
    QString m_randomId;
    QString m_randomDisplayName;
    QList<VehicleMountItem> m_vehicles;
    int m_currentVehicleRow;
    QString m_lastExportedFolder;
    float m_nominalTipHeight;
    float m_shoeOffset;

    QString m_loadedVehicleCacheKey;
    std::vector<EdpcVertex> m_cachedVehicleMesh;
    QImage m_cachedVehicleTexture;

    // --- Bottom Bar & Updater ---
    QWidget* m_bottomBarWidget;
    QLabel* m_lblVersionBadge;
    QPushButton* m_btnCheckUpdate;
    QNetworkAccessManager* m_netManager;
};
