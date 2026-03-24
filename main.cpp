#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ========== FONCTIONS GÉNÉRALES ==========
bool sendToSupabase(sensors_event_t &accel, sensors_event_t &gyro, sensors_event_t &temp, float humidity, float waterTemp);
void checkAlerts(sensors_event_t &accel, sensors_event_t &temp, float humidity, float waterTemp);
void sendAlert(String type, float value, String message);

// ========== CONFIGURATION WIFI ==========
const char* ssid     = "Livebox-LOGEN";
const char* password = "muriel3105";

// ========== CONFIGURATION SUPABASE ==========
const char* supabaseUrl = "https://fhauqbpgrwunmzzeqylj.supabase.co";
const char* supabaseKey = "sb_publishable_UGdWt_CWd-jPWRj0_vegKA_gAIbNKb_";
const String stationId  = "plage_nord_01";

// ========== PINS ==========
#define SDA_PIN        22
#define SCL_PIN        21
#define HUMIDITY_PIN   23   // AM2302 DATA → GPIO 23
#define WATER_TEMP_PIN  4   // DS18B20 DATA → GPIO 4  (pull-up 4.7kΩ ✅)
#define DHTTYPE DHT22

// ========== PINS FUTURS CAPTEURS ==========
// #define BATTERY_PIN   34
// #define TDS_SENSOR_PIN 33

// ========== INSTANCES CAPTEURS ==========
Adafruit_MPU6050 mpu;
DHT              dht(HUMIDITY_PIN, DHTTYPE);
OneWire          oneWire(WATER_TEMP_PIN);
DallasTemperature waterTempSensor(&oneWire);
HTTPClient       http;

// ========== SEUILS ALERTES ==========
const float TEMP_MAX       = 35.0;
const float ACCEL_MAX      = 15.0;
const float HUMIDITY_HIGH  = 80.0;
const float WATER_TEMP_MAX = 30.0;  // Alerte si eau > 30°C

// ========== TIMERS ==========
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 5000;

// ========================================================================
// ========== LECTURE AM2302 ==========
// ========================================================================

float readHumidity() {
    float h = dht.readHumidity();
    if (isnan(h)) {
        Serial.println("⚠️  Erreur lecture AM2302 (humidité) !");
        return -1.0;
    }
    return h;
}

float readAM2302Temp() {
    float t = dht.readTemperature();
    if (isnan(t)) {
        Serial.println("⚠️  Erreur lecture AM2302 (température) !");
        return -1.0;
    }
    return t;
}

// ========================================================================
// ========== LECTURE DS18B20 ==========
// ========================================================================

float readWaterTemperature() {
    waterTempSensor.requestTemperatures();
    float t = waterTempSensor.getTempCByIndex(0);
    if (t == DEVICE_DISCONNECTED_C) {
        Serial.println("⚠️  Erreur lecture DS18B20 (thermomètre eau) !");
        return -1.0;
    }
    return t;
}

// ========================================================================
// ========== FUTURS CAPTEURS (commentés) ==========
// ========================================================================

/*
// TODO: CAPTEUR BATTERIE
float readBatteryLevel() {
    int raw = analogRead(BATTERY_PIN);
    float voltage = (raw / 4095.0) * 3.3 * 2.0;
    float percentage = ((voltage - 3.0) / (4.2 - 3.0)) * 100.0;
    return constrain(percentage, 0, 100);
}
*/

/*
// TODO: CAPTEUR TDS/CONDUCTIVITÉ
float readConductivity() {
    int raw = analogRead(TDS_SENSOR_PIN);
    float voltage = (raw / 4095.0) * 3.3;
    return voltage * 200.0; // À CALIBRER
}
float readTDS() { return readConductivity() / 2.0; }
*/

// ========================================================================
// ========== SETUP ==========
// ========================================================================

