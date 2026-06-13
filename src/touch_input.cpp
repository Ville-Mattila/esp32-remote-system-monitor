#include "touch_input.h"
#include "ui_theme.h"
#include "pin_config.h"

// Raw panel coords are portrait (x 0..367, y 0..447). Map to landscape using
// the runtime rotation (1 or 3) so a double-tap flip also flips the touch map.
static void mapToLandscape(int32_t rawX, int32_t rawY, int16_t &lx, int16_t &ly) {
    if (uiRotation == 1) {
        lx = (int16_t)rawY;
        ly = (int16_t)(LCD_WIDTH - 1 - rawX);
    } else {
        lx = (int16_t)(LCD_HEIGHT - 1 - rawY);
        ly = (int16_t)rawX;
    }
}

TouchEvent TouchInput::poll() {
    int32_t fingers = _drv->IIC_Read_Device_Value(
        _drv->Arduino_IIC_Touch::Value_Information::TOUCH_FINGER_NUMBER);
    bool reported = fingers > 0;

    // Debounce release: the CST816 can briefly report zero fingers during a
    // static hold; only treat the press as ended after 120 ms of silence.
    static uint32_t lastSeenMs = 0;
    if (reported) lastSeenMs = millis();
    bool touching = reported || (_down && millis() - lastSeenMs < 120);

    if (touching) {
        int32_t rx = _drv->IIC_Read_Device_Value(
            _drv->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_X);
        int32_t ry = _drv->IIC_Read_Device_Value(
            _drv->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_Y);
        mapToLandscape(rx, ry, _x, _y);
    }

    if (touching && !_down) {                    // press started
        _down = true; _longFired = false;
        _downAtMs = millis();
        _startX = _x; _startY = _y;
        return TouchEvent::None;
    }
    if (touching && _down) {                     // held
        int dx = abs(_x - _startX), dy = abs(_y - _startY);
        if (!_longFired && millis() - _downAtMs >= 2000 && dx < 20 && dy < 20) {
            _longFired = true;
            return TouchEvent::LongPress;
        }
        return TouchEvent::None;
    }
    if (!touching && _down) {                    // released
        _down = false;
        if (_longFired) return TouchEvent::None; // long-press already consumed it
        int dx = _x - _startX, dy = _y - _startY;
        uint32_t heldMs = millis() - _downAtMs;
        if (abs(dx) >= 40 && abs(dx) > abs(dy))
            return dx < 0 ? TouchEvent::SwipeLeft : TouchEvent::SwipeRight;
        if (abs(dy) >= 40) return dy < 0 ? TouchEvent::SwipeUp : TouchEvent::SwipeDown;
        if (heldMs <= 400 && abs(dx) < 16 && abs(dy) < 16) return TouchEvent::Tap;
    }
    return TouchEvent::None;
}
