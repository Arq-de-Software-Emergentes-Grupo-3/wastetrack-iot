#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <math.h>

#define USE_API true

/*********  Wi-Fi  *********/
const char* ssid     = "Wokwi-GUEST";
const char* password = "";

/*********  API  ***********/
const char* signInUrl = "https://wastetrack-api.onrender.com/api/v1/auth/signin";
const char* userEmail = "alejandra@example.com";
const char* userPass  = "alejandra";

const char* CONTAINER_ID = "44G60A";
String capacityUrl = String("https://wastetrack-api.onrender.com/api/v1/containers/")
                   + CONTAINER_ID + "/capacity";

/*********  Umbrales y rangos  *********/
const float FILL_THRESHOLD  = 80.0;
const float CHANGE_EPSILON  = 5.0;
const float SENSOR_MIN_CM   =  2.0;
const float SENSOR_MAX_CM   = 400.0;

/*********  Pines  *********/
#define TRIG_PIN 5
#define ECHO_PIN 18

/*********  Variables globales  *********/
String authToken = "";
float lastReportedFill = -1.0;

/*********  Prototipos  *********/
void authenticate();
float measureFillPercent();
void sendCapacity(float fill);

/*********  SETUP  *********/
void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  Serial.printf("Conectando a Wi-Fi %s…\n", ssid);
  WiFi.begin(ssid, password, 6);
  while (WiFi.status() != WL_CONNECTED) { Serial.print('.'); delay(300); }
  Serial.println("\nWi-Fi OK");

#if USE_API
  authenticate();
#endif
}

/*********  LOOP  *********/
void loop() {

  float fill = measureFillPercent();
  Serial.printf("[INFO] Capacidad: %.1f %%\n", fill);

  bool fillChanged   = fabs(fill - lastReportedFill) >= CHANGE_EPSILON;
  bool crossedThresh = (fill >= FILL_THRESHOLD && lastReportedFill < FILL_THRESHOLD);

#if USE_API
  if (fillChanged || crossedThresh || lastReportedFill < 0) {
    sendCapacity(fill);
    lastReportedFill = fill;
  }
#else
  lastReportedFill = fill;
#endif

  delay(1000);
}

/*********  FUNCIONES  *********/

// Autenticación JWT
void authenticate() {
  if (WiFi.status() != WL_CONNECTED) return;

  StaticJsonDocument<128> j;
  j["email"] = userEmail;  j["password"] = userPass;
  String body;  serializeJson(j, body);

  WiFiClientSecure client;  client.setInsecure();
  HTTPClient http;
  http.begin(client, signInUrl);
  http.addHeader("Content-Type", "application/json");

  int code = http.POST(body);
  if (code == 200) {
    StaticJsonDocument<256> resp;
    deserializeJson(resp, http.getString());
    authToken = "Bearer " + String((const char*)resp["access_token"]);
    Serial.println("Auth OK");
  } else {
    Serial.printf("Auth FAIL (%d)\n", code);
  }
  http.end();
}

// Medir distancia y convertir a % de llenado
float measureFillPercent() {
  digitalWrite(TRIG_PIN, LOW);  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long dur = pulseIn(ECHO_PIN, HIGH, 30000);   // µs
  if (dur == 0) return 0.0;

  float dist = dur * 0.0343 / 2.0;             // cm
  float fill = (SENSOR_MAX_CM - dist) /
               (SENSOR_MAX_CM - SENSOR_MIN_CM) * 100.0;
  return constrain(fill, 0.0, 100.0);
}

// PUT /containers/{id}/capacity
void sendCapacity(float fill) {
  if (authToken == "") {
    Serial.println("[WARN] Sin token → re-auth");
    authenticate();
    if (authToken == "") return;
  }

  StaticJsonDocument<64> body;
  body["capacity"] = (int)round(fill);
  String payload;  
  serializeJson(body, payload);

  WiFiClientSecure client;  
  client.setInsecure();
  HTTPClient http;
  http.begin(client, capacityUrl);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", authToken);

  int code = http.PUT(payload);
  String resp = http.getString();
  Serial.printf("[API] code=%d body=%s\n", code, resp.c_str());
  if (code >= 200 && code < 300) {
    Serial.printf("[API] PUT OK (%.1f %%)\n", fill);
  } else if (code == 401) {
    Serial.println("[API] 401 → token expirado");
    authToken = "";
  } else {
    Serial.printf("[API] Error %d\n", code);
  }
  http.end();
}
