#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ESP32Servo.h>
#include <Preferences.h>
#include "secrets.h"

// ==========================================
// [ARGON PIN MAPPING]
// ==========================================
const uint8_t PIN_RAIN_ANALOG = 32;  // Analog Rain Drop Sensor
const uint8_t PIN_LDR_DIGITAL = 25;  // Digital Light Sensor (LOW = Terang)
const uint8_t PIN_SERVO_PWM   = 15;  // Servo Continuous 360

// ==========================================
// [CORE SYSTEM TYPEDEFS & STATES]
// ==========================================
enum RackPosition {
    POS_INSIDE = 0,    // Rak di dalam (terlindungi)
    POS_OUTSIDE = 1,   // Rak di luar (menjemur)
    POS_TRANSIT = 2    // Sedang bergerak
};

enum MotionAction {
    MOTION_STOP = 0,
    MOTION_RETRACT,    // Masuk (Clockwise)
    MOTION_DEPLOY      // Keluar (Counter-Clockwise)
};

struct SensorSnapshot {
    int rawRain;
    bool isRaining;
    bool isSunny;
    unsigned long filteredRain;
};

// ==========================================
// [HARDWARE DRIVERS & KERNEL OBJECTS]
// ==========================================
WiFiClientSecure securedClient;
UniversalTelegramBot telegramBot(BOT_TOKEN, securedClient);
Servo servoDriver;
Preferences nvsStorage;

// ==========================================
// [SYSTEM RUNTIME STATE]
// ==========================================
RackPosition currentPosition = POS_INSIDE;
MotionAction currentMotion   = MOTION_STOP;
bool autoPilotEnabled        = true;

// Timing & Motion Parameters
const unsigned long SERVO_RUN_TIME_MS = 7000;
unsigned long motionStartTime         = 0;

// Solar Drying Accumulator
unsigned long totalDryingTimeMs = 0;       // Total waktu terpapar sinar
unsigned long currentSessionStartMs = 0;   // Waktu mulai sesi aktif
const unsigned long TARGET_DRYING_MS = 21600000; // 6 Jam target

// Non-blocking Polling Schedulers
unsigned long lastTelegramPollMs = 0;
const unsigned long INTERVAL_BOT_POLL = 1200;

unsigned long lastSensorPollMs   = 0;
const unsigned long INTERVAL_SENSOR_READ = 1000;

unsigned long lastNvsSyncMs      = 0;
const unsigned long INTERVAL_NVS_SYNC = 30000; // Flush memory per 30 detik

// Rain Filter Buffer (Window of 5 samples)
const int RAIN_THRESHOLD = 2700;
int rainSampleBuffer[5]  = {4095, 4095, 4095, 4095, 4095};
uint8_t bufferIndex      = 0;

// ==========================================
// [FORWARD DECLARATIONS]
// ==========================================
void initHardware();
void connectWiFi();
void updateMotionKernel();
void sampleSensors(SensorSnapshot &snap);
void executeAutoPilot(const SensorSnapshot &snap);
void processTelegramInbox();
void startMotion(MotionAction action);
void updateAccumulator();
void commitStateToNVS();
void restoreStateFromNVS();
void sendArgonDashboard(const String &headline = "");

// ==========================================
// [MAIN SETUP]
// ==========================================
void setup() {
    Serial.begin(115200);
    delay(250);
    Serial.println("\n[ARGON KERNEL] Initializing Core Services...");

    initHardware();
    restoreStateFromNVS();
    connectWiFi();

    // Menggunakan safe SSL bypass untuk ESP32 TLS buffer
    securedClient.setInsecure();

    sendArgonDashboard("💠 *Argon Core Initialized & Online*");
    Serial.println("[ARGON KERNEL] System Operational.");
}

