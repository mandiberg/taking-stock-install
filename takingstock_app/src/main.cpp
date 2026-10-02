#include "ofMain.h"
#include "ofApp.h"
#include "SecondaryWindowApp.h"
#include "ConfigLoader.h"

#define GLFW_INCLUDE_NONE
#include "GLFW/glfw3.h"

int main() {
    // Load config before creating any window so we can use BOX_WIDTH/BOX_HEIGHT,
    // OUTPUT_MODE, and WINDOW_DECORATED to configure the window. Window position is hard-coded to (0, 0).
    BinSorterConfig cfg;
    ConfigLoader::load(kConfigPath, cfg);

    const bool syphonMode = (cfg.outputMode == OutputMode::Syphon);
    const int winW = syphonMode ? cfg.previewWidth : cfg.boxWidth;
    const int winH = syphonMode ? cfg.previewHeight : cfg.boxHeight;

    ofGLFWWindowSettings mainSettings;
    mainSettings.setSize(winW, winH);
    mainSettings.setPosition(glm::vec2(cfg.windowX, cfg.windowY));
    mainSettings.decorated = cfg.windowDecorated;
    mainSettings.resizable = cfg.windowDecorated;
    if (syphonMode) {
        mainSettings.title = "Taking Stock - Preview";
    }
    auto mainWindow = ofCreateWindow(mainSettings);

    // Log every monitor's position and resolution to help verify the display arrangement.
    // Output appears in the Xcode console or terminal. The window always opens at (0, 0),
    // so the display span must start at the primary display's top-left.
    int monitorCount = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
    ofLogNotice("Displays") << monitorCount << " monitor(s) found:";
    for (int i = 0; i < monitorCount; i++) {
        int mx, my;
        glfwGetMonitorPos(monitors[i], &mx, &my);
        const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
        ofLogNotice("Displays") << "  [" << i << "] " << glfwGetMonitorName(monitors[i])
                                << "  pos=(" << mx << ", " << my << ")"
                                << "  size=" << mode->width << "x" << mode->height;
    }
    ofLogNotice("Displays") << "OUTPUT_MODE=" << (syphonMode ? "syphon" : "window")
                            << " canvas=" << cfg.boxWidth << "x" << cfg.boxHeight
                            << " window created at (" << cfg.windowX << ", " << cfg.windowY
                            << ") size=" << winW << "x" << winH;

    auto mainApp = std::make_shared<ofApp>();
    ofRunApp(mainWindow, mainApp);

    if (cfg.secondaryWindowEnabled) {
        ofGLFWWindowSettings secSettings;
        secSettings.setSize(cfg.secondaryWindowWidth, cfg.secondaryWindowHeight);
        secSettings.title = "Taking Stock - Info";
        secSettings.shareContextWith = mainWindow;
        auto secWindow = ofCreateWindow(secSettings);
        ofRunApp(secWindow, std::make_shared<SecondaryWindowApp>(mainApp));
    }

    ofRunMainLoop();
}
