#include "DHT.h"
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <time.h>

#define DHT_PIN 32
#define FOTO_POWER_PIN 15
#define DHT_TYPE DHT11
#define FOTO_ANALOG 33
#define SOIL_ANALOG 25
#define POMPA_PIN 4

#define WIFI_SSID "Your WiFi SSID"
#define WIFI_PASS "Your Wifi password"

#define API_KEY "Your firebase api key"
#define FIREBASE_PROJECT_ID "Your firebase project id"

#define USER_EMAIL "Your firebase user email"
#define USER_PASSWORD "Your firebase user password"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig fConfig;

//dth init 
DHT dht(DHT_PIN, DHT_TYPE);

void initWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Łączenie z WiFi ");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(". ");
    delay(200);
  }
  Serial.println();
  Serial.print("Połączono, IP: ");
  Serial.println(WiFi.localIP());
}

void initFirebase() {
  fConfig.api_key = API_KEY;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  fConfig.token_status_callback = tokenStatusCallback;

  Firebase.begin(&fConfig, &auth);
  Firebase.reconnectWiFi(true);
}

bool readSensors(float &temperature, float &humidity, int &light, int &soil) {
  temperature = dht.readTemperature();
  humidity = dht.readHumidity();
  light = analogRead(FOTO_ANALOG);
  soil = analogRead(SOIL_ANALOG);
  return !(isnan(temperature) || isnan(humidity));
}

void sendToFirestore(float t, float h, int light, int soil) {
  String docPath = "esp32_firestore/measurements";
  FirebaseJson content;

  content.set("fields/temperature/stringValue", String(t, 2));
  content.set("fields/air_humidity/stringValue", String(h, 2));
  content.set("fields/soil_humidity/stringValue", String(soil));
  content.set("fields/light/stringValue", String(light));

  // Pobranie aktualnego czasu
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char timeString[30];
    strftime(timeString, sizeof(timeString), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
    content.set("fields/time/timestampValue", String(timeString));
  } else {
    content.set("fields/time/timestampValue", "czas_nieznany");
  }

//  if (Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", docPath.c_str(), content.raw(), "temperature,air_humidity,soil_humidity,light,time")) {
  if (Firebase.Firestore.createDocument(&fbdo, FIREBASE_PROJECT_ID, "", "esp32_firestore/measurements/dev1", content.raw())) {
    Serial.println("Dane zapisane pomyślnie:");
    Serial.println(fbdo.payload());
  } else {
    Serial.print("Błąd zapisu: ");
    Serial.println(fbdo.errorReason());
  }
}

void controlPump(int light) {
  digitalWrite(POMPA_PIN, light > 2 ? HIGH : LOW);
}

void initTime() {
  configTime(0, 0, "pool.ntp.org"); // UTC
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    Serial.println("Oczekiwanie na czas NTP...");
    delay(500);
  }
  Serial.println("Czas NTP ustawiony.");
}


void setup() {
  Serial.begin(115200);
  
  pinMode(POMPA_PIN, OUTPUT); // ustaw pin do sterowania przełącznikiem
  digitalWrite(POMPA_PIN, HIGH); // domyślnie wyłącz pompę (przełącznik)

  pinMode(FOTO_POWER_PIN, OUTPUT); // zasilanie fotorezystora przez GPIO
  digitalWrite(FOTO_POWER_PIN, HIGH); // włączenie zasilania fotorezystora

  initWiFi();
  initTime();
  initFirebase();
  dht.begin();

  float temp, hum;
  int foto, soil;

  delay(200);

  if (readSensors(temp, hum, foto, soil)) {
    sendToFirestore(temp, hum, foto, soil);
    Serial.printf("Temperatura (C): %.2f C\nWilgotność (%%): %.2f %%\nŚwiatło (0-4095): %d\nGleba (0-4095): %d\n", temp, hum, foto, soil);
  } else {
    Serial.println("Błąd odczytu z DHT!");
  }

  controlPump(foto);

  digitalWrite(FOTO_POWER_PIN, LOW); // Wyłączanie zasilania fotorezystora
  
  Serial.println("Przechodzę w tryb głębokiego snu na 5 minut...");
  // Konwersja minut na mikrosekundy: 5 minut = 5 * 60 * 1 000 000
  esp_sleep_enable_timer_wakeup(1 * 60 * 1000000ULL);
  esp_deep_sleep_start();
}

void loop() {
  // Nigdy nie zostanie wywołana w trybie deep sleep
}
