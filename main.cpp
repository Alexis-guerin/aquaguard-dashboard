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

// ========== WIFI ==========
constexpr char SSID[]     = "PiPro-31";
constexpr char PASSWORD[] = "motdepasse";

// ========== SUPABASE ==========
constexpr char SUPABASE_URL[] = "https://fhauqbpgrwunmzzeqylj.supabase.co";
constexpr char SUPABASE_KEY[] = "sb_publishable_UGdWt_CWd-jPWRj0_vegKA_gAIbNKb_";
constexpr char STATION_ID[]   = "plage_nord_01";

// ========== PINS ==========
constexpr uint8_t SDA_PIN        = 22;
constexpr uint8_t SCL_PIN        = 21;
constexpr uint8_t HUMIDITY_PIN   = 23;
constexpr uint8_t WATER_TEMP_PIN =  4;
constexpr uint8_t TDS_PIN        = 33;
// constexpr uint8_t BATTERY_PIN = 34;

// ========== SEUILS ==========
constexpr float TEMP_MAX       = 35.0f;
constexpr float ACCEL_MAX      = 15.0f;
constexpr float HUMIDITY_HIGH  = 80.0f;
constexpr float WATER_TEMP_MAX = 30.0f;
constexpr float TDS_HIGH       = 1000.0f;

// ========== TIMING ==========
constexpr unsigned long SEND_INTERVAL    = 5000;   // ms entre chaque envoi
constexpr unsigned long DS18B20_CONV_MS  = 750;    // temps conversion DS18B20
constexpr uint8_t       TDS_SAMPLES      = 10;     // moyennage ADC bruit ESP32
constexpr unsigned long WIFI_TIMEOUT_MS  = 15000;  // timeout reconnexion WiFi

// ========== INSTANCES ==========
Adafruit_MPU6050  mpu;
DHT               dht(HUMIDITY_PIN, DHT22);
OneWire           oneWire(WATER_TEMP_PIN);
DallasTemperature ds18b20(&oneWire);
HTTPClient        http;

// ========== STATE ==========
unsigned long lastSendTime    = 0;
unsigned long ds18b20ReqTime  = 0;
bool          ds18b20Pending  = false;

// ========================================================================
// ========== WIFI — avec reconnexion automatique ==========
// ========================================================================

bool ensureWiFi() {
    if (WiFi.status() == WL_CONNECTED) return true;

    Serial.print("🌐 Reconnexion WiFi");
    WiFi.reconnect();
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < WIFI_TIMEOUT_MS) {
        delay(500); Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println(" ✅ OK");
        return true;
    }
    Serial.println(" ❌ Échec");
    return false;
}

// ========================================================================
// ========== HTTP — fonction mutualisée ==========
// ========================================================================

bool postJSON(const char* endpoint, const String& payload) {
    if (!ensureWiFi()) return false;

    char url[128];
    snprintf(url, sizeof(url), "%s%s", SUPABASE_URL, endpoint);

    http.begin(url);
    http.addHeader("Content-Type",  "application/json");
    http.addHeader("apikey",        SUPABASE_KEY);
    http.addHeader("Authorization", "Bearer " + String(SUPABASE_KEY));
    http.addHeader("Prefer",        "return=minimal");

    int code    = http.POST(payload);
    bool ok     = (code == 201);
    if (!ok) Serial.printf("⚠️  HTTP %d : %s\n", code, http.getString().c_str());
    http.end();
    return ok;
}

// ========================================================================
// ========== LECTURE CAPTEURS ==========
// ========================================================================

// AM2302 — humidité
float readHumidity() {
    float h = dht.readHumidity();
    return isnan(h) ? (Serial.println("⚠️  AM2302 humidité"), -1.0f) : h;
}

// AM2302 — température ambiante
float readAM2302Temp() {
    float t = dht.readTemperature();
    return isnan(t) ? (Serial.println("⚠️  AM2302 température"), -1.0f) : t;
}

// DS18B20 — mode non-bloquant :
//   1) requestDS18B20() lance la conversion (sans attendre 750 ms)
//   2) readWaterTemperature() lit le résultat au cycle suivant
void requestDS18B20() {
    ds18b20.setWaitForConversion(false);  // non-bloquant
    ds18b20.requestTemperatures();
    ds18b20ReqTime = millis();
    ds18b20Pending = true;
}

