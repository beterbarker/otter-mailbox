#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <mbedtls/base64.h>

#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold12pt7b.h>

#include "../secrets.h"

#define EPD_CS   5
#define EPD_DC   22
#define EPD_RST  21
#define EPD_BUSY 4

GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT> display(
  GxEPD2_213_B74(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY)
);

const unsigned long REVEAL_DURATION_MS = 30000;
String lastRevealedMessageKey = "";
bool revealActive = false;
bool idleDrawn = false;
unsigned long revealStartedAt = 0;

const int BITMAP_WIDTH = 250;
const int BITMAP_HEIGHT = 122;
const int BITMAP_STRIDE = 32;
const int BITMAP_RAW_SIZE = BITMAP_STRIDE * BITMAP_HEIGHT;
uint8_t bitmapBuffer[BITMAP_RAW_SIZE];

const int MESSAGE_LEFT = 8;
const int MESSAGE_RIGHT = 250;
const int MESSAGE_START_Y = 24;
const int MESSAGE_MAX_Y = 122;
const int MESSAGE_LINE_HEIGHT = 24;
const int ASCII_CHAR_WIDTH = 14;

struct MailboxPayload {
  String message;
  String messageKey;
  bool hasMessage = false;
  bool hasBitmap = false;
  bool opened = true;
};

void wrapIfNeeded(int& x, int& y, int width) {
  if (x + width <= MESSAGE_RIGHT) return;

  x = MESSAGE_LEFT;
  y += MESSAGE_LINE_HEIGHT;
}

void renderMessageText(const String& msg) {
  int x = MESSAGE_LEFT;
  int y = MESSAGE_START_Y;
  int index = 0;

  while (index < msg.length() && y < MESSAGE_MAX_Y) {
    uint8_t current = (uint8_t)msg[index];

    if (current == '\r') {
      index++;
      continue;
    }

    if (current == '\n') {
      x = MESSAGE_LEFT;
      y += MESSAGE_LINE_HEIGHT;
      index++;
      continue;
    }

    if (current < 0x80) {
      if (current < 0x20) {
        index++;
        continue;
      }

      wrapIfNeeded(x, y, ASCII_CHAR_WIDTH);
      if (y >= MESSAGE_MAX_Y) break;

      display.setCursor(x, y);
      display.write(current);
      x = display.getCursorX();
      index++;
      continue;
    }

    index++;
    while (index < msg.length() && (((uint8_t)msg[index] & 0xC0) == 0x80)) {
      index++;
    }
  }
}

void drawMessage(String msg) {
  display.setRotation(1);
  display.setTextColor(GxEPD_BLACK);

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setFont(&FreeMonoBold12pt7b);
    renderMessageText(msg);
  } while (display.nextPage());
}

void drawIdleHeart(int x, int y) {
  display.fillCircle(x + 4, y + 4, 4, GxEPD_BLACK);
  display.fillCircle(x + 10, y + 4, 4, GxEPD_BLACK);
  display.fillTriangle(x, y + 5, x + 14, y + 5, x + 7, y + 15, GxEPD_BLACK);
}

void drawIdleEnvelope(int x, int y) {
  display.drawRect(x, y + 1, 26, 16, GxEPD_BLACK);
  display.drawLine(x, y + 1, x + 13, y + 10, GxEPD_BLACK);
  display.drawLine(x + 25, y + 1, x + 13, y + 10, GxEPD_BLACK);
  display.drawLine(x, y + 16, x + 9, y + 9, GxEPD_BLACK);
  display.drawLine(x + 25, y + 16, x + 17, y + 9, GxEPD_BLACK);
}

void drawIdleDecoration() {
  const int totalWidth = 78;
  const int startX = (BITMAP_WIDTH - totalWidth) / 2;
  const int y = 76;

  drawIdleHeart(startX, y + 1);
  drawIdleEnvelope(startX + 26, y);
  drawIdleHeart(startX + 64, y + 1);
}

void drawIdleScreen() {
  Serial.println("Drawing idle screen");

  display.setRotation(1);
  display.setTextColor(GxEPD_BLACK);

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setFont(&FreeMonoBold12pt7b);

    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds("Otter Mail", 0, 0, &x1, &y1, &w, &h);
    int titleX = (BITMAP_WIDTH - w) / 2;
    int titleY = 40 - y1;
    display.setCursor(titleX, titleY);
    display.print("Otter Mail");

    drawIdleDecoration();
  } while (display.nextPage());
}

void drawBitmapRender(const uint8_t* bitmap) {
  Serial.println("Drawing bitmap render");

  display.setRotation(1);
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.drawBitmap(0, 0, bitmap, BITMAP_WIDTH, BITMAP_HEIGHT, GxEPD_BLACK);
  } while (display.nextPage());
}