void setup() {
    Serial.begin(115200);
    Serial.println("\n\n========================================");
    Serial.println("  MAVANS AQUA-GUARD - Démarrage");
    Serial.println("========================================\n");

    // ========== INITIALISATION AM2302 ==========
    dht.begin();
    Serial.println("💧 AM2302 initialisé sur GPIO " + String(HUMIDITY_PIN));
    delay(2000); // Délai de stabilisation AM2302

    // ========== INITIALISATION DS18B20 ==========
    waterTempSensor.begin();
    int nbSensors = waterTempSensor.getDeviceCount();
    if (nbSensors == 0) {
        Serial.println("⚠️  DS18B20 non détecté sur GPIO " + String(WATER_TEMP_PIN));
        Serial.println("   Vérifie : VCC → 3.3V | GND → GND | DATA → GPIO 4 + pull-up 4.7kΩ");
    } else {
        Serial.println("🌊 DS18B20 initialisé sur GPIO " + String(WATER_TEMP_PIN) +
                       " (" + nbSensors + " capteur(s) détecté(s))");
    }

    // ========== CONNEXION WIFI ==========
    Serial.print("🌐 Connexion WiFi");
    WiFi.begin(ssid, password);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        Serial.print(".");
        attempts++;
        if (attempts % 10 == 0) {
            Serial.printf("\n⏳ Toujours en attente... (%d s)\n", attempts / 2);
        }
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n✅ WiFi connecté!");
        Serial.print("📡 IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\n❌ Échec connexion WiFi!");
        Serial.println("→ SSID : \"" + String(ssid) + "\"");
        Serial.println("→ Hotspot actif ? Réseau 2.4 GHz ?");
        while(1) delay(1000);
    }

    // ========== INITIALISATION MPU6050 ==========
    Serial.print("\n📊 Initialisation MPU6050");
    Wire.begin(SDA_PIN, SCL_PIN);

    if (!mpu.begin()) {
        Serial.println(" ❌ ÉCHEC!");
        Serial.println("  SCL → GPIO 22 | SDA → GPIO 21");
        while (1) delay(1000);
    }

    Serial.println(" ✅ OK!");
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

    Serial.println("\n========================================");
    Serial.println("  Système opérationnel");
    Serial.println("========================================\n");

    delay(1000);
}

// ========================================================================
// ========== LOOP PRINCIPAL ==========
// ========================================================================

void loop() {
    unsigned long currentTime = millis();

    if (currentTime - lastSendTime >= sendInterval) {
        lastSendTime = currentTime;

        // ========== LECTURE CAPTEURS ==========
        sensors_event_t accel, gyro, temp;
        mpu.getEvent(&accel, &gyro, &temp);

        float humidity  = readHumidity();
        float am2302T   = readAM2302Temp();
        float waterTemp = readWaterTemperature();   // ✅ DS18B20

        // TODO: Décommenter quand capteurs disponibles
        // float batteryLevel = readBatteryLevel();
        // float conductivity = readConductivity();
        // float tds          = readTDS();

        // ========== AFFICHAGE SÉRIE ==========
        Serial.println("─────────────────────────────────────");
        Serial.printf("⏰ %lu ms\n", currentTime);
        Serial.printf("📊 Accel: X=%.2f Y=%.2f Z=%.2f m/s²\n",
                      accel.acceleration.x, accel.acceleration.y, accel.acceleration.z);
        Serial.printf("🔄 Gyro:  X=%.2f Y=%.2f Z=%.2f °/s\n",
                      gyro.gyro.x * 57.2958, gyro.gyro.y * 57.2958, gyro.gyro.z * 57.2958);
        Serial.printf("🌡️  Temp MPU:    %.1f°C\n", temp.temperature);
        if (am2302T   >= 0) Serial.printf("🌡️  Temp AM2302: %.1f°C\n", am2302T);
        if (humidity  >= 0) Serial.printf("💧 Humidité:    %.1f%%\n", humidity);
        else                Serial.println("💧 Humidité:    -- (erreur)");
        if (waterTemp >= 0) Serial.printf("🌊 Temp eau:    %.1f°C\n", waterTemp);
        else                Serial.println("🌊 Temp eau:    -- (erreur)");

        // TODO:
        // Serial.printf("🔋 Batterie:     %.1f%%\n", batteryLevel);
        // Serial.printf("⚡ Conductivité: %.1f µS/cm\n", conductivity);
        // Serial.printf("📉 TDS:          %.1f ppm\n", tds);

        // ========== ENVOI SUPABASE ==========
        if (sendToSupabase(accel, gyro, temp, humidity, waterTemp)) {
            Serial.println("✅ Données envoyées à Supabase");
            checkAlerts(accel, temp, humidity, waterTemp);
        } else {
            Serial.println("❌ Erreur envoi Supabase");
        }

        Serial.println("─────────────────────────────────────\n");
    }

    delay(100);
}

