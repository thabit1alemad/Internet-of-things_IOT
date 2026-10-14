
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <mbedtls/gcm.h>

// 1. Device Settings
const char* DEVICE_ID = "ESP32-A";  // Board 1: ESP32-A, Board 2: ESP32-B

const char* ssid = "thabit_123";
const char* password = "zz12345";

const char* mqttServer = "broker.hivemq.com";
const int mqttPort = 1883;
const char* mqttTopic = "secureiot/esp32/message";

// 2. AES-128-GCM
const unsigned char aesKey[16] = {
  0x31, 0x42, 0x53, 0x64, 0x75, 0x86, 0x97, 0xA8,
  0xB9, 0xCA, 0xDB, 0xEC, 0xFD, 0x0E, 0x1F, 0x20
};

const size_t NONCE_SIZE = 12;
const size_t TAG_SIZE = 16;
const size_t MAX_LEN = 20;

const unsigned long RETRY_MS = 2000;
const unsigned long ACK_TIMEOUT = 5000;

// 3. Hardware
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
LiquidCrystal_I2C lcd(0x27, 16, 2);

const byte ROWS = 4;
const byte COLS = 4;

char keys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

byte rowPins[ROWS] = {12, 13, 14,27};
byte colPins[COLS] = {2, 4, 5, 18};

Keypad keypad = Keypad(
  makeKeymap(keys), rowPins, colPins, ROWS, COLS
);

// 4. Variables
String message = "";
String receivedMessage = "";
String txDisplay = "TX:";
String lastSentNonceHex = "";

bool wifiConnecting = false;
bool wifiWasConnected = false;
bool mqttWasConnected = false;
bool awaitingAck = false;
bool screenReady = false;

unsigned long wifiStart = 0;
unsigned long lastWiFiAttempt = 0;
unsigned long lastMQTTAttempt = 0;
unsigned long ackStart = 0;
unsigned long bootScreenStart = 0;

byte bootStage = 0;

// 5. LCD
void showLine(byte line, String text) {
  lcd.setCursor(0, line);
  lcd.print("                ");
  lcd.setCursor(0, line);
  lcd.print(text.substring(0, 16));
}

void updateRX() {
  if (screenReady) {
    showLine(0, "RX:" + receivedMessage);
  }
}

void updateTX() {
  if (screenReady) {
    showLine(1, txDisplay);
  }
}

void updateStartupScreen() {
  unsigned long now = millis();

  if (bootStage == 0 && now - bootScreenStart >= 1800) {
    showLine(0, "Welcome Toheab");
    showLine(1, "");
    bootStage = 1;
    bootScreenStart = now;
  } else if (bootStage == 1 &&
             now - bootScreenStart >= 1800) {
    screenReady = true;
    bootStage = 2;

    showLine(0, "RX:");
    showLine(1, "TX:");
  }
}

// 6. HEX Conversion
String toHex(const unsigned char* data, size_t len) {
  const char hex[] = "0123456789ABCDEF";
  String result;
  result.reserve(len * 2);

  for (size_t i = 0; i < len; i++) {
    result += hex[data[i] >> 4];
    result += hex[data[i] & 0x0F];
  }

  return result;
}

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

bool fromHex(const String& hex, unsigned char* output,
             size_t maxLen, size_t& outputLen) {
  if (hex.length() % 2 != 0) return false;

  outputLen = hex.length() / 2;
  if (outputLen > maxLen) return false;

  for (size_t i = 0; i < outputLen; i++) {
    int high = hexValue(hex[i * 2]);
    int low = hexValue(hex[i * 2 + 1]);

    if (high < 0 || low < 0) return false;

    output[i] = (high << 4) | low;
  }

  return true;
}

// 7. Wi-Fi Connection
void connectWiFi() {
  unsigned long now = millis();

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnecting = false;

    if (!wifiWasConnected) {
      wifiWasConnected = true;

      Serial.println("[WiFi] Connected");
      Serial.print("[WiFi] IP: ");
      Serial.println(WiFi.localIP());
    }

    return;
  }

  if (wifiWasConnected) {
    wifiWasConnected = false;
    Serial.println("[WiFi] Connection lost");
  }

  if (wifiConnecting && now - wifiStart >= 15000) {
    Serial.println("[WiFi] Timeout; retrying");
    WiFi.disconnect();
    wifiConnecting = false;
  }

  if (!wifiConnecting &&
      now - lastWiFiAttempt >= RETRY_MS) {
    lastWiFiAttempt = now;
    wifiStart = now;
    wifiConnecting = true;

    Serial.println("[WiFi] Connecting...");
    WiFi.begin(ssid, password);
  }
}

