#include <WiFi.h>
#include <HTTPClient.h>
#include <ThingSpeak.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <Adafruit_MLX90614.h>
#include <TFT_eSPI.h>

// Pin definitions for ESP32
#define TFT_CS 5
#define TFT_RST 17
#define TFT_DC 15
#define TFT_MOSI 23
#define TFT_SCLK 18
#define BUZZER_PIN 22

// MLX90614 Pins
#define SDA_PIN 21
#define SCL_PIN 22

// WiFi Credentials
const char* ssid = "Arudchelvan's Galaxy A22 5G";
const char* password = "bbiu7407";

// ThingSpeak Configuration
unsigned long channelID = 2776369;
const char* writeAPIKey = "0O430J900EZDY24U";
const char* readAPIKey = "XFJ0EYVNX1WDPYQD";

// MLX90614 Sensor
Adafruit_MLX90614 mlx = Adafruit_MLX90614();
WiFiClient client;

// TFT Display
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

// Lookup Table for StO2
float lut_ratios[] = { 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0, 1.1, 1.2, 1.3, 1.4, 1.5 };
float lut_values[] = { 90, 85, 80, 75, 70, 65, 60, 55, 50, 45, 40, 35, 30 };

static int i = 0;
String cramp_status = "";  // Declared as a global variable

// Timing management
unsigned long lastThingSpeakUpdate = 0;
unsigned long lastCrampDataFetch = 0;
const unsigned long thingSpeakInterval = 30000;
const unsigned long crampFetchInterval = 60000;

// Function to calculate StO2 using LUT
float calculate_Sto2(float ratio) {
  for (int j = 0; j < sizeof(lut_ratios) / sizeof(lut_ratios[0]) - 1; j++) {
    if (ratio >= lut_ratios[j] && ratio <= lut_ratios[j + 1]) {
      float r1 = lut_ratios[j];
      float r2 = lut_ratios[j + 1];
      float s1 = lut_values[j];
      float s2 = lut_values[j + 1];
      return s1 + (s2 - s1) * (ratio - r1) / (r2 - r1);
    }
  }
  return -1;
}

// Buzzer patterns
void continuousBuzzer() {
  digitalWrite(BUZZER_PIN, HIGH);
}

void intermittentBuzzer(int delayMs) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(500);
  digitalWrite(BUZZER_PIN, LOW);
  delay(delayMs - 500);
}

void displayCenteredText(String text) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(3);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);

  int centerX = (tft.width() - w) / 2;
  int centerY = (tft.height() - h) / 2;

  tft.setCursor(centerX, centerY);
  tft.print(text);
}

void setup() {
  Serial.begin(9600);

  tft.initR(INITR_BLACKTAB);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(5);

  Wire.begin(SDA_PIN, SCL_PIN);
  if (!mlx.begin()) {
    Serial.println("Error: MLX90614 not detected. Check wiring.");
    tft.setCursor(10, 50);
    tft.println("MLX90614 Error!");
    while (true)
      ;
  }

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
  displayCenteredText("Connected to WiFi");
  ThingSpeak.begin(client);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
}

void loop() {
  unsigned long currentMillis = millis();
  Wire.begin(SDA_PIN, SCL_PIN);

  if (currentMillis - lastThingSpeakUpdate >= thingSpeakInterval) {
    lastThingSpeakUpdate = currentMillis;
    float ambientTemp = mlx.readAmbientTempC();
    float objectTemp = mlx.readObjectTempC();
    if (isnan(ambientTemp) || isnan(objectTemp)) {
      Serial.println("Error: MLX90614 returned NaN. Check connections.");
      tft.fillScreen(ST77XX_RED);
      tft.setCursor(10, 50);
      tft.println("Sensor Error!");
      return;
    }

    int emg_signal = analogRead(36);
    float normalized_emg = map(emg_signal, 0, 1023, 0, 100);

    int ac_red[] = { 80, 70, 60, 50, 40 };
    int dc_red[] = { 40, 30, 20, 10, 5 };
    int ac_ir[] = { 80, 60, 70, 80, 60 };
    int dc_ir[] = { 5, 15, 25, 35, 45 };

    float ratio = (float(ac_red[i]) / dc_red[i]) / (float(ac_ir[i]) / dc_ir[i]);
    ratio = constrain(ratio, 0.3, 1.5);
    float sto2 = calculate_Sto2(ratio);
    i = (i + 1) % 5;

    tft.fillScreen(ST77XX_BLACK);
    tft.setCursor(10, 10);
    tft.print("Object Temp: ");
    tft.println(objectTemp);
    tft.print("StO2: ");
    tft.println(sto2);

    ThingSpeak.setField(1, normalized_emg);
    ThingSpeak.setField(2, objectTemp);
    ThingSpeak.setField(3, sto2);
    if (ThingSpeak.writeFields(channelID, writeAPIKey) == 200) {
      Serial.println("Data sent.");
    } else {
      Serial.println("Error sending data.");
      displayCenteredText("Error sending data.");
    }

    if (currentMillis - lastCrampDataFetch >= crampFetchInterval) {
      lastCrampDataFetch = currentMillis;

      String url = "http://api.thingspeak.com/channels/" + String(channelID) + "/fields/4/last?api_key=" + readAPIKey;
      HTTPClient http;
      http.begin(url);
      int httpResponseCode = http.GET();

      if (httpResponseCode == 200) {
        cramp_status = http.getString();  // Update global variable
        Serial.println("Cramp data: " + cramp_status);
      } else {
        Serial.println("Error retrieving cramp data: " + String(httpResponseCode));
      }
      http.end();
    }
  }

  if (cramp_status == "1") {
    continuousBuzzer();
    displayCenteredText("Cramp detect- sevier");
  } else if (cramp_status.indexOf('%') != -1) {
    int crampPercentage = cramp_status.toInt();
    String myString = String(crampPercentage);
    if (crampPercentage > 90) {
      continuousBuzzer();
      displayCenteredText( myString);
    } else if (crampPercentage > 75) {
      intermittentBuzzer(1000);
      displayCenteredText( myString);
    } else if (crampPercentage > 50) {
      intermittentBuzzer(2000);
      displayCenteredText( myString);
    } else {
      digitalWrite(BUZZER_PIN, LOW);
      displayCenteredText( myString);
    }
  } else {
    digitalWrite(BUZZER_PIN, LOW);
    displayCenteredText("Normal...");
  }

  delay(1000);
}