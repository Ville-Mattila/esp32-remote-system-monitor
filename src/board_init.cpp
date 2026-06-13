#include "board_init.h"
#include <Wire.h>
#include "pin_config.h"
#include "ui_theme.h"

Arduino_Canvas *canvas = nullptr;
Arduino_CO5300 *panel = nullptr;
std::unique_ptr<Arduino_IIC> Touch;
uint8_t uiRotation = UI_ROTATION;  // set from saved config before boardInit()

static Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);

// TCA9554 expander: raw Wire writes (a legacy-I2C-driver library here aborts
// the boot on core 3.3+ — see README "sharp edges").
static const uint8_t TCA9554_ADDR = 0x20;
static void tca9554_write(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(TCA9554_ADDR);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

static void touchInterrupt() { Touch->IIC_Interrupt_Flag = true; }

void boardInit() {
    // 1. Expander first: touch reset is gated behind it.
    Wire.begin(IIC_SDA, IIC_SCL);
    tca9554_write(0x03, (uint8_t)~0x07);  // P0-P2 outputs
    tca9554_write(0x01, 0x00);            // pulse low
    delay(20);
    tca9554_write(0x01, 0x07);            // then high

    // 2. Release the bus BEFORE DriveBus re-inits it (double begin() wedges
    //    the new I2C driver with ESP_ERR_INVALID_STATE).
    Wire.end();
    delay(50);

    // 3. Touch (CST816 at 0x15 on this board revision).
    static std::shared_ptr<Arduino_IIC_DriveBus> iicBus =
        std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);
    Touch.reset(new Arduino_CST816x(iicBus, CST816T_DEVICE_ADDRESS,
                                    DRIVEBUS_DEFAULT_VALUE, TP_INT, touchInterrupt));
    while (Touch->begin() == false) {
        Serial.println("CST816x init failed, retrying...");
        delay(1000);
    }
    Serial.println("CST816x init OK");
    // Continuous reporting: without this the controller goes quiet while a
    // finger rests still, which breaks long-press detection.
    Touch->IIC_Write_Device_State(
        Touch->Arduino_IIC_Touch::Device::TOUCH_DEVICE_INTERRUPT_MODE,
        Touch->Arduino_IIC_Touch::Device_Mode::TOUCH_DEVICE_INTERRUPT_PERIODIC);

    // 4. Display + landscape canvas (frame buffer lands in PSRAM).
    panel = new Arduino_CO5300(bus, GFX_NOT_DEFINED, 0, LCD_WIDTH, LCD_HEIGHT,
                               16, 0, 0, 0);
    canvas = new Arduino_Canvas(LCD_WIDTH, LCD_HEIGHT, panel);
    if (!canvas->begin(GFX_NOT_DEFINED)) {
        Serial.println("FATAL: canvas->begin() failed (framebuffer alloc?)");
        while (true) delay(1000);
    }
    canvas->setRotation(uiRotation);  // 448x368 logical
    panel->setBrightness(UI_BRIGHTNESS);
    Serial.printf("canvas %dx%d, free heap %u, free psram %u\n",
                  canvas->width(), canvas->height(),
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
}
