#include "ESP32QRCodeReader.h"

#include "freertos/idf_additions.h"
#include "quirc/quirc.h"
#include "Arduino.h"
#include "../debug_log.h"

namespace {

constexpr uint32_t kQrStatsLogEveryFrames = 30;
constexpr int kCrossSharpenKernelCenter = 7;
constexpr int kCrossSharpenKernelNeighbor = -1;

struct Transform
{
  int cw;
  int div;
  int off;
  const char *name;
};

constexpr Transform kDefaultTransform{8, 1, -32, "cw8_d1_o-32"};
constexpr Transform kFallbackNoCandidateTransform{10, 2, -64, "cw10_d2_o-64"};

const char *frameSizeName(framesize_t frameSize)
{
  switch (frameSize)
  {
    case FRAMESIZE_QQVGA:
      return "QQVGA";
    case FRAMESIZE_QVGA:
      return "QVGA";
    case FRAMESIZE_VGA:
      return "VGA";
    default:
      return "other";
  }
}

uint8_t clampToByte(int value)
{
  if (value < 0)
  {
    return 0;
  }
  if (value > 255)
  {
    return 255;
  }
  return static_cast<uint8_t>(value);
}

void copyBorders(const uint8_t *src, uint8_t *dst, int width, int height)
{
  if ((src == nullptr) || (dst == nullptr) || (width <= 0) || (height <= 0))
  {
    return;
  }

  memcpy(dst, src, width);
  if (height > 1)
  {
    const int lastRowStart = (height - 1) * width;
    memcpy(dst + lastRowStart, src + lastRowStart, width);
  }

  if (width == 1)
  {
    for (int y = 1; y < (height - 1); ++y)
    {
      const int rowStart = y * width;
      dst[rowStart] = src[rowStart];
    }
    return;
  }

  for (int y = 1; y < (height - 1); ++y)
  {
    const int rowStart = y * width;
    dst[rowStart] = src[rowStart];
    dst[rowStart + width - 1] = src[rowStart + width - 1];
  }
}

bool enqueueDecodeResults(ESP32QRCodeReader *self,
                          struct quirc *q,
                          int count,
                          uint32_t *successfulDecodeCount,
                          uint32_t *decodeFailureCount)
{
  bool decodedAnyValid = false;
  for (int i = 0; i < count; ++i)
  {
    struct quirc_code code;
    struct quirc_data data;
    quirc_decode_error_t err;

    quirc_extract(q, i, &code);
    err = quirc_decode(&code, &data);

    struct QRCodeData qrCodeData{};
    if (err)
    {
      (*decodeFailureCount)++;
      const char *error = quirc_strerror(err);
      const int len = strlen(error);
      for (int j = 0; j < len; ++j)
      {
        qrCodeData.payload[j] = error[j];
      }
      qrCodeData.valid = false;
      qrCodeData.payload[len] = '\0';
      qrCodeData.payloadLen = len;
    }
    else
    {
      (*successfulDecodeCount)++;
      qrCodeData.dataType = data.data_type;
      for (int j = 0; j < data.payload_len; ++j)
      {
        qrCodeData.payload[j] = data.payload[j];
      }
      qrCodeData.valid = true;
      qrCodeData.payload[data.payload_len] = '\0';
      qrCodeData.payloadLen = data.payload_len;
      decodedAnyValid = true;
    }

    xQueueSend(self->qrCodeQueue, &qrCodeData, (TickType_t)0);
  }

  return decodedAnyValid;
}

template <int CenterWeight>
inline int centerContribution(const uint8_t value)
{
  if constexpr (CenterWeight == 8)
  {
    return static_cast<int>(value) << 3;
  }
  else if constexpr (CenterWeight == 10)
  {
    const int promoted = static_cast<int>(value);
    return (promoted << 3) + (promoted << 1);
  }
  else
  {
    return CenterWeight * static_cast<int>(value);
  }
}

template <int Divisor>
inline int applyDivisor(const int value)
{
  if constexpr (Divisor == 1)
  {
    return value;
  }
  else if constexpr (Divisor == 2)
  {
    return value / 2;
  }
  else
  {
    return value / Divisor;
  }
}

template <int CenterWeight, int Divisor, int Offset>
void applyCrossKernelFixed(const uint8_t *src, uint8_t *dst, int width, int height)
{
  if ((src == nullptr) || (dst == nullptr) || (width <= 0) || (height <= 0))
  {
    return;
  }

  if ((width < 3) || (height < 3))
  {
    memcpy(dst, src, static_cast<size_t>(width) * static_cast<size_t>(height));
    return;
  }

  copyBorders(src, dst, width, height);

  for (int y = 1; y < (height - 1); ++y)
  {
    const uint8_t *__restrict__ prev = src + ((y - 1) * width);
    const uint8_t *__restrict__ cur = src + (y * width);
    const uint8_t *__restrict__ next = src + ((y + 1) * width);
    uint8_t *__restrict__ out = dst + (y * width);

    for (int x = 1; x < (width - 1); ++x)
    {
      int acc = centerContribution<CenterWeight>(cur[x]);
      acc -= static_cast<int>(cur[x - 1]);
      acc -= static_cast<int>(cur[x + 1]);
      acc -= static_cast<int>(prev[x]);
      acc -= static_cast<int>(next[x]);
      acc = applyDivisor<Divisor>(acc) + Offset;
      out[x] = clampToByte(acc);
    }
  }
}

} // namespace

