#include "ofMain.h"
#include "ofApp.h"
#include "SecondaryWindowApp.h"
#include "ConfigLoader.h"

#define GLFW_INCLUDE_NONE
#include "GLFW/glfw3.h"

int main() {
    // Load config before creating any window so we can use BOX_WIDTH/BOX_HEIGHT,
    // WINDOW_X/WINDOW_Y, and WINDOW_DECORATED to configure the render window.
    BinSorterConfig cfg;
    ConfigLoader::load(ofToDataPath("config.txt", true), cfg);

    ofGLFWWindowSettings mainSettings;
    mainSettings.setSize(cfg.boxWidth, cfg.boxHeight);
    mainSettings.setPosition(glm::vec2(cfg.windowX, cfg.windowY));
    mainSettings.decorated = cfg.windowDecorated;
    mainSettings.resizable = false;
    auto mainWindow = ofCreateWindow(mainSettings);

    // Log every monitor's position and resolution so WINDOW_X / WINDOW_Y can be
    // set accurately. Output appears in the Xcode console or terminal.
    // The leftmost display's x value is what WINDOW_X should be set to when you
    // want the render window to start at the left edge of your display span.
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
    ofLogNotice("Displays") << "Window created at (" << cfg.windowX << ", " << cfg.windowY
                            << ") size=" << cfg.boxWidth << "x" << cfg.boxHeight;

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
