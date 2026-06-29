#pragma once

#include "UITask.h"
#include "CannedMessages.h"
#include "icons.h"

#ifdef WIFI_SSID
  #include <WiFi.h>
#endif

#ifndef UI_RECENT_LIST_SIZE
  #define UI_RECENT_LIST_SIZE 4
#endif

#ifndef BATT_MIN_MILLIVOLTS
  #define BATT_MIN_MILLIVOLTS 3000
#endif
#ifndef BATT_MAX_MILLIVOLTS
  #define BATT_MAX_MILLIVOLTS 4200
#endif

#if UI_HAS_JOYSTICK
  #define PRESS_LABEL "press Enter"
#else
  #define PRESS_LABEL "long press"
#endif

// ============================================================================
// HomeScreen
// ============================================================================

class HomeScreen : public UIScreen {

    // ------------------------------------------------------------------------
    // Page enumeration
    // ------------------------------------------------------------------------
    enum HomePage : uint8_t {
        FIRST,
        PIN,
        RECENT,
        RADIO,
        BLUETOOTH,
        CANNED,
        ADVERT,
#if ENV_INCLUDE_GPS == 1
        GPS,
#endif
#if UI_SENSORS_PAGE == 1
        SENSORS,
#endif
        SHUTDOWN,
        Count   // keep last
    };

    // ------------------------------------------------------------------------
    // Dependencies
    // ------------------------------------------------------------------------
    UITask*           _task;
    mesh::RTCClock*   _rtc;
    SensorManager*    _sensors;
    NodePrefs*        _node_prefs;

    // ------------------------------------------------------------------------
    // Page state
    // ------------------------------------------------------------------------
    uint8_t   _page          = 0;
    bool      _shutdown_init = false;

    AdvertPath recent[UI_RECENT_LIST_SIZE];

    // ------------------------------------------------------------------------
    // Canned / custom message state
    // ------------------------------------------------------------------------
    bool    selectingCanned   = false;
    bool    selectingChannel  = false;
    bool    composingCustom   = false;
    bool    pendingCustomMessage = false;

    uint8_t cannedIndex          = 0;
    uint8_t channelIndex         = 0;
    uint8_t channelList[8];
    uint8_t channelListCount     = 0;
    uint8_t customCandidateIndex = 0;
    char    customMessage[80];
    uint8_t customMessageLen     = 0;

    // ------------------------------------------------------------------------
    // Sensor page state
    // ------------------------------------------------------------------------
    CayenneLPP sensors_lpp;
    int        sensors_nb            = 0;
    bool       sensors_scroll        = false;
    int        sensors_scroll_offset = 0;
    int        next_sensors_refresh  = 0;

    // ========================================================================
    // Helpers
    // ========================================================================

    void renderBatteryIndicator(DisplayDriver& display, uint16_t batteryMilliVolts) {
        int pct = ((int)(batteryMilliVolts - BATT_MIN_MILLIVOLTS) * 100)
                  / (BATT_MAX_MILLIVOLTS - BATT_MIN_MILLIVOLTS);
        if (pct < 0)   pct = 0;
        if (pct > 100) pct = 100;

        const int iconW = 24, iconH = 10;
        const int iconX = display.width() - iconW - 5;
        const int iconY = 0;

        display.setColor(DisplayDriver::GREEN);
        display.drawRect(iconX, iconY, iconW, iconH);
        display.fillRect(iconX + iconW, iconY + iconH / 4, 3, iconH / 2);  // cap

        int fillW = (pct * (iconW - 4)) / 100;
        display.fillRect(iconX + 2, iconY + 2, fillW, iconH - 4);

#ifdef PIN_BUZZER
        if (_task->isBuzzerQuiet()) {
            display.setColor(DisplayDriver::RED);
            display.drawXbm(iconX - 9, iconY + 1, muted_icon, 8, 8);
        }
#endif
    }

    void renderPageDots(DisplayDriver& display) {
        int y = 14;
        int x = display.width() / 2 - 5 * (HomePage::Count - 1);
        for (uint8_t i = 0; i < HomePage::Count; i++, x += 10) {
            if (i == _page)
                display.fillRect(x - 1, y - 1, 3, 3);
            else
                display.fillRect(x, y, 1, 1);
        }
    }

    // Populate channelList[] with every valid channel index (0-7).
    void buildChannelList() {
        channelListCount = 0;
        ChannelDetails channel;
        for (uint8_t i = 0; i < 15; i++) {
            if (the_mesh.getChannel(i, channel))
                channelList[channelListCount++] = i;
        }
        channelIndex = 0;
    }

