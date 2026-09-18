#define BLYNK_TEMPLATE_ID "TMPL655Y65KWr"
#define BLYNK_TEMPLATE_NAME "SwiftSign"
#define BLYNK_AUTH_TOKEN "AvO4klvBs6NVbUUcMUBG9reMgtP8nfZN"

#include <WiFi.h>
#include <WebServer.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <DFRobotDFPlayerMini.h>

// WiFi
char ssid[] = "Chonny";
char pass[] = "12345678";

// OLED SH1107G 1.5 inch
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 128

Adafruit_SH1107 display = Adafruit_SH1107(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire
);

// Pins
#define PIR_PIN 27
#define TRIG_PIN 5
#define ECHO_PIN 18

// DFPlayer Mini Pins
#define DFPLAYER_RX 16
#define DFPLAYER_TX 17

HardwareSerial dfSerial(2);
DFRobotDFPlayerMini dfPlayer;
WebServer server(80);

// System State
String currentGesture = "NONE";
String currentText = "Waiting...";
float currentConfidence = 0.0;
int currentTrack = 0;

bool deviceEnabled = true;
bool audioEnabled = true;

int voiceMode = 0;
// 0 = Female/Base voice: tracks 0001-0008
// 1 = Male voice: tracks 0009-0016

unsigned long lastSensorUpdate = 0;
bool gestureDisplayed = false;

// OLED FACE FUNCTIONS

void drawCuteFace() {
  display.fillCircle(42, 35, 6, SH110X_WHITE);
  display.fillCircle(86, 35, 6, SH110X_WHITE);

  display.drawPixel(58, 58, SH110X_WHITE);
  display.drawPixel(59, 60, SH110X_WHITE);
  display.drawPixel(60, 62, SH110X_WHITE);
  display.drawPixel(62, 64, SH110X_WHITE);
  display.drawPixel(64, 65, SH110X_WHITE);
  display.drawPixel(66, 65, SH110X_WHITE);
  display.drawPixel(68, 64, SH110X_WHITE);
  display.drawPixel(70, 62, SH110X_WHITE);
  display.drawPixel(71, 60, SH110X_WHITE);
  display.drawPixel(72, 58, SH110X_WHITE);
}

void drawSadFace() {
  display.fillCircle(42, 35, 6, SH110X_WHITE);
  display.fillCircle(86, 35, 6, SH110X_WHITE);

  display.drawPixel(58, 66, SH110X_WHITE);
  display.drawPixel(60, 64, SH110X_WHITE);
  display.drawPixel(62, 62, SH110X_WHITE);
  display.drawPixel(64, 61, SH110X_WHITE);
  display.drawPixel(66, 61, SH110X_WHITE);
  display.drawPixel(68, 62, SH110X_WHITE);
  display.drawPixel(70, 64, SH110X_WHITE);
  display.drawPixel(72, 66, SH110X_WHITE);
}

void drawConfusedFace() {
  display.fillCircle(42, 35, 6, SH110X_WHITE);
  display.fillCircle(86, 35, 6, SH110X_WHITE);

  display.setTextSize(2);
  display.setCursor(59, 55);
  display.print("?");
}

void showStatus(String face, String status) {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  if (face == "-_-") {
    drawSadFace();
  } else if (face == "?_?") {
    drawConfusedFace();
  } else {
    drawCuteFace();
  }

  display.setTextSize(1);
  display.setCursor(15, 95);
  display.println(status);

  display.display();
}

void showPhrase(String face, String message) {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  drawCuteFace();

  display.setTextSize(1);
  display.setCursor(8, 92);

  if (message.length() <= 18) {
    display.println(message);
  } else {
    display.println(message.substring(0, 18));
    display.setCursor(8, 106);
    display.println(message.substring(18, 36));
  }

  display.display();
}

// SENSOR FUNCTIONS

float getDistanceCM() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);

  if (duration == 0) {
    return -1;
  }

  return duration * 0.034 / 2;
}

// AUDIO FUNCTIONS

int getVoiceTrack(int baseTrack) {
  if (baseTrack <= 0) return 0;

  if (voiceMode == 0) {
    return baseTrack;        // Female/Base: 0001-0008
  }

  return baseTrack + 8;      // Male: 0009-0016
}

void playAudioTrack(int baseTrack) {
  Serial.println("playAudioTrack called");

  if (!audioEnabled) {
    Serial.println("Audio disabled.");
    return;
  }

  int finalTrack = getVoiceTrack(baseTrack);

  Serial.print("Base Track: ");
  Serial.println(baseTrack);

  Serial.print("Final Track: ");
  Serial.println(finalTrack);

  dfPlayer.stop();
  delay(200);

  dfPlayer.volume(30);
  delay(100);

  dfPlayer.play(finalTrack);

  Serial.print("Playing Track: ");
  Serial.println(finalTrack);
}

// WEB SERVER HANDLERS

void handleRoot() {
  server.send(200, "text/plain", "SwiftSign Controller Online");
}

