#pragma once
#include <Arduino_DriveBus_Library.h>

enum class TouchEvent { None, Tap, LongPress, SwipeUp, SwipeDown, SwipeLeft, SwipeRight };

// Polls the CST816 and turns raw touches into events, in landscape coords.
// Call poll() every loop iteration (it is cheap when nothing is touching).
class TouchInput {
public:
    void begin(Arduino_IIC *driver) { _drv = driver; }
    TouchEvent poll();
    int16_t x() const { return _x; }  // landscape coords (0..447)
    int16_t y() const { return _y; }  // landscape coords (0..367)
    bool isDown() const { return _down; }

private:
    Arduino_IIC *_drv = nullptr;
    bool _down = false, _longFired = false;
    uint32_t _downAtMs = 0;
    int16_t _x = 0, _y = 0, _startX = 0, _startY = 0;
};