// ==========================================
// [MAIN EXECUTION PIPELINE]
// ==========================================
void loop() {
    unsigned long now = millis();

    // 1. Task: Hardware Motion State Machine
    updateMotionKernel();

    // 2. Task: WiFi Link Health Monitor
    if (WiFi.status() != WL_CONNECTED) {
        connectWiFi();
    }

    // 3. Task: Sensor Acquisition & AutoPilot Evaluation
    if (now - lastSensorPollMs >= INTERVAL_SENSOR_READ) {
        lastSensorPollMs = now;
        SensorSnapshot snapshot;
        sampleSensors(snapshot);

        if (autoPilotEnabled) {
            executeAutoPilot(snapshot);
        }
    }

    // 4. Task: Realtime Accumulator Tracking
    if (currentPosition == POS_OUTSIDE && currentMotion == MOTION_STOP) {
        updateAccumulator();
    }

    // 5. Task: Non-blocking Telegram Dispatcher
    if (now - lastTelegramPollMs >= INTERVAL_BOT_POLL) {
        lastTelegramPollMs = now;
        processTelegramInbox();
    }

    // 6. Task: NVS Flash Memory State Preservation
    if (now - lastNvsSyncMs >= INTERVAL_NVS_SYNC) {
        lastNvsSyncMs = now;
        commitStateToNVS();
    }
}

// ==========================================
// [HARDWARE & SENSORY SUBROUTINES]
// ==========================================
void initHardware() {
    pinMode(PIN_LDR_DIGITAL, INPUT);
    pinMode(PIN_RAIN_ANALOG, INPUT);

    // Initial stop condition
    servoDriver.write(90);
}

void sampleSensors(SensorSnapshot &snap) {
    int raw = analogRead(PIN_RAIN_ANALOG);
    
    // Rolling Average Filter
    rainSampleBuffer[bufferIndex] = raw;
    bufferIndex = (bufferIndex + 1) % 5;

    long sum = 0;
    for (int i = 0; i < 5; i++) sum += rainSampleBuffer[i];
    snap.filteredRain = sum / 5;

    snap.rawRain   = raw;
    snap.isRaining = (snap.filteredRain < RAIN_THRESHOLD);
    snap.isSunny   = (digitalRead(PIN_LDR_DIGITAL) == LOW);
}

void startMotion(MotionAction action) {
    if (currentMotion != MOTION_STOP) return; // Busy check

    if (!servoDriver.attached()) {
        servoDriver.attach(PIN_SERVO_PWM);
    }

    if (action == MOTION_DEPLOY) {
        currentMotion = MOTION_DEPLOY;
        currentPosition = POS_TRANSIT;
        servoDriver.write(0); // CCW - Bergerak keluar
        motionStartTime = millis();
    } else if (action == MOTION_RETRACT) {
        currentMotion = MOTION_RETRACT;
        currentPosition = POS_TRANSIT;
        servoDriver.write(180); // CW - Bergerak masuk
        motionStartTime = millis();
    }
}

void updateMotionKernel() {
    if (currentMotion == MOTION_STOP) return;

    if (millis() - motionStartTime >= SERVO_RUN_TIME_MS) {
        // Hentikan putaran
        servoDriver.write(90);
        delay(20);
        servoDriver.detach(); // Power saving & anti-jitter

        if (currentMotion == MOTION_DEPLOY) {
            currentPosition = POS_OUTSIDE;
            currentSessionStartMs = millis();
        } else if (currentMotion == MOTION_RETRACT) {
            updateAccumulator();
            currentSessionStartMs = 0;
            currentPosition = POS_INSIDE;
        }

        currentMotion = MOTION_STOP;
        commitStateToNVS();
    }
}

void updateAccumulator() {
    if (currentSessionStartMs > 0) {
        unsigned long currentMillis = millis();
        totalDryingTimeMs += (currentMillis - currentSessionStartStartTracker(currentMillis));
    }
}

unsigned long currentSessionStartStartTracker(unsigned long now) {
    unsigned long prev = currentSessionStartMs;
    currentSessionStartMs = now;
    return prev;
}

