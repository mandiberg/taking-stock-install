#include "ofMain.h"
#include "ofApp.h"
#include "SecondaryWindowApp.h"
#include "ConfigLoader.h"

int main() {
    ofGLFWWindowSettings mainSettings;
    mainSettings.setSize(1920, 1080);
    auto mainWindow = ofCreateWindow(mainSettings);

    // Load config before starting the main loop so we know whether to open the secondary window.
    // ofApp::setup() will load config again in full; this early read is lightweight (text file only).
    BinSorterConfig cfg;
    ConfigLoader::load(ofToDataPath("config.txt", true), cfg);

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
