# Smart Drying Automation System

Sistem Jemuran Otomatis pintar berbasis **ESP32**, motor **Servo Continuous 360°**, sensor cuaca (**Rain Drop Sensor** & **LDR**), dan terintegrasi dengan **Telegram Bot** untuk pemantauan dan kontrol jarak jauh secara real-time.

---

## 🚀 Fitur Utama

- **Otomasi Cerdas (Auto-Pilot):**
  - Menarik jemuran ke dalam secara otomatis saat hujan atau cuaca gelap/malam.
  - Mengeluarkan jemuran saat cuaca cerah/terang.
- **Kontrol Jarak Jauh via Telegram Bot:**
  - Perintah manual untuk memasukkan atau mengeluarkan jemuran (`/masuk`, `/keluar`, `/stop`).
  - Mengaktifkan atau menonaktifkan mode otomatis (`/auto on`, `/auto off`).
  - Laporan status kondisi cuaca, sensor, posisi rak, dan akumulasi waktu penjemuran (`/status`).
- **Solar Drying Accumulator:**
  - Menghitung akumulasi durasi jemuran terpapar sinar matahari secara presisi.
  - Target durasi jemur default: 6 jam.
- **NVS State Persistence:**
  - Menyimpan status posisi rak dan akumulator waktu jemur ke flash NVS (Non-Volatile Storage) menggunakan library `Preferences`.
  - Kondisi sistem tetap terjaga meski ESP32 mengalami restart/mati listrik mendadak.
- **Noise Filtering Sensor Hujan:**
  - Dilengkapi moving buffer filter (5 sampel) untuk mencegah false trigger akibat percikan air sementara.

---

## 🛠️ Pin Mapping ESP32

| Komponen | Pin ESP32 | Keterangan |
| :--- | :--- | :--- |
| **Rain Drop Sensor** | `GPIO 32` | Input Analog (Threshold: 2700) |
| **LDR Sensor** | `GPIO 25` | Input Digital (`LOW` = Terang) |
| **Servo 360°** | `GPIO 15` | PWM Driver Kontrol Rak |
| **LED Indikator** | `GPIO 2` | Indikator status sistem |

---

## 📦 Kebutuhan Hardware & Library

### Hardware
1. ESP32 Development Board (DOIT ESP32 DevKit V1)
2. Motor Servo Continuous 360°
3. Rain Drop Sensor Module
4. LDR Sensor Module (Digital Output)
5. Power Supply 5V eksternal (disarankan untuk servo)

### Library PlatformIO
- `madhephaestus/ESP32Servo@^3.0.6`
- `bblanchon/ArduinoJson@^7.4.1`
- `witnessmenow/UniversalTelegramBot@^1.3.0`

---

## ⚙️ Petunjuk Instalasi & Konfigurasi

1. **Clone Repositori:**
   ```bash
   git clone https://github.com/thomasfebryc/Smart-Drying-Automation-System.git
   cd Smart-Drying-Automation-System
   ```

2. **Konfigurasi Kredensial:**
   Salin file `include/secrets.example.h` menjadi `include/secrets.h`:
   ```bash
   cp include/secrets.example.h include/secrets.h
   ```
   Buka `include/secrets.h` dan isi konfigurasi jaringan WiFi serta Telegram Bot Anda:
   ```cpp
   const char* WIFI_SSID     = "NAMA_WIFI_ANDA";
   const char* WIFI_PASSWORD = "PASSWORD_WIFI_ANDA";
   const char* BOT_TOKEN     = "BOT_TOKEN_TELEGRAM_ANDA";
   const char* TARGET_CHAT_ID = "CHAT_ID_TELEGRAM_ANDA";
   ```

3. **Build & Upload:**
   - Buka project ini di VS Code dengan ekstensi **PlatformIO**.
   - Hubungkan ESP32 via kabel USB.
   - Klik **Build** (ikon centang) dan **Upload** (ikon panah kanan) di toolbar bawah PlatformIO.

---

## 🤖 Daftar Perintah Telegram Bot

| Perintah | Deskripsi |
| :--- | :--- |
| `/status` | Cek status sistem, cuaca, sensor, posisi rak, dan timer jemur |
| `/keluar` | Memindahkan rak jemuran ke luar secara manual |
| `/masuk` | Memindahkan rak jemuran ke dalam secara manual |
| `/stop` | Menghentikan pergerakan motor secara darurat |
| `/auto on` | Mengaktifkan mode otomatis (Auto-Pilot) |
| `/auto off` | Menonaktifkan mode otomatis (Mode Manual) |
| `/reset_timer` | Mereset akumulasi waktu penjemuran kembali ke 0 |
| `/bantuan` | Menampilkan menu panduan perintah |

---

## 📄 Lisensi
Proyek ini dibuat untuk keperluan otomatisasi rumah cerdas (Smart Home / IoT). Bebas digunakan dan dimodifikasi untuk edukasi dan pengembangan lebih lanjut.