float readWaterTemperature() {
    if (!ds18b20Pending) return -1.0f;
    if (millis() - ds18b20ReqTime < DS18B20_CONV_MS) return -1.0f; // pas encore prêt
    ds18b20Pending = false;
    float t = ds18b20.getTempCByIndex(0);
    return (t == DEVICE_DISCONNECTED_C) ? (Serial.println("⚠️  DS18B20"), -1.0f) : t;
}

// TDS — moyennage sur N échantillons (ADC ESP32 bruité)
float readConductivity() {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < TDS_SAMPLES; i++) {
        sum += analogRead(TDS_PIN);
        delay(2);
    }
    float voltage = ((sum / TDS_SAMPLES) / 4095.0f) * 3.3f;
    return voltage * 200.0f; // ⚠️ À calibrer
}

float readTDS() { return readConductivity() / 2.0f; }

// ========================================================================
// ========== ALERTES ==========
// ========================================================================

void sendAlert(const char* type, float value, const char* message) {
    Serial.printf("🚨 ALERTE : %s\n", type);
    StaticJsonDocument<192> doc;
    doc["station_id"]  = STATION_ID;
    doc["alert_type"]  = type;
    doc["alert_value"] = value;
    doc["message"]     = message;
    String payload;
    serializeJson(doc, payload);
    if (postJSON("/rest/v1/alerts", payload)) Serial.println("✅ Alerte envoyée");
}

void checkAlerts(const sensors_event_t& accel, const sensors_event_t& temp,
                 float humidity, float waterTemp, float tds) {
    char msg[80];

    if (temp.temperature > TEMP_MAX) {
        snprintf(msg, sizeof(msg), "Température critique : %.1f°C", temp.temperature);
        sendAlert("temperature_haute", temp.temperature, msg);
    }

    float mag = sqrtf(accel.acceleration.x * accel.acceleration.x +
                      accel.acceleration.y * accel.acceleration.y +
                      accel.acceleration.z * accel.acceleration.z);
    if (mag > ACCEL_MAX) {
        snprintf(msg, sizeof(msg), "Mouvement anormal : %.1f m/s²", mag);
        sendAlert("mouvement_anormal", mag, msg);
    }

    if (humidity >= 0 && humidity > HUMIDITY_HIGH) {
        snprintf(msg, sizeof(msg), "Humidité élevée : %.1f%%", humidity);
        sendAlert("humidite_elevee", humidity, msg);
    }

    if (waterTemp >= 0 && waterTemp > WATER_TEMP_MAX) {
        snprintf(msg, sizeof(msg), "Température eau élevée : %.1f°C", waterTemp);
        sendAlert("temp_eau_elevee", waterTemp, msg);
    }

    if (tds > TDS_HIGH) {
        snprintf(msg, sizeof(msg), "Qualité eau anormale - TDS : %.1f ppm", tds);
        sendAlert("conductivite_anormale", tds, msg);
    }
}

// ========================================================================
// ========== ENVOI SUPABASE ==========
// ========================================================================

bool sendToSupabase(const sensors_event_t& accel, const sensors_event_t& gyro,
                    const sensors_event_t& temp,
                    float humidity, float waterTemp, float conductivity, float tds) {
    StaticJsonDocument<512> doc;
    doc["station_id"]  = STATION_ID;
    doc["accel_x"]     = accel.acceleration.x;
    doc["accel_y"]     = accel.acceleration.y;
    doc["accel_z"]     = accel.acceleration.z;
    doc["gyro_x"]      = gyro.gyro.x  * 57.2958f;
    doc["gyro_y"]      = gyro.gyro.y  * 57.2958f;
    doc["gyro_z"]      = gyro.gyro.z  * 57.2958f;
    doc["temperature"] = temp.temperature;
    doc["latitude"]    = 45.793903f;
    doc["longitude"]   = -1.121994f;
    if (humidity  >= 0) doc["humidity"]          = humidity;
    if (waterTemp >= 0) doc["water_temperature"] = waterTemp;
    doc["conductivity"] = conductivity;
    doc["tds"]          = tds;
    // doc["ph"]           = ph;
    // doc["battery_level"] = batteryLevel;

    String payload;
    serializeJson(doc, payload);
    return postJSON("/rest/v1/sensor_data", payload);
}

