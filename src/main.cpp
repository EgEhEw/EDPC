#include <QtWidgets/qapplication.h>
#include <QtGui/qvulkaninstance.h>
#include <QtGui/qicon.h>
#include <QtGui/qpixmap.h>
#include <QtCore/qloggingcategory.h>
#include <QtCore/qdebug.h>

#include "icon_embedded.h"
#include "mainwindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("EDPC");
    app.setApplicationDisplayName("EDPC - EgEhE's Dynamic Pantograph Configurator");
    app.setOrganizationName("EgEhE");
    app.setApplicationVersion("1.2.0");

    QPixmap iconPix;
    if (iconPix.loadFromData(EDPC_ICON_PNG, static_cast<uint>(EDPC_ICON_PNG_LEN))) {
        app.setWindowIcon(QIcon(iconPix));
    }

    QVulkanInstance inst;
    // Enable standard validation if available
    inst.setLayers({"VK_LAYER_KHRONOS_validation"});
    if (!inst.create()) {
        qWarning("Could not create Vulkan instance with validation layers, trying without validation layers...");
        inst.setLayers({});
        if (!inst.create()) {
            qWarning("Failed to create Vulkan instance! Vulkan viewport may not be available.");
        }
    }

    MainWindow window(&inst);
    window.show();

    return app.exec();
}
