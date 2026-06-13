#pragma once
#include <Arduino_GFX_Library.h>
#include <Arduino_DriveBus_Library.h>
#include <memory>

// Initializes display (CO5300 via QSPI), PSRAM canvas (landscape), expander,
// and CST816 touch — in the validated order. Halts with serial logging on
// unrecoverable failure.
void boardInit();

extern Arduino_Canvas *canvas;             // draw here, then canvas->flush()
extern Arduino_CO5300 *panel;              // for setBrightness only
extern std::unique_ptr<Arduino_IIC> Touch; // CST816