void ESP32QRCodeReader::applyCrossSharpen7(const uint8_t *src, uint8_t *dst, int width, int height)
{
  applyCrossKernel(src, dst, width, height, kCrossSharpenKernelCenter, 1, 0);
}

void ESP32QRCodeReader::applyCrossKernel(const uint8_t *src, uint8_t *dst, int width, int height, int centerWeight, int divisor, int offset)
{
  if ((centerWeight == 8) && (divisor == 1) && (offset == -32))
  {
    applyCrossKernelFixed<8, 1, -32>(src, dst, width, height);
    return;
  }
  if ((centerWeight == 10) && (divisor == 2) && (offset == -64))
  {
    applyCrossKernelFixed<10, 2, -64>(src, dst, width, height);
    return;
  }
  if ((centerWeight == kCrossSharpenKernelCenter) && (divisor == 1) && (offset == 0))
  {
    applyCrossKernelFixed<kCrossSharpenKernelCenter, 1, 0>(src, dst, width, height);
    return;
  }

  if ((src == nullptr) || (dst == nullptr) || (width <= 0) || (height <= 0))
  {
    return;
  }

  if ((width < 3) || (height < 3))
  {
    memcpy(dst, src, static_cast<size_t>(width) * static_cast<size_t>(height));
    return;
  }

  copyBorders(src, dst, width, height);

  for (int y = 1; y < (height - 1); ++y)
  {
    const uint8_t *__restrict__ prev = src + ((y - 1) * width);
    const uint8_t *__restrict__ cur = src + (y * width);
    const uint8_t *__restrict__ next = src + ((y + 1) * width);
    uint8_t *__restrict__ out = dst + (y * width);

    for (int x = 1; x < (width - 1); ++x)
    {
      int acc = centerWeight * static_cast<int>(cur[x]);
      acc -= static_cast<int>(cur[x - 1]);
      acc -= static_cast<int>(cur[x + 1]);
      acc -= static_cast<int>(prev[x]);
      acc -= static_cast<int>(next[x]);
      acc = (acc / divisor) + offset;
      out[x] = clampToByte(acc);
    }
  }
}

ESP32QRCodeReader::ESP32QRCodeReader() : ESP32QRCodeReader(CAMERA_MODEL_AI_THINKER, FRAMESIZE_QVGA)
{
}

ESP32QRCodeReader::ESP32QRCodeReader(framesize_t frameSize) : ESP32QRCodeReader(CAMERA_MODEL_AI_THINKER, frameSize)
{
}

ESP32QRCodeReader::ESP32QRCodeReader(CameraPins pins) : ESP32QRCodeReader(pins, FRAMESIZE_QVGA)
{
}

