#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <ESP32Servo.h>

// WIFI
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// MQTT (HiveMQ)
const char* mqtt_server = "broker.hivemq.com";

WiFiClient espClient;
PubSubClient client(espClient);

// SENSOR PINS
#define DHT_PIN 4
#define DHTTYPE DHT22
#define PIR_PIN 14
#define LDR_PIN 34
#define TRIG_PIN 5
#define ECHO_PIN 18

// ACTUATOR PINS
#define LED_RED 25
#define LED_YELLOW 26
#define LED_GREEN 27
#define BUZZER 32
#define RELAY 13
#define SERVO_PIN 33

DHT dht(DHT_PIN, DHTTYPE);
Servo servo;

unsigned long lastMsg = 0;

// THRESHOLDS
#define TEMP_THRESHOLD  30.0   // °C  — red LED turns ON above this
#define LIGHT_THRESHOLD 2500   // ADC — yellow LED turns ON above this (higher ADC = darker)
#define DIST_THRESHOLD  20.0   // cm  — servo opens below this

// non-blocking buzzer state
bool          buzzerActive  = false;
unsigned long buzzerEndTime = 0;

// WIFI CONNECT
void connectWiFi() {

  Serial.print("Connecting WiFi");

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println(" Connected!");
}

// MQTT CALLBACK
void callback(char* topic, byte* payload, unsigned int length) {

  String message = "";

  for (int i = 0; i < length; i++)
    message += (char)payload[i];

  Serial.print("Message received: ");
  Serial.println(message);

  String t = String(topic);

  if (t == "actuators/led/red")
    digitalWrite(LED_RED, message == "ON");

  if (t == "actuators/led/yellow")
    digitalWrite(LED_YELLOW, message == "ON");

  if (t == "actuators/led/green")
    digitalWrite(LED_GREEN, message == "ON");

  if (t == "actuators/buzzer")
    digitalWrite(BUZZER, message == "ON");

  if (t == "actuators/relay")
    digitalWrite(RELAY, message == "ON");

  if (t == "actuators/servo") {
    int angle = message.toInt();
    servo.write(angle);
  }
}

// MQTT RECONNECT
void reconnect() {

  while (!client.connected()) {

    Serial.print("Connecting MQTT...");

    String clientId = "ESP32-" + String(random(1000));

    if (client.connect(clientId.c_str())) {

      Serial.println("connected");

      client.subscribe("actuators/led/red");
      client.subscribe("actuators/led/yellow");
      client.subscribe("actuators/led/green");
      client.subscribe("actuators/buzzer");
      client.subscribe("actuators/relay");
      client.subscribe("actuators/servo");

    } else {

      Serial.println("retrying...");
      delay(2000);
    }
  }
}

// ULTRASONIC
long readDistance() {

  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);

  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH);

  return duration * 0.034 / 2;
}

void setup() {

  Serial.begin(115200);

  pinMode(PIR_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(RELAY, OUTPUT);

  servo.attach(SERVO_PIN);
  servo.write(90);   // start in closed position

  dht.begin();

  connectWiFi();

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void loop() {

  // WIFI RECONNECT
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  // MQTT RECONNECT
  if (!client.connected()) {
    reconnect();
  }

  client.loop();

  // stop buzzer after 2 seconds
  if (buzzerActive && millis() >= buzzerEndTime) {
    noTone(BUZZER);
    buzzerActive = false;
    Serial.println("Buzzer OFF");
  }

  // SENSOR COLLECTION EVERY 2 SECONDS
  if (millis() - lastMsg > 2000) {

    lastMsg = millis();

    float temperature = dht.readTemperature();
    float humidity    = dht.readHumidity();

    int  motion   = digitalRead(PIR_PIN);
    int  light    = analogRead(LDR_PIN);
    long distance = readDistance();

    // SERIAL DEBUG (F1)
    Serial.println("------ Sensor Data ------");

    Serial.print("Temperature: ");
    Serial.print(temperature);
    Serial.println(" °C");

    Serial.print("Humidity: ");
    Serial.print(humidity);
    Serial.println(" %");

    Serial.print("Light Level: ");
    Serial.println(light);

    Serial.print("Motion: ");
    Serial.println(motion ? "Detected" : "Not Detected");

    Serial.print("Distance: ");
    Serial.print(distance);
    Serial.println(" cm");

    Serial.println("-------------------------");

    // JSON PAYLOADS (F2)
    String tempJson   = "{\"value\":" + String(temperature) + ",\"unit\":\"C\"}";
    String humJson    = "{\"value\":" + String(humidity)    + ",\"unit\":\"%\"}";
    String lightJson  = "{\"value\":" + String(light)       + "}";
    String motionJson = "{\"detected\":" + String(motion)   + "}";
    String distJson   = "{\"value\":" + String(distance)    + ",\"unit\":\"cm\"}";

    client.publish("sensors/temperature", tempJson.c_str());
    client.publish("sensors/humidity",    humJson.c_str());
    client.publish("sensors/light",       lightJson.c_str());
    client.publish("sensors/motion",      motionJson.c_str());
    client.publish("sensors/distance",    distJson.c_str());

    // AUTOMATIC ACTUATOR CONTROL (F4)

    // Red LED: high temperature
    if (temperature > TEMP_THRESHOLD) {
      digitalWrite(LED_RED, HIGH);
      Serial.println("AUTO: High temp — Red LED ON");
    } else {
      digitalWrite(LED_RED, LOW);
    }

    // Yellow LED: dark condition
    if (light > LIGHT_THRESHOLD) {
      digitalWrite(LED_YELLOW, HIGH);
      Serial.println("AUTO: Dark — Yellow LED ON");
    } else {
      digitalWrite(LED_YELLOW, LOW);
    }

    // Buzzer: motion detected — 2 second non-blocking beep
    if (motion && !buzzerActive) {
      buzzerActive  = true;
      buzzerEndTime = millis() + 2000;
      tone(BUZZER, 1000);
      Serial.println("AUTO: Motion — Buzzer ON 2s");
    }

    // Servo: distance barrier (F7)
    // distance > 0 filters out false zeros when nothing is in range
    if (distance > 0 && distance < DIST_THRESHOLD) {
      servo.write(0);
      Serial.println("AUTO: Object close — Servo OPEN (0°)");
    } else {
      servo.write(90);
    }
  }
}