// 8. MQTT Connection
void connectMQTT() {
  if (WiFi.status() != WL_CONNECTED) {
    mqttWasConnected = false;
    return;
  }

  if (mqttClient.connected()) {
    if (!mqttWasConnected) {
      mqttWasConnected = true;
      Serial.println("[MQTT] Connected");
    }
    return;
  }

  mqttWasConnected = false;

  unsigned long now = millis();
  if (now - lastMQTTAttempt < RETRY_MS) return;

  lastMQTTAttempt = now;

  Serial.println("[MQTT] Connecting...");

  if (mqttClient.connect(DEVICE_ID)) {
    mqttWasConnected = true;
    Serial.println("[MQTT] Connected");

    if (mqttClient.subscribe(mqttTopic)) {
      Serial.println("[MQTT] Subscribed");
    } else {
      Serial.println("[MQTT] Subscribe failed");
    }
  } else {
    Serial.print("[MQTT] Connection failed, state=");
    Serial.println(mqttClient.state());
  }
}

// 9. Encrypt and Send
void encryptAndSend() {
  if (message.isEmpty()) return;

  if (!mqttClient.connected()) {
    txDisplay = "TX:MQTT offline";
    updateTX();
    Serial.println("[TX] MQTT disconnected");
    return;
  }

  if (awaitingAck) {
    txDisplay = "TX:Waiting...";
    updateTX();
    Serial.println("[TX] Previous message pending");
    return;
  }

  size_t len = message.length();

  if (len > MAX_LEN) {
    txDisplay = "TX:Too long";
    updateTX();
    return;
  }

  unsigned char nonce[NONCE_SIZE];
  unsigned char cipher[MAX_LEN];
  unsigned char tag[TAG_SIZE];

  for (size_t i = 0; i < NONCE_SIZE; i += 4) {
    uint32_t r = esp_random();

    for (size_t j = 0; j < 4 && i + j < NONCE_SIZE; j++) {
      nonce[i + j] = (r >> (j * 8)) & 0xFF;
    }
  }

  mbedtls_gcm_context ctx;
  mbedtls_gcm_init(&ctx);

  int rc = mbedtls_gcm_setkey(
    &ctx, MBEDTLS_CIPHER_ID_AES, aesKey, 128
  );

  if (rc == 0) {
    rc = mbedtls_gcm_crypt_and_tag(
      &ctx, MBEDTLS_GCM_ENCRYPT, len,
      nonce, NONCE_SIZE, NULL, 0,
      (const unsigned char*)message.c_str(),
      cipher, TAG_SIZE, tag
    );
  }

  mbedtls_gcm_free(&ctx);

  if (rc != 0) {
    Serial.println("[ENC] Encryption failed");
    txDisplay = "TX:Encrypt error";
    updateTX();
    return;
  }

  String nonceHex = toHex(nonce, NONCE_SIZE);

  String packet = String(DEVICE_ID) + "|" +
                  nonceHex + "|" +
                  toHex(cipher, len) + "|" +
                  toHex(tag, TAG_SIZE);

  // Wait for confirmation only if publish succeeds
  if (mqttClient.publish(mqttTopic, packet.c_str())) {
    lastSentNonceHex = nonceHex;
    awaitingAck = true;
    ackStart = millis();

    Serial.println("[TX] Encrypted packet:");
    Serial.println(packet);

    message = "";
    txDisplay = "TX:Waiting...";
    updateTX();
  } else {
    Serial.println("[TX] Publish failed");
    txDisplay = "TX:Send failed";
    updateTX();
  }
}