ESP32QRCodeReader::ESP32QRCodeReader(CameraPins pins, framesize_t frameSize)
    : qrCodeTaskHandler(NULL), pins(pins), frameSize(frameSize), cameraConfig{}, qrCodeQueue(NULL)
{
  qrCodeQueue = xQueueCreate(10, sizeof(struct QRCodeData));
}

ESP32QRCodeReader::~ESP32QRCodeReader()
{
  end();
  if (qrCodeQueue != NULL)
  {
    vQueueDelete(qrCodeQueue);
    qrCodeQueue = NULL;
  }
}

QRCodeReaderSetupErr ESP32QRCodeReader::setup()
{
  if (!psramFound())
  {
    return SETUP_NO_PSRAM_ERROR;
  }

  cameraConfig.ledc_channel = LEDC_CHANNEL_0;
  cameraConfig.ledc_timer = LEDC_TIMER_0;
  cameraConfig.pin_d0 = pins.Y2_GPIO_NUM;
  cameraConfig.pin_d1 = pins.Y3_GPIO_NUM;
  cameraConfig.pin_d2 = pins.Y4_GPIO_NUM;
  cameraConfig.pin_d3 = pins.Y5_GPIO_NUM;
  cameraConfig.pin_d4 = pins.Y6_GPIO_NUM;
  cameraConfig.pin_d5 = pins.Y7_GPIO_NUM;
  cameraConfig.pin_d6 = pins.Y8_GPIO_NUM;
  cameraConfig.pin_d7 = pins.Y9_GPIO_NUM;
  cameraConfig.pin_xclk = pins.XCLK_GPIO_NUM;
  cameraConfig.pin_pclk = pins.PCLK_GPIO_NUM;
  cameraConfig.pin_vsync = pins.VSYNC_GPIO_NUM;
  cameraConfig.pin_href = pins.HREF_GPIO_NUM;
  cameraConfig.pin_sccb_sda = pins.SIOD_GPIO_NUM;
  cameraConfig.pin_sccb_scl = pins.SIOC_GPIO_NUM;
  cameraConfig.pin_pwdn = pins.PWDN_GPIO_NUM;
  cameraConfig.pin_reset = pins.RESET_GPIO_NUM;
  cameraConfig.xclk_freq_hz = 10000000;
  cameraConfig.pixel_format = PIXFORMAT_GRAYSCALE;
  cameraConfig.frame_size = frameSize;
  cameraConfig.jpeg_quality = 15;
  cameraConfig.fb_count = 1;

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // camera init
  esp_err_t err = esp_camera_init(&cameraConfig);
  if (err != ESP_OK)
  {
    return SETUP_CAMERA_INIT_ERROR;
  }
  return SETUP_OK;
}

void dumpData(const struct quirc_data *data)
{
  Serial.printf("Version: %d\n", data->version);
  Serial.printf("ECC level: %c\n", "MLHQ"[data->ecc_level]);
  Serial.printf("Mask: %d\n", data->mask);
  Serial.printf("Length: %d\n", data->payload_len);
  Serial.printf("Payload: %s\n", data->payload);
}

