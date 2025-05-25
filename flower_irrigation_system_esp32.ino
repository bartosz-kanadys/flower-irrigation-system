#include "DHT.h"
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <time.h>

//sensor power pins
#define FOTO_POWER_PIN 26
#define SOIL_POWER_PIN 14
#define DHT_POWER_PIN 4

#define POMP_PIN 27

//sensor data pins
#define DHT_PIN 32
#define DHT_TYPE DHT11
#define FOTO_ANALOG 33
#define SOIL_ANALOG 25


#define WIFI_SSID "Your WiFi SSID"
#define WIFI_PASS "Your WiFi password"

#define API_KEY "Your firebase api key"
#define FIREBASE_PROJECT_ID "Your firebase project id"

#define USER_EMAIL "Firebase user email"
#define USER_PASSWORD "Firebase user password"

#define TANK_CAPACITY 1000 // ml
// in 1 second pump take 25 ml

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

void sendSensorDataToFirestore(float t, float h, int light, int soil) {
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

  if (Firebase.Firestore.createDocument(&fbdo, FIREBASE_PROJECT_ID, "", "esp32_firestore/measurements/dev1", content.raw())) {
    Serial.println("Dane pomiarowe zapisane pomyślnie:");
    //Serial.println(fbdo.payload());
  } else {
    Serial.print("Błąd zapisu: ");
    Serial.println(fbdo.errorReason());
  }
}

void sendControlDataToFirestore(int water_condition, boolean pompRun) {
    FirebaseJson content;
    content.set("fields/water_condition/integerValue", water_condition);
    content.set("fields/user_run_pomp/booleanValue", false);

    if (Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", "esp32_firestore/measurements", content.raw(), "water_condition,user_run_pomp")) {
      Serial.println("Dane kontrolne zapisane pomyślnie:");
      Serial.println(fbdo.payload());
    } else {
      Serial.print("Błąd zapisu: ");
      Serial.println(fbdo.errorReason());
    }
}

void controlPump(int light, boolean run_pump, int &water_condition) {
  if((light < 100 || run_pump) && water_condition >= 50) {
    digitalWrite(POMP_PIN, LOW);
    delay(1000);
    digitalWrite(POMP_PIN, HIGH);
    water_condition  -=  25;
  }
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

void getDataFromFirebase(int &water_condition, boolean &run_pump){
  if (Firebase.Firestore.getDocument(&fbdo, FIREBASE_PROJECT_ID, "", "esp32_firestore/measurements")) {
    FirebaseJson payload;
    payload.setJsonData(fbdo.payload());

    FirebaseJsonData jsonData;
  
    if (payload.get(jsonData, "fields/water_condition/integerValue")) {
      water_condition = jsonData.to<int>();
    }
    if (payload.get(jsonData, "fields/user_run_pomp/booleanValue")) {
      run_pump = jsonData.to<bool>();
    }
  } else {
    Serial.print("Błąd pobierania: ");
    Serial.println(fbdo.errorReason());
  }
}

void startPinSetup(){
  pinMode(POMP_PIN, OUTPUT); // ustaw pin do sterowania przełącznikiem
  digitalWrite(POMP_PIN, HIGH); // domyślnie wyłącz pompę (przełącznik)

  pinMode(FOTO_POWER_PIN, OUTPUT); // pin zasilajacy czujnik na wyjscie
  digitalWrite(FOTO_POWER_PIN, HIGH); //stan wysoki na wyjscie

  pinMode(SOIL_POWER_PIN, OUTPUT); 
  digitalWrite(SOIL_POWER_PIN, HIGH); 

  pinMode(DHT_POWER_PIN, OUTPUT); 
  digitalWrite(DHT_POWER_PIN, HIGH); 
}


void setup() {
  Serial.begin(115200);
  
  startPinSetup();

  initWiFi();
  initTime();
  initFirebase();
  dht.begin();

  float temp, hum;
  int foto, soil, water_condition;
  boolean user_run_pump;

  getDataFromFirebase(water_condition, user_run_pump);
  Serial.println(water_condition);
  Serial.println(user_run_pump);

  delay(200);

  if (readSensors(temp, hum, foto, soil)) {
    sendSensorDataToFirestore(temp, hum, foto, soil);
    Serial.printf("Temperatura (C): %.2f C\nWilgotność (%%): %.2f %%\nŚwiatło (0-4095): %d\nGleba (0-4095): %d\n", temp, hum, foto, soil);
  } else {
    Serial.println("Błąd odczytu z DHT!");
  }

  controlPump(foto, user_run_pump, water_condition);

  sendControlDataToFirestore(water_condition, false);

  digitalWrite(FOTO_POWER_PIN, LOW); // Wyłączanie zasilania fotorezystora
  digitalWrite(SOIL_POWER_PIN, LOW);
  digitalWrite(DHT_POWER_PIN, LOW);
  
  Serial.println("Przechodzę w tryb głębokiego snu na 5 minut...");
  // Konwersja minut na mikrosekundy: 5 minut = 5 * 60 * 1 000 000
  esp_sleep_enable_timer_wakeup(10 * 60 * 1000000ULL);
  esp_deep_sleep_start();
}

void loop() {
  // Nigdy nie zostanie wywołana w trybie deep sleep
}
