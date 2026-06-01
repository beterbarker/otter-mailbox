#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <mbedtls/base64.h>

#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMono9pt7b.h>

#include "../secrets.h"

#define EPD_CS   5
#define EPD_DC   22
#define EPD_RST  21
#define EPD_BUSY 4

GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT> display(
  GxEPD2_213_B74(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY)
);

String lastMessage = "";

const int BITMAP_WIDTH = 250;
const int BITMAP_HEIGHT = 122;
const int BITMAP_STRIDE = 32;
const int BITMAP_RAW_SIZE = BITMAP_STRIDE * BITMAP_HEIGHT;
uint8_t bitmapBuffer[BITMAP_RAW_SIZE];

const int MESSAGE_LEFT = 8;
const int MESSAGE_RIGHT = 250;
const int MESSAGE_START_Y = 58;
const int MESSAGE_MAX_Y = 120;
const int MESSAGE_LINE_HEIGHT = 24;
const int ASCII_CHAR_WIDTH = 14;
const int EMOJI_WIDTH = 18;

enum EmojiIcon {
  EMOJI_NONE,
  EMOJI_HEART,
  EMOJI_MAIL,
  EMOJI_STAR,
  EMOJI_SMILE,
  EMOJI_CRY,
  EMOJI_OTTER
};

struct MailboxPayload {
  String message;
  String changeKey;
  bool hasMessage = false;
  bool hasBitmap = false;
};

bool hasBytes(const String& msg, int index, const uint8_t* bytes, int length) {
  if (index + length > msg.length()) return false;

  for (int i = 0; i < length; i++) {
    if ((uint8_t)msg[index + i] != bytes[i]) return false;
  }

  return true;
}

int utf8TokenLength(uint8_t firstByte) {
  if ((firstByte & 0x80) == 0) return 1;
  if ((firstByte & 0xE0) == 0xC0) return 2;
  if ((firstByte & 0xF0) == 0xE0) return 3;
  if ((firstByte & 0xF8) == 0xF0) return 4;
  return 1;
}

EmojiIcon matchEmoji(const String& msg, int index, int& consumed) {
  static const uint8_t HEART[] = {0xE2, 0x9D, 0xA4};
  static const uint8_t VARIATION_16[] = {0xEF, 0xB8, 0x8F};
  static const uint8_t MAIL[] = {0xF0, 0x9F, 0x92, 0x8C};
  static const uint8_t STAR[] = {0xE2, 0xAD, 0x90};
  static const uint8_t SMILE[] = {0xF0, 0x9F, 0x98, 0x8A};
  static const uint8_t CRY[] = {0xF0, 0x9F, 0x98, 0xAD};
  static const uint8_t OTTER[] = {0xF0, 0x9F, 0xA6, 0xA6};

  consumed = 0;

  if (hasBytes(msg, index, HEART, 3)) {
    consumed = hasBytes(msg, index + 3, VARIATION_16, 3) ? 6 : 3;
    return EMOJI_HEART;
  }

  if (hasBytes(msg, index, MAIL, 4)) {
    consumed = 4;
    return EMOJI_MAIL;
  }

  if (hasBytes(msg, index, STAR, 3)) {
    consumed = hasBytes(msg, index + 3, VARIATION_16, 3) ? 6 : 3;
    return EMOJI_STAR;
  }

  if (hasBytes(msg, index, SMILE, 4)) {
    consumed = 4;
    return EMOJI_SMILE;
  }

  if (hasBytes(msg, index, CRY, 4)) {
    consumed = 4;
    return EMOJI_CRY;
  }

  if (hasBytes(msg, index, OTTER, 4)) {
    consumed = 4;
    return EMOJI_OTTER;
  }

  return EMOJI_NONE;
}

void wrapIfNeeded(int& x, int& y, int width) {
  if (x + width <= MESSAGE_RIGHT) return;

  x = MESSAGE_LEFT;
  y += MESSAGE_LINE_HEIGHT;
}

void drawHeartIcon(int x, int y) {
  int top = y - 17;
  display.fillCircle(x + 5, top + 6, 4, GxEPD_BLACK);
  display.fillCircle(x + 11, top + 6, 4, GxEPD_BLACK);
  display.fillTriangle(x + 1, top + 7, x + 15, top + 7, x + 8, top + 17, GxEPD_BLACK);
}

void drawMailIcon(int x, int y) {
  int top = y - 16;
  display.drawRect(x + 1, top + 3, 16, 11, GxEPD_BLACK);
  display.drawLine(x + 1, top + 3, x + 9, top + 10, GxEPD_BLACK);
  display.drawLine(x + 16, top + 3, x + 9, top + 10, GxEPD_BLACK);
  display.drawLine(x + 1, top + 14, x + 7, top + 9, GxEPD_BLACK);
  display.drawLine(x + 16, top + 14, x + 11, top + 9, GxEPD_BLACK);
  display.fillCircle(x + 11, top + 8, 1, GxEPD_BLACK);
  display.fillCircle(x + 14, top + 8, 1, GxEPD_BLACK);
  display.fillTriangle(x + 10, top + 9, x + 15, top + 9, x + 12, top + 12, GxEPD_BLACK);
}

