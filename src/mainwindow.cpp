#include "mainwindow.h"
#include "vulkan_viewport.h"

#include <QtWidgets/qboxlayout.h>
#include <QtWidgets/qgridlayout.h>
#include <QtWidgets/qformlayout.h>
#include <QtWidgets/qpushbutton.h>
#include <QtWidgets/qfiledialog.h>
#include <QtWidgets/qmessagebox.h>
#include <QtWidgets/qheaderview.h>
#include <QtWidgets/qsplitter.h>
#include <QtWidgets/qscrollarea.h>
#include <QtWidgets/qtabbar.h>
#include <QtWidgets/qbuttongroup.h>
#include <QtWidgets/qstylefactory.h>
#include <QtWidgets/qapplication.h>
#include <QtGui/qpainter.h>
#include <QtGui/qshortcut.h>
#include <QtGui/qfontdatabase.h>
#include <QtGui/qdesktopservices.h>
#include <QtCore/qfileinfo.h>
#include <QtCore/qdir.h>
#include <QtCore/qrandom.h>
#include <QtCore/qdebug.h>
#include <QtNetwork/qnetworkaccessmanager.h>
#include <QtNetwork/qnetworkreply.h>
#include <QtNetwork/qnetworkrequest.h>

static const QString APP_VERSION = "v1.3.0";

MainWindow::MainWindow(QVulkanInstance* vulkanInst, QWidget* parent)
    : QMainWindow(parent),
      m_vulkanInst(vulkanInst),
      m_viewport(nullptr),
      m_vehicleViewport(nullptr),
      m_viewportContainer(nullptr),
      m_vehicleViewportContainer(nullptr),
      m_lblVehicleVpInfo(nullptr),
      m_tabs(nullptr),
      m_comboLang(nullptr),
      m_currentLang(AppLanguage::English),
      m_grpPantoProfile(nullptr),
      m_lblProfileSelect(nullptr),
      m_comboPantographs(nullptr),
      m_btnAddPanto(nullptr),
      m_btnRemovePanto(nullptr),
      m_lblProfileInfo(nullptr),
      m_lblVehiclePanto(nullptr),
      m_comboVehiclePanto(nullptr),
      m_currentPantoIndex(0),
      m_currentVehicleRow(-1),
      m_packIsZip(false),
      m_nominalTipHeight(1.4480f),
      m_shoeOffset(0.1048f),
      m_bottomBarWidget(nullptr),
      m_lblVersionBadge(nullptr),
      m_btnCheckUpdate(nullptr),
      m_netManager(nullptr) {

    setWindowTitle("EDPC - EgEhE's Dynamic Pantograph Configurator");
    resize(1480, 920);
    setMinimumSize(1100, 720);

    // Initialize default pantograph profile #1
    PantographProfile p0 = createNewPantographProfile(0);
    m_pantographs.append(p0);
    m_randomId = p0.randomScriptId;
    m_randomDisplayName = p0.randomModelName;

    m_netManager = new QNetworkAccessManager(this);
    connect(m_netManager, &QNetworkAccessManager::finished, this, &MainWindow::onUpdateReplyFinished);

    applyDarkTheme();
    setupUi();
}

MainWindow::~MainWindow() {
    for (auto& p : m_pantographs) {
        if (p.modelHandle) {
            edpc_model_free(p.modelHandle);
            p.modelHandle = nullptr;
        }
    }
}