void qrCodeDetectTask(void *taskData)
{
  ESP32QRCodeReader *self = (ESP32QRCodeReader *)taskData;
  self->taskRunning = true;
  camera_config_t camera_config = self->cameraConfig;
  if (camera_config.frame_size > FRAMESIZE_SVGA)
  {
    if (self->debug)
    {
      Serial.println("Camera Size err");
    }
      self->taskRunning = false;
      vTaskDelete(NULL);
      return;
  }

  struct quirc *q = NULL;
  uint8_t *image = NULL;
  camera_fb_t *fb = NULL;

  uint16_t old_width = 0;
  uint16_t old_height = 0;
  uint32_t frameCounter = 0;
  uint32_t successfulDecodeCount = 0;
  uint32_t decodeFailureCount = 0;
  uint32_t noCodeFrameCount = 0;
  uint64_t totalCaptureUs = 0;
  uint64_t totalProcessUs = 0;

  Serial.printf("ESP32QRCodeReader: decoder task started frameSize=%s default=%s fallback=%s_on_no_candidate throttle=%lums.\n",
                frameSizeName(camera_config.frame_size),
                kDefaultTransform.name,
                kFallbackNoCandidateTransform.name,
                0UL);

  if (self->debug)
  {
    Serial.printf("begin to qr_recoginze\r\n");
  }
  q = quirc_new();
  if (q == NULL)
  {
    if (self->debug)
    {
      Serial.print("can't create quirc object\r\n");
    }
    self->taskRunning = false;
    vTaskDelete(NULL);
    return;
  }

  while (!self->stopRequested)
  {

    if (self->debug)
    {
      Serial.printf("alloc qr heap: %u\r\n", xPortGetFreeHeapSize());
      Serial.printf("uxHighWaterMark = %d\r\n", uxTaskGetStackHighWaterMark(NULL));
      Serial.print("begin camera get fb\r\n");
    }
    const uint32_t captureStartedAtUs = micros();
    fb = esp_camera_fb_get();
    const uint32_t captureElapsedUs = micros() - captureStartedAtUs;
    if (!fb)
    {
      if (self->debug)
      {
        Serial.println("Camera capture failed");
      }
      continue;
    }

    if (self->stopRequested)
    {
      esp_camera_fb_return(fb);
      fb = NULL;
      image = NULL;
      break;
    }

    if (old_width != fb->width || old_height != fb->height)
    {
      if (self->debug)
      {
        Serial.printf("Recognizer size change w h len: %d, %d, %d \r\n", fb->width, fb->height, fb->len);
        Serial.println("Resize the QR-code recognizer.");
        // Resize the QR-code recognizer.
      }
      if (quirc_resize(q, fb->width, fb->height) < 0)
      {
        if (self->debug)
        {
          Serial.println("Resize the QR-code recognizer err (cannot allocate memory).");
        }
        esp_camera_fb_return(fb);
        fb = NULL;
        image = NULL;
        continue;
      }
      else
      {
        old_width = fb->width;
        old_height = fb->height;
      }
    }

    const uint32_t processStartedAtUs = micros();
    bool foundInFrame = false;
    bool decodedValidInFrame = false;
    bool usedFallback = false;
    int lastCandidateCount = 0;

    image = quirc_begin(q, NULL, NULL);
    ESP32QRCodeReader::applyCrossKernel(fb->buf,
                                        image,
                                        fb->width,
                                        fb->height,
                                        kDefaultTransform.cw,
                                        kDefaultTransform.div,
                                        kDefaultTransform.off);
    quirc_end(q);

    lastCandidateCount = quirc_count(q);
    if (lastCandidateCount > 0)
    {
      foundInFrame = true;
      decodedValidInFrame = enqueueDecodeResults(self,
                                                 q,
                                                 lastCandidateCount,
                                                 &successfulDecodeCount,
                                                 &decodeFailureCount);
    }

    if (!decodedValidInFrame && lastCandidateCount == 0)
    {
      usedFallback = true;
      image = quirc_begin(q, NULL, NULL);
      ESP32QRCodeReader::applyCrossKernel(fb->buf,
                                          image,
                                          fb->width,
                                          fb->height,
                                          kFallbackNoCandidateTransform.cw,
                                          kFallbackNoCandidateTransform.div,
                                          kFallbackNoCandidateTransform.off);
      quirc_end(q);

      lastCandidateCount = quirc_count(q);
      if (lastCandidateCount > 0)
      {
        foundInFrame = true;
        decodedValidInFrame = enqueueDecodeResults(self,
                                                   q,
                                                   lastCandidateCount,
                                                   &successfulDecodeCount,
                                                   &decodeFailureCount);
      }
    }

    if (!foundInFrame)
    {
      noCodeFrameCount++;
    }

    frameCounter++;
    totalCaptureUs += captureElapsedUs;
    totalProcessUs += micros() - processStartedAtUs;
    if ((frameCounter % kQrStatsLogEveryFrames) == 0 || foundInFrame)
    {
      Serial.printf("ESP32QRCodeReader: stats frames=%lu success=%lu decode_fail=%lu no_code=%lu last_candidates=%d fallback=%d avg_capture=%.1fms avg_process=%.1fms size=%ux%u.\n",
                    static_cast<unsigned long>(frameCounter),
                    static_cast<unsigned long>(successfulDecodeCount),
                    static_cast<unsigned long>(decodeFailureCount),
                    static_cast<unsigned long>(noCodeFrameCount),
                    lastCandidateCount,
                    usedFallback ? 1 : 0,
                    static_cast<double>(totalCaptureUs) / 1000.0 / frameCounter,
                    static_cast<double>(totalProcessUs) / 1000.0 / frameCounter,
                    old_width,
                    old_height);
    }

    esp_camera_fb_return(fb);
    fb = NULL;
    image = NULL;
  }

  if (fb)
  {
    esp_camera_fb_return(fb);
    fb = NULL;
  }
  quirc_destroy(q);
  Serial.printf("ESP32QRCodeReader: decoder task stopped frames=%lu success=%lu decode_fail=%lu no_code=%lu.\n",
                static_cast<unsigned long>(frameCounter),
                static_cast<unsigned long>(successfulDecodeCount),
                static_cast<unsigned long>(decodeFailureCount),
                static_cast<unsigned long>(noCodeFrameCount));
  self->qrCodeTaskHandler = NULL;
  self->taskRunning = false;
  if (self->begunWithCaps)
  {
    vTaskDeleteWithCaps(NULL);
  }
  else
  {
    vTaskDelete(NULL);
  }
}

