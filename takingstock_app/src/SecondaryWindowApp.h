#pragma once

#include "ofMain.h"
#include <memory>

class ofApp;

class SecondaryWindowApp : public ofBaseApp {
public:
    SecondaryWindowApp(std::shared_ptr<ofApp> mainApp);

    void setup() override;
    void draw() override;

private:
    std::shared_ptr<ofApp> mainApp;
    ofTrueTypeFont font;
    ofTrueTypeFont labelFont;
};