// ========================================================================
// ========== ENVOI DONNÉES À SUPABASE ==========
// ========================================================================

bool sendToSupabase(sensors_event_t &accel, sensors_event_t &gyro, sensors_event_t &temp,
                    float humidity, float waterTemp) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("❌ WiFi déconnecté!");
        return false;
    }

    StaticJsonDocument<768> doc;

    doc["station_id"]  = stationId;
    doc["accel_x"]     = accel.acceleration.x;
    doc["accel_y"]     = accel.acceleration.y;
    doc["accel_z"]     = accel.acceleration.z;
    doc["gyro_x"]      = gyro.gyro.x * 57.2958;
    doc["gyro_y"]      = gyro.gyro.y * 57.2958;
    doc["gyro_z"]      = gyro.gyro.z * 57.2958;
    doc["temperature"] = temp.temperature;
    doc["latitude"]    = 45.793903;
    doc["longitude"]   = -1.121994;

    if (humidity  >= 0) doc["humidity"]          = humidity;
    if (waterTemp >= 0) doc["water_temperature"] = waterTemp;  // ✅ DS18B20

    // TODO:
    // doc["battery_level"] = batteryLevel;
    // doc["conductivity"]  = conductivity;
    // doc["tds"]           = tds;

    String jsonString;
    serializeJson(doc, jsonString);

    http.begin(String(supabaseUrl) + "/rest/v1/sensor_data");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("apikey", supabaseKey);
    http.addHeader("Authorization", "Bearer " + String(supabaseKey));
    http.addHeader("Prefer", "return=minimal");

    int httpCode = http.POST(jsonString);
    bool success = (httpCode == 201);

    if (!success) {
        Serial.printf("⚠️  Code HTTP: %d\n", httpCode);
        Serial.println("Réponse: " + http.getString());
    }

    http.end();
    return success;
}

// ========================================================================
// ========== VÉRIFIER ALERTES ==========
// ========================================================================

void checkAlerts(sensors_event_t &accel, sensors_event_t &temp,
                 float humidity, float waterTemp) {
    // Température ambiante élevée
    if (temp.temperature > TEMP_MAX)
        sendAlert("temperature_haute", temp.temperature,
                  "Température critique : " + String(temp.temperature, 1) + "°C");

    // Mouvement anormal
    float mag = sqrt(
        accel.acceleration.x * accel.acceleration.x +
        accel.acceleration.y * accel.acceleration.y +
        accel.acceleration.z * accel.acceleration.z
    );
    if (mag > ACCEL_MAX)
        sendAlert("mouvement_anormal", mag,
                  "Mouvement anormal : " + String(mag, 1) + " m/s²");

    // Humidité élevée
    if (humidity >= 0 && humidity > HUMIDITY_HIGH)
        sendAlert("humidite_elevee", humidity,
                  "Humidité élevée : " + String(humidity, 1) + "%");

    // ✅ Température eau élevée (DS18B20)
    if (waterTemp >= 0 && waterTemp > WATER_TEMP_MAX)
        sendAlert("temp_eau_elevee", waterTemp,
                  "Température eau élevée : " + String(waterTemp, 1) + "°C");

    // TODO:
    // if (batteryLevel < BATTERY_LOW)
    //     sendAlert("batterie_faible", batteryLevel, "Batterie : " + String(batteryLevel,1) + "%");
    // if (tds > TDS_HIGH)
    //     sendAlert("conductivite_anormale", tds, "TDS : " + String(tds,1) + " ppm");
}

// ========================================================================
// ========== ENVOYER ALERTE ==========
// ========================================================================

void sendAlert(String type, float value, String message) {
    Serial.println("🚨 ALERTE : " + type);

    StaticJsonDocument<256> doc;
    doc["station_id"]  = stationId;
    doc["alert_type"]  = type;
    doc["alert_value"] = value;
    doc["message"]     = message;

    String jsonString;
    serializeJson(doc, jsonString);

    http.begin(String(supabaseUrl) + "/rest/v1/alerts");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("apikey", supabaseKey);
    http.addHeader("Authorization", "Bearer " + String(supabaseKey));
    http.addHeader("Prefer", "return=minimal");

    int httpCode = http.POST(jsonString);

    if (httpCode == 201) Serial.println("✅ Alerte envoyée!");
    else Serial.printf("❌ Erreur alerte (code %d)\n", httpCode);

    http.end();
}
