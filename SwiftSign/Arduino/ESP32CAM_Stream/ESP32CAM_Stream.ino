#include "esp_camera.h"
#include <WiFi.h>
#include "esp_http_server.h"

const char* ssid = "Chonny";
const char* password = "12345678";

// AI Thinker ESP32-CAM Pins
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5

#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

httpd_handle_t stream_httpd = NULL;

static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  char part_buf[64];

  res = httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");
  if (res != ESP_OK) return res;

  while (true) {
    fb = esp_camera_fb_get();

    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
    } else {
      size_t hlen = snprintf(
        part_buf,
        64,
        "\r\n--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
        fb->len
      );

      res = httpd_resp_send_chunk(req, part_buf, hlen);

      if (res == ESP_OK) {
        res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
      }

      if (res == ESP_OK) {
        res = httpd_resp_send_chunk(req, "\r\n", 2);
      }

      esp_camera_fb_return(fb);
    }

    if (res != ESP_OK) break;
  }

  return res;
}

void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;

  httpd_uri_t stream_uri = {
    .uri       = "/stream",
    .method    = HTTP_GET,
    .handler   = stream_handler,
    .user_ctx  = NULL
  };

  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(false);

  camera_config_t config;

  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;

  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;

  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;

  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;

  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Optimized for SwiftSign hand recognition:
  // VGA gives enough detail for fingers without too much lag.
  config.frame_size = FRAMESIZE_QVGA;   // 640x480
  config.jpeg_quality = 12;            // good balance of quality + speed
  config.fb_count = 1;                 // smoother stream if board supports it

  esp_err_t err = esp_camera_init(&config);

  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();

  // Orientation
  s->set_vflip(s, 1);        // fixes upside-down image

  // If the image is mirrored wrong, change this to 1.
  s->set_hmirror(s, 0);

  // Image tuning for hand landmarks
  s->set_brightness(s, 1);   // -2 to 2
  s->set_contrast(s, 1);     // -2 to 2
  s->set_saturation(s, 0);   // -2 to 2
  s->set_sharpness(s, 2);    // -2 to 2
  s->set_denoise(s, 1);

  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi Connected");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  startCameraServer();

  Serial.println();
  Serial.print("Stream URL: http://");
  Serial.print(WiFi.localIP());
  Serial.println("/stream");
}

void loop() {
  delay(10000);
}