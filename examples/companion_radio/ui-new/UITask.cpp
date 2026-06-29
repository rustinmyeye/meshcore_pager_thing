#include "UITask.h"
#include <string.h>
#include <helpers/TxtDataHelpers.h>
#include "../MyMesh.h"
#include "target.h"

// Screen implementations (header-only classes)
#include "SplashScreen.h"
#include "HomeScreen.h"
#include "InboxScreen.h"

#ifndef AUTO_OFF_MILLIS
  #define AUTO_OFF_MILLIS  15000   // 15 seconds
#endif

#ifdef PIN_STATUS_LED
  #define LED_ON_MILLIS      20
  #define LED_ON_MSG_MILLIS  200
  #define LED_CYCLE_MILLIS   4000
#endif

// ---------------------------------------------------------------------------
// Startup
// ---------------------------------------------------------------------------

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
    _display = display;
    _sensors = sensors;
    _auto_off = millis() + AUTO_OFF_MILLIS;

#if defined(PIN_USER_BTN)
    user_btn.begin();
#endif
#if defined(PIN_USER_BTN_ANA)
    analog_btn.begin();
#endif

    _node_prefs = node_prefs;

    if (_display != NULL)
        _display->turnOn();

#ifdef PIN_BUZZER
    buzzer.begin();
    buzzer.quiet(_node_prefs->buzzer_quiet);
    buzzer.startup();
#endif

#ifdef PIN_VIBRATION
    vibration.begin();
#endif

    ui_started_at  = millis();
    _alert_expiry  = 0;

    splash      = new SplashScreen(this);
    home        = new HomeScreen(this, &rtc_clock, sensors, node_prefs);
    msg_preview = new InboxScreen(this, &rtc_clock);
    setCurrScreen(splash);
}

// ---------------------------------------------------------------------------
// Alerts & notifications
// ---------------------------------------------------------------------------

void UITask::showAlert(const char* text, int duration_millis) {
    strcpy(_alert, text);
    _alert_expiry = millis() + duration_millis;
}

void UITask::notify(UIEventType t) {
#if defined(PIN_BUZZER)
    switch (t) {
        case UIEventType::contactMessage:
            buzzer.play("MsgRcv3:d=4,o=6,b=200:32e,32g,32b,16c7");
            break;
        case UIEventType::channelMessage:
            buzzer.play("kerplop:d=16,o=6,b=120:32g#,32c#");
            break;
        case UIEventType::ack:
            buzzer.play("ack:d=32,o=8,b=120:c");
            break;
        case UIEventType::roomMessage:
        case UIEventType::newContactMessage:
        case UIEventType::none:
        default:
            break;
    }
#endif

#ifdef PIN_VIBRATION
    if (t != UIEventType::none)
        vibration.trigger();
#endif
}

// ---------------------------------------------------------------------------
// Message tracking
// ---------------------------------------------------------------------------

void UITask::msgRead(int msgcount) {
    _msgcount = msgcount;
    if (msgcount == 0)
        gotoHomeScreen();
}

void UITask::newMsg(uint8_t path_len, const char* from_name,
                    const char* text, int msgcount) {
    _msgcount = msgcount;

    InboxScreen* inbox = (InboxScreen*)msg_preview;
    inbox->addMessage(path_len, from_name, text, rtc_clock.getCurrentTime());
    _unread_count = inbox->unreadCount();
    setCurrScreen(msg_preview);

    if (_display != NULL) {
        if (!_display->isOn() && !hasConnection())
            _display->turnOn();
        if (_display->isOn()) {
            _auto_off    = millis() + AUTO_OFF_MILLIS;
            _next_refresh = 100;
        }
    }
}

// ---------------------------------------------------------------------------
// Status LED
// ---------------------------------------------------------------------------

void UITask::userLedHandler() {
#ifdef PIN_STATUS_LED
    int cur_time = millis();
    if (cur_time > next_led_change) {
        if (led_state == 0) {
            led_state         = 1;
            last_led_increment = (_msgcount > 0) ? LED_ON_MSG_MILLIS : LED_ON_MILLIS;
            next_led_change    = cur_time + last_led_increment;
        } else {
            led_state       = 0;
            next_led_change = cur_time + LED_CYCLE_MILLIS - last_led_increment;
        }
        digitalWrite(PIN_STATUS_LED, led_state == LED_STATE_ON);
    }
#endif
}

// ---------------------------------------------------------------------------
// Screen management
// ---------------------------------------------------------------------------

void UITask::setCurrScreen(UIScreen* c) {
    curr         = c;
    _next_refresh = 100;
}