// ==========================================
// [DECISION ENGINE (AUTOPILOT)]
// ==========================================
void executeAutoPilot(const SensorSnapshot &snap) {
    // Abaikan evaluasi jika mekanik sedang bergerak
    if (currentMotion != MOTION_STOP) return;

    bool targetAchieved = (totalDryingTimeMs >= TARGET_DRYING_MS);

    // Kasus 1: Bahaya Cuaca (Hujan) atau Target Tercapai -> Masuk
    if ((snap.isRaining || targetAchieved) && currentPosition == POS_OUTSIDE) {
        startMotion(MOTION_RETRACT);
        if (snap.isRaining) {
            sendArgonDashboard("🌧 *Alert: Presipitasi Terdeteksi!* Rak otomatis ditarik.");
        } else {
            sendArgonDashboard("🎉 *Target 6 Jam Terpenuhi!* Pakaian ditarik ke dalam.");
        }
    }
    // Kasus 2: Cuaca Optimal (Kering + Terang) & Belum Selesai -> Keluar
    else if (!snap.isRaining && snap.isSunny && !targetAchieved && currentPosition == POS_INSIDE) {
        startMotion(MOTION_DEPLOY);
        sendArgonDashboard("🌤 *Cuaca Kondusif.* Menjemur pakaian.");
    }
}

// ==========================================
// [TELEGRAM INBOX & EVENT DISPATCHER]
// ==========================================
void processTelegramInbox() {
    int pendingMessages = telegramBot.getUpdates(telegramBot.last_message_received + 1);

    for (int i = 0; i < pendingMessages; i++) {
        String senderId = String(telegramBot.messages[i].chat_id);
        if (senderId != TARGET_CHAT_ID) {
            telegramBot.sendMessage(senderId, "⛔ *Akses Ditolak:* Perangkat Argon diproteksi.", "Markdown");
            continue;
        }

        String command = telegramBot.messages[i].text;

        if (command == "/start" || command == "/menu") {
            sendArgonDashboard();
        } 
        else if (command == "/btn_mode_auto") {
            autoPilotEnabled = true;
            commitStateToNVS();
            sendArgonDashboard("🤖 *Mode Auto Pilot Aktif.*");
        } 
        else if (command == "/btn_mode_manual") {
            autoPilotEnabled = false;
            commitStateToNVS();
            sendArgonDashboard("🖐 *Mode Manual Aktif.* Kontrol otomatis dinonaktifkan.");
        } 
        else if (command == "/btn_deploy") {
            if (currentPosition == POS_OUTSIDE) {
                telegramBot.sendMessage(TARGET_CHAT_ID, "ℹ️ Rak sudah berada di luar.", "");
            } else if (currentMotion != MOTION_STOP) {
                telegramBot.sendMessage(TARGET_CHAT_ID, "⚠️ Motor sedang beroperasi.", "");
            } else {
                startMotion(MOTION_DEPLOY);
                telegramBot.sendMessage(TARGET_CHAT_ID, "⚙️ Membuka rak jemuran...", "");
            }
        } 
        else if (command == "/btn_retract") {
            if (currentPosition == POS_INSIDE) {
                telegramBot.sendMessage(TARGET_CHAT_ID, "ℹ️ Rak sudah berada di dalam.", "");
            } else if (currentMotion != MOTION_STOP) {
                telegramBot.sendMessage(TARGET_CHAT_ID, "⚠️ Motor sedang beroperasi.", "");
            } else {
                startMotion(MOTION_RETRACT);
                telegramBot.sendMessage(TARGET_CHAT_ID, "⚙️ Menarik rak jemuran...", "");
            }
        } 
        else if (command == "/btn_reset_time") {
            totalDryingTimeMs = 0;
            if (currentPosition == POS_OUTSIDE) currentSessionStartMs = millis();
            commitStateToNVS();
            sendArgonDashboard("🔄 *Akumulator waktu jemur di-reset.*");
        } 
        else if (command == "/btn_refresh") {
            sendArgonDashboard();
        }
    }
}