    void refresh_sensors() {
        if (millis() <= (unsigned)next_sensors_refresh) return;

        sensors_lpp.reset();
        sensors_nb = 0;
        sensors_lpp.addVoltage(TELEM_CHANNEL_SELF,
                               (float)board.getBattMilliVolts() / 1000.0f);
        sensors.querySensors(0xFF, sensors_lpp);

        LPPReader reader(sensors_lpp.getBuffer(), sensors_lpp.getSize());
        uint8_t ch, type;
        while (reader.readHeader(ch, type)) {
            reader.skipData(type);
            sensors_nb++;
        }
        sensors_scroll = (sensors_nb > UI_RECENT_LIST_SIZE);

#if AUTO_OFF_MILLIS > 0
        next_sensors_refresh = millis() + 5000;
#else
        next_sensors_refresh = millis() + 60000;
#endif
    }

    // ========================================================================
    // Per-page RENDER helpers
    // ========================================================================

    void renderFirstPage(DisplayDriver& display) {
        char tmp[40];
        int unread = _task->getUnreadCount();

        display.setTextSize(2);
        if (unread > 0) {
            display.setColor(DisplayDriver::YELLOW);
            snprintf(tmp, sizeof(tmp), "Inbox (%d)", unread);
        } else {
            display.setColor(DisplayDriver::LIGHT);
            strcpy(tmp, "Inbox");
        }
        display.drawTextCentered(display.width() / 2, 24, tmp);

        display.setTextSize(1);
        display.setColor(DisplayDriver::LIGHT);
        display.drawTextCentered(display.width() / 2, 42, "hold to open");

#ifdef WIFI_SSID
        IPAddress ip = WiFi.localIP();
        snprintf(tmp, sizeof(tmp), "IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        display.setTextSize(1);
        display.drawTextCentered(display.width() / 2, 54, tmp);
#endif
    }

    void renderPinPage(DisplayDriver& display) {
        char tmp[32];
        display.setTextSize(1);

        if (_task->hasConnection()) {
            display.setColor(DisplayDriver::GREEN);
            display.drawTextCentered(display.width() / 2, 20, "BLE");
            display.drawTextCentered(display.width() / 2, 34, "Connected");
        } else if (the_mesh.getBLEPin() != 0) {
            display.setColor(DisplayDriver::YELLOW);
            display.drawTextCentered(display.width() / 2, 16, "BLE Pairing");
            display.setColor(DisplayDriver::RED);
            display.setTextSize(2);
            snprintf(tmp, sizeof(tmp), "%d", the_mesh.getBLEPin());
            display.drawTextCentered(display.width() / 2, 30, tmp);
        } else {
            display.setColor(DisplayDriver::LIGHT);
            display.drawTextCentered(display.width() / 2, 20, "BLE");
            display.drawTextCentered(display.width() / 2, 34, "Not connected");
        }
    }

    void renderRecentPage(DisplayDriver& display) {
        the_mesh.getRecentlyHeard(recent, UI_RECENT_LIST_SIZE);
        display.setColor(DisplayDriver::GREEN);
        char tmp[32];
        int y = 20;
        for (int i = 0; i < UI_RECENT_LIST_SIZE; i++, y += 11) {
            auto a = &recent[i];
            if (a->name[0] == 0) continue;

            int secs = _rtc->getCurrentTime() - a->recv_timestamp;
            if      (secs < 60)      sprintf(tmp, "%ds",  secs);
            else if (secs < 60 * 60) sprintf(tmp, "%dm",  secs / 60);
            else                     sprintf(tmp, "%dh",  secs / (60 * 60));

            int tsW         = display.getTextWidth(tmp);
            int maxNameW    = display.width() - tsW - 1;
            char filtered[sizeof(a->name)];
            display.translateUTF8ToBlocks(filtered, a->name, sizeof(filtered));
            display.drawTextEllipsized(0, y, maxNameW, filtered);
            display.setCursor(display.width() - tsW - 1, y);
            display.print(tmp);
        }
    }

    void renderRadioPage(DisplayDriver& display) {
        char tmp[48];
        display.setColor(DisplayDriver::YELLOW);
        display.setTextSize(1);

        display.setCursor(0, 20);
        sprintf(tmp, "FQ: %06.3f   SF: %d", _node_prefs->freq, _node_prefs->sf);
        display.print(tmp);

        display.setCursor(0, 31);
        sprintf(tmp, "BW: %03.2f     CR: %d", _node_prefs->bw, _node_prefs->cr);
        display.print(tmp);

        display.setCursor(0, 42);
        sprintf(tmp, "TX: %ddBm", _node_prefs->tx_power_dbm);
        display.print(tmp);

        display.setCursor(0, 53);
        sprintf(tmp, "Noise floor: %d", radio_driver.getNoiseFloor());
        display.print(tmp);
    }

    void renderBluetoothPage(DisplayDriver& display) {
        display.setColor(DisplayDriver::GREEN);
        display.drawXbm((display.width() - 32) / 2, 18,
                        _task->isSerialEnabled() ? bluetooth_on : bluetooth_off,
                        32, 32);
        display.setTextSize(1);
        display.drawTextCentered(display.width() / 2, 64 - 11, "toggle: " PRESS_LABEL);
    }

    // -- Canned sub-renderers ------------------------------------------------

    void renderCannedIdle(DisplayDriver& display) {
        display.drawXbm((display.width() - 32) / 2, 18, advert_icon, 32, 32);
        display.drawTextCentered(display.width() / 2, 48, "Send canned msg");
        display.drawTextCentered(display.width() / 2, 58, " ");
    }

    void renderCannedCompose(DisplayDriver& display) {
        const char *candidate = customCandidates[customCandidateIndex];

        display.drawTextCentered(display.width() / 2, 8, "Custom Message");

        display.setColor(DisplayDriver::YELLOW);
        display.drawTextCentered(display.width() / 2, 22,
                                 customMessageLen ? customMessage : "<empty>");

        display.setColor(DisplayDriver::LIGHT);
        display.drawTextCentered(display.width() / 2, 36, "char:");

        display.setColor(DisplayDriver::YELLOW);
        display.drawTextCentered(display.width() / 2, 46, candidate);

        display.setColor(DisplayDriver::LIGHT);
        display.drawTextCentered(display.width() / 2, 56, "short press scroll");
        display.drawTextCentered(display.width() / 2, 64, " ");
    }

    void renderCannedSelectMessage(DisplayDriver& display) {
        bool custom      = (cannedIndex == NUM_CANNED_MESSAGES);
        const char *label = custom ? "Custom" : cannedMessages[cannedIndex].label;

        display.drawTextCentered(display.width() / 2, 8,  "Select Message");
        display.setColor(DisplayDriver::YELLOW);
        display.drawTextCentered(display.width() / 2, 28, label);
        display.setColor(DisplayDriver::LIGHT);
        display.drawTextCentered(display.width() / 2, 46, "short press to scroll");
        display.drawTextCentered(display.width() / 2, 56, " ");
    }

    void renderCannedSelectChannel(DisplayDriver& display) {
        bool back = (channelIndex == channelListCount);
        char buf[32] = "";

        if (!back && channelIndex < channelListCount) {
            ChannelDetails channel;
            if (the_mesh.getChannel(channelList[channelIndex], channel)) {
                if (channel.name[0] != '\0')
                    snprintf(buf, sizeof(buf), "%s", channel.name);
                else
                    snprintf(buf, sizeof(buf), "Channel %d", channelList[channelIndex]);
            }
        }

        display.drawTextCentered(display.width() / 2, 8,  "Select Channel");
        display.setColor(DisplayDriver::YELLOW);
        display.drawTextCentered(display.width() / 2, 28,
                                 back ? "Back" : (buf[0] ? buf : "No channel"));
        display.setColor(DisplayDriver::LIGHT);
        display.drawTextCentered(display.width() / 2, 46, "short press to scroll");
        display.drawTextCentered(display.width() / 2, 56, "long press to send");
    }

    void renderCannedPage(DisplayDriver& display) {
        display.setColor(DisplayDriver::GREEN);
        display.setTextSize(1);

        if (!selectingCanned && !selectingChannel && !composingCustom)
            renderCannedIdle(display);
        else if (composingCustom)
            renderCannedCompose(display);
        else if (selectingCanned)
            renderCannedSelectMessage(display);
        else if (selectingChannel)
            renderCannedSelectChannel(display);
    }

    // -- remaining pages -----------------------------------------------------

    void renderAdvertPage(DisplayDriver& display) {
        display.setColor(DisplayDriver::GREEN);
        display.drawXbm((display.width() - 32) / 2, 18, advert_icon, 32, 32);
        display.drawTextCentered(display.width() / 2, 64 - 11, "advert: " PRESS_LABEL);
    }

#if ENV_INCLUDE_GPS == 1
    void renderGPSPage(DisplayDriver& display) {
        LocationProvider* nmea = sensors.getLocationProvider();
        char buf[50];
        int y = 18;

        bool gps_state = _task->getGPSState();
#ifdef PIN_GPS_SWITCH
        bool hw_gps_state = digitalRead(PIN_GPS_SWITCH);
        if (gps_state != hw_gps_state)
            strcpy(buf, gps_state ? "gps off(hw)" : "gps off(sw)");
        else
            strcpy(buf, gps_state ? "gps on" : "gps off");
#else
        strcpy(buf, gps_state ? "gps on" : "gps off");
#endif
        display.drawTextLeftAlign(0, y, buf);

        if (nmea == NULL) {
            display.drawTextLeftAlign(0, y + 12, "Can't access GPS");
            return;
        }

        display.drawTextRightAlign(display.width() - 1, y,
                                   nmea->isValid() ? "fix" : "no fix");
        y += 12;

        display.drawTextLeftAlign(0, y, "sat");
        sprintf(buf, "%d", nmea->satellitesCount());
        display.drawTextRightAlign(display.width() - 1, y, buf);
        y += 12;

        display.drawTextLeftAlign(0, y, "pos");
        sprintf(buf, "%.4f %.4f",
                nmea->getLatitude()  / 1000000.,
                nmea->getLongitude() / 1000000.);
        display.drawTextRightAlign(display.width() - 1, y, buf);
        y += 12;

        display.drawTextLeftAlign(0, y, "alt");
        sprintf(buf, "%.2f", nmea->getAltitude() / 1000.);
        display.drawTextRightAlign(display.width() - 1, y, buf);
    }
#endif // ENV_INCLUDE_GPS

#if UI_SENSORS_PAGE == 1
    void renderSensorsPage(DisplayDriver& display) {
        refresh_sensors();
        char buf[30], name[30];
        int y = 18;

        LPPReader r(sensors_lpp.getBuffer(), sensors_lpp.getSize());

        // Skip to scroll offset
        for (int i = 0; i < sensors_scroll_offset; i++) {
            uint8_t ch, type;
            r.readHeader(ch, type);
            r.skipData(type);
        }

        int rows = sensors_scroll ? UI_RECENT_LIST_SIZE : sensors_nb;
        for (int i = 0; i < rows; i++) {
            uint8_t ch, type;
            if (!r.readHeader(ch, type)) {  // wrap around
                r.reset();
                r.readHeader(ch, type);
            }

            float v;
            switch (type) {
                case LPP_GPS: {
                    float lat, lon, alt;
                    r.readGPS(lat, lon, alt);
                    strcpy(name, "gps");
                    sprintf(buf, "%.4f %.4f", lat, lon);
                    break;
                }
                case LPP_VOLTAGE:
                    r.readVoltage(v);
                    strcpy(name, "voltage");    sprintf(buf, "%6.2f", v); break;
                case LPP_CURRENT:
                    r.readCurrent(v);
                    strcpy(name, "current");    sprintf(buf, "%.3f",  v); break;
                case LPP_TEMPERATURE:
                    r.readTemperature(v);
                    strcpy(name, "temperature"); sprintf(buf, "%.2f",  v); break;
                case LPP_RELATIVE_HUMIDITY:
                    r.readRelativeHumidity(v);
                    strcpy(name, "humidity");   sprintf(buf, "%.2f",  v); break;
                case LPP_BAROMETRIC_PRESSURE:
                    r.readPressure(v);
                    strcpy(name, "pressure");   sprintf(buf, "%.2f",  v); break;
                case LPP_ALTITUDE:
                    r.readAltitude(v);
                    strcpy(name, "altitude");   sprintf(buf, "%.0f",  v); break;
                case LPP_POWER:
                    r.readPower(v);
                    strcpy(name, "power");      sprintf(buf, "%6.2f", v); break;
                default:
                    r.skipData(type);
                    strcpy(name, "unk");        buf[0] = '\0';            break;
            }

            display.setCursor(0, y);
            display.print(name);
            display.setCursor(display.width() - display.getTextWidth(buf) - 1, y);
            display.print(buf);
            y += 12;
        }

        if (sensors_scroll)
            sensors_scroll_offset = (sensors_scroll_offset + 1) % sensors_nb;
        else
            sensors_scroll_offset = 0;
    }
#endif // UI_SENSORS_PAGE

    void renderShutdownPage(DisplayDriver& display) {
        display.setColor(DisplayDriver::GREEN);
        display.setTextSize(1);
        if (_shutdown_init) {
            display.drawTextCentered(display.width() / 2, 34, "hibernating...");
        } else {
            display.drawXbm((display.width() - 32) / 2, 18, power_icon, 32, 32);
            display.drawTextCentered(display.width() / 2, 64 - 11, "hibernate:" PRESS_LABEL);
        }
    }

    // ========================================================================
    // Per-page INPUT helpers
    // ========================================================================

    // Returns true if the event was consumed.
    bool handleCannedInput(char c) {
        if (c == KEY_SELECT) {
            return handleCannedSelect();
        }
        if (c == KEY_ENTER || c == KEY_NEXT) {
            return handleCannedNext();
        }
        if (c == KEY_LEFT || c == KEY_PREV) {
            return handleCannedPrev();
        }
        return false;
    }

    bool handleCannedSelect() {
        // Idle → start selecting a message
        if (!selectingCanned && !selectingChannel && !composingCustom) {
            selectingCanned = true;
            cannedIndex = 0;
            return true;
        }

        // Composing: act on the currently highlighted candidate
        if (composingCustom) {
            const char *candidate = customCandidates[customCandidateIndex];
            if (strcmp(candidate, "SEND") == 0) {
                if (customMessageLen == 0) {
                    _task->showAlert("Message empty", 1000);
                    return true;
                }
                pendingCustomMessage = true;
                composingCustom     = false;
                selectingCanned     = false;
                buildChannelList();
                selectingChannel    = true;
                return true;
            }
            if (strcmp(candidate, "DEL") == 0) {
                if (customMessageLen > 0)
                    customMessage[--customMessageLen] = '\0';
                return true;
            }
            if (strcmp(candidate, "BACK") == 0) {
                composingCustom      = false;
                pendingCustomMessage = false;
                selectingCanned      = false;
                selectingChannel     = false;
                return true;
            }
            // Append character
            if (customMessageLen < (int)sizeof(customMessage) - 1) {
                customMessage[customMessageLen++] = candidate[0];
                customMessage[customMessageLen]   = '\0';
            }
            return true;
        }

        // Selected a canned message → move to channel selection
        if (selectingCanned) {
            if (cannedIndex == NUM_CANNED_MESSAGES) {
                // "Custom" option chosen
                selectingCanned      = false;
                composingCustom      = true;
                pendingCustomMessage = false;
                customCandidateIndex = 0;
                customMessageLen     = 0;
                customMessage[0]     = '\0';
                return true;
            }
            selectingCanned      = false;
            pendingCustomMessage = false;
            buildChannelList();
            selectingChannel     = true;
            return true;
        }

        // Channel selected → send or go back
        if (selectingChannel) {
            if (channelIndex == channelListCount) {
                // "Back" entry selected
                selectingChannel = false;
                selectingCanned  = false;
                if (pendingCustomMessage)
                    composingCustom = true;
                pendingCustomMessage = false;
                return true;
            }
            trySendMessage();
            selectingChannel     = false;
            selectingCanned      = false;
            composingCustom      = false;
            pendingCustomMessage = false;
            return true;
        }

        return false;
    }

    void trySendMessage() {
        ChannelDetails channel;
        if (channelIndex < channelListCount &&
            the_mesh.getChannel(channelList[channelIndex], channel))
        {
            const char *msg    = pendingCustomMessage ? customMessage
                                                      : cannedMessages[cannedIndex].text;
            size_t      msgLen = pendingCustomMessage ? customMessageLen : strlen(msg);
            if (the_mesh.sendGroupMessage(rtc_clock.getCurrentTime(), channel.channel,
                                          the_mesh.getNodePrefs()->node_name,
                                          msg, msgLen))
                _task->showAlert("Message Sent", 1200);
            else
                _task->showAlert("Send Failed",  1200);
        }
    }

    bool handleCannedNext() {
        if (composingCustom) {
            customCandidateIndex = (customCandidateIndex + 1) % NUM_CUSTOM_CANDIDATES;
            return true;
        }
        if (selectingCanned) {
            if (cannedIndex >= NUM_CANNED_MESSAGES) cannedIndex = 0;
            else cannedIndex++;
            return true;
        }
        if (selectingChannel) {
            if (channelIndex >= channelListCount) channelIndex = 0;
            else channelIndex++;
            return true;
        }
        // Not in any sub-mode: navigate pages forward
        _page = (_page + 1) % HomePage::Count;
        if (_page == HomePage::RECENT)
            _task->showAlert("Recent adverts", 800);
        return true;
    }

    bool handleCannedPrev() {
        if (composingCustom) {
            customCandidateIndex = (customCandidateIndex == 0)
                                   ? NUM_CUSTOM_CANDIDATES - 1
                                   : customCandidateIndex - 1;
            return true;
        }
        if (selectingCanned) {
            cannedIndex = (cannedIndex == 0) ? NUM_CANNED_MESSAGES : cannedIndex - 1;
            return true;
        }
        if (selectingChannel) {
            channelIndex = (channelIndex == 0) ? channelListCount : channelIndex - 1;
            return true;
        }
        // Not in any sub-mode: navigate pages backward
        _page = (_page + HomePage::Count - 1) % HomePage::Count;
        return true;
    }

public:
    // ========================================================================
    // Constructor
    // ========================================================================
    HomeScreen(UITask* task, mesh::RTCClock* rtc,
               SensorManager* sensors, NodePrefs* node_prefs)
        : _task(task), _rtc(rtc), _sensors(sensors), _node_prefs(node_prefs),
          sensors_lpp(200) {}

    // ========================================================================
    // UIScreen interface
    // ========================================================================

    void poll() override {
        // Wait for the button to be released before actually shutting down.
        if (_shutdown_init && !_task->isButtonPressed())
            _task->shutdown();
    }

    int render(DisplayDriver& display) override {
        // ---- Common header -------------------------------------------------
        display.setTextSize(1);
        display.setColor(DisplayDriver::GREEN);
        char filtered_name[sizeof(_node_prefs->node_name)];
        display.translateUTF8ToBlocks(filtered_name,
                                      _node_prefs->node_name,
                                      sizeof(filtered_name));
        display.setCursor(0, 0);
        display.print(filtered_name);

        renderBatteryIndicator(display, _task->getBattMilliVolts());
        renderPageDots(display);

        // ---- Per-page body -------------------------------------------------
        switch (static_cast<HomePage>(_page)) {
            case FIRST:     renderFirstPage(display);     break;
            case PIN:       renderPinPage(display);       break;
            case RECENT:    renderRecentPage(display);    break;
            case RADIO:     renderRadioPage(display);     break;
            case BLUETOOTH: renderBluetoothPage(display); break;
            case CANNED:    renderCannedPage(display);    break;
            case ADVERT:    renderAdvertPage(display);    break;
#if ENV_INCLUDE_GPS == 1
            case GPS:       renderGPSPage(display);       break;
#endif
#if UI_SENSORS_PAGE == 1
            case SENSORS:   renderSensorsPage(display);   break;
#endif
            case SHUTDOWN:  renderShutdownPage(display);  break;
            default:        break;
        }

        return 5000;
    }

    bool handleInput(char c) override {
        // The CANNED page has its own navigation logic that intercepts
        // LEFT/NEXT/SELECT while in a sub-mode.
        if (_page == HomePage::CANNED)
            return handleCannedInput(c);

        // Global page navigation
        if (c == KEY_LEFT || c == KEY_PREV) {
            _page = (_page + HomePage::Count - 1) % HomePage::Count;
            return true;
        }
        if (c == KEY_NEXT || c == KEY_RIGHT) {
            _page = (_page + 1) % HomePage::Count;
            if (_page == HomePage::RECENT)
                _task->showAlert("Recent adverts", 800);
            return true;
        }

        // Page-specific ENTER/SELECT actions
        if (c == KEY_ENTER || c == KEY_SELECT) {
            if (_page == HomePage::FIRST) {
                _task->gotoInbox();
                return true;
            }
            if (_page == HomePage::BLUETOOTH) {
                if (_task->isSerialEnabled()) _task->disableSerial();
                else                          _task->enableSerial();
                return true;
            }
            if (_page == HomePage::ADVERT) {
                _task->notify(UIEventType::ack);
                if (the_mesh.advert()) _task->showAlert("Advert sent!",   1000);
                else                   _task->showAlert("Advert failed..", 1000);
                return true;
            }
#if ENV_INCLUDE_GPS == 1
            if (_page == HomePage::GPS) {
                _task->toggleGPS();
                return true;
            }
#endif
#if UI_SENSORS_PAGE == 1
            if (_page == HomePage::SENSORS) {
                _task->toggleGPS();
                next_sensors_refresh = 0;
                return true;
            }
#endif
            if (_page == HomePage::SHUTDOWN) {
                _shutdown_init = true;  // wait for button release in poll()
                return true;
            }
        }

        return false;
    }
};