bool decodeBase64Bitmap(const char* data) {
  if (!data || data[0] == '\0') {
    Serial.println("Render decode failed: data missing");
    return false;
  }

  size_t decodedLength = 0;
  int result = mbedtls_base64_decode(
    bitmapBuffer,
    BITMAP_RAW_SIZE,
    &decodedLength,
    (const unsigned char*)data,
    strlen(data)
  );

  if (result != 0) {
    Serial.print("Render decode failed: base64 error ");
    Serial.println(result);
    return false;
  }

  if (decodedLength != BITMAP_RAW_SIZE) {
    Serial.print("Render decode failed: decoded bytes ");
    Serial.println(decodedLength);
    return false;
  }

  Serial.println("Render decode success");
  return true;
}

bool readBitmapRender(JsonObject render) {
  if (render.isNull()) {
    return false;
  }

  Serial.println("Render found");

  const char* type = render["type"] | "";
  const char* bitOrder = render["bitOrder"] | "";
  const char* encoding = render["encoding"] | "";
  const char* data = render["data"];

  bool valid =
    strcmp(type, "bitmap-1bpp") == 0 &&
    render["width"].as<int>() == BITMAP_WIDTH &&
    render["height"].as<int>() == BITMAP_HEIGHT &&
    render["stride"].as<int>() == BITMAP_STRIDE &&
    strcmp(bitOrder, "msb") == 0 &&
    strcmp(encoding, "base64") == 0 &&
    data &&
    data[0] != '\0';

  if (!valid) {
    Serial.println("Render invalid; falling back to text");
    return false;
  }

  Serial.println("Render valid");
  if (!decodeBase64Bitmap(data)) {
    Serial.println("Falling back to text");
    return false;
  }

  return true;
}

void drawMailboxPayload(const MailboxPayload& mailbox) {
  if (mailbox.hasBitmap) {
    drawBitmapRender(bitmapBuffer);
    return;
  }

  Serial.println("Falling back to text");
  drawMessage(mailbox.message);
}

void showIdleIfNeeded() {
  revealActive = false;

  if (idleDrawn) {
    return;
  }

  drawIdleScreen();
  idleDrawn = true;
}

void startMessageReveal(const MailboxPayload& mailbox) {
  Serial.println("Starting message reveal");
  lastRevealedMessageKey = mailbox.messageKey;
  revealStartedAt = millis();
  revealActive = true;
  idleDrawn = false;
  drawMailboxPayload(mailbox);
}

void updateDisplayState(const MailboxPayload& mailbox, bool fetched) {
  if (!fetched || !mailbox.hasMessage) {
    Serial.println("No valid message; showing idle");
    showIdleIfNeeded();
    return;
  }

  if (mailbox.opened) {
    Serial.println("Message already opened; showing idle");
    showIdleIfNeeded();
    return;
  }

  if (mailbox.messageKey != lastRevealedMessageKey) {
    startMessageReveal(mailbox);
    return;
  }

  if (revealActive && millis() - revealStartedAt >= REVEAL_DURATION_MS) {
    Serial.println("Reveal expired; returning to idle");
    showIdleIfNeeded();
  }
}

bool fetchMailbox(MailboxPayload& mailbox) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, FIREBASE_URL);

  int code = http.GET();

  if (code != 200) {
    Serial.print("HTTP error: ");
    Serial.println(code);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  Serial.print("Firebase payload bytes: ");
  Serial.println(payload.length());

  DynamicJsonDocument doc(9000);
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.print("JSON parse failed: ");
    Serial.println(error.c_str());
    return false;
  }

  const char* message = doc["message"];
  if (!message || message[0] == '\0') return false;

  mailbox.message = String(message);
  mailbox.hasMessage = true;
  mailbox.opened = doc["opened"] | true;
  mailbox.hasBitmap = readBitmapRender(doc["render"].as<JsonObject>());

  String sentAt = doc["sentAt"].as<String>();
  mailbox.messageKey = sentAt.length() > 0 ? sentAt : mailbox.message;

  return true;
}

String fetchMessage() {
  MailboxPayload mailbox;
  if (!fetchMailbox(mailbox) || !mailbox.hasMessage) {
    return "";
  }

  return mailbox.message;
}

void setup() {
  Serial.begin(115200);
  delay(500);

  SPI.begin(18, -1, 23, 5);

  display.init(115200);
  drawMessage("Connecting...");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected!");

  drawMessage("WiFi connected");

  MailboxPayload mailbox;
  bool fetched = fetchMailbox(mailbox);
  updateDisplayState(mailbox, fetched);
}

void loop() {
  MailboxPayload mailbox;
  bool fetched = fetchMailbox(mailbox);
  updateDisplayState(mailbox, fetched);

  delay(10000);
}