// ========================================================================
// ========== SETUP ==========
// ========================================================================

void setup() {
    Serial.begin(115200);
    Serial.println("\n========================================");
    Serial.println("  MAVANS AQUA-GUARD - Démarrage");
    Serial.println("========================================\n");

    // AM2302
    dht.begin();
    Serial.println("💧 AM2302    → GPIO " + String(HUMIDITY_PIN));
    delay(2000); // stabilisation AM2302

    // DS18B20
    ds18b20.begin();
    Serial.printf("🌊 DS18B20   → GPIO %d (%d capteur(s))\n",
                  WATER_TEMP_PIN, ds18b20.getDeviceCount());

    // TDS
    pinMode(TDS_PIN, INPUT);
    Serial.println("⚡ TDS Meter → GPIO " + String(TDS_PIN));

    // WiFi
    Serial.print("🌐 Connexion WiFi");
    WiFi.begin(SSID, PASSWORD);
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < WIFI_TIMEOUT_MS) {
        delay(500); Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n✅ WiFi : " + WiFi.localIP().toString());
    } else {
        Serial.println("\n❌ WiFi échoué — le système continuera à tenter.");
    }

    // MPU6050
    Wire.begin(SDA_PIN, SCL_PIN);
    Serial.print("📊 MPU6050   → ");
    if (!mpu.begin()) {
        Serial.println("❌ Non détecté! SCL→22 SDA→21");
        while (1) delay(1000);
    }
    Serial.println("✅ OK");
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

    // Lance une première conversion DS18B20
    requestDS18B20();

    Serial.println("\n========================================");
    Serial.println("  Système opérationnel");
    Serial.println("========================================\n");
}

// ========================================================================
// ========== LOOP ==========
// ========================================================================

void loop() {
    unsigned long now = millis();

    if (now - lastSendTime >= SEND_INTERVAL) {
        lastSendTime = now;

        // Lecture capteurs
        sensors_event_t accel, gyro, temp;
        mpu.getEvent(&accel, &gyro, &temp);

        float humidity    = readHumidity();
        float am2302T     = readAM2302Temp();
        float waterTemp   = readWaterTemperature();   // lit résultat conversion précédente
        float conductivity = readConductivity();
        float tds          = readTDS();

        // Lance la prochaine conversion DS18B20 en arrière-plan
        requestDS18B20();

        // Affichage série
        Serial.println("─────────────────────────────────────");
        Serial.printf("⏰ %lums\n", now);
        Serial.printf("📊 Accel X=%.2f Y=%.2f Z=%.2f m/s²\n",
                      accel.acceleration.x, accel.acceleration.y, accel.acceleration.z);
        Serial.printf("🔄 Gyro  X=%.2f Y=%.2f Z=%.2f °/s\n",
                      gyro.gyro.x * 57.2958f, gyro.gyro.y * 57.2958f, gyro.gyro.z * 57.2958f);
        Serial.printf("🌡️  Temp MPU:    %.1f°C\n", temp.temperature);
        if (am2302T  >= 0) Serial.printf("🌡️  Temp AM2302: %.1f°C\n", am2302T);
        if (humidity >= 0) Serial.printf("💧 Humidité:    %.1f%%\n", humidity);
        else               Serial.println("💧 Humidité:    -- (erreur)");
        if (waterTemp >= 0) Serial.printf("🌊 Temp eau:    %.1f°C\n", waterTemp);
        else                Serial.println("🌊 Temp eau:    -- (erreur)");
        Serial.printf("⚡ Conductivité: %.1f µS/cm\n", conductivity);
        Serial.printf("📉 TDS:          %.1f ppm\n", tds);

        // Envoi + alertes
        if (sendToSupabase(accel, gyro, temp, humidity, waterTemp, conductivity, tds)) {
            Serial.println("✅ Données envoyées");
            checkAlerts(accel, temp, humidity, waterTemp, tds);
        } else {
            Serial.println("❌ Erreur Supabase");
        }

        Serial.println("─────────────────────────────────────\n");
    }

    delay(10); // cède le CPU au stack WiFi
}