// ---------------------------------------------------------------------------
// Shutdown / restart
// ---------------------------------------------------------------------------

void UITask::shutdown(bool restart) {
#ifdef PIN_BUZZER
    buzzer.shutdown();
    uint32_t buzzer_timer = millis();
    while (buzzer.isPlaying() && (millis() - 2500) < buzzer_timer)
        buzzer.loop();
#endif

    if (restart) {
        _board->reboot();
    } else {
        _display->turnOff();
        radio_driver.powerOff();
        _board->powerOff();
    }
}

// ---------------------------------------------------------------------------
// Button helpers
// ---------------------------------------------------------------------------

bool UITask::isButtonPressed() const {
#ifdef PIN_USER_BTN
    return user_btn.isPressed();
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------

void UITask::loop() {
    char c = 0;

    // -- Input handling ------------------------------------------------------
#if UI_HAS_JOYSTICK
    {
        int ev = user_btn.check();
        if      (ev == BUTTON_EVENT_CLICK)      c = checkDisplayOn(KEY_ENTER);
        else if (ev == BUTTON_EVENT_LONG_PRESS)  c = handleLongPress(KEY_ENTER);

        ev = joystick_left.check();
        if      (ev == BUTTON_EVENT_CLICK)      c = checkDisplayOn(KEY_LEFT);
        else if (ev == BUTTON_EVENT_LONG_PRESS)  c = handleLongPress(KEY_LEFT);

        ev = joystick_right.check();
        if      (ev == BUTTON_EVENT_CLICK)      c = checkDisplayOn(KEY_RIGHT);
        else if (ev == BUTTON_EVENT_LONG_PRESS)  c = handleLongPress(KEY_RIGHT);

        ev = back_btn.check();
        if (ev == BUTTON_EVENT_TRIPLE_CLICK)
            c = handleTripleClick(KEY_SELECT);
    }
#elif defined(PIN_USER_BTN)
    {
        int ev = user_btn.check();
        if      (ev == BUTTON_EVENT_CLICK)        c = checkDisplayOn(KEY_NEXT);
        else if (ev == BUTTON_EVENT_LONG_PRESS)   c = handleLongPress(KEY_ENTER);
        else if (ev == BUTTON_EVENT_DOUBLE_CLICK) c = handleDoubleClick(KEY_PREV);
        else if (ev == BUTTON_EVENT_TRIPLE_CLICK) c = handleTripleClick(KEY_SELECT);
    }
#endif

#if defined(PIN_USER_BTN_ANA)
    if (abs((long)(millis() - _analogue_pin_read_millis)) > 10) {
        int ev = analog_btn.check();
        if      (ev == BUTTON_EVENT_CLICK)        c = checkDisplayOn(KEY_NEXT);
        else if (ev == BUTTON_EVENT_LONG_PRESS)   c = handleLongPress(KEY_ENTER);
        else if (ev == BUTTON_EVENT_DOUBLE_CLICK) c = handleDoubleClick(KEY_PREV);
        else if (ev == BUTTON_EVENT_TRIPLE_CLICK) c = handleTripleClick(KEY_SELECT);
        _analogue_pin_read_millis = millis();
    }
#endif

#if defined(BACKLIGHT_BTN)
    if (millis() > next_backlight_btn_check) {
        bool touch_state = digitalRead(PIN_BUTTON2);
#if defined(DISP_BACKLIGHT)
        digitalWrite(DISP_BACKLIGHT, !touch_state);
#elif defined(EXP_PIN_BACKLIGHT)
        expander.digitalWrite(EXP_PIN_BACKLIGHT, !touch_state);
#endif
        next_backlight_btn_check = millis() + 300;
    }
#endif

    // -- Dispatch input event ------------------------------------------------
    if (c != 0 && curr) {
        curr->handleInput(c);
        _auto_off    = millis() + AUTO_OFF_MILLIS;
        _next_refresh = 100;
    }

    // -- Peripherals ---------------------------------------------------------
    userLedHandler();

#ifdef PIN_BUZZER
    if (buzzer.isPlaying()) buzzer.loop();
#endif

    if (curr) curr->poll();

    // -- Display refresh -----------------------------------------------------
    if (_display != NULL && _display->isOn()) {
        if (millis() >= _next_refresh && curr) {
            _display->startFrame();
            int delay_millis = curr->render(*_display);

            if (millis() < _alert_expiry) {
                // Render alert popup overlay
                int y = _display->height() / 3;
                int p = _display->height() / 32;
                _display->setTextSize(1);
                _display->setColor(DisplayDriver::DARK);
                _display->fillRect(p, y, _display->width() - p * 2, y);
                _display->setColor(DisplayDriver::LIGHT);
                _display->drawRect(p, y, _display->width() - p * 2, y);
                _display->drawTextCentered(_display->width() / 2, y + p * 3, _alert);
                _next_refresh = _alert_expiry;
            } else {
                _next_refresh = millis() + delay_millis;
            }

            _display->endFrame();
        }

#if AUTO_OFF_MILLIS > 0
#ifdef KEEP_DISPLAY_ON_USB
        if (board.isExternalPowered())
            _auto_off = millis() + AUTO_OFF_MILLIS;
#endif
        if (millis() > _auto_off)
            _display->turnOff();
#endif
    }

    // -- Vibration -----------------------------------------------------------
#ifdef PIN_VIBRATION
    vibration.loop();
#endif

    // -- Low battery auto-shutdown -------------------------------------------
#ifdef AUTO_SHUTDOWN_MILLIVOLTS
    if (millis() > next_batt_chck) {
        uint16_t milliVolts = getBattMilliVolts();
        if (milliVolts > 0 && milliVolts < AUTO_SHUTDOWN_MILLIVOLTS) {
            if (!board.isExternalPowered()) {
                if (_display != NULL) {
                    _display->startFrame();
                    _display->setTextSize(2);
                    _display->setColor(DisplayDriver::RED);
                    _display->drawTextCentered(_display->width() / 2, 20, "Low Battery.");
                    _display->drawTextCentered(_display->width() / 2, 40, "Shutting Down!");
                    _display->endFrame();
                    if (!_display->isEink()) delay(3000);
                }
                shutdown();
            }
        }
        next_batt_chck = millis() + 8000;
    }
#endif
}

// ---------------------------------------------------------------------------
// Input event filters
// ---------------------------------------------------------------------------

char UITask::checkDisplayOn(char c) {
    if (_display != NULL) {
        if (!_display->isOn()) {
            _display->turnOn();
            c = 0;  // consume event — just turning the display on
        }
        _auto_off    = millis() + AUTO_OFF_MILLIS;
        _next_refresh = 0;
    }
    return c;
}

char UITask::handleLongPress(char c) {
    if (millis() - ui_started_at < 8000) {
        // Long press within 8 s of boot → CLI rescue mode
        the_mesh.enterCLIRescue();
        c = 0;
    } else if (c == KEY_ENTER) {
        c = KEY_SELECT;
    }
    return c;
}

char UITask::handleDoubleClick(char c) {
    MESH_DEBUG_PRINTLN("UITask: double-click triggered");
    checkDisplayOn(c);
    return c;
}

char UITask::handleTripleClick(char c) {
    MESH_DEBUG_PRINTLN("UITask: triple click triggered");
    checkDisplayOn(c);
    toggleBuzzer();
    return 0;  // consume event
}

// ---------------------------------------------------------------------------
// GPS
// ---------------------------------------------------------------------------

bool UITask::getGPSState() {
    if (_sensors != NULL) {
        int num = _sensors->getNumSettings();
        for (int i = 0; i < num; i++) {
            if (strcmp(_sensors->getSettingName(i), "gps") == 0)
                return !strcmp(_sensors->getSettingValue(i), "1");
        }
    }
    return false;
}

void UITask::toggleGPS() {
    if (_sensors == NULL) return;

    int num = _sensors->getNumSettings();
    for (int i = 0; i < num; i++) {
        if (strcmp(_sensors->getSettingName(i), "gps") != 0) continue;

        bool currently_on = strcmp(_sensors->getSettingValue(i), "1") == 0;
        _sensors->setSettingValue("gps", currently_on ? "0" : "1");
        _node_prefs->gps_enabled = currently_on ? 0 : 1;
        notify(UIEventType::ack);
        the_mesh.savePrefs();
        showAlert(_node_prefs->gps_enabled ? "GPS: Enabled" : "GPS: Disabled", 800);
        _next_refresh = 0;
        break;
    }
}

// ---------------------------------------------------------------------------
// Buzzer
// ---------------------------------------------------------------------------

void UITask::toggleBuzzer() {
#ifdef PIN_BUZZER
    if (buzzer.isQuiet()) {
        buzzer.quiet(false);
        notify(UIEventType::ack);
    } else {
        buzzer.quiet(true);
    }
    _node_prefs->buzzer_quiet = buzzer.isQuiet();
    the_mesh.savePrefs();
    showAlert(buzzer.isQuiet() ? "Buzzer: OFF" : "Buzzer: ON", 800);
    _next_refresh = 0;
#endif
}