// The empty canvas is drawn locally so it stays sharp at every display scale.
namespace {
class PantographEmblem : public QWidget {
public:
    explicit PantographEmblem(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(160, 82);
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(0.8, 0.66);
        p.setPen(QPen(QColor("#303338"), 1));
        for (int x = 12; x < 200; x += 22)
            for (int y = 6; y < 124; y += 22) p.drawPoint(x, y);
        p.setPen(QPen(QColor("#55585f"), 2));
        p.drawLine(14, 106, 186, 106);
        p.drawLine(20, 15, 180, 15);
        p.setPen(QPen(QColor("#eea66b"), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPolyline(QPolygonF{QPointF(125, 100), QPointF(58, 60), QPointF(120, 22)});
        p.drawLine(102, 100, 48, 60);
        p.drawLine(48, 60, 110, 22);
        p.drawLine(89, 22, 145, 22);
        p.drawLine(95, 100, 140, 100);
        p.setBrush(QColor("#191b1f"));
        for (const QPointF pt : {QPointF(125, 100), QPointF(58, 60), QPointF(120, 22)})
            p.drawEllipse(pt, 4, 4);
    }
};
}

void MainWindow::applyDarkTheme() {
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    QFont font = QApplication::font();
    const auto families = QFontDatabase::families();
    for (const QString& family : {QString("Inter"), QString("Segoe UI"), QString("Noto Sans")}) {
        if (families.contains(family)) { font.setFamily(family); break; }
    }
    font.setPixelSize(12);
    qApp->setFont(font);
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#191b1f"));
    palette.setColor(QPalette::WindowText, QColor("#e7e7e9"));
    palette.setColor(QPalette::Base, QColor("#17191c"));
    palette.setColor(QPalette::AlternateBase, QColor("#202226"));
    palette.setColor(QPalette::Text, QColor("#e7e7e9"));
    palette.setColor(QPalette::Button, QColor("#292c31"));
    palette.setColor(QPalette::ButtonText, QColor("#e7e7e9"));
    palette.setColor(QPalette::Highlight, QColor("#bc8052"));
    palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#747880"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#747880"));
    qApp->setPalette(palette);
    setStyleSheet(R"(
        QWidget { color: #dedfe2; font-size: 12px; }
        QMainWindow, QWidget#workspaceRoot { background: #191b1f; }
        QLabel { background: transparent; }
        QWidget#navigation { background: #141619; border-right: 1px solid #2c2f34; }
        QLabel#brand { font-size: 27px; font-weight: 800; color: #f3f0eb; letter-spacing: 3px; }
        QLabel#eyebrow { color: #858990; font-size: 10px; font-weight: 600; letter-spacing: 2px; }
        QLabel#pageTitle { font-size: 24px; font-weight: 600; color: #f0f0f1; }
        QLabel#muted { color: #969aa3; }
        QLabel#step { color: #eea66b; font-size: 11px; font-weight: 600; }
        QLabel#emptyTitle { font-size: 18px; font-weight: 600; color: #ececef; }
        QWidget#canvasEmpty { background: #17191c; border: 1px solid #303339; border-radius: 8px; }
        QTabWidget::pane { border: 0; background: transparent; }
        QTabBar::tab { padding: 10px 15px; color: #979ba4; border-bottom: 2px solid #303339; }
        QTabBar::tab:selected { color: #f0bc91; border-bottom-color: #eea66b; }
        QTabBar::tab:hover { color: #eeeeef; background: #24272c; }
        QPushButton { background: #292c31; border: 1px solid #3a3e45; border-radius: 5px; padding: 7px 12px; font-weight: 500; }
        QPushButton:hover { background: #34383e; border-color: #747880; }
        QPushButton:pressed { background: #202226; }
        QPushButton:focus { border-color: #eea66b; }
        QPushButton:disabled { color: #747880; background: #23252a; border-color: #303339; }
        QPushButton#navButton { text-align: left; background: transparent; border: 1px solid transparent; padding: 14px 12px; color: #969aa3; font-size: 12px; }
        QPushButton#navButton:hover { background: #202226; color: #e9eaec; }
        QPushButton#navButton:checked { background: #30271f; border-color: #584232; color: #f0bc91; }
        QPushButton#actionButton { color: #211b17; background: #eea66b; border-color: #eea66b; font-weight: 600; padding: 10px 16px; }
        QPushButton#actionButton:hover { background: #f6ba89; border-color: #f6ba89; }
        QPushButton#actionButton:disabled { background: #39312a; color: #a18b79; border-color: #493b30; }
        QPushButton#quietButton { color: #a2a6ae; background: transparent; border-color: transparent; font-size: 11px; padding: 4px 8px; }
        QPushButton#quietButton:hover { color: #f0bc91; background: #292c31; }
        QGroupBox { background: #202226; border: 1px solid #33363c; border-radius: 7px; margin-top: 12px; padding: 17px 10px 10px; font-weight: 600; }
        QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 12px; padding: 0 5px; color: #d8d9dd; }
        QGroupBox:disabled { color: #777b83; border-color: #2c2f34; }
        QLineEdit, QDoubleSpinBox, QComboBox { background: #17191c; border: 1px solid #3b3f46; border-radius: 4px; padding: 7px 8px; selection-background-color: #775339; min-height: 16px; }
        QLineEdit:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: #eea66b; }
        QLineEdit:disabled, QDoubleSpinBox:disabled, QComboBox:disabled { color: #777b83; background: #1c1e22; border-color: #303339; }
        QComboBox::drop-down { border: 0; width: 24px; }
        QComboBox QAbstractItemView { background: #24272c; border: 1px solid #484c54; selection-background-color: #514030; }
        QDoubleSpinBox { font-family: "JetBrains Mono", "DejaVu Sans Mono", monospace; font-size: 11px; }
        QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 17px; border-left: 1px solid #33363c; }
        QTableWidget { background: #17191c; alternate-background-color: #202226; gridline-color: #303339; border: 1px solid #33363c; border-radius: 4px; selection-background-color: #514030; selection-color: #fff0e3; }
        QHeaderView::section { background: #292c31; color: #a5a9b1; border: 0; border-bottom: 1px solid #3b3f46; padding: 9px 7px; font-size: 11px; }
        QTableWidget::item { padding: 5px; }
        QSlider::groove:horizontal { height: 4px; background: #363a41; border-radius: 2px; }
        QSlider::sub-page:horizontal { background: #eea66b; border-radius: 2px; }
        QSlider::handle:horizontal { width: 12px; margin: -5px 0; border-radius: 6px; background: #f2c6a4; border: 1px solid #eea66b; }
        QCheckBox { spacing: 7px; font-weight: 400; }
        QCheckBox::indicator { width: 15px; height: 15px; }
        QPlainTextEdit { background: #141619; color: #d6c3a8; font-family: "JetBrains Mono", "DejaVu Sans Mono", monospace; font-size: 12px; border: 1px solid #33363c; border-radius: 5px; padding: 12px; selection-background-color: #514030; }
        QScrollArea { background: transparent; border: 0; }
        QScrollBar:vertical { background: transparent; width: 7px; margin: 0; }
        QScrollBar::handle:vertical { background: #454950; border-radius: 3px; min-height: 32px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
        QSplitter::handle { background: #303339; width: 1px; height: 1px; }
        QToolTip { color: #eeeef0; background: #292c31; border: 1px solid #555a63; padding: 6px; }
    )");
}

void MainWindow::setupUi() {
    auto* central = new QWidget(this);
    central->setObjectName("workspaceRoot");
    setCentralWidget(central);
    auto* shell = new QHBoxLayout(central);
    shell->setContentsMargins(0, 0, 0, 0);
    shell->setSpacing(0);

    auto* rail = new QWidget(central);
    rail->setObjectName("navigation");
    rail->setFixedWidth(194);
    auto* railLayout = new QVBoxLayout(rail);
    railLayout->setContentsMargins(18, 26, 18, 16);
    railLayout->setSpacing(10);
    auto* brand = new QLabel("EDPC", rail);
    brand->setObjectName("brand");
    railLayout->addWidget(brand);
    railLayout->addSpacing(36);
    m_stepLabel = new QLabel(rail);
    m_stepLabel->setObjectName("eyebrow");
    railLayout->addWidget(m_stepLabel);
    auto* navGroup = new QButtonGroup(this);
    for (int i = 0; i < 3; ++i) {
        auto* button = new QPushButton(rail);
        button->setObjectName("navButton");
        button->setCheckable(true);
        button->setMinimumHeight(66);
        navGroup->addButton(button, i);
        m_navButtons[i] = button;
        railLayout->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, i]() { m_tabs->setCurrentIndex(i); });
        auto* shortcut = new QShortcut(QKeySequence(QString("Alt+%1").arg(i + 1)), this);
        connect(shortcut, &QShortcut::activated, button, &QPushButton::click);
    }
    m_navButtons[0]->setChecked(true);
    railLayout->addStretch();
    auto* compatibility = new QLabel("MINECRAFT TRANSIT RAILWAY\nMTR / EgEhELib", rail);
    compatibility->setStyleSheet("color: #858990; font-size: 9px; line-height: 150%;");
    railLayout->addWidget(compatibility);
    railLayout->addSpacing(12);
    m_comboLang = new QComboBox(rail);
    m_comboLang->addItems({"English", "Türkçe"});
    m_comboLang->setAccessibleName("Language / Dil");
    connect(m_comboLang, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onLanguageChanged);
    railLayout->addWidget(m_comboLang);
    shell->addWidget(rail);

    auto* content = new QWidget(central);
    auto* root = new QVBoxLayout(content);
    root->setContentsMargins(24, 24, 24, 10);
    root->setSpacing(16);
    auto* header = new QHBoxLayout();
    auto* titles = new QVBoxLayout();
    titles->setSpacing(5);
    m_workspaceTitle = new QLabel(content);
    m_workspaceTitle->setObjectName("pageTitle");
    m_workspaceHint = new QLabel(content);
    m_workspaceHint->setObjectName("muted");
    m_workspaceHint->setWordWrap(true);
    titles->addWidget(m_workspaceTitle);
    titles->addWidget(m_workspaceHint);
    header->addLayout(titles, 1);
    auto* version = new QLabel(APP_VERSION, content);
    version->setObjectName("step");
    header->addWidget(version, 0, Qt::AlignTop);
    root->addLayout(header);

    m_tabs = new QTabWidget(content);
    m_tabs->setObjectName("workflowPages");
    m_tabs->addTab(createTab1Pantograph(), "Pantograph");
    m_tabs->addTab(createTab2TrainPack(), "Train pack");
    m_tabs->addTab(createTab3Export(), "Export");
    m_tabs->tabBar()->hide();
    root->addWidget(m_tabs, 1);

    m_bottomBarWidget = new QWidget(content);
    auto* bottom = new QHBoxLayout(m_bottomBarWidget);
    bottom->setContentsMargins(0, 6, 0, 0);
    m_sessionStatus = new QLabel(m_bottomBarWidget);
    m_sessionStatus->setObjectName("muted");
    bottom->addWidget(m_sessionStatus);
    bottom->addStretch();
    m_btnCheckUpdate = new QPushButton(m_bottomBarWidget);
    m_btnCheckUpdate->setObjectName("quietButton");
    connect(m_btnCheckUpdate, &QPushButton::clicked, this, &MainWindow::onCheckForUpdates);
    bottom->addWidget(m_btnCheckUpdate);
    m_lblVersionBadge = version;
    root->addWidget(m_bottomBarWidget);
    shell->addWidget(content, 1);

    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        updateWorkspace();
        if (index == 0 && m_viewport) {
            m_viewport->reapplyTextures();
            updateViewportKinematics();
        } else if (index == 1 && m_vehicleViewport) {
            m_vehicleViewport->reapplyTextures();
            if (m_currentVehicleRow >= 0 && m_currentVehicleRow < m_vehicles.size())
                loadVehicleModelForPreview(m_vehicles[m_currentVehicleRow]);
            updateVehicleViewportMesh();
            QTimer::singleShot(50, this, [this]() {
                if (m_tabs->currentIndex() == 1) updateVehicleViewportMesh();
            });
        } else if (index == 2) onRefreshScriptPreview();
    });
    retranslateUi();
}

void MainWindow::updateWorkspace() {
    const bool tr = m_currentLang == AppLanguage::Turkish;
    const int stage = m_tabs->currentIndex();
    const QStringList titles = tr
        ? QStringList{"Pantograf atölyesi", "Araç montajı", "Paketle ve dışa aktar"}
        : QStringList{"Pantograph workshop", "Vehicle mounting", "Package & export"};
    const QStringList hints = tr
        ? QStringList{"Modelini içe aktar, eklemlerini ayarla ve hareketini test et.",
                      "Tren paketini aç ve pantografları araç tavanlarına yerleştir.",
                      "Scripti incele ve MTR için hazır kaynak paketini oluştur."}
        : QStringList{"Import your model, refine its joints and test it in motion.",
                      "Open a train pack and position pantographs on your vehicles.",
                      "Review the generated script and build your MTR resource pack."};
    m_workspaceTitle->setText(titles[stage]);
    m_workspaceHint->setText(hints[stage]);
    for (int i = 0; i < 3; ++i) m_navButtons[i]->setChecked(i == stage);
    m_sessionStatus->setText(tr ? QString("%1 pantograf profili   /   %2 araç").arg(m_pantographs.size()).arg(m_vehicles.size())
                               : QString("%1 pantograph profile(s)   /   %2 vehicles").arg(m_pantographs.size()).arg(m_vehicles.size()));
    if (m_previewStack && m_viewport)
        m_previewStack->setCurrentIndex(m_viewport->hasModel() ? 1 : 0);
}

void MainWindow::onLanguageChanged(int index) {
    m_currentLang = (index == 1) ? AppLanguage::Turkish : AppLanguage::English;
    retranslateUi();
}

void MainWindow::retranslateUi() {
    const bool isTr = m_currentLang == AppLanguage::Turkish;
    setWindowTitle(isTr ? "EDPC — Pantograf Atölyesi" : "EDPC — Pantograph Studio");
    auto text = [isTr](auto* widget, const char* en, const char* tr) {
        if (widget) widget->setText(QString::fromUtf8(isTr ? tr : en));
    };
    auto title = [isTr](QGroupBox* widget, const char* en, const char* tr) {
        if (widget) widget->setTitle(QString::fromUtf8(isTr ? tr : en));
    };
    text(m_stepLabel, "WORKFLOW", "ÇALIŞMA AKIŞI");
    text(m_navButtons[0], "01   Pantograph\n       Model · kinematics", "01   Pantograf\n       Model ve kinematik");
    text(m_navButtons[1], "02   Train pack\n       Vehicles · mounting", "02   Tren paketi\n       Araçlar ve montaj");
    text(m_navButtons[2], "03   Export\n       Review · package", "03   Dışa aktar\n       İncele ve paketle");
    for (int i = 0; i < 3; ++i) m_navButtons[i]->setToolTip(QString("Alt+%1").arg(i + 1));
    title(m_grpPantoProfile, "Active profile", "Etkin profil");
    text(m_btnAddPanto, "+ New", "+ Yeni");
    text(m_btnRemovePanto, "Remove", "Sil");
    title(m_grpModel, "Model source", "Model dosyası");
    m_txtObjPath->setPlaceholderText(isTr ? ".obj veya .bbmodel dosyası" : ".obj or .bbmodel file");
    text(m_btnBrowseObj, "Browse…", "Gözat…");
    if (m_objPath.isEmpty()) {
        text(m_lblModelInfo, "Import a model with p_base, p1, p2 and p3 groups.", "p_base, p1, p2 ve p3 gruplarını içeren bir model yükle.");
        text(m_lblPivotsInfo, "Joint data appears after import.", "Eklem verileri içe aktardıktan sonra görünür.");
    }
    title(m_grpPivots, "Joint positions", "Eklem konumları");
    text(m_lblPivotHelp, "Coordinates in meters. Changes update the preview instantly.", "Koordinatlar metre cinsindedir. Değişiklikler önizlemeye anında yansır.");
    text(m_lblP1, "● Base · p1", "● Taban · p1");
    text(m_lblP2, "● Elbow · p2", "● Dirsek · p2");
    text(m_lblP3, "● Head · p3", "● Başlık · p3");
    text(m_chkP4Enabled, "Enable 4-bar linkage (p4)", "4 çubuklu bağlantı (p4)");
    text(m_lblP4, "● Rod base · p4", "● Çubuk tabanı · p4");
    text(m_lblP4Elbow, "● Rod knee", "● Çubuk dirseği");
    text(m_btnResetPivots, "Reset joints to model defaults", "Eklemleri model değerlerine sıfırla");
    title(m_grpParams, "Operating heights", "Çalışma yükseklikleri");
    text(m_lblWireRef, "Reference wire", "Referans katener");
    text(m_lblNoWirePark, "No-wire park", "Telsiz park");
    text(m_chkDepotPark, "Enable depot parking", "Depoda park et");
    text(m_lblDepotPark, "Depot park", "Depo parkı");
    text(m_lblUpperThresh, "Upper threshold (+Δ)", "Üst eşik (+Δ)");
    text(m_lblLowerThresh, "Lower threshold (−Δ)", "Alt eşik (−Δ)");
    for (auto* button : {m_btnTestWireRef, m_btnTestNoWire, m_btnTestDepot}) text(button, "Test", "Dene");
    title(m_grpLive, "Simulation", "Simülasyon");
    text(m_lblTestHeight, "Wire height", "Katener yüksekliği");
    text(m_chkShowWire, "Catenary wire", "Katener teli");
    text(m_chkShowPivots, "Joint markers", "Eklem noktaları");
    text(m_btnResetCam, "Reset view", "Görünümü sıfırla");
    text(m_chkInvertCam, "Invert orbit", "Ters döndür");
    text(m_lblTab1Help, "DRAG  Orbit     SHIFT + DRAG  Pan     SCROLL  Zoom", "SÜRÜKLE  Döndür     SHIFT + SÜRÜKLE  Kaydır     TEKERLEK  Yakınlaş");
    text(m_btnNextToPack, "Continue to train pack  →", "Tren paketine geç  →");
    text(m_lblTab1VpTitle, "3D WORKSPACE", "3D ÇALIŞMA ALANI");
    if (!m_viewport) text(m_lblTab1VpTitle, "3D WORKSPACE · Vulkan unavailable", "3D ÇALIŞMA ALANI · Vulkan kullanılamıyor");
    text(m_emptyTitle, "Your next pantograph starts here", "Yeni pantografın burada başlıyor");
    text(m_emptyHint, "Import a Blockbench or OBJ model to configure its joints\nand preview its movement in real time.", "Eklemleri ayarlamak ve hareketi canlı görmek için\nbir Blockbench veya OBJ modeli içe aktar.");
    text(m_emptyImport, "Import pantograph model", "Pantograf modelini içe aktar");
    if (m_inspectorTabs) {
        m_inspectorTabs->setTabText(0, isTr ? "Model" : "Model");
        m_inspectorTabs->setTabText(1, isTr ? "Eklemler" : "Joints");
        m_inspectorTabs->setTabText(2, isTr ? "Çalışma" : "Operation");
    }
    title(m_grpPack, "Train pack source", "Tren paketi kaynağı");
    m_txtPackPath->setPlaceholderText(isTr ? "Bir ZIP paketi veya klasör seç" : "Choose a ZIP pack or an unpacked folder");
    text(m_btnBrowseZip, "Open ZIP…", "ZIP aç…");
    text(m_btnBrowseFolder, "Open folder…", "Klasör aç…");
    if (m_packInputPath.isEmpty()) text(m_lblPackStatus, "No pack loaded. Vehicles appear here after import.", "Paket yüklenmedi. İçe aktardıktan sonra araçlar burada görünür.");
    title(m_grpVehicles, "Vehicles", "Araçlar");
    m_tableVehicles->setHorizontalHeaderLabels(isTr
        ? QStringList{"Tak", "Pantograf", "Araç ID", "Araç adı", "Açıklama"}
        : QStringList{"Mount", "Pantograph", "Vehicle ID", "Vehicle name", "Description"});
    text(m_btnSelectAll, "Select all", "Tümünü seç");
    text(m_btnDeselectAll, "Clear selection", "Seçimi temizle");
    title(m_groupVehicleMount, "Roof mounting", "Tavan montajı");
    text(m_lblVehiclePanto, "Pantograph", "Pantograf");
    if (m_currentVehicleRow < 0) text(m_lblSelectedVehicleTitle, "Select a vehicle to configure", "Ayarlamak için bir araç seç");
    text(m_lblMountHeight, "Height · Y", "Yükseklik · Y");
    text(m_lblMountX, "Lateral · X", "Enine · X");
    text(m_lblMountZ, "Longitudinal · Z", "Boyuna · Z");
    text(m_lblRotationY, "Orientation", "Yönelim");
    m_comboRotationY->setItemText(0, isTr ? "0° · İleri" : "0° · Forward");
    m_comboRotationY->setItemText(1, isTr ? "180° · Geri" : "180° · Backward");
    text(m_lblWireDetectZ, "Wire detection · Z", "Tel algılama · Z");
    text(m_lblPresets, "POSITION PRESETS", "KONUM ÖNAYARLARI");
    text(m_btnPreFront, "Front  +5.2 m", "Ön  +5.2 m");
    text(m_btnPreRear, "Rear  −5.2 m / 180°", "Arka  −5.2 m / 180°");
    text(m_btnPreCenter, "Center  0 m", "Merkez  0 m");
    text(m_lblTab2VpTitle, "VEHICLE PREVIEW", "ARAÇ ÖNİZLEMESİ");
    text(m_btnResetVehCam, "Reset", "Sıfırla");
    text(m_btnFrontVehCam, "Front cab", "Ön kabin");
    text(m_btnRearVehCam, "Rear view", "Arka görünüm");
    text(m_chkInvertVehCam, "Invert orbit", "Ters döndür");
    text(m_vehicleEmptyTitle, "A place for your pantograph", "Pantografın için bir yer");
    text(m_vehicleEmptyHint, "Open a train pack, then select a vehicle\nto preview and adjust its roof mounting.", "Bir tren paketi aç, ardından tavan montajını\nayarlamak için bir araç seç.");
    text(m_vehicleEmptyImport, "Open train pack", "Tren paketini aç");
    if (m_packInputPath.isEmpty()) text(m_lblVehicleVpInfo, "No vehicle selected", "Araç seçilmedi");
    if (!m_vehicleViewport) m_vehicleEmptyHint->setToolTip(isTr ? "3D önizleme için Vulkan sürücüsü gereklidir." : "A Vulkan driver is required for 3D preview.");
    text(m_lblTab2Help, "Drag to orbit · Shift + drag to pan · Scroll to zoom", "Sürükle: döndür · Shift + sürükle: kaydır · Tekerlek: yakınlaş");
    text(m_btnNextToExport, "Continue to export  →", "Dışa aktarmaya geç  →");
    title(m_grpConfig, "Output destination", "Çıktı konumu");
    text(m_lblOutputPath, "Resource pack · ZIP", "Kaynak paketi · ZIP");
    text(m_btnBrowseOut, "Browse…", "Gözat…");
    m_txtOutputPath->setPlaceholderText(isTr ? "Çıktı ZIP dosyasının yolu" : "Path to the output ZIP file");
    title(m_grpPreview, "Generated script", "Oluşturulan script");
    text(m_lblScriptPreviewHint, "Included in your pack at assets/mtr/panto/", "Paket içindeki konumu: assets/mtr/panto/");
    text(m_btnRefreshPreview, "Refresh", "Yenile");
    title(m_grpAction, "Build resource pack", "Kaynak paketini oluştur");
    text(m_btnPackage, "Package & export ZIP  ↗", "Paketle ve ZIP olarak aktar  ↗");
    text(m_btnOpenOutputFolder, "Open output folder", "Çıktı klasörünü aç");
    if (m_lastExportedFolder.isEmpty()) text(m_lblExportStatus, "Choose a model, mount it on a vehicle, then export your pack.", "Bir model seç, araca yerleştir ve paketini dışa aktar.");
    text(m_btnCheckUpdate, "Check for updates", "Güncellemeleri denetle");
    updatePantographCombos();
    if (m_currentPantoIndex >= 0 && m_currentPantoIndex < m_pantographs.size()) {
        const auto& p = m_pantographs[m_currentPantoIndex];
        m_lblProfileInfo->setText(QString("%1_pantograph.js\n%2.obj").arg(p.randomScriptId, p.randomModelName));
    }
    for (int r = 0; r < m_vehicles.size(); ++r) {
        if (r < m_tableVehicles->rowCount() && m_tableVehicles->item(r, 1))
            m_tableVehicles->item(r, 1)->setText(m_vehicles[r].hasPantograph ? m_pantographs[m_vehicles[r].pantographIndex].displayName(isTr) : "—");
    }
    updateWorkspace();
    if (m_viewport && m_viewport->hasModel()) updateViewportKinematics();
    else {
        text(m_lblArmAngles, "p1  —     p2  —     p4  —", "p1  —     p2  —     p4  —");
        m_lblReachability->setStyleSheet("color: #969aa3;");
        text(m_lblReachability, "Awaiting model", "Model bekleniyor");
    }
}

QWidget* MainWindow::createTab1Pantograph() {
    QWidget* tab = new QWidget(this);
    QHBoxLayout* tabLayout = new QHBoxLayout(tab);
    tabLayout->setContentsMargins(0, 0, 0, 0);
    tabLayout->setSpacing(12);

    // Left control sidebar
    QWidget* leftPanel = new QWidget(tab);
    leftPanel->setMinimumWidth(355);
    leftPanel->setMaximumWidth(410);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(10);

    // --- Group 0: Pantograph Profile Management ---
    m_grpPantoProfile = new QGroupBox("0. Pantograph Profile Management", leftPanel);
    QVBoxLayout* grpPantoLayout = new QVBoxLayout(m_grpPantoProfile);
    grpPantoLayout->setSpacing(6);

    QHBoxLayout* pantoRow = new QHBoxLayout();
    m_comboPantographs = new QComboBox(m_grpPantoProfile);
    m_btnAddPanto = new QPushButton("➕ New", m_grpPantoProfile);
    m_btnRemovePanto = new QPushButton("🗑 Delete", m_grpPantoProfile);
    m_btnRemovePanto->setEnabled(false);

    connect(m_comboPantographs, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onPantographSelectionChanged);
    connect(m_btnAddPanto, &QPushButton::clicked, this, &MainWindow::onAddPantograph);
    connect(m_btnRemovePanto, &QPushButton::clicked, this, &MainWindow::onRemovePantograph);

    pantoRow->addWidget(m_comboPantographs, 1);
    pantoRow->addWidget(m_btnAddPanto);
    pantoRow->addWidget(m_btnRemovePanto);
    grpPantoLayout->addLayout(pantoRow);

    m_lblProfileInfo = new QLabel(m_grpPantoProfile);
    m_lblProfileInfo->setStyleSheet("color: #a2a6ae; font-size: 10px; font-family: monospace;");
    m_lblProfileInfo->setWordWrap(true);
    grpPantoLayout->addWidget(m_lblProfileInfo);

    leftLayout->addWidget(m_grpPantoProfile);
    m_inspectorTabs = new QTabWidget(leftPanel);
    leftLayout->addWidget(m_inspectorTabs, 1);
    auto inspectorPage = [this](QWidget* widget, const QString& title) {
        auto* page = new QWidget();
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 8, 0, 0);
        layout->addWidget(widget);
        layout->addStretch();
        auto* scroll = new QScrollArea();
        scroll->setWidgetResizable(true);
        scroll->setWidget(page);
        m_inspectorTabs->addTab(scroll, title);
    };

    // --- Group 1: OBJ / BBModel File Selection ---
    m_grpModel = new QGroupBox("1. Pantograph 3D Model Import (.obj, .bbmodel)", leftPanel);
    QVBoxLayout* grpModelLayout = new QVBoxLayout(m_grpModel);

    QHBoxLayout* fileRow = new QHBoxLayout();
    m_txtObjPath = new QLineEdit(m_grpModel);
    m_txtObjPath->setPlaceholderText("Select pantograph .obj or .bbmodel file...");
    m_txtObjPath->setReadOnly(true);
    m_btnBrowseObj = new QPushButton("📁 Browse...", m_grpModel);
    connect(m_btnBrowseObj, &QPushButton::clicked, this, &MainWindow::onBrowseModel);
    fileRow->addWidget(m_txtObjPath);
    fileRow->addWidget(m_btnBrowseObj);
    grpModelLayout->addLayout(fileRow);

    m_lblModelInfo = new QLabel("No model loaded yet. (Required parts: p_base, p1, p2, p3)", m_grpModel);
    m_lblModelInfo->setWordWrap(true);
    m_lblModelInfo->setStyleSheet("color: #8f96a3; ");
    grpModelLayout->addWidget(m_lblModelInfo);

    m_lblPivotsInfo = new QLabel("Pivots: [Not loaded]", m_grpModel);
    m_lblPivotsInfo->setWordWrap(true);
    m_lblPivotsInfo->setStyleSheet("font-family: monospace; font-size: 11px; color: #a0a8b7;");
    grpModelLayout->addWidget(m_lblPivotsInfo);

    inspectorPage(m_grpModel, "Model");

    // --- Group 2: Pantograph Pivot Positions & Rigging ---
    m_grpPivots = new QGroupBox("2. Pantograph Pivot Positions & Rigging", leftPanel);
    QVBoxLayout* grpPivotsLayout = new QVBoxLayout(m_grpPivots);
    grpPivotsLayout->setSpacing(6);

    m_lblPivotHelp = new QLabel("Adjust pivot axes in meters (Y: height, Z: longitudinal). Changes update 3D kinematics in real time.", m_grpPivots);
    m_lblPivotHelp->setWordWrap(true);
    m_lblPivotHelp->setStyleSheet("color: #7f848e; font-size: 11px;");
    grpPivotsLayout->addWidget(m_lblPivotHelp);

    QGridLayout* pivotGrid = new QGridLayout();
    pivotGrid->setHorizontalSpacing(8);
    pivotGrid->setVerticalSpacing(4);

    QLabel* hdrJoint = new QLabel("JOINT", m_grpPivots);
    hdrJoint->setStyleSheet("font-weight: bold; color: #a0a8b7; font-size: 11px;");
    QLabel* hdrY = new QLabel("Y (m)", m_grpPivots);
    hdrY->setStyleSheet("font-weight: bold; color: #a0a8b7; font-size: 11px;");
    QLabel* hdrZ = new QLabel("Z (m)", m_grpPivots);
    hdrZ->setStyleSheet("font-weight: bold; color: #a0a8b7; font-size: 11px;");

    pivotGrid->addWidget(hdrJoint, 0, 0);
    pivotGrid->addWidget(hdrY, 0, 1);
    pivotGrid->addWidget(hdrZ, 0, 2);

    auto createSpin = [this](double val = 0.0) -> QDoubleSpinBox* {
        auto* spin = new QDoubleSpinBox(m_grpPivots);
        spin->setRange(-20.0, 20.0);
        spin->setSingleStep(0.001);
        spin->setDecimals(5);
        spin->setValue(val);
        spin->setMinimumWidth(82);
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onPivotSpinboxChanged);
        return spin;
    };

    // p1 - Base (Cyan)
    m_lblP1 = new QLabel("● Base (p1):", m_grpPivots);
    m_lblP1->setStyleSheet("color: #00e5ff; font-weight: bold;");
    m_lblP1->setToolTip("Pantograph lower hinge on roof (part_id 8 in 3D: Cyan)");
    m_spinP1Y = createSpin(0.18357);
    m_spinP1Z = createSpin(0.11706);
    pivotGrid->addWidget(m_lblP1, 1, 0);
    pivotGrid->addWidget(m_spinP1Y, 1, 1);
    pivotGrid->addWidget(m_spinP1Z, 1, 2);

    // p2 - Elbow (Orange)
    m_lblP2 = new QLabel("● Elbow (p2):", m_grpPivots);
    m_lblP2->setStyleSheet("color: #ff9800; font-weight: bold;");
    m_lblP2->setToolTip("Knee/elbow joint between lower and upper arms (part_id 9 in 3D: Orange)");
    m_spinP2Y = createSpin(0.83591);
    m_spinP2Z = createSpin(-1.21875);
    pivotGrid->addWidget(m_lblP2, 2, 0);
    pivotGrid->addWidget(m_spinP2Y, 2, 1);
    pivotGrid->addWidget(m_spinP2Z, 2, 2);

    // p3 - Tip / Head (Green)
    m_lblP3 = new QLabel("● Head / Tip (p3):", m_grpPivots);
    m_lblP3->setStyleSheet("color: #00e676; font-weight: bold;");
    m_lblP3->setToolTip("Top collector head hinge point (part_id 10 in 3D: Neon Green)");
    m_spinP3Y = createSpin(1.42328);
    m_spinP3Z = createSpin(0.08750);
    pivotGrid->addWidget(m_lblP3, 3, 0);
    pivotGrid->addWidget(m_spinP3Y, 3, 1);
    pivotGrid->addWidget(m_spinP3Z, 3, 2);

    // p4 - 4-Bar Coupler Checkbox
    m_chkP4Enabled = new QCheckBox("Enable 4-Bar Coupler Pushrod (p4)", m_grpPivots);
    m_chkP4Enabled->setStyleSheet("font-weight: bold; color: #e040fb;");
    connect(m_chkP4Enabled, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_spinP4BaseY) m_spinP4BaseY->setEnabled(checked);
        if (m_spinP4BaseZ) m_spinP4BaseZ->setEnabled(checked);
        if (m_spinP4ElbowY) m_spinP4ElbowY->setEnabled(checked);
        if (m_spinP4ElbowZ) m_spinP4ElbowZ->setEnabled(checked);
        onPivotSpinboxChanged();
    });
    pivotGrid->addWidget(m_chkP4Enabled, 4, 0, 1, 3);

    // p4 - Coupler Base (Magenta)
    m_lblP4 = new QLabel("● Pushrod Base (p4):", m_grpPivots);
    m_lblP4->setStyleSheet("color: #e040fb;");
    m_lblP4->setToolTip("Lower hinge of parallel pushrod (part_id 11 in 3D: Magenta)");
    m_spinP4BaseY = createSpin(0.21560);
    m_spinP4BaseZ = createSpin(-0.32557);
    pivotGrid->addWidget(m_lblP4, 5, 0);
    pivotGrid->addWidget(m_spinP4BaseY, 5, 1);
    pivotGrid->addWidget(m_spinP4BaseZ, 5, 2);

    // p4_elbow - Coupler Elbow (Coral Pink)
    m_lblP4Elbow = new QLabel("● Pushrod Knee (p4_elbow):", m_grpPivots);
    m_lblP4Elbow->setStyleSheet("color: #ff6e40;");
    m_lblP4Elbow->setToolTip("Upper hinge of parallel pushrod (part_id 12 in 3D: Coral Pink)");
    m_spinP4ElbowY = createSpin(0.75445);
    m_spinP4ElbowZ = createSpin(-1.37158);
    pivotGrid->addWidget(m_lblP4Elbow, 6, 0);
    pivotGrid->addWidget(m_spinP4ElbowY, 6, 1);
    pivotGrid->addWidget(m_spinP4ElbowZ, 6, 2);

    grpPivotsLayout->addLayout(pivotGrid);

    // Action buttons: Reset
    QHBoxLayout* pivotBtnRow = new QHBoxLayout();
    m_btnResetPivots = new QPushButton("🔄 Reset to Original", m_grpPivots);
    m_btnResetPivots->setToolTip("Reset pivot positions to default values from loaded model");
    connect(m_btnResetPivots, &QPushButton::clicked, this, &MainWindow::onResetPivots);

    pivotBtnRow->addWidget(m_btnResetPivots);
    grpPivotsLayout->addLayout(pivotBtnRow);

    m_grpPivots->setEnabled(false);
    inspectorPage(m_grpPivots, "Joints");

    // --- Group 3: Parameters ---
    m_grpParams = new QGroupBox("3. Catenary & Operating Parameters", leftPanel);
    QFormLayout* formParams = new QFormLayout(m_grpParams);
    formParams->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    formParams->setRowWrapPolicy(QFormLayout::WrapAllRows);
    formParams->setSpacing(8);

    QHBoxLayout* wireRefRow = new QHBoxLayout();
    m_spinWireRef = new QDoubleSpinBox(m_grpParams);
    m_spinWireRef->setRange(0.2, 5.0);
    m_spinWireRef->setSingleStep(0.001);
    m_spinWireRef->setDecimals(4);
    m_spinWireRef->setValue(1.4480);
    m_spinWireRef->setSuffix(" m");
    connect(m_spinWireRef, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onWireRefChanged);
    m_btnTestWireRef = new QPushButton("👁 Test", m_grpParams);
    m_btnTestWireRef->setToolTip("Simulate pantograph pose at reference catenary height");
    connect(m_btnTestWireRef, &QPushButton::clicked, this, &MainWindow::onTestWireRef);
    wireRefRow->addWidget(m_spinWireRef, 1);
    wireRefRow->addWidget(m_btnTestWireRef);
    m_lblWireRef = new QLabel("Reference Catenary Height:", m_grpParams);
    formParams->addRow(m_lblWireRef, wireRefRow);

    QHBoxLayout* noWireRow = new QHBoxLayout();
    m_spinNoWirePark = new QDoubleSpinBox(m_grpParams);
    m_spinNoWirePark->setRange(0.1, 3.0);
    m_spinNoWirePark->setSingleStep(0.01);
    m_spinNoWirePark->setDecimals(4);
    m_spinNoWirePark->setValue(1.3300);
    m_spinNoWirePark->setSuffix(" m");
    connect(m_spinNoWirePark, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onRefreshScriptPreview);
    m_btnTestNoWire = new QPushButton("👁 Test", m_grpParams);
    m_btnTestNoWire->setToolTip("Simulate idle / no-wire park height");
    connect(m_btnTestNoWire, &QPushButton::clicked, this, &MainWindow::onTestNoWirePark);
    noWireRow->addWidget(m_spinNoWirePark, 1);
    noWireRow->addWidget(m_btnTestNoWire);
    m_lblNoWirePark = new QLabel("Idle / No-Wire Park Height:", m_grpParams);
    formParams->addRow(m_lblNoWirePark, noWireRow);

    m_chkDepotPark = new QCheckBox("Enable Depot Park Mode", m_grpParams);
    m_chkDepotPark->setChecked(false);
    connect(m_chkDepotPark, &QCheckBox::toggled, this, &MainWindow::onDepotParkToggled);
    formParams->addRow("", m_chkDepotPark);

    QHBoxLayout* depotRow = new QHBoxLayout();
    m_spinDepotPark = new QDoubleSpinBox(m_grpParams);
    m_spinDepotPark->setRange(0.05, 2.0);
    m_spinDepotPark->setSingleStep(0.01);
    m_spinDepotPark->setDecimals(4);
    m_spinDepotPark->setValue(0.3800);
    m_spinDepotPark->setSuffix(" m");
    m_spinDepotPark->setEnabled(false);
    connect(m_spinDepotPark, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onRefreshScriptPreview);
    m_btnTestDepot = new QPushButton("👁 Test", m_grpParams);
    m_btnTestDepot->setToolTip("Simulate depot park height");
    connect(m_btnTestDepot, &QPushButton::clicked, this, &MainWindow::onTestDepotPark);
    depotRow->addWidget(m_spinDepotPark, 1);
    depotRow->addWidget(m_btnTestDepot);
    m_lblDepotPark = new QLabel("Depot Park Height:", m_grpParams);
    formParams->addRow(m_lblDepotPark, depotRow);

    m_spinUpperThresh = new QDoubleSpinBox(m_grpParams);
    m_spinUpperThresh->setRange(0.5, 6.0);
    m_spinUpperThresh->setSingleStep(0.1);
    m_spinUpperThresh->setDecimals(2);
    m_spinUpperThresh->setValue(3.0);
    m_spinUpperThresh->setSuffix(" m");
    connect(m_spinUpperThresh, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onRefreshScriptPreview);
    m_lblUpperThresh = new QLabel("Upper Catenary Threshold (+Δ):", m_grpParams);
    formParams->addRow(m_lblUpperThresh, m_spinUpperThresh);

    m_spinLowerThresh = new QDoubleSpinBox(m_grpParams);
    m_spinLowerThresh->setRange(-6.0, 0.0);
    m_spinLowerThresh->setSingleStep(0.1);
    m_spinLowerThresh->setDecimals(2);
    m_spinLowerThresh->setValue(-3.0);
    m_spinLowerThresh->setSuffix(" m");
    connect(m_spinLowerThresh, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onRefreshScriptPreview);
    m_lblLowerThresh = new QLabel("Lower Catenary Threshold (-Δ):", m_grpParams);
    formParams->addRow(m_lblLowerThresh, m_spinLowerThresh);

    inspectorPage(m_grpParams, "Operation");

    // --- Group 4: Live Interactive Testing ---
    m_grpLive = new QGroupBox("4. Live Kinematics & Interactive Test", leftPanel);
    QVBoxLayout* liveLayout = new QVBoxLayout(m_grpLive);
    liveLayout->setSpacing(8);

    QHBoxLayout* testHeightRow = new QHBoxLayout();
    m_lblTestHeight = new QLabel("Simulated Catenary Height:", m_grpLive);
    testHeightRow->addWidget(m_lblTestHeight);
    m_spinTestHeight = new QDoubleSpinBox(m_grpLive);
    m_spinTestHeight->setMaximumWidth(140);
    m_spinTestHeight->setRange(0.20, 2.60);
    m_spinTestHeight->setSingleStep(0.005);
    m_spinTestHeight->setDecimals(4);
    m_spinTestHeight->setValue(1.4480);
    m_spinTestHeight->setSuffix(" m");
    connect(m_spinTestHeight, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onKinematicsSpinChanged);
    testHeightRow->addStretch();
    testHeightRow->addWidget(m_spinTestHeight);
    liveLayout->addLayout(testHeightRow);

    m_sliderTestHeight = new QSlider(Qt::Horizontal, m_grpLive);
    m_sliderTestHeight->setRange(200, 2600); // 0.200m to 2.600m
    m_sliderTestHeight->setValue(1448);
    connect(m_sliderTestHeight, &QSlider::valueChanged, this, &MainWindow::onKinematicsSliderChanged);
    liveLayout->addWidget(m_sliderTestHeight);

    m_chkShowWire = new QCheckBox("Show Reference Catenary Wire", m_grpLive);
    m_chkShowWire->setChecked(true);
    connect(m_chkShowWire, &QCheckBox::toggled, this, &MainWindow::onShowWireToggled);


    m_chkShowPivots = new QCheckBox("Show Pantograph Pivot Markers", m_grpLive);
    m_chkShowPivots->setChecked(true);
    connect(m_chkShowPivots, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_viewport) m_viewport->setShowPivots(checked);
    });
    auto* visibilityRow = new QHBoxLayout();
    visibilityRow->addWidget(m_chkShowWire);
    visibilityRow->addWidget(m_chkShowPivots);
    visibilityRow->addStretch();
    liveLayout->addLayout(visibilityRow);

    m_lblArmAngles = new QLabel("Lower Arm (p1): -- | Upper Arm (p2): -- | Pushrod (p4): --", m_grpLive);
    m_lblArmAngles->setWordWrap(true);
    m_lblArmAngles->setStyleSheet("font-weight: 600; color: #61afef;");
    liveLayout->addWidget(m_lblArmAngles);

    m_lblReachability = new QLabel("Status: Ready", m_grpLive);
    m_lblReachability->setStyleSheet("font-weight: bold; color: #98c379;");
    liveLayout->addWidget(m_lblReachability);

    QHBoxLayout* camRow = new QHBoxLayout();
    m_btnResetCam = new QPushButton("🎥 Reset Camera", m_grpLive);
    connect(m_btnResetCam, &QPushButton::clicked, this, [this]() {
        if (m_viewport) m_viewport->resetCamera();
    });
    camRow->addWidget(m_btnResetCam);

    m_chkInvertCam = new QCheckBox("Invert Orbit Controls", m_grpLive);
    m_chkInvertCam->setChecked(true);
    connect(m_chkInvertCam, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_viewport) m_viewport->setInvertCamera(checked);
    });
    camRow->addWidget(m_chkInvertCam);
    liveLayout->addLayout(camRow);

    m_lblTab1Help = new QLabel("💡 3D Controls: Left Drag: Orbit | Shift+Left / Middle Drag: Pan | Wheel: Zoom", m_grpLive);
    m_lblTab1Help->setStyleSheet("color: #7f848e; font-size: 11px;");
    m_lblTab1Help->setWordWrap(true);
    liveLayout->addWidget(m_lblTab1Help);



    m_btnNextToPack = new QPushButton("Next Step: Train Pack Selection ➔", leftPanel);
    m_btnNextToPack->setObjectName("actionButton");
    connect(m_btnNextToPack, &QPushButton::clicked, this, [this]() {
        m_tabs->setCurrentIndex(1);
    });
    leftLayout->addWidget(m_btnNextToPack);



    // Right Viewport Panel
    QWidget* rightPanel = new QWidget(tab);
    QVBoxLayout* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(6);

    QHBoxLayout* vpHeader = new QHBoxLayout();
    m_lblTab1VpTitle = new QLabel("⚡ 3D Real-Time Preview", rightPanel);
    m_lblTab1VpTitle->setStyleSheet("font-weight: bold; font-size: 13px; color: #eea66b;");
    vpHeader->addWidget(m_lblTab1VpTitle);
    vpHeader->addStretch();
    rightLayout->addLayout(vpHeader);
    m_previewStack = new QStackedWidget(rightPanel);
    auto* empty = new QWidget(m_previewStack);
    empty->setObjectName("canvasEmpty");
    auto* emptyLayout = new QVBoxLayout(empty);
    emptyLayout->setContentsMargins(16, 16, 16, 16);
    emptyLayout->setSpacing(10);
    emptyLayout->addStretch();
    emptyLayout->addWidget(new PantographEmblem(empty), 0, Qt::AlignHCenter);
    m_emptyTitle = new QLabel(empty);
    m_emptyTitle->setObjectName("emptyTitle");
    m_emptyTitle->setWordWrap(true);
    m_emptyTitle->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(m_emptyTitle);
    m_emptyHint = new QLabel(empty);
    m_emptyHint->setObjectName("muted");
    m_emptyHint->setWordWrap(true);
    m_emptyHint->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(m_emptyHint);
    m_emptyImport = new QPushButton(empty);
    m_emptyImport->setObjectName("actionButton");
    connect(m_emptyImport, &QPushButton::clicked, this, &MainWindow::onBrowseModel);
    emptyLayout->addWidget(m_emptyImport, 0, Qt::AlignHCenter);
    auto* formats = new QLabel("BLOCKBENCH .bbmodel   /   WAVEFRONT .obj", empty);
    formats->setObjectName("eyebrow");
    emptyLayout->addWidget(formats, 0, Qt::AlignHCenter);
    emptyLayout->addStretch();
    m_previewStack->addWidget(empty);
    rightLayout->addWidget(m_previewStack, 1);

    // Initialize Vulkan Viewport
    if (m_vulkanInst && m_vulkanInst->isValid()) {
        m_viewport = new VulkanViewport();
        m_viewport->setVulkanInstance(m_vulkanInst);
        m_viewportContainer = QWidget::createWindowContainer(m_viewport, rightPanel);
        m_viewportContainer->setMinimumSize(300, 200);
        m_viewportContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        m_viewportContainer->setFocusPolicy(Qt::TabFocus);

        connect(m_viewport, &VulkanViewport::modelLoaded, this, [this](bool success, const QString& msg) {
            QString fullMsg = msg;
            if (success) {
                if (!m_texturePath.isEmpty()) {
                    fullMsg += (m_currentLang == AppLanguage::Turkish)
                        ? QString("\n✓ Doku tespit edildi: %1").arg(QFileInfo(m_texturePath).fileName())
                        : QString("\n✓ Texture detected: %1").arg(QFileInfo(m_texturePath).fileName());
                } else {
                    fullMsg += (m_currentLang == AppLanguage::Turkish)
                        ? "\n⚠ Doku bulunamadı (varsayılan renklendirme kullanılıyor)."
                        : "\n⚠ No texture found (using default coloring).";
                }
            }
            m_lblModelInfo->setText(fullMsg);
            if (success) {
                m_lblModelInfo->setStyleSheet("color: #98c379;");
                EdpcPivots p;
                if (m_viewport->getPivots(&p)) {
                    QString pivText = QString("Base: (0, %1, %2)\nElbow: (0, %3, %4)\nTip: (0, %5, %6)\nLink 4: %7")
                        .arg(p.base.y, 0, 'f', 3).arg(p.base.z, 0, 'f', 3)
                        .arg(p.elbow.y, 0, 'f', 3).arg(p.elbow.z, 0, 'f', 3)
                        .arg(p.tip.y, 0, 'f', 3).arg(p.tip.z, 0, 'f', 3)
                        .arg(p.has_p4 ? (m_currentLang == AppLanguage::Turkish ? "Var (4-Bar Bağlantılı)" : "Present (4-Bar Link)")
                                      : (m_currentLang == AppLanguage::Turkish ? "Yok (Doğrudan IK)" : "None (Direct IK)"));
                    m_lblPivotsInfo->setText(pivText);
                }
            } else {
                m_lblModelInfo->setStyleSheet("color: #e06c75;");
            }
        });

        connect(m_viewport, &VulkanViewport::kinematicsUpdated, this, &MainWindow::onKinematicsUpdated);

        m_previewStack->addWidget(m_viewportContainer);
        connect(m_viewport, &VulkanViewport::modelLoaded, this, [this]() { updateWorkspace(); });
    }

    rightLayout->addWidget(m_grpLive);
    tabLayout->addWidget(rightPanel, 1);
    tabLayout->addWidget(leftPanel);
    return tab;
}

QWidget* MainWindow::createTab2TrainPack() {
    QWidget* tab = new QWidget(this);
    QVBoxLayout* tabLayout = new QVBoxLayout(tab);
    tabLayout->setContentsMargins(0, 0, 0, 0);
    tabLayout->setSpacing(12);

    // Pack file picker
    m_grpPack = new QGroupBox("1. Train Pack Import (.zip or Unpacked Folder)", tab);
    QVBoxLayout* packLayout = new QVBoxLayout(m_grpPack);

    QHBoxLayout* packFileRow = new QHBoxLayout();
    m_txtPackPath = new QLineEdit(m_grpPack);
    m_txtPackPath->setPlaceholderText("Select train pack .zip file or folder...");
    m_txtPackPath->setReadOnly(true);

    m_btnBrowseZip = new QPushButton("📦 Select ZIP Pack...", m_grpPack);
    connect(m_btnBrowseZip, &QPushButton::clicked, this, &MainWindow::onBrowsePackZip);

    m_btnBrowseFolder = new QPushButton("📂 Select Folder...", m_grpPack);
    connect(m_btnBrowseFolder, &QPushButton::clicked, this, &MainWindow::onBrowsePackFolder);

    packFileRow->addWidget(m_txtPackPath);
    packFileRow->addWidget(m_btnBrowseZip);
    packFileRow->addWidget(m_btnBrowseFolder);
    packLayout->addLayout(packFileRow);

    m_lblPackStatus = new QLabel("No train pack selected yet.", m_grpPack);
    m_lblPackStatus->setStyleSheet("color: #8f96a3; ");
    packLayout->addWidget(m_lblPackStatus);

    tabLayout->addWidget(m_grpPack);

    // Splitter for Vehicle List and Vehicle Mount adjustments
    QSplitter* splitter = new QSplitter(Qt::Horizontal, tab);
    splitter->setHandleWidth(14);
    splitter->setStyleSheet("QSplitter::handle { background: #191b1f; }");

    // Left: Vehicles Table
    QWidget* leftVehicles = new QWidget(splitter);
    leftVehicles->setMinimumWidth(350);
    QVBoxLayout* leftVehiclesLayout = new QVBoxLayout(leftVehicles);
    leftVehiclesLayout->setContentsMargins(0, 0, 0, 0);

    m_grpVehicles = new QGroupBox("2. Pack Vehicles (Check Vehicles to Mount Pantograph)", leftVehicles);
    QVBoxLayout* grpVehiclesLayout = new QVBoxLayout(m_grpVehicles);

    m_tableVehicles = new QTableWidget(m_grpVehicles);
    m_tableVehicles->setColumnCount(5);
    m_tableVehicles->setHorizontalHeaderLabels({"Panto?", "Pantograph", "Vehicle ID", "Vehicle Name", "Description"});
    m_tableVehicles->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tableVehicles->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tableVehicles->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tableVehicles->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_tableVehicles->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_tableVehicles->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableVehicles->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_tableVehicles, &QTableWidget::itemSelectionChanged, this, &MainWindow::onVehicleSelectionChanged);
    connect(m_tableVehicles, &QTableWidget::cellChanged, this, &MainWindow::onVehicleCellChanged);

    grpVehiclesLayout->addWidget(m_tableVehicles);

    QHBoxLayout* selBtnsRow = new QHBoxLayout();
    m_btnSelectAll = new QPushButton("Check All", m_grpVehicles);
    connect(m_btnSelectAll, &QPushButton::clicked, this, [this]() { onSelectAllVehicles(true); });
    m_btnDeselectAll = new QPushButton("Uncheck All", m_grpVehicles);
    connect(m_btnDeselectAll, &QPushButton::clicked, this, [this]() { onSelectAllVehicles(false); });
    selBtnsRow->addWidget(m_btnSelectAll);
    selBtnsRow->addWidget(m_btnDeselectAll);
    selBtnsRow->addStretch();
    grpVehiclesLayout->addLayout(selBtnsRow);

    m_tableVehicles->setAlternatingRowColors(true);
    m_tableVehicles->setShowGrid(false);
    m_tableVehicles->verticalHeader()->hide();
    m_tableVehicles->verticalHeader()->setDefaultSectionSize(34);
    m_tableVehicles->setMinimumHeight(120);
    leftVehiclesLayout->addWidget(m_grpVehicles, 1);
    splitter->addWidget(leftVehicles);

    // Middle: Selected Vehicle Mount Controls
    QWidget* rightMount = new QWidget(leftVehicles);
    QVBoxLayout* rightMountLayout = new QVBoxLayout(rightMount);
    rightMountLayout->setContentsMargins(0, 0, 0, 0);

    m_groupVehicleMount = new QGroupBox("3. Selected Vehicle Roof Mount Settings", rightMount);
    QVBoxLayout* mountLayout = new QVBoxLayout(m_groupVehicleMount);

    m_lblSelectedVehicleTitle = new QLabel("Please select a vehicle from the list.", m_groupVehicleMount);
    m_lblSelectedVehicleTitle->setWordWrap(true);
    m_lblSelectedVehicleTitle->setStyleSheet("font-weight: bold; font-size: 14px; color: #eea66b;");
    mountLayout->addWidget(m_lblSelectedVehicleTitle);

    QFormLayout* formMount = new QFormLayout();
    formMount->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    formMount->setSpacing(10);

    m_lblVehiclePanto = new QLabel("Assigned Pantograph:", m_groupVehicleMount);
    m_comboVehiclePanto = new QComboBox(m_groupVehicleMount);
    connect(m_comboVehiclePanto, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onVehiclePantoChanged);
    formMount->addRow(m_lblVehiclePanto, m_comboVehiclePanto);

    m_spinMountHeight = new QDoubleSpinBox(m_groupVehicleMount);
    m_spinMountHeight->setRange(-10.0, 10.0);
    m_spinMountHeight->setSingleStep(0.01);
    m_spinMountHeight->setDecimals(3);
    m_spinMountHeight->setValue(3.450);
    m_spinMountHeight->setSuffix(" m");
    connect(m_spinMountHeight, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onVehicleMountParamChanged);
    m_lblMountHeight = new QLabel("Roof Mount Height (Y):", m_groupVehicleMount);
    formMount->addRow(m_lblMountHeight, m_spinMountHeight);

    m_spinMountX = new QDoubleSpinBox(m_groupVehicleMount);
    m_spinMountX->setRange(-10.0, 10.0);
    m_spinMountX->setSingleStep(0.01);
    m_spinMountX->setDecimals(3);
    m_spinMountX->setValue(0.000);
    m_spinMountX->setSuffix(" m");
    connect(m_spinMountX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onVehicleMountParamChanged);
    m_lblMountX = new QLabel("Transverse Mount Offset (X):", m_groupVehicleMount);
    formMount->addRow(m_lblMountX, m_spinMountX);

    m_spinMountZ = new QDoubleSpinBox(m_groupVehicleMount);
    m_spinMountZ->setRange(-50.0, 50.0);
    m_spinMountZ->setSingleStep(0.05);
    m_spinMountZ->setDecimals(3);
    m_spinMountZ->setValue(5.200);
    m_spinMountZ->setSuffix(" m");
    connect(m_spinMountZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onVehicleMountParamChanged);
    m_lblMountZ = new QLabel("Longitudinal Mount Offset (Z):", m_groupVehicleMount);
    formMount->addRow(m_lblMountZ, m_spinMountZ);

    m_comboRotationY = new QComboBox(m_groupVehicleMount);
    m_comboRotationY->addItem("0.0° (Forward / Normal)");
    m_comboRotationY->addItem("180.0° (Backward / Inverted)");
    connect(m_comboRotationY, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onVehicleMountParamChanged);
    m_lblRotationY = new QLabel("Orientation / Rotation (Y):", m_groupVehicleMount);
    formMount->addRow(m_lblRotationY, m_comboRotationY);

    m_spinWireDetectZ = new QDoubleSpinBox(m_groupVehicleMount);
    m_spinWireDetectZ->setRange(-50.0, 50.0);
    m_spinWireDetectZ->setSingleStep(0.05);
    m_spinWireDetectZ->setDecimals(3);
    m_spinWireDetectZ->setValue(0.000);
    m_spinWireDetectZ->setSuffix(" m");
    connect(m_spinWireDetectZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onVehicleMountParamChanged);
    m_lblWireDetectZ = new QLabel("Wire Detection Z Offset:", m_groupVehicleMount);
    formMount->addRow(m_lblWireDetectZ, m_spinWireDetectZ);

    mountLayout->addLayout(formMount);

    m_lblPresets = new QLabel("Quick Position Presets:", m_groupVehicleMount);
    m_lblPresets->setStyleSheet("font-weight: bold; color: #abb2bf; margin-top: 10px;");
    mountLayout->addWidget(m_lblPresets);

    QVBoxLayout* presetRow = new QVBoxLayout();
    m_btnPreFront = new QPushButton("📍 Front Roof (+5.2m)", m_groupVehicleMount);
    connect(m_btnPreFront, &QPushButton::clicked, this, &MainWindow::onPresetFrontRoof);
    m_btnPreRear = new QPushButton("📍 Rear Roof (-5.2m, 180°)", m_groupVehicleMount);
    connect(m_btnPreRear, &QPushButton::clicked, this, &MainWindow::onPresetRearRoof);
    m_btnPreCenter = new QPushButton("📍 Center (0.0m)", m_groupVehicleMount);
    connect(m_btnPreCenter, &QPushButton::clicked, this, &MainWindow::onPresetCenterRoof);
    presetRow->addWidget(m_btnPreFront);
    presetRow->addWidget(m_btnPreRear);
    presetRow->addWidget(m_btnPreCenter);
    mountLayout->addLayout(presetRow);


    m_groupVehicleMount->setEnabled(false);
    rightMountLayout->addWidget(m_groupVehicleMount);
    rightMountLayout->addStretch();

    auto* mountScroll = new QScrollArea(leftVehicles);
    mountScroll->setWidgetResizable(true);
    mountScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    mountScroll->setWidget(rightMount);
    leftVehiclesLayout->addWidget(mountScroll, 2);

    // 3. Right: 3D Vehicle Preview
    QWidget* rightVpPanel = new QWidget(splitter);
    QVBoxLayout* rightVpLayout = new QVBoxLayout(rightVpPanel);
    rightVpLayout->setContentsMargins(0, 0, 0, 0);
    rightVpLayout->setSpacing(6);

    QHBoxLayout* vpHeader = new QHBoxLayout();
    m_lblTab2VpTitle = new QLabel("⚡ 3D Vehicle & Roof Mount Preview", rightVpPanel);
    m_lblTab2VpTitle->setStyleSheet("font-weight: bold; font-size: 13px; color: #eea66b;");
    vpHeader->addWidget(m_lblTab2VpTitle);
    vpHeader->addStretch();
    rightVpLayout->addLayout(vpHeader);

    m_lblVehicleVpInfo = new QLabel("Model: No train pack loaded yet.", rightVpPanel);
    m_lblVehicleVpInfo->setWordWrap(true);
    m_lblVehicleVpInfo->setStyleSheet("color: #8f96a3; font-size: 11px;");
    rightVpLayout->addWidget(m_lblVehicleVpInfo);
    m_vehiclePreviewStack = new QStackedWidget(rightVpPanel);
    auto* vehicleEmpty = new QWidget(m_vehiclePreviewStack);
    vehicleEmpty->setObjectName("canvasEmpty");
    auto* emptyLayout = new QVBoxLayout(vehicleEmpty);
    emptyLayout->setContentsMargins(16, 16, 16, 16);
    emptyLayout->setSpacing(14);
    emptyLayout->addStretch();
    emptyLayout->addWidget(new PantographEmblem(vehicleEmpty), 0, Qt::AlignHCenter);
    m_vehicleEmptyTitle = new QLabel(vehicleEmpty);
    m_vehicleEmptyTitle->setObjectName("emptyTitle");
    m_vehicleEmptyTitle->setWordWrap(true);
    m_vehicleEmptyTitle->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(m_vehicleEmptyTitle);
    m_vehicleEmptyHint = new QLabel(vehicleEmpty);
    m_vehicleEmptyHint->setObjectName("muted");
    m_vehicleEmptyHint->setWordWrap(true);
    m_vehicleEmptyHint->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(m_vehicleEmptyHint);
    m_vehicleEmptyImport = new QPushButton(vehicleEmpty);
    m_vehicleEmptyImport->setObjectName("actionButton");
    connect(m_vehicleEmptyImport, &QPushButton::clicked, this, &MainWindow::onBrowsePackZip);
    emptyLayout->addWidget(m_vehicleEmptyImport, 0, Qt::AlignHCenter);
    emptyLayout->addStretch();
    m_vehiclePreviewStack->addWidget(vehicleEmpty);
    rightVpLayout->addWidget(m_vehiclePreviewStack, 1);

    if (m_vulkanInst && m_vulkanInst->isValid()) {
        m_vehicleViewport = new VulkanViewport();
        m_vehicleViewport->setVulkanInstance(m_vulkanInst);
        m_vehicleViewportContainer = QWidget::createWindowContainer(m_vehicleViewport, rightVpPanel);
        m_vehicleViewportContainer->setMinimumSize(320, 200);
        m_vehicleViewportContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        m_vehicleViewportContainer->setFocusPolicy(Qt::TabFocus);
        m_vehiclePreviewStack->addWidget(m_vehicleViewportContainer);

        QGridLayout* vpControls = new QGridLayout();
        m_btnResetVehCam = new QPushButton("🎥 Front View (Reset)", rightVpPanel);
        connect(m_btnResetVehCam, &QPushButton::clicked, this, [this]() {
            if (m_vehicleViewport) {
                m_vehicleViewport->setCameraParams(45.0f, 20.0f, 18.0f, QVector3D(0.0f, 1.8f, 0.0f));
            }
        });
        vpControls->addWidget(m_btnResetVehCam, 0, 0);

        m_btnFrontVehCam = new QPushButton("🎥 Front Cab (45°)", rightVpPanel);
        connect(m_btnFrontVehCam, &QPushButton::clicked, this, [this]() {
            if (m_vehicleViewport) {
                m_vehicleViewport->setCameraParams(45.0f, 20.0f, 18.0f, QVector3D(0.0f, 1.8f, 0.0f));
            }
        });
        vpControls->addWidget(m_btnFrontVehCam, 0, 1);

        m_btnRearVehCam = new QPushButton("🎥 Rear Gangway (135°)", rightVpPanel);
        connect(m_btnRearVehCam, &QPushButton::clicked, this, [this]() {
            if (m_vehicleViewport) {
                m_vehicleViewport->setCameraParams(135.0f, 20.0f, 18.0f, QVector3D(0.0f, 1.8f, 0.0f));
            }
        });
        vpControls->addWidget(m_btnRearVehCam, 0, 2);

        m_chkInvertVehCam = new QCheckBox("Invert Orbit Controls", rightVpPanel);
        m_chkInvertVehCam->setChecked(true);
        connect(m_chkInvertVehCam, &QCheckBox::toggled, this, [this](bool checked) {
            if (m_vehicleViewport) m_vehicleViewport->setInvertCamera(checked);
        });
        vpControls->addWidget(m_chkInvertVehCam, 1, 0, 1, 3);

        rightVpLayout->addLayout(vpControls);

        m_lblTab2Help = new QLabel("💡 3D Controls: Left Drag: Orbit | Shift+Left / Middle Drag: Pan | Wheel: Zoom", rightVpPanel);
        m_lblTab2Help->setWordWrap(true);
        m_lblTab2Help->setStyleSheet("color: #7f848e; font-size: 11px;");
        rightVpLayout->addWidget(m_lblTab2Help);
    }

    splitter->addWidget(rightVpPanel);
    splitter->setChildrenCollapsible(false);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes({420, 700});

    tabLayout->addWidget(splitter, 1);

    QHBoxLayout* bottomNav = new QHBoxLayout();
    bottomNav->addStretch();
    m_btnNextToExport = new QPushButton("Next Step: Export & Packaging ➔", tab);
    m_btnNextToExport->setObjectName("actionButton");
    connect(m_btnNextToExport, &QPushButton::clicked, this, [this]() {
        m_tabs->setCurrentIndex(2);
    });
    bottomNav->addWidget(m_btnNextToExport);
    tabLayout->addLayout(bottomNav);

    return tab;
}

QWidget* MainWindow::createTab3Export() {
    QWidget* tab = new QWidget(this);
    QVBoxLayout* tabLayout = new QVBoxLayout(tab);
    tabLayout->setContentsMargins(0, 0, 0, 0);
    tabLayout->setSpacing(12);

    // Group 1: Output Settings
    m_grpConfig = new QGroupBox("1. Packaging Settings", tab);
    QGridLayout* gridConfig = new QGridLayout(m_grpConfig);
    gridConfig->setSpacing(10);

    m_lblOutputPath = new QLabel("Target Output ZIP Path:", m_grpConfig);
    gridConfig->addWidget(m_lblOutputPath, 0, 0);
    QHBoxLayout* outRow = new QHBoxLayout();
    m_txtOutputPath = new QLineEdit(m_grpConfig);
    m_txtOutputPath->setPlaceholderText("Target output .zip file path...");
    m_btnBrowseOut = new QPushButton("📁 Browse...", m_grpConfig);
    connect(m_btnBrowseOut, &QPushButton::clicked, this, &MainWindow::onBrowseOutputPath);
    outRow->addWidget(m_txtOutputPath);
    outRow->addWidget(m_btnBrowseOut);
    gridConfig->addLayout(outRow, 0, 1);

    tabLayout->addWidget(m_grpConfig);

    // Group 2: Script preview
    m_grpPreview = new QGroupBox("2. Generated JavaScript Code Preview (MTR / EgEhELib)", tab);
    QVBoxLayout* previewLayout = new QVBoxLayout(m_grpPreview);

    QHBoxLayout* previewTopRow = new QHBoxLayout();
    m_lblScriptPreviewHint = new QLabel("This script will be automatically embedded into assets/mtr/panto/ inside the train pack.", m_grpPreview);
    m_lblScriptPreviewHint->setWordWrap(true);
    previewTopRow->addWidget(m_lblScriptPreviewHint, 1);
    m_btnRefreshPreview = new QPushButton("🔄 Refresh Preview", m_grpPreview);
    connect(m_btnRefreshPreview, &QPushButton::clicked, this, &MainWindow::onRefreshScriptPreview);
    previewTopRow->addWidget(m_btnRefreshPreview);
    previewLayout->addLayout(previewTopRow);

    m_txtScriptPreview = new QPlainTextEdit(m_grpPreview);
    m_txtScriptPreview->setReadOnly(true);
    previewLayout->addWidget(m_txtScriptPreview);

    tabLayout->addWidget(m_grpPreview, 1);

    // Group 3: Package & Export Actions
    m_grpAction = new QGroupBox("3. Packaging Action", tab);
    QVBoxLayout* actionLayout = new QVBoxLayout(m_grpAction);

    QHBoxLayout* actBtnRow = new QHBoxLayout();
    m_btnPackage = new QPushButton("🚀 PACKAGE & EXPORT (Produce MTR-Ready ZIP)", m_grpAction);
    m_btnPackage->setObjectName("actionButton");
    m_btnPackage->setMinimumHeight(45);
    connect(m_btnPackage, &QPushButton::clicked, this, &MainWindow::onPackageAndExport);
    actBtnRow->addWidget(m_btnPackage, 2);

    m_btnOpenOutputFolder = new QPushButton("📁 Open Output Folder", m_grpAction);
    m_btnOpenOutputFolder->setEnabled(false);
    connect(m_btnOpenOutputFolder, &QPushButton::clicked, this, &MainWindow::onOpenOutputFolder);
    actBtnRow->addWidget(m_btnOpenOutputFolder, 1);
    actionLayout->addLayout(actBtnRow);

    m_lblExportStatus = new QLabel("Ready.", m_grpAction);
    m_lblExportStatus->setStyleSheet("color: #8f96a3; font-weight: bold; margin-top: 4px;");
    actionLayout->addWidget(m_lblExportStatus);

    tabLayout->addWidget(m_grpAction);

    return tab;
}

void MainWindow::onBrowseModel() {
    bool isTr = (m_currentLang == AppLanguage::Turkish);
    QString filter = isTr
        ? "3D Modeller (*.obj *.bbmodel);;Blockbench Modelleri (*.bbmodel);;OBJ 3D Model (*.obj);;Tüm Dosyalar (*.*)"
        : "3D Models (*.obj *.bbmodel);;Blockbench Models (*.bbmodel);;OBJ 3D Model (*.obj);;All Files (*.*)";
    QString path = QFileDialog::getOpenFileName(this, isTr ? "Pantograf Modeli Seç (.obj / .bbmodel)" : "Select Pantograph Model (.obj / .bbmodel)", QString(), filter);
    if (path.isEmpty()) return;

    QString finalObjPath = path;
    if (path.endsWith(".bbmodel", Qt::CaseInsensitive)) {
        char* converted = edpc_convert_or_fix_bbmodel(path.toUtf8().constData());
        if (converted) {
            finalObjPath = QString::fromUtf8(converted);
            edpc_free_string(converted);
        } else {
            QMessageBox::warning(this, isTr ? "Dönüştürme Hatası" : "Conversion Error",
                isTr ? "Blockbench (.bbmodel) modeli dönüştürülemedi. Dosya biçimini veya içeriğini kontrol edin."
                     : "Failed to convert Blockbench (.bbmodel) model. Please check the file format or contents.");
            return;
        }
    }

    m_objPath = finalObjPath;
    m_txtObjPath->setText(path);

    QFileInfo fi(finalObjPath);
    QString baseName = fi.baseName();
    QString dir = fi.absolutePath();

    // Note: Model file name and display name are strictly randomized (5-character alphanumeric)

    // 1. Detect MTL
    QString candidateMtl = dir + "/" + baseName + ".mtl";
    m_mtlPath.clear();
    m_texturePath.clear();
    QColor mtlKdColor;

    if (QFile::exists(candidateMtl)) {
        m_mtlPath = candidateMtl;
        QFile mtlFile(candidateMtl);
        if (mtlFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            while (!mtlFile.atEnd()) {
                QString line = QString::fromUtf8(mtlFile.readLine()).trimmed();
                if (line.startsWith("map_Kd ", Qt::CaseInsensitive)) {
                    QString texName = line.mid(7).trimmed();
                    QString fullTex = dir + "/" + texName;
                    if (QFile::exists(fullTex)) {
                        m_texturePath = fullTex;
                        break;
                    }
                } else if (line.startsWith("Kd ", Qt::CaseInsensitive) && !mtlKdColor.isValid()) {
                    QStringList parts = line.mid(3).trimmed().split(' ', Qt::SkipEmptyParts);
                    if (parts.size() >= 3) {
                        float r = parts[0].toFloat();
                        float g = parts[1].toFloat();
                        float b = parts[2].toFloat();
                        mtlKdColor = QColor::fromRgbF(qBound(0.0f, r, 1.0f), qBound(0.0f, g, 1.0f), qBound(0.0f, b, 1.0f));
                    }
                }
            }
            mtlFile.close();
        }
    }

    // 2. If texture not found via map_Kd, check common filenames
    if (m_texturePath.isEmpty()) {
        QStringList candidates = {
            dir + "/" + baseName + ".png",
            dir + "/panto.png",
            dir + "/pantograph.png",
            dir + "/texture.png"
        };
        for (const QString& c : candidates) {
            if (QFile::exists(c)) {
                m_texturePath = c;
                break;
            }
        }
    }

    // 3. Fallback: search for any .png in directory
    if (m_texturePath.isEmpty()) {
        QDir d(dir);
        QStringList pngs = d.entryList({"*.png"}, QDir::Files);
        if (!pngs.isEmpty()) {
            m_texturePath = d.filePath(pngs.first());
        }
    }

    if (m_currentPantoIndex >= 0 && m_currentPantoIndex < m_pantographs.size()) {
        auto& curPanto = m_pantographs[m_currentPantoIndex];
        curPanto.objPath = m_objPath;
        curPanto.mtlPath = m_mtlPath;
        curPanto.texturePath = m_texturePath;

        if (!m_texturePath.isEmpty()) {
            curPanto.textureImage.load(m_texturePath);
        } else if (mtlKdColor.isValid()) {
            curPanto.textureImage = QImage(16, 16, QImage::Format_RGBA8888);
            curPanto.textureImage.fill(mtlKdColor);
        } else {
            curPanto.textureImage = QImage();
        }

        if (curPanto.modelHandle) {
            edpc_model_free(curPanto.modelHandle);
            curPanto.modelHandle = nullptr;
        }
        curPanto.modelHandle = edpc_model_load_file(m_objPath.toUtf8().constData());
    }

    if (m_viewport) {
        m_viewport->loadModel(m_objPath);
        if (!m_texturePath.isEmpty()) {
            m_viewport->loadTexture(m_texturePath);
        } else if (mtlKdColor.isValid()) {
            QImage kdImg(16, 16, QImage::Format_RGBA8888);
            kdImg.fill(mtlKdColor);
            m_viewport->setTextureImage(kdImg);
        }

        EdpcPivots pivots;
        if (m_viewport->getPivots(&pivots)) {
            m_nominalTipHeight = 1.4480f;
            m_shoeOffset = pivots.shoe_offset > 0.001f ? pivots.shoe_offset : 0.1048f;

            populatePivotSpinboxes(pivots);

            if (m_currentPantoIndex >= 0 && m_currentPantoIndex < m_pantographs.size()) {
                auto& curPanto = m_pantographs[m_currentPantoIndex];
                curPanto.nominalTipHeight = m_nominalTipHeight;
                curPanto.shoeOffset = m_shoeOffset;
                curPanto.customPivotsSet = false;
                curPanto.customPivots = pivots;
            }

            m_spinWireRef->blockSignals(true);
            m_spinWireRef->setValue(1.4480);
            m_spinWireRef->blockSignals(false);

            m_spinTestHeight->blockSignals(true);
            m_spinTestHeight->setValue(1.4480);
            m_spinTestHeight->blockSignals(false);

            m_sliderTestHeight->blockSignals(true);
            m_sliderTestHeight->setValue(1448);
            m_sliderTestHeight->blockSignals(false);

            updateViewportKinematics();
            if (m_tabs && m_tabs->currentIndex() == 1) {
                updateVehicleViewportMesh();
            }

            QTimer::singleShot(0, this, [this]() {
                if (m_viewport) {
                    m_viewport->reapplyTextures();
                    updateViewportKinematics();
                    m_viewport->requestUpdate();
                }
                if (m_viewportContainer) {
                    m_viewportContainer->update();
                }
            });
        }
    }
}

void MainWindow::updateViewportKinematics() {
    if (!m_viewport) return;

    double wireRef = m_spinWireRef ? m_spinWireRef->value() : 1.4480;
    double simWireHeight = m_spinTestHeight ? m_spinTestHeight->value() : 1.4480;

    // 1. Position the yellow catenary wire at the simulated wire height + shoe offset
    // When simWireHeight == 1.4480 and wireRef == 1.4480,
    // the bottom of the wire rests flush on the top of the contact shoe with exactly 0.0000m offset!
    float wireLevel = static_cast<float>(simWireHeight + m_shoeOffset);
    m_viewport->updateWireHeight(wireLevel);

    // 2. Pantograph reference point is a fine-tuning value to adjust whether it touches the catenary wire:
    //    signedDistance = simWireHeight - 1.4480
    //    targetPantoHeight = wireRef + signedDistance
    //    When simWireHeight == 1.4480: targetPantoHeight = wireRef
    //    When wireRef is fine-tuned, the contact shoe moves up/down relative to the catenary wire!
    //    As simWireHeight moves, the pantograph follows while preserving the fine-tuned contact offset.
    double signedDistance = simWireHeight - 1.4480;
    double targetPantoHeight = wireRef + signedDistance;

    m_viewport->updateKinematics(static_cast<float>(targetPantoHeight));

    // Also update Tab 2 vehicle preview if active
    if (m_vehicleViewport && m_currentVehicleRow >= 0 && m_currentVehicleRow < m_vehicles.size()) {
        updateVehicleViewportMesh();
    }
}

void MainWindow::onKinematicsSliderChanged(int value) {
    double h = value / 1000.0;
    m_spinTestHeight->blockSignals(true);
    m_spinTestHeight->setValue(h);
    m_spinTestHeight->blockSignals(false);

    updateViewportKinematics();
}

void MainWindow::onKinematicsSpinChanged(double value) {
    int sliderVal = static_cast<int>(std::round(value * 1000.0));
    m_sliderTestHeight->blockSignals(true);
    m_sliderTestHeight->setValue(sliderVal);
    m_sliderTestHeight->blockSignals(false);

    updateViewportKinematics();
}

void MainWindow::onWireRefChanged(double value) {
    Q_UNUSED(value);
    updateViewportKinematics();
    onRefreshScriptPreview();
}

void MainWindow::onTestWireRef() {
    // Reset test height to nominal so user sees the reference pose
    m_spinTestHeight->blockSignals(true);
    m_spinTestHeight->setValue(1.4480);
    m_spinTestHeight->blockSignals(false);

    m_sliderTestHeight->blockSignals(true);
    m_sliderTestHeight->setValue(1448);
    m_sliderTestHeight->blockSignals(false);

    updateViewportKinematics();
}

void MainWindow::onTestNoWirePark() {
    double val = m_spinNoWirePark->value();
    if (m_viewport) {
        m_viewport->updateKinematics(static_cast<float>(val));
    }
}

void MainWindow::onTestDepotPark() {
    double val = m_spinDepotPark->value();
    if (m_viewport) {
        m_viewport->updateKinematics(static_cast<float>(val));
    }
}

void MainWindow::onWireHeightChanged(double value) {
    if (m_viewport) {
        m_viewport->updateWireHeight(static_cast<float>(value));
    }
}

void MainWindow::onShowWireToggled(bool checked) {
    if (m_viewport) {
        m_viewport->setWireVisible(checked);
    }
}

void MainWindow::onDepotParkToggled(bool checked) {
    m_spinDepotPark->setEnabled(checked);
    onRefreshScriptPreview();
}

void MainWindow::onKinematicsUpdated(float height, float angle1, float angle2, float angle4, bool reachable) {
    Q_UNUSED(height);
    bool isTr = (m_currentLang == AppLanguage::Turkish);
    m_lblArmAngles->setText(isTr
        ? QString("Alt Kol (p1): %1° | Üst Kol (p2): %2° | Çubuk (p4): %3°")
            .arg(angle1, 0, 'f', 1)
            .arg(angle2, 0, 'f', 1)
            .arg(angle4, 0, 'f', 1)
        : QString("Lower Arm (p1): %1° | Upper Arm (p2): %2° | Pushrod (p4): %3°")
            .arg(angle1, 0, 'f', 1)
            .arg(angle2, 0, 'f', 1)
            .arg(angle4, 0, 'f', 1));

    if (reachable) {
        m_lblReachability->setText(isTr ? "Durum: ● Geçerli Kinematik Çözüm" : "Status: ● Valid Kinematic Solution");
        m_lblReachability->setStyleSheet("font-weight: bold; color: #98c379;");
    } else {
        m_lblReachability->setText(isTr ? "Durum: ✖ Geometrik Sınır Dışı (Ulaşılamaz)" : "Status: ✖ Out of Geometric Limits (Unreachable)");
        m_lblReachability->setStyleSheet("font-weight: bold; color: #e06c75;");
    }
}

void MainWindow::populatePivotSpinboxes(const EdpcPivots& p) {
    m_syncingPivots = true;
    if (m_spinP1Y) m_spinP1Y->setValue(p.base.y);
    if (m_spinP1Z) m_spinP1Z->setValue(p.base.z);
    if (m_spinP2Y) m_spinP2Y->setValue(p.elbow.y);
    if (m_spinP2Z) m_spinP2Z->setValue(p.elbow.z);
    if (m_spinP3Y) m_spinP3Y->setValue(p.tip.y);
    if (m_spinP3Z) m_spinP3Z->setValue(p.tip.z);
    if (m_chkP4Enabled) m_chkP4Enabled->setChecked(p.has_p4);
    if (m_spinP4BaseY) {
        m_spinP4BaseY->setValue(p.p4_base.y);
        m_spinP4BaseY->setEnabled(p.has_p4);
    }
    if (m_spinP4BaseZ) {
        m_spinP4BaseZ->setValue(p.p4_base.z);
        m_spinP4BaseZ->setEnabled(p.has_p4);
    }
    if (m_spinP4ElbowY) {
        m_spinP4ElbowY->setValue(p.p4_elbow.y);
        m_spinP4ElbowY->setEnabled(p.has_p4);
    }
    if (m_spinP4ElbowZ) {
        m_spinP4ElbowZ->setValue(p.p4_elbow.z);
        m_spinP4ElbowZ->setEnabled(p.has_p4);
    }
    if (m_grpPivots) m_grpPivots->setEnabled(true);
    m_syncingPivots = false;
}

void MainWindow::onPivotSpinboxChanged() {
    if (m_syncingPivots || !m_viewport || !m_viewport->hasModel()) return;

    EdpcPivots p;
    std::memset(&p, 0, sizeof(p));
    p.base.x = 0.0f;
    p.base.y = static_cast<float>(m_spinP1Y ? m_spinP1Y->value() : 0.0);
    p.base.z = static_cast<float>(m_spinP1Z ? m_spinP1Z->value() : 0.0);

    p.elbow.x = 0.0f;
    p.elbow.y = static_cast<float>(m_spinP2Y ? m_spinP2Y->value() : 0.0);
    p.elbow.z = static_cast<float>(m_spinP2Z ? m_spinP2Z->value() : 0.0);

    p.tip.x = 0.0f;
    p.tip.y = static_cast<float>(m_spinP3Y ? m_spinP3Y->value() : 0.0);
    p.tip.z = static_cast<float>(m_spinP3Z ? m_spinP3Z->value() : 0.0);

    p.has_p4 = m_chkP4Enabled ? m_chkP4Enabled->isChecked() : false;
    p.p4_base.x = 0.0f;
    p.p4_base.y = static_cast<float>(m_spinP4BaseY ? m_spinP4BaseY->value() : 0.0);
    p.p4_base.z = static_cast<float>(m_spinP4BaseZ ? m_spinP4BaseZ->value() : 0.0);

    p.p4_elbow.x = 0.0f;
    p.p4_elbow.y = static_cast<float>(m_spinP4ElbowY ? m_spinP4ElbowY->value() : 0.0);
    p.p4_elbow.z = static_cast<float>(m_spinP4ElbowZ ? m_spinP4ElbowZ->value() : 0.0);

    EdpcPivots curPivots;
    if (m_viewport->getPivots(&curPivots)) {
        p.shoe_offset = curPivots.shoe_offset;
    } else {
        p.shoe_offset = 0.1048f;
    }

    m_viewport->setPivots(p);
    updateViewportKinematics();

    if (m_currentPantoIndex >= 0 && m_currentPantoIndex < m_pantographs.size()) {
        auto& curPanto = m_pantographs[m_currentPantoIndex];
        curPanto.customPivotsSet = true;
        curPanto.customPivots = p;
        if (curPanto.modelHandle) {
            edpc_model_set_pivots(curPanto.modelHandle, &p);
        }
        if (m_tabs && m_tabs->currentIndex() == 1) {
            updateVehicleViewportMesh();
        }
    }

    QString pivText = QString("Base: (0, %1, %2)\nElbow: (0, %3, %4)\nTip: (0, %5, %6)\nLink 4: %7")
        .arg(p.base.y, 0, 'f', 3).arg(p.base.z, 0, 'f', 3)
        .arg(p.elbow.y, 0, 'f', 3).arg(p.elbow.z, 0, 'f', 3)
        .arg(p.tip.y, 0, 'f', 3).arg(p.tip.z, 0, 'f', 3)
        .arg(p.has_p4 ? (m_currentLang == AppLanguage::Turkish ? "Var (4-Bar Bağlantılı)" : "Present (4-Bar Link)")
                      : (m_currentLang == AppLanguage::Turkish ? "Yok (Doğrudan IK)" : "None (Direct IK)"));
    if (m_lblPivotsInfo) {
        m_lblPivotsInfo->setText(pivText);
    }
}

void MainWindow::onResetPivots() {
    if (m_objPath.isEmpty() || !QFile::exists(m_objPath)) return;

    if (m_viewport) {
        m_viewport->loadModel(m_objPath);
        if (!m_texturePath.isEmpty() && QFile::exists(m_texturePath)) {
            m_viewport->loadTexture(m_texturePath);
        } else {
            m_viewport->reapplyTextures();
        }

        EdpcPivots p;
        if (m_viewport->getPivots(&p)) {
            populatePivotSpinboxes(p);
            if (m_currentPantoIndex >= 0 && m_currentPantoIndex < m_pantographs.size()) {
                auto& curPanto = m_pantographs[m_currentPantoIndex];
                curPanto.customPivotsSet = false;
                curPanto.customPivots = p;
                if (curPanto.modelHandle) {
                    edpc_model_set_pivots(curPanto.modelHandle, &p);
                }
                if (m_tabs && m_tabs->currentIndex() == 1) {
                    updateVehicleViewportMesh();
                }
            }
            updateViewportKinematics();
        }
    }
}

void MainWindow::onBrowsePackZip() {
    bool isTr = (m_currentLang == AppLanguage::Turkish);
    QString filter = isTr ? "Tren Paketi ZIP (*.zip);;Tüm Dosyalar (*.*)" : "Train Pack ZIP (*.zip);;All Files (*.*)";
    QString path = QFileDialog::getOpenFileName(this, isTr ? "Tren Paketi .zip Seç" : "Select Train Pack .zip", QString(), filter);
    if (!path.isEmpty()) {
        loadPackPath(path);
    }
}

void MainWindow::onBrowsePackFolder() {
    bool isTr = (m_currentLang == AppLanguage::Turkish);
    QString path = QFileDialog::getExistingDirectory(this, isTr ? "Çıkarılmış Tren Paketi Klasörü Seç" : "Select Extracted Train Pack Folder");
    if (!path.isEmpty()) {
        loadPackPath(path);
    }
}

void MainWindow::loadPackPath(const QString& path) {
    m_packInputPath = path;
    m_txtPackPath->setText(path);

    char* outJson = nullptr;
    bool ok = edpc_inspect_pack_json(path.toUtf8().constData(), &outJson);
    if (!ok || !outJson) {
        bool isTr = (m_currentLang == AppLanguage::Turkish);
        QString err = isTr ? "Paket taranamadı veya mtr_custom_resources.json bulunamadı." : "Could not scan package or mtr_custom_resources.json not found.";
        if (outJson) {
            QJsonDocument errDoc = QJsonDocument::fromJson(outJson);
            if (errDoc.isObject() && errDoc.object().contains("error")) {
                err = errDoc.object()["error"].toString();
            }
            edpc_free_string(outJson);
        }
        m_lblPackStatus->setText(isTr ? QString("Hata: %1").arg(err) : QString("Error: %1").arg(err));
        m_lblPackStatus->setStyleSheet("color: #e06c75; font-weight: bold;");
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(outJson);
    edpc_free_string(outJson);

    if (!doc.isObject()) {
        bool isTr = (m_currentLang == AppLanguage::Turkish);
        m_lblPackStatus->setText(isTr ? "Geçersiz JSON yanıtı." : "Invalid JSON response.");
        m_lblPackStatus->setStyleSheet("color: #e06c75;");
        return;
    }

    QJsonObject root = doc.object();
    m_customResourcesRelPath = root["custom_resources_path"].toString();
    m_packIsZip = root["is_zip"].toBool();

    m_vehicles.clear();
    m_loadedVehicleCacheKey.clear();
    m_tableVehicles->setRowCount(0);

    QJsonArray vehiclesArr = root["vehicles"].toArray();
    bool isTr = (m_currentLang == AppLanguage::Turkish);
    for (int i = 0; i < vehiclesArr.size(); ++i) {
        QJsonObject vo = vehiclesArr[i].toObject();
        VehicleMountItem item;
        item.id = vo["id"].toString();
        item.name = vo["name"].toString();
        item.description = vo["description"].toString();
        item.hasPantograph = vo["has_pantograph"].toBool();

        int pantoIdx = 0;
        if (vo.contains("pantograph_id") && !vo["pantograph_id"].isNull()) {
            QString pid = vo["pantograph_id"].toString();
            if (pid.startsWith("panto_")) {
                pantoIdx = pid.mid(6).toInt();
            }
        }
        item.pantographIndex = pantoIdx;

        item.mountHeight = vo["mount_height"].toDouble(3.450);
        item.mountX = vo["mount_x"].toDouble(0.000);
        item.mountZ = vo["mount_z"].toDouble(5.200);
        item.rotationYDeg = vo["rotation_y_deg"].toDouble(0.0);
        item.wireDetectZ = vo["wire_detect_z"].toDouble(0.000);
        item.modelResource = vo["model_resource"].toString();
        item.textureResource = vo["texture_resource"].toString();
        item.modelPropertiesResource = vo["model_properties_resource"].toString();
        item.positionDefinitionsResource = vo["position_definitions_resource"].toString();
        item.bogie1ModelResource = vo["bogie1_model_resource"].toString();
        item.bogie1PropertiesResource = vo["bogie1_properties_resource"].toString();
        item.bogie1DefinitionsResource = vo["bogie1_definitions_resource"].toString();
        item.bogie1Position = vo["bogie1_position"].toDouble(0.0);
        item.bogie2ModelResource = vo["bogie2_model_resource"].toString();
        item.bogie2PropertiesResource = vo["bogie2_properties_resource"].toString();
        item.bogie2DefinitionsResource = vo["bogie2_definitions_resource"].toString();
        item.bogie2Position = vo["bogie2_position"].toDouble(0.0);
        item.hasGangway1 = vo["has_gangway1"].toBool(false);
        item.hasGangway2 = vo["has_gangway2"].toBool(false);

        m_vehicles.append(item);

        int row = m_tableVehicles->rowCount();
        m_tableVehicles->insertRow(row);

        QTableWidgetItem* checkItem = new QTableWidgetItem();
        checkItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        checkItem->setCheckState(item.hasPantograph ? Qt::Checked : Qt::Unchecked);
        m_tableVehicles->setItem(row, 0, checkItem);

        QString pantoDisp = (item.hasPantograph && item.pantographIndex >= 0 && item.pantographIndex < m_pantographs.size())
            ? m_pantographs[item.pantographIndex].displayName(isTr) : "—";
        QTableWidgetItem* pantoItem = new QTableWidgetItem(pantoDisp);
        pantoItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_tableVehicles->setItem(row, 1, pantoItem);

        QTableWidgetItem* idItem = new QTableWidgetItem(item.id);
        idItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_tableVehicles->setItem(row, 2, idItem);

        QTableWidgetItem* nameItem = new QTableWidgetItem(item.name);
        nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_tableVehicles->setItem(row, 3, nameItem);

        QTableWidgetItem* descItem = new QTableWidgetItem(item.description);
        descItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_tableVehicles->setItem(row, 4, descItem);
    }

    m_lblPackStatus->setText(isTr
        ? QString("✓ %1 içinde %2 araç tespit edildi.").arg(m_customResourcesRelPath).arg(m_vehicles.size())
        : QString("✓ %2 vehicles detected in %1.").arg(m_vehicles.size()).arg(m_customResourcesRelPath));
    m_lblPackStatus->setStyleSheet("color: #98c379; font-weight: bold;");
    updateWorkspace();

    // Suggest output zip path
    QFileInfo fi(path);
    if (m_packIsZip) {
        QString outName = fi.completeBaseName() + "_with_panto.zip";
        m_txtOutputPath->setText(fi.absoluteDir().filePath(outName));
    } else {
        QString outName = fi.fileName() + "_with_panto.zip";
        m_txtOutputPath->setText(fi.dir().filePath(outName));
    }

    if (m_vehicles.size() > 0) {
        int defaultRow = 0;
        for (int i = 0; i < m_vehicles.size(); ++i) {
            if (m_vehicles[i].hasPantograph) {
                defaultRow = i;
                break;
            }
        }
        m_tableVehicles->selectRow(defaultRow);
    }
}

void MainWindow::onVehicleSelectionChanged() {
    int row = m_tableVehicles->currentRow();
    if (row < 0 || row >= m_vehicles.size()) {
        m_groupVehicleMount->setEnabled(false);
        m_lblSelectedVehicleTitle->setText(m_currentLang == AppLanguage::Turkish ? "Lütfen soldan bir araç seçin." : "Please select a vehicle from the list.");
        m_currentVehicleRow = -1;
        if (m_vehicleViewport) {
            m_vehicleViewport->updateMeshData({});
        }
        return;
    }

    m_currentVehicleRow = row;
    updateVehicleMountUi(row);

    const auto& v = m_vehicles[row];
    loadVehicleModelForPreview(v);
    updateVehicleViewportMesh();
}

void MainWindow::updateVehicleMountUi(int row) {
    if (row < 0 || row >= m_vehicles.size()) return;

    const VehicleMountItem& v = m_vehicles[row];
    m_groupVehicleMount->setEnabled(true);
    bool isTr = (m_currentLang == AppLanguage::Turkish);
    m_lblSelectedVehicleTitle->setText(isTr ? QString("Seçili Araç: %1 (%2)").arg(v.name).arg(v.id)
                                           : QString("Selected Vehicle: %1 (%2)").arg(v.name).arg(v.id));

    if (m_comboVehiclePanto) {
        m_comboVehiclePanto->blockSignals(true);
        int curVehPanto = v.pantographIndex;
        if (curVehPanto >= 0 && curVehPanto < m_comboVehiclePanto->count()) {
            m_comboVehiclePanto->setCurrentIndex(curVehPanto);
        } else {
            m_comboVehiclePanto->setCurrentIndex(0);
        }
        m_comboVehiclePanto->blockSignals(false);
    }

    m_spinMountHeight->blockSignals(true);
    m_spinMountX->blockSignals(true);
    m_spinMountZ->blockSignals(true);
    m_comboRotationY->blockSignals(true);
    m_spinWireDetectZ->blockSignals(true);

    m_spinMountHeight->setValue(v.mountHeight);
    m_spinMountX->setValue(v.mountX);
    m_spinMountZ->setValue(v.mountZ);
    m_comboRotationY->setCurrentIndex(std::abs(v.rotationYDeg - 180.0) < 1.0 ? 1 : 0);
    m_spinWireDetectZ->setValue(v.wireDetectZ);

    m_spinMountHeight->blockSignals(false);
    m_spinMountX->blockSignals(false);
    m_spinMountZ->blockSignals(false);
    m_comboRotationY->blockSignals(false);
}

void MainWindow::saveCurrentVehicleMountUi() {
    if (m_currentVehicleRow < 0 || m_currentVehicleRow >= m_vehicles.size()) return;

    VehicleMountItem& v = m_vehicles[m_currentVehicleRow];
    if (m_comboVehiclePanto) {
        v.pantographIndex = m_comboVehiclePanto->currentIndex();
    }
    v.mountHeight = m_spinMountHeight->value();
    v.mountX = m_spinMountX->value();
    v.mountZ = m_spinMountZ->value();
    v.rotationYDeg = m_comboRotationY->currentIndex() == 1 ? 180.0 : 0.0;
    v.wireDetectZ = m_spinWireDetectZ->value();
}

void MainWindow::onVehicleCellChanged(int row, int column) {
    if (column == 0 && row >= 0 && row < m_vehicles.size()) {
        QTableWidgetItem* it = m_tableVehicles->item(row, 0);
        if (it) {
            bool checked = (it->checkState() == Qt::Checked);
            m_vehicles[row].hasPantograph = checked;

            bool isTr = (m_currentLang == AppLanguage::Turkish);
            if (m_tableVehicles->item(row, 1)) {
                int pIdx = m_vehicles[row].pantographIndex;
                QString text = (checked && pIdx >= 0 && pIdx < m_pantographs.size())
                    ? m_pantographs[pIdx].displayName(isTr)
                    : "—";
                m_tableVehicles->item(row, 1)->setText(text);
            }

            updateVehicleViewportMesh();
            onRefreshScriptPreview();
        }
    }
}

void MainWindow::onVehiclePantoChanged(int index) {
    if (m_currentVehicleRow < 0 || m_currentVehicleRow >= m_vehicles.size()) return;
    if (index < 0 || index >= m_pantographs.size()) return;

    m_vehicles[m_currentVehicleRow].pantographIndex = index;

    bool isTr = (m_currentLang == AppLanguage::Turkish);
    if (m_tableVehicles->item(m_currentVehicleRow, 1)) {
        m_tableVehicles->item(m_currentVehicleRow, 1)->setText(
            m_vehicles[m_currentVehicleRow].hasPantograph ? m_pantographs[index].displayName(isTr) : "—"
        );
    }

    updateVehicleViewportMesh();
    onRefreshScriptPreview();
}

void MainWindow::onVehicleMountParamChanged() {
    saveCurrentVehicleMountUi();
    updateVehicleViewportMesh();
    onRefreshScriptPreview();
}

void MainWindow::onPresetFrontRoof() {
    m_spinMountZ->setValue(5.200);
    m_spinWireDetectZ->setValue(0.000);
    m_comboRotationY->setCurrentIndex(0);
    saveCurrentVehicleMountUi();
    updateVehicleViewportMesh();
    onRefreshScriptPreview();
}

void MainWindow::onPresetRearRoof() {
    m_spinMountZ->setValue(-5.200);
    m_spinWireDetectZ->setValue(0.000);
    m_comboRotationY->setCurrentIndex(1);
    saveCurrentVehicleMountUi();
    updateVehicleViewportMesh();
    onRefreshScriptPreview();
}

void MainWindow::onPresetCenterRoof() {
    m_spinMountZ->setValue(0.000);
    m_spinWireDetectZ->setValue(0.000);
    m_comboRotationY->setCurrentIndex(0);
    saveCurrentVehicleMountUi();
    updateVehicleViewportMesh();
    onRefreshScriptPreview();
}

void MainWindow::onSelectAllVehicles(bool select) {
    bool isTr = (m_currentLang == AppLanguage::Turkish);
    m_tableVehicles->blockSignals(true);
    for (int r = 0; r < m_tableVehicles->rowCount(); ++r) {
        QTableWidgetItem* it = m_tableVehicles->item(r, 0);
        if (it) {
            it->setCheckState(select ? Qt::Checked : Qt::Unchecked);
            if (r < m_vehicles.size()) {
                m_vehicles[r].hasPantograph = select;
                if (m_tableVehicles->item(r, 1)) {
                    int pIdx = m_vehicles[r].pantographIndex;
                    QString text = (select && pIdx >= 0 && pIdx < m_pantographs.size())
                        ? m_pantographs[pIdx].displayName(isTr)
                        : "—";
                    m_tableVehicles->item(r, 1)->setText(text);
                }
            }
        }
    }
    m_tableVehicles->blockSignals(false);
    updateVehicleViewportMesh();
    onRefreshScriptPreview();
}

QString MainWindow::generateScriptId() const {
    for (int attempts = 0; attempts < 100; ++attempts) {
        char* randId = nullptr;
        QString res;
        if (edpc_generate_random_script_id(&randId)) {
            res = QString::fromUtf8(randId);
            edpc_free_string(randId);
        } else {
            res = QString("p%1").arg(QRandomGenerator::global()->bounded(1000000, 9999999));
        }
        bool collision = false;
        for (const auto& p : m_pantographs) {
            if (p.randomScriptId == res) {
                collision = true;
                break;
            }
        }
        if (!collision) return res;
    }
    return QString("p%1").arg(QRandomGenerator::global()->bounded(1000000, 9999999));
}

QString MainWindow::generateDisplayName() const {
    for (int attempts = 0; attempts < 100; ++attempts) {
        char* randName = nullptr;
        QString res;
        if (edpc_generate_random_display_name(&randName)) {
            res = QString::fromUtf8(randName);
            edpc_free_string(randName);
        } else {
            res = QString("p%1").arg(QRandomGenerator::global()->bounded(1000, 9999));
        }
        bool collision = false;
        for (const auto& p : m_pantographs) {
            if (p.randomModelName == res) {
                collision = true;
                break;
            }
        }
        if (!collision) return res;
    }
    return QString("p%1").arg(QRandomGenerator::global()->bounded(1000, 9999));
}

PantographProfile MainWindow::createNewPantographProfile(int index) {
    PantographProfile p;
    p.index = index;
    p.id = QString("panto_%1").arg(index);
    p.name = QString("Pantograph %1").arg(index + 1);
    p.randomScriptId = generateScriptId();
    p.randomModelName = generateDisplayName();
    p.wireRef = 1.4480;
    p.noWirePark = 1.3300;
    p.enableDepotPark = false;
    p.depotPark = 0.3800;
    p.upperThresh = 3.0;
    p.lowerThresh = -3.0;
    p.nominalTipHeight = 1.4480f;
    p.shoeOffset = 0.1048f;
    p.modelHandle = nullptr;
    return p;
}

void MainWindow::syncCurrentPantoUiToProfile() {
    if (m_currentPantoIndex < 0 || m_currentPantoIndex >= m_pantographs.size()) return;
    auto& p = m_pantographs[m_currentPantoIndex];
    if (m_spinWireRef) p.wireRef = m_spinWireRef->value();
    if (m_spinNoWirePark) p.noWirePark = m_spinNoWirePark->value();
    if (m_chkDepotPark) p.enableDepotPark = m_chkDepotPark->isChecked();
    if (m_spinDepotPark) p.depotPark = m_spinDepotPark->value();
    if (m_spinUpperThresh) p.upperThresh = m_spinUpperThresh->value();
    if (m_spinLowerThresh) p.lowerThresh = m_spinLowerThresh->value();

    if (m_viewport && m_viewport->hasModel()) {
        EdpcPivots piv;
        if (m_viewport->getPivots(&piv)) {
            p.customPivots = piv;
        }
    }
}

void MainWindow::loadPantoProfileToUi(int index) {
    if (index < 0 || index >= m_pantographs.size()) return;
    m_currentPantoIndex = index;
    const auto& p = m_pantographs[index];

    m_objPath = p.objPath;
    m_mtlPath = p.mtlPath;
    m_texturePath = p.texturePath;
    m_randomId = p.randomScriptId;
    m_randomDisplayName = p.randomModelName;

    if (m_txtObjPath) m_txtObjPath->setText(p.objPath);

    if (m_spinWireRef) {
        m_spinWireRef->blockSignals(true);
        m_spinWireRef->setValue(p.wireRef);
        m_spinWireRef->blockSignals(false);
    }
    if (m_spinNoWirePark) {
        m_spinNoWirePark->blockSignals(true);
        m_spinNoWirePark->setValue(p.noWirePark);
        m_spinNoWirePark->blockSignals(false);
    }
    if (m_chkDepotPark) {
        m_chkDepotPark->blockSignals(true);
        m_chkDepotPark->setChecked(p.enableDepotPark);
        m_chkDepotPark->blockSignals(false);
    }
    if (m_spinDepotPark) {
        m_spinDepotPark->blockSignals(true);
        m_spinDepotPark->setEnabled(p.enableDepotPark);
        m_spinDepotPark->setValue(p.depotPark);
        m_spinDepotPark->blockSignals(false);
    }
    if (m_spinUpperThresh) {
        m_spinUpperThresh->blockSignals(true);
        m_spinUpperThresh->setValue(p.upperThresh);
        m_spinUpperThresh->blockSignals(false);
    }
    if (m_spinLowerThresh) {
        m_spinLowerThresh->blockSignals(true);
        m_spinLowerThresh->setValue(p.lowerThresh);
        m_spinLowerThresh->blockSignals(false);
    }
    if (m_spinTestHeight) {
        m_spinTestHeight->blockSignals(true);
        m_spinTestHeight->setValue(p.wireRef);
        m_spinTestHeight->blockSignals(false);
    }
    if (m_sliderTestHeight) {
        m_sliderTestHeight->blockSignals(true);
        m_sliderTestHeight->setValue(static_cast<int>(std::round(p.wireRef * 1000.0)));
        m_sliderTestHeight->blockSignals(false);
    }

    m_nominalTipHeight = p.nominalTipHeight;
    m_shoeOffset = p.shoeOffset;

    bool isTr = (m_currentLang == AppLanguage::Turkish);
    if (m_lblProfileInfo) {
        m_lblProfileInfo->setText(QString("%1_pantograph.js\n%2.obj").arg(p.randomScriptId, p.randomModelName));
    }

    if (m_viewport) {
        if (!p.objPath.isEmpty() && QFile::exists(p.objPath)) {
            m_viewport->loadModel(p.objPath);
            if (!p.texturePath.isEmpty() && QFile::exists(p.texturePath)) {
                m_viewport->loadTexture(p.texturePath);
            }
            if (p.customPivotsSet) {
                m_viewport->setPivots(p.customPivots);
                populatePivotSpinboxes(p.customPivots);
                if (p.modelHandle) {
                    edpc_model_set_pivots(p.modelHandle, &p.customPivots);
                }
            } else {
                EdpcPivots piv;
                if (m_viewport->getPivots(&piv)) {
                    populatePivotSpinboxes(piv);
                    if (p.modelHandle) {
                        edpc_model_set_pivots(p.modelHandle, &piv);
                    }
                }
            }
        } else {
            m_viewport->loadModel("");
            if (m_grpPivots) m_grpPivots->setEnabled(false);
            if (m_lblModelInfo) {
                m_lblModelInfo->setText(isTr ? "Henüz model yüklenmedi. (Gereken parçalar: p_base, p1, p2, p3)"
                                             : "No model loaded yet. (Required parts: p_base, p1, p2, p3)");
                m_lblModelInfo->setStyleSheet("color: #8f96a3; ");
            }
            if (m_lblPivotsInfo) {
                m_lblPivotsInfo->setText(isTr ? "Pivots: [Belirlenmedi]" : "Pivots: [Not loaded]");
            }
        }
    }

    updateViewportKinematics();
    onRefreshScriptPreview();
    updateWorkspace();
}

void MainWindow::updatePantographCombos() {
    bool isTr = (m_currentLang == AppLanguage::Turkish);

    if (m_comboPantographs) {
        m_comboPantographs->blockSignals(true);
        int oldIdx = m_currentPantoIndex;
        m_comboPantographs->clear();
        for (int i = 0; i < m_pantographs.size(); ++i) {
            m_comboPantographs->addItem(m_pantographs[i].displayName(isTr));
        }
        if (oldIdx >= 0 && oldIdx < m_comboPantographs->count()) {
            m_comboPantographs->setCurrentIndex(oldIdx);
        }
        m_comboPantographs->blockSignals(false);
    }

    if (m_comboVehiclePanto) {
        m_comboVehiclePanto->blockSignals(true);
        m_comboVehiclePanto->clear();
        for (int i = 0; i < m_pantographs.size(); ++i) {
            m_comboVehiclePanto->addItem(m_pantographs[i].displayName(isTr));
        }
        int curVehPanto = (m_currentVehicleRow >= 0 && m_currentVehicleRow < m_vehicles.size())
            ? m_vehicles[m_currentVehicleRow].pantographIndex : 0;
        if (curVehPanto >= 0 && curVehPanto < m_comboVehiclePanto->count()) {
            m_comboVehiclePanto->setCurrentIndex(curVehPanto);
        }
        m_comboVehiclePanto->blockSignals(false);
    }

    if (m_btnRemovePanto) {
        m_btnRemovePanto->setEnabled(m_pantographs.size() > 1);
    }
}

void MainWindow::onAddPantograph() {
    syncCurrentPantoUiToProfile();

    int newIdx = m_pantographs.size();
    PantographProfile newProfile = createNewPantographProfile(newIdx);
    m_pantographs.append(newProfile);

    updatePantographCombos();
    m_comboPantographs->setCurrentIndex(newIdx);
    loadPantoProfileToUi(newIdx);
}

void MainWindow::onRemovePantograph() {
    if (m_pantographs.size() <= 1) {
        bool isTr = (m_currentLang == AppLanguage::Turkish);
        QMessageBox::information(this, isTr ? "Silinemez" : "Cannot Delete",
                                 isTr ? "En az bir pantograf profili bulunmalıdır."
                                      : "At least one pantograph profile must exist.");
        return;
    }

    bool isTr = (m_currentLang == AppLanguage::Turkish);
    auto reply = QMessageBox::question(this,
        isTr ? "Pantograf Profilini Sil" : "Delete Pantograph Profile",
        isTr ? QString("'%1' profilini silmek istediğinizden emin misiniz?").arg(m_pantographs[m_currentPantoIndex].name)
             : QString("Are you sure you want to delete profile '%1'?").arg(m_pantographs[m_currentPantoIndex].name),
        QMessageBox::Yes | QMessageBox::No);

    if (reply != QMessageBox::Yes) return;

    int removeIdx = m_currentPantoIndex;
    if (m_pantographs[removeIdx].modelHandle) {
        edpc_model_free(m_pantographs[removeIdx].modelHandle);
        m_pantographs[removeIdx].modelHandle = nullptr;
    }
    m_pantographs.removeAt(removeIdx);

    // Update indices
    for (int i = 0; i < m_pantographs.size(); ++i) {
        m_pantographs[i].index = i;
        m_pantographs[i].id = QString("panto_%1").arg(i);
        m_pantographs[i].name = QString("Pantograph %1").arg(i + 1);
    }

    // Reassign vehicles
    for (int i = 0; i < m_vehicles.size(); ++i) {
        if (m_vehicles[i].pantographIndex == removeIdx) {
            m_vehicles[i].pantographIndex = 0;
        } else if (m_vehicles[i].pantographIndex > removeIdx) {
            m_vehicles[i].pantographIndex--;
        }
    }

    // Refresh vehicle table col 1
    for (int r = 0; r < m_vehicles.size(); ++r) {
        if (r < m_tableVehicles->rowCount() && m_tableVehicles->item(r, 1)) {
            m_tableVehicles->item(r, 1)->setText(
                m_vehicles[r].hasPantograph ? m_pantographs[m_vehicles[r].pantographIndex].displayName(isTr) : "—"
            );
        }
    }

    if (m_currentPantoIndex >= m_pantographs.size()) {
        m_currentPantoIndex = m_pantographs.size() - 1;
    }

    updatePantographCombos();
    loadPantoProfileToUi(m_currentPantoIndex);

    if (m_currentVehicleRow >= 0 && m_currentVehicleRow < m_vehicles.size()) {
        updateVehicleMountUi(m_currentVehicleRow);
    }
}

void MainWindow::onPantographSelectionChanged(int index) {
    if (index < 0 || index >= m_pantographs.size() || index == m_currentPantoIndex) return;
    syncCurrentPantoUiToProfile();
    loadPantoProfileToUi(index);
}

void MainWindow::onGenerateNewRandomId() {
    if (m_currentPantoIndex >= 0 && m_currentPantoIndex < m_pantographs.size()) {
        m_pantographs[m_currentPantoIndex].randomScriptId = generateScriptId();
        m_pantographs[m_currentPantoIndex].randomModelName = generateDisplayName();
        m_randomId = m_pantographs[m_currentPantoIndex].randomScriptId;
        m_randomDisplayName = m_pantographs[m_currentPantoIndex].randomModelName;
        updatePantographCombos();
        loadPantoProfileToUi(m_currentPantoIndex);
    }
}

void MainWindow::onBrowseOutputPath() {
    bool isTr = (m_currentLang == AppLanguage::Turkish);
    QString filter = isTr ? "MTR Tren Paketi ZIP (*.zip);;Tüm Dosyalar (*.*)" : "MTR Train Pack ZIP (*.zip);;All Files (*.*)";
    QString path = QFileDialog::getSaveFileName(this, isTr ? "Hedef ZIP Paketi Seç" : "Select Target ZIP Package", m_txtOutputPath->text(), filter);
    if (!path.isEmpty()) {
        if (!path.endsWith(".zip", Qt::CaseInsensitive)) {
            path += ".zip";
        }
        m_txtOutputPath->setText(path);
    }
}

QJsonObject MainWindow::buildPackageRequestJson() const {
    QJsonObject req;
    req["pack_input_path"] = m_packInputPath;
    req["pack_output_path"] = m_txtOutputPath->text();
    req["custom_resources_rel_path"] = m_customResourcesRelPath.isEmpty() ? "assets/mtr/mtr_custom_resources.json" : m_customResourcesRelPath;

    // Single-pantograph fallback fields from profile 0
    if (!m_pantographs.isEmpty()) {
        const auto& p0 = m_pantographs[0];
        req["panto_obj_path"] = p0.objPath;
        req["panto_mtl_path"] = p0.mtlPath.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(p0.mtlPath);
        req["panto_texture_path"] = p0.texturePath.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(p0.texturePath);
        req["random_id"] = p0.randomScriptId;
        req["model_file_name"] = QString("%1.obj").arg(p0.randomModelName);
        req["model_display_name"] = p0.randomModelName;
        req["wire_tracking_reference"] = p0.wireRef;
        req["no_wire_park_height"] = p0.noWirePark;
        req["enable_depot_park"] = p0.enableDepotPark;
        req["depot_park_height"] = p0.depotPark;
        req["upper_threshold"] = p0.upperThresh;
        req["lower_threshold"] = p0.lowerThresh;
        if (p0.customPivotsSet) {
            QJsonObject pivObj;
            pivObj["base_x"] = p0.customPivots.base.x;
            pivObj["base_y"] = p0.customPivots.base.y;
            pivObj["base_z"] = p0.customPivots.base.z;
            pivObj["elbow_x"] = p0.customPivots.elbow.x;
            pivObj["elbow_y"] = p0.customPivots.elbow.y;
            pivObj["elbow_z"] = p0.customPivots.elbow.z;
            pivObj["tip_x"] = p0.customPivots.tip.x;
            pivObj["tip_y"] = p0.customPivots.tip.y;
            pivObj["tip_z"] = p0.customPivots.tip.z;
            pivObj["shoe_offset"] = p0.customPivots.shoe_offset;
            pivObj["has_p4"] = p0.customPivots.has_p4;
            pivObj["p4_base_x"] = p0.customPivots.p4_base.x;
            pivObj["p4_base_y"] = p0.customPivots.p4_base.y;
            pivObj["p4_base_z"] = p0.customPivots.p4_base.z;
            pivObj["p4_elbow_x"] = p0.customPivots.p4_elbow.x;
            pivObj["p4_elbow_y"] = p0.customPivots.p4_elbow.y;
            pivObj["p4_elbow_z"] = p0.customPivots.p4_elbow.z;
            req["custom_pivots"] = pivObj;
        }
    }

    // Pantographs array
    QJsonArray pantoArr;
    for (int i = 0; i < m_pantographs.size(); ++i) {
        const auto& p = m_pantographs[i];
        QJsonObject po;
        po["id"] = QString("panto_%1").arg(i);
        po["random_id"] = p.randomScriptId;
        po["model_file_name"] = QString("%1.obj").arg(p.randomModelName);
        po["model_display_name"] = p.randomModelName;
        po["panto_obj_path"] = p.objPath;
        po["panto_mtl_path"] = p.mtlPath.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(p.mtlPath);
        po["panto_texture_path"] = p.texturePath.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(p.texturePath);
        po["wire_tracking_reference"] = p.wireRef;
        po["no_wire_park_height"] = p.noWirePark;
        po["enable_depot_park"] = p.enableDepotPark;
        po["depot_park_height"] = p.depotPark;
        po["upper_threshold"] = p.upperThresh;
        po["lower_threshold"] = p.lowerThresh;
        if (p.customPivotsSet) {
            QJsonObject pivObj;
            pivObj["base_x"] = p.customPivots.base.x;
            pivObj["base_y"] = p.customPivots.base.y;
            pivObj["base_z"] = p.customPivots.base.z;
            pivObj["elbow_x"] = p.customPivots.elbow.x;
            pivObj["elbow_y"] = p.customPivots.elbow.y;
            pivObj["elbow_z"] = p.customPivots.elbow.z;
            pivObj["tip_x"] = p.customPivots.tip.x;
            pivObj["tip_y"] = p.customPivots.tip.y;
            pivObj["tip_z"] = p.customPivots.tip.z;
            pivObj["shoe_offset"] = p.customPivots.shoe_offset;
            pivObj["has_p4"] = p.customPivots.has_p4;
            pivObj["p4_base_x"] = p.customPivots.p4_base.x;
            pivObj["p4_base_y"] = p.customPivots.p4_base.y;
            pivObj["p4_base_z"] = p.customPivots.p4_base.z;
            pivObj["p4_elbow_x"] = p.customPivots.p4_elbow.x;
            pivObj["p4_elbow_y"] = p.customPivots.p4_elbow.y;
            pivObj["p4_elbow_z"] = p.customPivots.p4_elbow.z;
            po["custom_pivots"] = pivObj;
        }
        pantoArr.append(po);
    }
    req["pantographs"] = pantoArr;

    // Vehicles array
    QJsonArray vehArr;
    for (const auto& v : m_vehicles) {
        QJsonObject vo;
        vo["id"] = v.id;
        vo["name"] = v.name;
        vo["description"] = v.description;
        vo["has_pantograph"] = v.hasPantograph;
        vo["pantograph_id"] = QString("panto_%1").arg(v.pantographIndex);
        vo["mount_height"] = v.mountHeight;
        vo["mount_x"] = v.mountX;
        vo["mount_z"] = v.mountZ;
        vo["rotation_y_deg"] = v.rotationYDeg;
        vo["wire_detect_z"] = v.wireDetectZ;
        vehArr.append(vo);
    }
    req["vehicles"] = vehArr;

    return req;
}

void MainWindow::onRefreshScriptPreview() {
    if (!m_txtScriptPreview) return;
    QJsonObject req = buildPackageRequestJson();
    QByteArray reqBytes = QJsonDocument(req).toJson(QJsonDocument::Compact);

    char* outScript = nullptr;
    if (edpc_preview_script_json(reqBytes.constData(), &outScript)) {
        if (outScript) {
            m_txtScriptPreview->setPlainText(QString::fromUtf8(outScript));
            edpc_free_string(outScript);
        }
    }
}

void MainWindow::onPackageAndExport() {
    bool isTr = (m_currentLang == AppLanguage::Turkish);

    if (m_packInputPath.isEmpty() || (!QFile::exists(m_packInputPath) && !QDir(m_packInputPath).exists())) {
        QMessageBox::warning(this, isTr ? "Eksik Tren Paketi" : "Missing Train Pack",
                             isTr ? "Lütfen 2. Adımda geçerli bir tren paketi seçin."
                                  : "Please select a valid train pack in Step 2.");
        m_tabs->setCurrentIndex(1);
        return;
    }

    bool hasAnyPanto = false;
    for (const auto& v : m_vehicles) {
        if (v.hasPantograph) {
            hasAnyPanto = true;
            break;
        }
    }
    if (!hasAnyPanto) {
        QMessageBox::warning(this, isTr ? "Araç Seçilmedi" : "No Vehicle Selected",
                             isTr ? "Lütfen 2. Adımda pantograf takılacak en az bir aracı işaretleyin."
                                  : "Please check at least one vehicle to mount a pantograph in Step 2.");
        m_tabs->setCurrentIndex(1);
        return;
    }

    // Verify all active pantographs have valid models
    for (const auto& v : m_vehicles) {
        if (v.hasPantograph) {
            int pIdx = v.pantographIndex;
            if (pIdx < 0 || pIdx >= m_pantographs.size() || m_pantographs[pIdx].objPath.isEmpty() || !QFile::exists(m_pantographs[pIdx].objPath)) {
                QString pName = (pIdx >= 0 && pIdx < m_pantographs.size()) ? m_pantographs[pIdx].displayName(isTr) : QString("#%1").arg(pIdx + 1);
                QMessageBox::warning(this, isTr ? "Eksik Pantograf Modeli" : "Missing Pantograph Model",
                    isTr ? QString("'%1' aracına atanan '%2' için henüz bir 3D .obj modeli seçilmemiş.\nLütfen 1. Adımda bu pantograf için bir model yükleyin.").arg(v.name, pName)
                         : QString("No 3D .obj model loaded for '%2' assigned to vehicle '%1'.\nPlease load a model in Step 1.").arg(v.name, pName));
                m_tabs->setCurrentIndex(0);
                if (pIdx >= 0 && pIdx < m_pantographs.size()) {
                    m_comboPantographs->setCurrentIndex(pIdx);
                    loadPantoProfileToUi(pIdx);
                }
                return;
            }
        }
    }

    QString outZipPath = m_txtOutputPath->text().trimmed();
    if (outZipPath.isEmpty()) {
        QMessageBox::warning(this, isTr ? "Eksik Çıktı Yolu" : "Missing Output Path",
                             isTr ? "Lütfen geçerli bir hedef ZIP paketi kayıt yolu belirtin."
                                  : "Please specify a valid target ZIP package save path.");
        return;
    }

    syncCurrentPantoUiToProfile();
    saveCurrentVehicleMountUi();

    m_lblExportStatus->setText(isTr ? "Paketleme yapılıyor, lütfen bekleyin..." : "Packaging in progress, please wait...");
    m_lblExportStatus->setStyleSheet("color: #61afef; font-weight: bold;");

    QJsonObject req = buildPackageRequestJson();
    QByteArray reqBytes = QJsonDocument(req).toJson(QJsonDocument::Compact);

    char* outResult = nullptr;
    bool ok = edpc_package_pack_json(reqBytes.constData(), &outResult);
    if (!ok || !outResult) {
        QString errStr = outResult ? QString::fromUtf8(outResult) : (isTr ? "Bilinmeyen hata." : "Unknown error.");
        if (outResult) edpc_free_string(outResult);

        m_lblExportStatus->setText(isTr ? QString("Paketleme başarısız: %1").arg(errStr) : QString("Packaging failed: %1").arg(errStr));
        m_lblExportStatus->setStyleSheet("color: #e06c75; font-weight: bold;");
        QMessageBox::critical(this, isTr ? "Paketleme Hatası" : "Packaging Error",
                              isTr ? QString("Paketleme sırasında hata oluştu:\n\n%1").arg(errStr)
                                   : QString("An error occurred during packaging:\n\n%1").arg(errStr));
        return;
    }

    QString finalPath = QString::fromUtf8(outResult);
    edpc_free_string(outResult);

    m_lastExportedFolder = QFileInfo(finalPath).absolutePath();
    m_btnOpenOutputFolder->setEnabled(true);

    m_lblExportStatus->setText(isTr ? QString("✓ Başarıyla paketlendi: %1").arg(finalPath) : QString("✓ Successfully packaged: %1").arg(finalPath));
    m_lblExportStatus->setStyleSheet("color: #98c379; font-weight: bold;");

    QString pantoListText;
    for (int i = 0; i < m_pantographs.size(); ++i) {
        bool used = false;
        QStringList assignedVehs;
        for (const auto& v : m_vehicles) {
            if (v.hasPantograph && v.pantographIndex == i) {
                used = true;
                assignedVehs.append(v.name.isEmpty() ? v.id : v.name);
            }
        }
        if (used) {
            pantoListText += QString("\n• %1 (assets/mtr/panto/%2_pantograph.js, %3.obj)\n  ➔ %4: %5\n")
                .arg(m_pantographs[i].displayName(isTr))
                .arg(m_pantographs[i].randomScriptId)
                .arg(m_pantographs[i].randomModelName)
                .arg(isTr ? "Atanan Araçlar" : "Assigned Vehicles")
                .arg(assignedVehs.join(", "));
        }
    }

    QString summary = isTr ? QString(
        "Tebrikler! MTR Tren Paketi dinamik pantograf(lar) ile başarıyla paketlendi.\n\n"
        "• Çıktı Dosyası: %1\n"
        "• Eklenen Pantograflar:\n%2\n"
        "• Güncellenen Yapılandırma: %3\n\n"
        "Minecraft'a yüklemeye hazır!"
    ).arg(finalPath, pantoListText, m_customResourcesRelPath)
    : QString(
        "Congratulations! MTR Train Pack successfully packaged with dynamic pantograph(s).\n\n"
        "• Output File: %1\n"
        "• Injected Pantographs:\n%2\n"
        "• Updated Configuration: %3\n\n"
        "Ready to load into Minecraft!"
    ).arg(finalPath, pantoListText, m_customResourcesRelPath);

    // Re-roll random IDs for all pantographs for next session/export
    for (int i = 0; i < m_pantographs.size(); ++i) {
        m_pantographs[i].randomScriptId = generateScriptId();
        m_pantographs[i].randomModelName = generateDisplayName();
    }
    updatePantographCombos();
    loadPantoProfileToUi(m_currentPantoIndex);
    for (int r = 0; r < m_vehicles.size(); ++r) {
        if (r < m_tableVehicles->rowCount() && m_tableVehicles->item(r, 1)) {
            m_tableVehicles->item(r, 1)->setText(
                m_vehicles[r].hasPantograph ? m_pantographs[m_vehicles[r].pantographIndex].displayName(isTr) : "—"
            );
        }
    }
    onRefreshScriptPreview();

    QMessageBox::information(this, isTr ? "Paketleme Başarılı!" : "Packaging Successful!", summary);
}

void MainWindow::onOpenOutputFolder() {
    if (!m_lastExportedFolder.isEmpty() && QDir(m_lastExportedFolder).exists()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_lastExportedFolder));
    }
}

void MainWindow::loadVehicleModelForPreview(const VehicleMountItem& v) {
    bool isTr = (m_currentLang == AppLanguage::Turkish);

    if (m_packInputPath.isEmpty()) {
        m_cachedVehicleMesh.clear();
        m_cachedVehicleTexture = QImage();
        if (m_vehicleViewport) {
            m_vehicleViewport->setVehicleMesh({});
        }
        if (m_lblVehicleVpInfo) {
            m_lblVehicleVpInfo->setText(isTr ? "3D Model: Tren paketi yüklenmedi." : "3D Model: No train pack loaded.");
            m_lblVehicleVpInfo->setStyleSheet("color: #8f96a3; font-size: 11px;");
        }
        return;
    }

    QString cacheKey = QString("%1|%2|%3|%4|%5|%6|%7")
        .arg(v.modelResource, v.modelPropertiesResource, v.positionDefinitionsResource)
        .arg(v.bogie1ModelResource, v.bogie2ModelResource)
        .arg(v.hasGangway1 ? "g1" : "nog1", v.hasGangway2 ? "g2" : "nog2");

    if (!v.modelResource.isEmpty() && cacheKey == m_loadedVehicleCacheKey && !m_cachedVehicleMesh.empty()) {
        if (m_vehicleViewport && !m_cachedVehicleTexture.isNull()) {
            m_vehicleViewport->setVehicleTexture(m_cachedVehicleTexture);
        }
        return; // Already loaded!
    }

    m_loadedVehicleCacheKey = cacheKey;

    if (!v.modelResource.isEmpty()) {
        // Gangway bogies (e.g. inter-car articulated Jacobs bogies) should not be rendered on standalone train car preview
        const char* b1Model = (v.hasGangway1 || v.bogie1ModelResource.isEmpty()) ? nullptr : v.bogie1ModelResource.toUtf8().constData();
        const char* b1Props = (v.hasGangway1 || v.bogie1PropertiesResource.isEmpty()) ? nullptr : v.bogie1PropertiesResource.toUtf8().constData();
        const char* b1Defs = (v.hasGangway1 || v.bogie1DefinitionsResource.isEmpty()) ? nullptr : v.bogie1DefinitionsResource.toUtf8().constData();

        const char* b2Model = (v.hasGangway2 || v.bogie2ModelResource.isEmpty()) ? nullptr : v.bogie2ModelResource.toUtf8().constData();
        const char* b2Props = (v.hasGangway2 || v.bogie2PropertiesResource.isEmpty()) ? nullptr : v.bogie2PropertiesResource.toUtf8().constData();
        const char* b2Defs = (v.hasGangway2 || v.bogie2DefinitionsResource.isEmpty()) ? nullptr : v.bogie2DefinitionsResource.toUtf8().constData();

        // Load modular .bbmodel or .obj vehicle mesh with properties and definitions
        EdpcVehicleMesh* vMesh = edpc_vehicle_mesh_load_modular(
            m_packInputPath.toUtf8().constData(),
            v.modelResource.toUtf8().constData(),
            v.modelPropertiesResource.isEmpty() ? nullptr : v.modelPropertiesResource.toUtf8().constData(),
            v.positionDefinitionsResource.isEmpty() ? nullptr : v.positionDefinitionsResource.toUtf8().constData(),
            b1Model,
            b1Props,
            b1Defs,
            static_cast<float>(v.bogie1Position),
            b2Model,
            b2Props,
            b2Defs,
            static_cast<float>(v.bogie2Position)
        );

        if (vMesh) {
            uint32_t count = 0;
            const EdpcVertex* verts = edpc_vehicle_mesh_get_vertices(vMesh, &count);
            if (verts && count > 0) {
                m_cachedVehicleMesh.assign(verts, verts + count);
                // Permanently and unconditionally rotate vehicle 180 degrees around Y axis
                for (auto& vert : m_cachedVehicleMesh) {
                    vert.pos.x = -vert.pos.x;
                    vert.pos.z = -vert.pos.z;
                    vert.normal.x = -vert.normal.x;
                    vert.normal.z = -vert.normal.z;
                }
            } else {
                m_cachedVehicleMesh.clear();
            }
            edpc_vehicle_mesh_free(vMesh);
        } else {
            m_cachedVehicleMesh.clear();
        }

        // Load vehicle texture
        if (!v.textureResource.isEmpty()) {
            uint8_t* texBytes = nullptr;
            uint32_t texLen = 0;
            if (edpc_read_pack_file(m_packInputPath.toUtf8().constData(), v.textureResource.toUtf8().constData(), &texBytes, &texLen)) {
                QImage img;
                if (img.loadFromData(texBytes, texLen)) {
                    m_cachedVehicleTexture = img;
                    if (m_vehicleViewport) {
                        m_vehicleViewport->setVehicleTexture(m_cachedVehicleTexture);
                    }
                }
                edpc_free_bytes(texBytes, texLen);
            }
        }

        if (m_vehicleViewport) {
            m_vehicleViewport->setVehicleMesh(m_cachedVehicleMesh);
            // Default to yaw = 45 deg to view front cab in foreground matching Blockbench!
            m_vehicleViewport->setCameraParams(45.0f, 20.0f, 18.0f, QVector3D(0.0f, 1.8f, 0.0f));
        }

        if (m_lblVehicleVpInfo) {
            QString info = isTr ? QString("3D Model: %1 (%2 üçgen)").arg(v.modelResource).arg(m_cachedVehicleMesh.size() / 3)
                                : QString("3D Model: %1 (%2 triangles)").arg(v.modelResource).arg(m_cachedVehicleMesh.size() / 3);
            if (!v.modelPropertiesResource.isEmpty()) {
                info += isTr ? QString(" | Modüler Tanım: Aktif") : QString(" | Modular Defs: Active");
            }
            if (!v.textureResource.isEmpty()) {
                info += isTr ? QString(" | Doku: %1").arg(v.textureResource) : QString(" | Texture: %1").arg(v.textureResource);
            }
            m_lblVehicleVpInfo->setText(info);
            m_lblVehicleVpInfo->setStyleSheet("color: #98c379; font-size: 11px; font-weight: bold;");
        }
    } else {
        // Fallback: generate reference carriage box
        m_cachedVehicleMesh.clear();
        float length = 14.0f;
        float width = 2.8f;
        float height = 2.568f;
        float halfL = length * 0.5f;
        float halfW = width * 0.5f;

        auto addBoxFace = [this](EdpcVec3 a, EdpcVec3 b, EdpcVec3 c, EdpcVec3 d, EdpcVec3 n) {
            EdpcVec2 zeroUv{0.0f, 0.0f};
            m_cachedVehicleMesh.push_back({a, n, zeroUv, 5});
            m_cachedVehicleMesh.push_back({b, n, zeroUv, 5});
            m_cachedVehicleMesh.push_back({c, n, zeroUv, 5});
            m_cachedVehicleMesh.push_back({a, n, zeroUv, 5});
            m_cachedVehicleMesh.push_back({c, n, zeroUv, 5});
            m_cachedVehicleMesh.push_back({d, n, zeroUv, 5});
        };

        addBoxFace({-halfW, height, halfL}, {halfW, height, halfL}, {halfW, height, -halfL}, {-halfW, height, -halfL}, {0, 1, 0});
        addBoxFace({-halfW, 0.2f, halfL}, {halfW, 0.2f, halfL}, {halfW, height, halfL}, {-halfW, height, halfL}, {0, 0, 1});
        addBoxFace({halfW, 0.2f, -halfL}, {-halfW, 0.2f, -halfL}, {-halfW, height, -halfL}, {halfW, height, -halfL}, {0, 0, -1});
        addBoxFace({halfW, 0.2f, halfL}, {halfW, 0.2f, -halfL}, {halfW, height, -halfL}, {halfW, height, halfL}, {1, 0, 0});
        addBoxFace({-halfW, 0.2f, -halfL}, {-halfW, 0.2f, halfL}, {-halfW, height, halfL}, {-halfW, height, -halfL}, {-1, 0, 0});

        // Permanently and unconditionally rotate vehicle 180 degrees around Y axis
        for (auto& vert : m_cachedVehicleMesh) {
            vert.pos.x = -vert.pos.x;
            vert.pos.z = -vert.pos.z;
            vert.normal.x = -vert.normal.x;
            vert.normal.z = -vert.normal.z;
        }

        if (m_vehicleViewport) {
            m_vehicleViewport->setVehicleMesh(m_cachedVehicleMesh);
            m_vehicleViewport->setCameraParams(45.0f, 20.0f, 18.0f, QVector3D(0.0f, 1.5f, 0.0f));
        }
        if (m_lblVehicleVpInfo) {
            m_lblVehicleVpInfo->setText(isTr ? "3D Model: [Belirtilmemiş - Referans Vagon Gövdesi Gösteriliyor]"
                                             : "3D Model: [Unspecified - Reference Carriage Body Displayed]");
            m_lblVehicleVpInfo->setStyleSheet("color: #e5c07b; font-size: 11px;");
        }
    }
}

void MainWindow::updateVehicleViewportMesh() {
    if (!m_vehicleViewport) return;
    if (m_tabs && m_tabs->currentIndex() != 1) return;
    if (m_vehiclePreviewStack)
        m_vehiclePreviewStack->setCurrentIndex(m_currentVehicleRow >= 0 && !m_cachedVehicleMesh.empty() ? 1 : 0);

    if (m_currentVehicleRow < 0 || m_currentVehicleRow >= m_vehicles.size()) {
        m_vehicleViewport->updateMeshData({});
        m_vehicleViewport->setWireVisible(false);
        return;
    }

    const VehicleMountItem& v = m_vehicles[m_currentVehicleRow];

    if (!v.hasPantograph) {
        m_vehicleViewport->updateMeshData({});
        m_vehicleViewport->setWireVisible(false);
        return;
    }

    int pIdx = v.pantographIndex;
    if (pIdx < 0 || pIdx >= m_pantographs.size()) {
        m_vehicleViewport->updateMeshData({});
        m_vehicleViewport->setWireVisible(false);
        return;
    }

    const auto& panto = m_pantographs[pIdx];

    EdpcModelHandle* handleToUse = panto.modelHandle;
    if (!handleToUse && pIdx == m_currentPantoIndex && m_viewport && m_viewport->hasModel()) {
        handleToUse = m_viewport->modelHandle();
    }

    if (handleToUse) {
        if (panto.customPivotsSet) {
            edpc_model_set_pivots(handleToUse, &panto.customPivots);
        }
        double wireRef = panto.wireRef;

        const EdpcVertex* pantoVerts = nullptr;
        uint32_t pantoCount = 0;
        if (edpc_model_update_transformed_mesh(handleToUse, static_cast<float>(wireRef), &pantoVerts, &pantoCount) && pantoVerts && pantoCount > 0) {
            float mountX = static_cast<float>(v.mountX);
            float mountZ = static_cast<float>(v.mountZ);
            float rotDeg = static_cast<float>(v.rotationYDeg);

            // Pantograph is NEVER affected by vehicle flipping!
            float rad = qDegreesToRadians(rotDeg);
            float cosA = std::cos(rad);
            float sinA = std::sin(rad);

            std::vector<EdpcVertex> mountedPanto;
            mountedPanto.reserve(pantoCount);

            for (uint32_t i = 0; i < pantoCount; ++i) {
                EdpcVertex vert = pantoVerts[i];

                // Rotate around Y axis
                float rx = vert.pos.x * cosA + vert.pos.z * sinA;
                float rz = -vert.pos.x * sinA + vert.pos.z * cosA;

                // Translate to vehicle roof mount position
                vert.pos.x = rx + mountX;
                vert.pos.y = vert.pos.y + static_cast<float>(v.mountHeight);
                vert.pos.z = rz + mountZ;

                // Rotate normal around Y axis
                float rnx = vert.normal.x * cosA + vert.normal.z * sinA;
                float rnz = -vert.normal.x * sinA + vert.normal.z * cosA;
                vert.normal.x = rnx;
                vert.normal.z = rnz;

                mountedPanto.push_back(vert);
            }

            m_vehicleViewport->updateMeshData(mountedPanto);

            // Forward pantograph texture
            if (!panto.textureImage.isNull()) {
                m_vehicleViewport->setTextureImage(panto.textureImage);
            } else if (pIdx == m_currentPantoIndex && m_viewport && !m_viewport->cachedTexture().isNull()) {
                m_vehicleViewport->setTextureImage(m_viewport->cachedTexture());
            }

            // Reference catenary wire above the train car
            float totalWireH = static_cast<float>(v.mountHeight + wireRef + panto.shoeOffset);
            m_vehicleViewport->updateWireHeight(totalWireH);
            m_vehicleViewport->setWireVisible(true);
        } else {
            m_vehicleViewport->updateMeshData({});
            m_vehicleViewport->setWireVisible(false);
        }
    } else {
        m_vehicleViewport->updateMeshData({});
        m_vehicleViewport->setWireVisible(false);
    }
}

void MainWindow::onCheckForUpdates() {
    bool isTr = (m_currentLang == AppLanguage::Turkish);
    m_btnCheckUpdate->setEnabled(false);
    m_btnCheckUpdate->setText(isTr ? "⏳ Denetleniyor..." : "⏳ Checking...");

    QNetworkRequest req(QUrl("https://api.github.com/repos/EgEhEw/EDPC/releases/latest"));
    req.setHeader(QNetworkRequest::UserAgentHeader, "EDPC-Configurator");
    m_netManager->get(req);
}

void MainWindow::onUpdateReplyFinished(QNetworkReply* reply) {
    bool isTr = (m_currentLang == AppLanguage::Turkish);
    m_btnCheckUpdate->setEnabled(true);
    m_btnCheckUpdate->setText(isTr ? "🔄 Güncellemeleri Denetle" : "🔄 Check for Updates");

    if (reply->error() != QNetworkReply::NoError) {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (statusCode == 404) {
            QMessageBox::information(this,
                isTr ? "Güncelleme Bilgisi" : "Update Info",
                isTr ? QString("Henüz GitHub üzerinde yayınlanmış bir sürüm (Release) bulunamadı.\nŞu anki sürümünüz: %1").arg(APP_VERSION)
                     : QString("No releases found on GitHub yet.\nYour current version: %1").arg(APP_VERSION));
        } else {
            QMessageBox::warning(this,
                isTr ? "Güncelleme Hatası" : "Update Error",
                isTr ? QString("GitHub sunucusuna bağlanılamadı: %1").arg(reply->errorString())
                     : QString("Could not connect to GitHub: %1").arg(reply->errorString()));
        }
        reply->deleteLater();
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return;

    QJsonObject obj = doc.object();
    QString latestTag = obj["tag_name"].toString().trimmed();
    QString releaseName = obj["name"].toString().trimmed();
    QString body = obj["body"].toString().trimmed();
    QString htmlUrl = obj["html_url"].toString();
    if (htmlUrl.isEmpty()) {
        htmlUrl = "https://github.com/EgEhEw/EDPC/releases";
    }

    if (latestTag.isEmpty()) {
        QMessageBox::information(this,
            isTr ? "Güncelleme Bilgisi" : "Update Info",
            isTr ? "Henüz GitHub üzerinde yeni bir sürüm bulunamadı." : "No releases found on GitHub.");
        return;
    }

    // Compare SemVer version tags (e.g. "v1.0.1" vs "v1.0.0")
    auto parseSemVer = [](QString s) -> std::tuple<int, int, int> {
        if (s.startsWith('v', Qt::CaseInsensitive)) s.remove(0, 1);
        QStringList parts = s.split('.');
        int maj = parts.value(0).toInt();
        int min = parts.value(1).toInt();
        int pat = parts.value(2).toInt();
        return {maj, min, pat};
    };

    if (parseSemVer(latestTag) > parseSemVer(APP_VERSION)) {
        auto res = QMessageBox::question(this,
            isTr ? "Yeni Sürüm Mevcut! 🚀" : "New Version Available! 🚀",
            isTr ? QString("Yeni bir EDPC sürümü yayınlandı: <b>%1</b> (Mevcut: %2)<br><br>"
                           "<b>Sürüm Notları:</b><br>%3<br><br>"
                           "GitHub indirme sayfasını açmak ister misiniz?")
                       .arg(latestTag, APP_VERSION, body.isEmpty() ? releaseName : body.left(300))
                 : QString("A new version of EDPC is available: <b>%1</b> (Current: %2)<br><br>"
                           "<b>Release Notes:</b><br>%3<br><br>"
                           "Would you like to open the GitHub download page?")
                       .arg(latestTag, APP_VERSION, body.isEmpty() ? releaseName : body.left(300)),
            QMessageBox::Yes | QMessageBox::No);

        if (res == QMessageBox::Yes) {
            QDesktopServices::openUrl(QUrl(htmlUrl));
        }
    } else {
        QMessageBox::information(this,
            isTr ? "Sürüm Güncel" : "Up to Date",
            isTr ? QString("Tebrikler! En güncel sürümü kullanıyorsunuz (%1).").arg(APP_VERSION)
                 : QString("You are running the latest version (%1).").arg(APP_VERSION));
    }
}