void handleGesture() {
  if (!deviceEnabled) {
    showStatus("-_-", "Device OFF");
    server.send(403, "text/plain", "Device is OFF");
    return;
  }

  if (server.hasArg("gesture")) {
    currentGesture = server.arg("gesture");
  }

  if (server.hasArg("text")) {
    currentText = server.arg("text");
  }

  if (server.hasArg("confidence")) {
    currentConfidence = server.arg("confidence").toFloat();
  }

  if (server.hasArg("track")) {
    currentTrack = server.arg("track").toInt();
  }

  if (currentGesture == "RESET") {
    gestureDisplayed = false;
    currentText = "Ready";

    Blynk.virtualWrite(V4, "Ready");
    showStatus("^_^", "Ready");

    Serial.println("Reset received");
    server.send(200, "text/plain", "Reset received");
    return;
  }

  if (currentGesture == "UNKNOWN" || currentText == "Unknown gesture") {
    Blynk.virtualWrite(V4, "Unknown gesture");
    showStatus("?_?", "Try again");
    server.send(200, "text/plain", "Unknown gesture");
    return;
  }

  Blynk.virtualWrite(V0, currentText);
  Blynk.virtualWrite(V1, currentConfidence);
  Blynk.virtualWrite(V4, "Gesture recognized");

  showPhrase(":D", currentText);
  gestureDisplayed = true;

  playAudioTrack(currentTrack);

  Serial.println("Gesture received:");
  Serial.print("Gesture: ");
  Serial.println(currentGesture);
  Serial.print("Text: ");
  Serial.println(currentText);
  Serial.print("Base track: ");
  Serial.println(currentTrack);
  Serial.print("Confidence: ");
  Serial.println(currentConfidence);

  server.send(200, "text/plain", "Gesture received: " + currentText);
}

// BLYNK HANDLERS

BLYNK_WRITE(V5) {
  deviceEnabled = param.asInt();

  if (deviceEnabled) {
    Blynk.virtualWrite(V4, "Device ON");
    gestureDisplayed = false;
    showStatus("^_^", "Device ON");
  } else {
    Blynk.virtualWrite(V4, "Device OFF");
    gestureDisplayed = false;
    showStatus("-_-", "Device OFF");
  }

  Serial.print("Device enabled: ");
  Serial.println(deviceEnabled);
}

BLYNK_WRITE(V6) {
  voiceMode = param.asInt();

  if (voiceMode == 0) {
    Blynk.virtualWrite(V4, "Voice: Female");
    showStatus("^_^", "Female voice");
  } else {
    Blynk.virtualWrite(V4, "Voice: Male");
    showStatus("^_^", "Male voice");
  }

  Serial.print("Voice mode: ");
  Serial.println(voiceMode);
}

BLYNK_WRITE(V7) {
  audioEnabled = param.asInt();

  if (audioEnabled) {
    Blynk.virtualWrite(V4, "Audio ON");
    showStatus("^_^", "Audio ON");
  } else {
    Blynk.virtualWrite(V4, "Text Only Mode");
    showStatus("^_^", "Text only");
  }

  Serial.print("Audio enabled: ");
  Serial.println(audioEnabled);
}

// SETUP

void setup() {
  Serial.begin(115200);

  pinMode(PIR_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  Wire.begin(21, 22);

  if (!display.begin(0x3C, true)) {
    Serial.println("OLED not found");
  }

  display.setRotation(0);

  showStatus("^_^", "Starting...");

  dfSerial.begin(9600, SERIAL_8N1, DFPLAYER_RX, DFPLAYER_TX);
  delay(1000);

  if (dfPlayer.begin(dfSerial, false, true)) {
    dfPlayer.volume(30);
    Serial.println("DFPlayer connected");
  } else {
    Serial.println("DFPlayer not detected");
    showStatus("-_-", "Audio error");
    delay(1000);
  }

  WiFi.begin(ssid, pass);
  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("Controller IP: ");
  Serial.println(WiFi.localIP());

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  server.on("/", handleRoot);
  server.on("/gesture", handleGesture);
  server.begin();

  Blynk.virtualWrite(V4, "Ready");
  showStatus("^_^", "Waiting");
}

// LOOP

void loop() {
  Blynk.run();
  server.handleClient();

  if (!deviceEnabled) {
    return;
  }

  if (gestureDisplayed) {
    return;
  }

  if (millis() - lastSensorUpdate > 1500) {
    lastSensorUpdate = millis();

    int motion = digitalRead(PIR_PIN);
    float distance = getDistanceCM();

    Blynk.virtualWrite(V2, motion);
    Blynk.virtualWrite(V3, distance);

    if (motion == HIGH) {
      if (distance > 0 && distance < 50) {
        Blynk.virtualWrite(V4, "Ready for gesture");
        showStatus("^_^", "Show gesture");
      } else {
        Blynk.virtualWrite(V4, "Move closer");
        showStatus("-_-", "Move closer");
      }
    } else {
      Blynk.virtualWrite(V4, "Waiting");
      showStatus("^_^", "Waiting");
    }
  }
}