// 10. Receive and Decrypt
void callback(char* topic, byte* payload, unsigned int length) {
  if (length == 0 || length >= 256) {
    Serial.println("[RX] Invalid packet size");
    return;
  }

  char buffer[256];
  memcpy(buffer, payload, length);
  buffer[length] = '\0';

  String packet(buffer);

  // ACK format: ACK|receiver|originalSender|nonce
  if (packet.startsWith("ACK|")) {
    int a = packet.indexOf('|');
    int b = packet.indexOf('|', a + 1);
    int c = packet.indexOf('|', b + 1);

    if (a >= 0 && b >= 0 && c >= 0) {
      String originalSender = packet.substring(b + 1, c);
      String nonceHex = packet.substring(c + 1);

      if (originalSender == DEVICE_ID &&
          awaitingAck &&
          nonceHex == lastSentNonceHex) {
        awaitingAck = false;
        txDisplay = "TX:Received";
        updateTX();
        Serial.println("[ACK] Receiver verified message");
      }
    }

    return;
  }

  int a = packet.indexOf('|');
  int b = packet.indexOf('|', a + 1);
  int c = packet.indexOf('|', b + 1);

  if (a < 1 || b < 0 || c < 0) {
    Serial.println("[RX] Invalid packet");
    return;
  }

  String sender = packet.substring(0, a);
  String nonceHex = packet.substring(a + 1, b);
  String cipherHex = packet.substring(b + 1, c);
  String tagHex = packet.substring(c + 1);

  if (sender == DEVICE_ID) return;

  if (nonceHex.length() != NONCE_SIZE * 2 ||
      tagHex.length() != TAG_SIZE * 2 ||
      cipherHex.length() % 2 != 0 ||
      cipherHex.length() > MAX_LEN * 2) {
    Serial.println("[RX] Invalid data length");
    return;
  }

  unsigned char nonce[NONCE_SIZE];
  unsigned char cipher[MAX_LEN];
  unsigned char tag[TAG_SIZE];
  unsigned char plain[MAX_LEN + 1];

  size_t nonceLen = 0;
  size_t cipherLen = 0;
  size_t tagLen = 0;

  if (!fromHex(nonceHex, nonce, sizeof(nonce), nonceLen) ||
      !fromHex(cipherHex, cipher, sizeof(cipher), cipherLen) ||
      !fromHex(tagHex, tag, sizeof(tag), tagLen) ||
      nonceLen != NONCE_SIZE ||
      tagLen != TAG_SIZE) {
    Serial.println("[RX] HEX conversion failed");
    return;
  }

  mbedtls_gcm_context ctx;
  mbedtls_gcm_init(&ctx);

  int rc = mbedtls_gcm_setkey(
    &ctx, MBEDTLS_CIPHER_ID_AES, aesKey, 128
  );

  if (rc == 0) {
    rc = mbedtls_gcm_auth_decrypt(
      &ctx, cipherLen,
      nonce, NONCE_SIZE,
      NULL, 0,
      tag, TAG_SIZE,
      cipher, plain
    );
  }

  mbedtls_gcm_free(&ctx);

  if (rc != 0) {
    Serial.println("[SECURITY] Authentication failed");
    return;
  }

  plain[cipherLen] = '\0';
  receivedMessage = String((char*)plain);

  // Show received plaintext on the LCD, not Serial
  updateRX();
  Serial.print("[RX] Verified sender: ");
  Serial.println(sender);

  // Send ACK after successful authentication
  String ack = "ACK|" + String(DEVICE_ID) + "|" +
               sender + "|" + nonceHex;

  if (mqttClient.publish(mqttTopic, ack.c_str())) {
    Serial.println("[ACK] Confirmation sent");
  } else {
    Serial.println("[ACK] Confirmation failed");
  }
}

// 11. Setup
void setup() {
  Serial.begin(115200);

  Wire.begin(22, 21);
  lcd.init();
  lcd.backlight();

  showLine(0, "Secure IoT Sys");
  showLine(1, "");

  bootScreenStart = millis();

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setCallback(callback);
  mqttClient.setKeepAlive(30);
  mqttClient.setSocketTimeout(3);
  mqttClient.setBufferSize(256);

  Serial.println("=== Secure IoT System ===");
  Serial.print("[Device] ");
  Serial.println(DEVICE_ID);
}

// 12. Main Loop
void loop() {
  updateStartupScreen();

  connectWiFi();
  connectMQTT();

  if (mqttClient.connected()) {
    mqttClient.loop();
  }

  if (awaitingAck && millis() - ackStart >= ACK_TIMEOUT) {
    awaitingAck = false;
    txDisplay = "TX:Not received";
    updateTX();
    Serial.println("[ACK] No confirmation received");
  }

  if (!screenReady) return;

  char key = keypad.getKey();
  if (!key) return;

  if (key == '*') {
    message = "";
    txDisplay = "TX:";
    updateTX();
  } else if (key == '#') {
    encryptAndSend();
  } else if (((key >= '0' && key <= '9') ||
              (key >= 'A' && key <= 'D')) &&
             message.length() < MAX_LEN) {
    message += key;
    txDisplay = "TX:" + message;
    updateTX();
  }
}
