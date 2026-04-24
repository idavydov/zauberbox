#ifndef ESP32_QR_CODE_ARDUINO_H_
#define ESP32_QR_CODE_ARDUINO_H_

#include "Arduino.h"
#include "ESP32CameraPins.h"
#include "esp_camera.h"

#ifndef QR_CODE_READER_STACK_SIZE
#define QR_CODE_READER_STACK_SIZE 40 * 1024
#endif

#ifndef QR_CODE_READER_TASK_PRIORITY
#define QR_CODE_READER_TASK_PRIORITY 5
#endif

enum QRCodeReaderSetupErr
{
  SETUP_OK,
  SETUP_NO_PSRAM_ERROR,
  SETUP_CAMERA_INIT_ERROR,
};

/* This structure holds the decoded QR-code data */
struct QRCodeData
{
  bool valid;
  int dataType;
  uint8_t payload[1024];
  int payloadLen;
};

class ESP32QRCodeReader
{
  friend void qrCodeDetectTask(void *taskData);

public:
  using RawFrameObserver = void (*)(void *context,
                                    const uint8_t *buffer,
                                    size_t length,
                                    uint16_t width,
                                    uint16_t height,
                                    uint32_t frameCounter);

private:
  TaskHandle_t qrCodeTaskHandler;
  CameraPins pins;
  framesize_t frameSize;
  volatile bool stopRequested = false;
  volatile bool taskRunning = false;
  RawFrameObserver rawFrameObserver = nullptr;
  void *rawFrameObserverContext = nullptr;

public:
  camera_config_t cameraConfig;
  QueueHandle_t qrCodeQueue;
  bool begun = false;
  bool begunWithCaps = false;
  bool debug = false;

  // Constructor
  ESP32QRCodeReader();
  ESP32QRCodeReader(CameraPins pins);
  ESP32QRCodeReader(CameraPins pins, framesize_t frameSize);
  ESP32QRCodeReader(framesize_t frameSize);
  ~ESP32QRCodeReader();

  // Setup camera
  QRCodeReaderSetupErr setup();

  void begin();
  bool beginOnCore(BaseType_t core);
  bool receiveQrCode(struct QRCodeData *qrCodeData, long timeoutMs);
  void end();

  void setDebug(bool);
  void setRawFrameObserver(RawFrameObserver observer, void *context);

  static void applyCrossSharpen7(const uint8_t *src, uint8_t *dst, int width, int height);
  static void applyCrossKernel(const uint8_t *src, uint8_t *dst, int width, int height, int centerWeight, int divisor = 1, int offset = 0);
};

#endif // ESP32_QR_CODE_ARDUINO_H_
