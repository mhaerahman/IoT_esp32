#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ESP32Servo.h>
#include "HX711.h"
#include <OneWire.h>
#include <DallasTemperature.h>

// -------- WiFi dan Telegram --------
const char* ssid = "Wokwi-GUEST";
const char* password = "";
const char* BOTtoken = "7385408421:AAGRVEFRMv6JDbXU6TFvg5usmrK72zwcopE";
const String chat_id = "8153139524";

WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);

// -------- NTP (Waktu Real) --------
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 7 * 3600, 60000); // GMT+7

// -------- HC-SR04 --------
#define TRIG_PIN 4   // GPIO4
#define ECHO_PIN 5   // GPIO5

// -------- LED Indikator --------
#define LED_PIN 2    // GPIO2 (bawaan LED ESP32)

#define SERVO1_PIN 18 // GPIO18 (katup)
#define SERVO2_PIN 19 // GPIO19 (dorong)
Servo servo1, servo2;

// -------- HX711 --------
#define HX_DT 21    // GPIO21
#define HX_SCK 22   // GPIO22
HX711 scale;

// -------- DS18B20 --------
#define ONE_WIRE_BUS 23   // GPIO23
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// -------- Timer dan Waktu Pakan --------
bool sudahPakanPagi = false;
bool sudahPakanSore = false;
int jamPakanPagi = 7;
int jamPakanSore = 17;

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  client.setInsecure();

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");

  timeClient.begin();

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  servo1.attach(SERVO1_PIN);
  servo2.attach(SERVO2_PIN);
  servo1.write(0);
  servo2.write(0);

  scale.begin(HX_DT, HX_SCK);
  scale.set_scale();
  scale.tare();

  sensors.begin();  // Mulai sensor suhu
}

// -------- Ukur Jarak Air --------
long ukurJarak() {
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH);
  long distance = duration * 0.034 / 2;
  if (distance < 5 || distance > 400) return -1;
  return distance;
}

// -------- Proses Pakan --------
void beriPakan() {
  Serial.println("🔄 Memberi pakan...");

  digitalWrite(LED_PIN, HIGH);

  // Buka katup makan
  servo1.write(90); delay(2000);

  //baca berat dari load cell
  float berat = scale.get_units();
  unsigned long startTime = millis();
  unsigned long timeout = 10000; // 10 detik

  while (berat < 100.0 && millis() - startTime < timeout) {
    berat = scale.get_units();
    Serial.print("Berat: "); Serial.println(berat);
    delay(500);
  }

  if (berat >= 100.0) {
    Serial.println("✅ Berat cukup, lanjut proses.");
  } else {
    Serial.println("❗ Timeout: Berat tidak mencukupi.");
    // Tambahkan tindakan darurat jika diperlukan
  }

  //tutup katup makan
  servo1.write(0); delay(1000);

  //dorong pakan ke kolam
  servo2.write(90); delay(1000);
  servo2.write(0);

  digitalWrite(LED_PIN, LOW);

  //reset loadcell
  scale.tare();
  Serial.println("✅ Pakan selesai.");
}

// -------- Telegram Handler --------
void handleTelegram() {
  int newMsg = bot.getUpdates(bot.last_message_received + 1);
  for (int i = 0; i < newMsg; i++) {
    String text = bot.messages[i].text;
    String id = bot.messages[i].chat_id;

    if (text == "/start") {
      String msg = "🐟 Sistem Monitoring Kolam Aktif!\n";
      msg += "/status - Lihat kondisi\n";
      msg += "/feed - Pakan manual";
      bot.sendMessage(id, msg, "");
    }
    else if (text == "/status") {
      long jarak = ukurJarak();
      float berat = scale.get_units();
      sensors.requestTemperatures();
      float suhuAir = sensors.getTempCByIndex(0);

      String msg = "📡 Status Kolam:\n";
      msg += "💧 Tinggi air: " + (jarak == -1 ? String("Tak terbaca") : String(jarak) + " cm") + "\n";
      msg += "⚖️ Berat pakan: " + String(berat, 2) + " g\n";
      msg += "🌡️ Suhu air: " + String(suhuAir, 2) + " °C";
      bot.sendMessage(id, msg, "");
    }
    else if (text == "/feed") {
      bot.sendMessage(id, "🐟 Memberikan pakan...", "");
      beriPakan();
      bot.sendMessage(id, "✅ Pakan selesai!", "");
    }
  }
}

void loop() {
  timeClient.update();
  handleTelegram();

  int jam = timeClient.getHours();

  if (jam == jamPakanPagi && !sudahPakanPagi) {
    beriPakan();
    bot.sendMessage(chat_id, "🐟 Pakan pagi otomatis diberikan!", "");
    sudahPakanPagi = true;
  }

  if (jam == jamPakanSore && !sudahPakanSore) {
    beriPakan();
    bot.sendMessage(chat_id, "🐟 Pakan sore otomatis diberikan!", "");
    sudahPakanSore = true;
  }

  if (jam != jamPakanPagi) sudahPakanPagi = false;
  if (jam != jamPakanSore) sudahPakanSore = false;

  // ✅ Tambahkan log semua sensor ke Serial Monitor
  long jarak = ukurJarak();
  float berat = scale.get_units();
  sensors.requestTemperatures();
  float suhuAir = sensors.getTempCByIndex(0);

  Serial.println("=== STATUS SENSOR ===");
  Serial.print("💧 Jarak permukaan air: ");
  Serial.print(jarak);
  Serial.println(" cm");

  Serial.print("⚖️ Berat pakan: ");
  Serial.print(berat);
  Serial.println(" g");

  Serial.print("🌡️ Suhu air: ");
  Serial.print(suhuAir);
  Serial.println(" °C");
  Serial.println("======================\n");

  delay(1000);
}