// ==========================================
// [ARGON DASHBOARD & UI BUILDER]
// ==========================================
void sendArgonDashboard(const String &headline) {
    SensorSnapshot snap;
    sampleSensors(snap);

    unsigned long totalSec = totalDryingTimeMs / 1000;
    int hours   = totalSec / 3600;
    int minutes = (totalSec % 3600) / 60;
    int seconds = totalSec % 60;

    String card = "";
    if (headline.length() > 0) card += headline + "\n\n";

    card += "╔══════════════════════╗\n";
    card += "   *ARGON SYSTEM TELEMETRY*   \n";
    card += "╚══════════════════════╝\n";
    card += "• *Status Rak*   : " + String(currentPosition == POS_OUTSIDE ? "🏕 DI LUAR" : (currentPosition == POS_INSIDE ? "🏠 DI DALAM" : "⏳ BERGERAK")) + "\n";
    card += "• *AutoPilot*    : " + String(autoPilotEnabled ? "🟢 ENABLED" : "🔴 DISABLED") + "\n";
    card += "• *Sensor Hujan* : " + String(snap.filteredRain) + (snap.isRaining ? " (🌧 Basah)" : " (☀️ Kering)") + "\n";
    card += "• *Intensitas*   : " + String(snap.isSunny ? "☀️ Terang" : "☁️ Mendung/Gelap") + "\n";
    card += "• *Akumulasi*    : `" + String(hours) + "j " + String(minutes) + "m " + String(seconds) + "s` / 6 Jam\n";

    // Dynamic Inline Keyboard Layout
    String keyboard = "[";
    if (autoPilotEnabled) {
        keyboard += "[{\"text\":\"🛑 Matikan AutoPilot\",\"callback_data\":\"/btn_mode_manual\"}],";
    } else {
        keyboard += "[{\"text\":\"🤖 Aktifkan AutoPilot\",\"callback_data\":\"/btn_mode_auto\"}],";
    }
    keyboard += "[{\"text\":\"☀️ Keluarkan Rak\",\"callback_data\":\"/btn_deploy\"},{\"text\":\"🏠 Tarik ke Dalam\",\"callback_data\":\"/btn_retract\"}],";
    keyboard += "[{\"text\":\"🔄 Refresh Telemetry\",\"callback_data\":\"/btn_refresh\"},{\"text\":\"⏱ Reset Timer\",\"callback_data\":\"/btn_reset_time\"}]";
    keyboard += "]";

    telegramBot.sendMessageWithInlineKeyboard(TARGET_CHAT_ID, card, "Markdown", keyboard);
}

// ==========================================
// [PERSISTENCE LAYER (NVS)]
// ==========================================
void commitStateToNVS() {
    nvsStorage.begin("argon_app", false);
    nvsStorage.putBool("auto_mode", autoPilotEnabled);
    nvsStorage.putInt("rack_pos", (int)currentPosition);
    nvsStorage.putULong("dry_time", totalDryingTimeMs);
    nvsStorage.end();
}

void restoreStateFromNVS() {
    nvsStorage.begin("argon_app", true);
    autoPilotEnabled  = nvsStorage.getBool("auto_mode", true);
    currentPosition   = (RackPosition)nvsStorage.getInt("rack_pos", POS_INSIDE);
    totalDryingTimeMs = nvsStorage.getULong("dry_time", 0);
    nvsStorage.end();
}

// ==========================================
// [NETWORK DAEMON]
// ==========================================
void connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("[WIFI] Establishing link");

    uint8_t retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 25) {
        delay(400);
        Serial.print(".");
        retries++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[WIFI] Link established. IP: " + WiFi.localIP().toString());
    } else {
        Serial.println("\n[WIFI] Link timeout. Retrying on next cycle.");
    }
}