void drawStarIcon(int x, int y) {
  int top = y - 17;
  display.fillTriangle(x + 8, top + 1, x + 11, top + 11, x + 5, top + 11, GxEPD_BLACK);
  display.fillTriangle(x + 8, top + 16, x + 11, top + 6, x + 5, top + 6, GxEPD_BLACK);
  display.fillTriangle(x + 1, top + 7, x + 15, top + 7, x + 8, top + 11, GxEPD_BLACK);
}

void drawSmileIcon(int x, int y) {
  int top = y - 17;
  display.drawCircle(x + 8, top + 9, 7, GxEPD_BLACK);
  display.fillCircle(x + 5, top + 7, 1, GxEPD_BLACK);
  display.fillCircle(x + 11, top + 7, 1, GxEPD_BLACK);
  display.drawPixel(x + 5, top + 11, GxEPD_BLACK);
  display.drawLine(x + 6, top + 12, x + 10, top + 12, GxEPD_BLACK);
  display.drawPixel(x + 11, top + 11, GxEPD_BLACK);
}

void drawCryIcon(int x, int y) {
  int top = y - 17;
  display.drawCircle(x + 8, top + 9, 7, GxEPD_BLACK);
  display.fillCircle(x + 5, top + 7, 1, GxEPD_BLACK);
  display.fillCircle(x + 11, top + 7, 1, GxEPD_BLACK);
  display.drawCircle(x + 8, top + 13, 2, GxEPD_BLACK);
  display.fillTriangle(x + 3, top + 9, x + 5, top + 13, x + 1, top + 13, GxEPD_BLACK);
}

void drawOtterIcon(int x, int y) {
  int top = y - 17;
  display.drawCircle(x + 8, top + 9, 7, GxEPD_BLACK);
  display.fillCircle(x + 3, top + 4, 2, GxEPD_BLACK);
  display.fillCircle(x + 13, top + 4, 2, GxEPD_BLACK);
  display.fillCircle(x + 6, top + 8, 1, GxEPD_BLACK);
  display.fillCircle(x + 10, top + 8, 1, GxEPD_BLACK);
  display.fillCircle(x + 8, top + 11, 2, GxEPD_BLACK);
  display.drawLine(x + 4, top + 12, x + 1, top + 11, GxEPD_BLACK);
  display.drawLine(x + 12, top + 12, x + 15, top + 11, GxEPD_BLACK);
}

void drawEmojiIcon(EmojiIcon icon, int x, int y) {
  switch (icon) {
    case EMOJI_HEART:
      drawHeartIcon(x, y);
      break;
    case EMOJI_MAIL:
      drawMailIcon(x, y);
      break;
    case EMOJI_STAR:
      drawStarIcon(x, y);
      break;
    case EMOJI_SMILE:
      drawSmileIcon(x, y);
      break;
    case EMOJI_CRY:
      drawCryIcon(x, y);
      break;
    case EMOJI_OTTER:
      drawOtterIcon(x, y);
      break;
    default:
      break;
  }
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

    int consumed = 0;
    EmojiIcon icon = matchEmoji(msg, index, consumed);

    if (icon != EMOJI_NONE) {
      wrapIfNeeded(x, y, EMOJI_WIDTH);
      if (y >= MESSAGE_MAX_Y) break;

      drawEmojiIcon(icon, x, y);
      x += EMOJI_WIDTH;
      index += consumed;
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

    int tokenLength = utf8TokenLength(current);
    if (index + tokenLength > msg.length()) tokenLength = 1;

    index += tokenLength;
  }
}

void drawMessage(String msg) {
  display.setRotation(1);
  display.setTextColor(GxEPD_BLACK);

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    display.setFont(&FreeMono9pt7b);
    display.setCursor(8, 20);
    display.print("Otter Mail");

    display.drawLine(0, 28, 250, 28, GxEPD_BLACK);

    display.setFont(&FreeMonoBold12pt7b);
    renderMessageText(msg);

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
  if (!message) return false;

  mailbox.message = String(message);
  mailbox.hasMessage = true;
  mailbox.hasBitmap = readBitmapRender(doc["render"].as<JsonObject>());

  String sentAt = doc["sentAt"].as<String>();
  mailbox.changeKey = mailbox.message + "|" + sentAt + "|" + String(mailbox.hasBitmap ? "bitmap" : "text");

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

  if (fetchMailbox(mailbox) && mailbox.hasMessage) {
    lastMessage = mailbox.changeKey;
    drawMailboxPayload(mailbox);
  } else {
    drawMessage("No mail yet");
  }
}

void loop() {
  MailboxPayload mailbox;

  if (fetchMailbox(mailbox) && mailbox.hasMessage && mailbox.changeKey != lastMessage) {
    lastMessage = mailbox.changeKey;
    drawMailboxPayload(mailbox);
  }

  delay(10000);
}