void ESP32QRCodeReader::begin()
{
  (void)beginOnCore(0);
}

bool ESP32QRCodeReader::beginOnCore(BaseType_t core)
{
  if (!begun)
  {
    stopRequested = false;
    qrCodeTaskHandler = NULL;
    begunWithCaps = false;

    BaseType_t result = xTaskCreatePinnedToCoreWithCaps(qrCodeDetectTask,
                                                        "qrCodeDetectTask",
                                                        QR_CODE_READER_STACK_SIZE,
                                                        this,
                                                        QR_CODE_READER_TASK_PRIORITY,
                                                        &qrCodeTaskHandler,
                                                        core,
                                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (result == pdPASS)
    {
      begunWithCaps = true;
      begun = true;
      return true;
    }

    Serial.printf("ESP32QRCodeReader: PSRAM QR task creation failed (%ld), retrying with internal RAM stack.\n",
                  static_cast<long>(result));
    result = xTaskCreatePinnedToCore(qrCodeDetectTask,
                                     "qrCodeDetectTask",
                                     QR_CODE_READER_STACK_SIZE,
                                     this,
                                     QR_CODE_READER_TASK_PRIORITY,
                                     &qrCodeTaskHandler,
                                     core);
    if (result != pdPASS)
    {
      Serial.printf("ESP32QRCodeReader: QR task creation failed (%ld).\n",
                    static_cast<long>(result));
      qrCodeTaskHandler = NULL;
      begun = false;
      return false;
    }

    begun = true;
    return true;
  }

  return true;
}

bool ESP32QRCodeReader::receiveQrCode(struct QRCodeData *qrCodeData, long timeoutMs)
{
  return xQueueReceive(qrCodeQueue, qrCodeData, (TickType_t)pdMS_TO_TICKS(timeoutMs)) != 0;
}

void ESP32QRCodeReader::end()
{
  if (begun)
  {
    stopRequested = true;
    unsigned long waitStartedAt = millis();
    while (taskRunning && (millis() - waitStartedAt) < 1000)
    {
      vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (taskRunning && qrCodeTaskHandler != NULL)
    {
      Serial.println("ESP32QRCodeReader: forcing QR task shutdown after timeout.");
      if (begunWithCaps)
      {
        vTaskDeleteWithCaps(qrCodeTaskHandler);
      }
      else
      {
        vTaskDelete(qrCodeTaskHandler);
      }
      qrCodeTaskHandler = NULL;
      taskRunning = false;
    }
  }
  begun = false;
  begunWithCaps = false;
  stopRequested = false;
}

void ESP32QRCodeReader::setDebug(bool on)
{
  debug = on;
}
