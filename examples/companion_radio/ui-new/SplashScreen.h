#pragma once

#include "UITask.h"
#include "icons.h"
#include <string.h>

#ifndef BOOT_SCREEN_MILLIS
  #define BOOT_SCREEN_MILLIS  3000
#endif

class SplashScreen : public UIScreen {
    UITask* _task;
    unsigned long dismiss_after;
    char _version_info[12];

public:
    SplashScreen(UITask* task) : _task(task) {
        // Strip the dash+commit hash from the version string.
        // e.g. "v1.2.3-abcdef" -> "v1.2.3"
        const char *ver  = FIRMWARE_VERSION;
        const char *dash = strchr(ver, '-');
        int len = dash ? (int)(dash - ver) : (int)strlen(ver);
        if (len >= (int)sizeof(_version_info)) len = sizeof(_version_info) - 1;
        memcpy(_version_info, ver, len);
        _version_info[len] = 0;

        dismiss_after = millis() + BOOT_SCREEN_MILLIS;
    }

    int render(DisplayDriver& display) override {
        // MeshCore logo
        display.setColor(DisplayDriver::BLUE);
        const int logoWidth = 128;
        display.drawXbm((display.width() - logoWidth) / 2, 3,
                        meshcore_logo, logoWidth, 13);

        // Website URL
        const char* website = "pager thing";
        display.setColor(DisplayDriver::LIGHT);
        display.setTextSize(1);
        uint16_t websiteWidth = display.getTextWidth(website);
        display.setCursor((display.width() - websiteWidth) / 2, 22);
        display.print(website);

        // Version and build date
        display.setColor(DisplayDriver::LIGHT);
        display.setTextSize(1);
        display.drawTextCentered(display.width() / 2, 35, _version_info);
        display.drawTextCentered(display.width() / 2, 48, FIRMWARE_BUILD_DATE);

        return 1000;
    }

    void poll() override {
        if (millis() >= dismiss_after) {
            _task->gotoHomeScreen();
        }
    }
};
