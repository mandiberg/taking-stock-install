#include "SecondaryWindowApp.h"
#include "ofApp.h"

SecondaryWindowApp::SecondaryWindowApp(std::shared_ptr<ofApp> mainApp_)
    : mainApp(mainApp_) {}

void SecondaryWindowApp::setup() {
    ofBackground(0);
    ofSetWindowTitle("Taking Stock - Info");

    font.load(OF_TTF_SANS, 48, true, true);
    labelFont.load(OF_TTF_SANS, 18, true, true);
}

void SecondaryWindowApp::draw() {
    ofBackground(0);
    ofSetColor(255);

    float cx = ofGetWidth() / 2.f;
    float cy = ofGetHeight() / 2.f;

    if (!mainApp->isKeyVideoEnabled()) {
        std::string line1 = "KEY_VIDEO must be enabled";
        std::string line2 = "for secondary window";
        float w1 = labelFont.stringWidth(line1);
        float w2 = labelFont.stringWidth(line2);
        float lh = labelFont.getLineHeight();
        labelFont.drawString(line1, cx - w1 / 2.f, cy - lh / 2.f);
        labelFont.drawString(line2, cx - w2 / 2.f, cy + lh);
        return;
    }

    std::string clusterNo = mainApp->getCurrentKeyVideoClusterNo();
    std::string valueStr  = clusterNo.empty() ? "--" : clusterNo;
    std::string label     = "Cluster:";

    float labelW = labelFont.stringWidth(label);
    float valueW = font.stringWidth(valueStr);
    float lh     = labelFont.getLineHeight();
    float fh     = font.getLineHeight();

    float totalH = lh + 8.f + fh;
    float startY = cy - totalH / 2.f + lh;

    ofSetColor(160);
    labelFont.drawString(label, cx - labelW / 2.f, startY);

    ofSetColor(255);
    font.drawString(valueStr, cx - valueW / 2.f, startY + lh + 8.f + fh * 0.8f);
}
