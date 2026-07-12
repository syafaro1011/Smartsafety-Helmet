# 🪖 Smart Helmet Monitoring System (SHM-001)

> **UAS — Sistem Mikrokontroller | Semester 6**  
> Sistem pemantauan helm cerdas berbasis ESP32 dengan deteksi kecelakaan real-time menggunakan FreeRTOS dan konektivitas MQTT.
> <br></br>
> **Anggota Kelompok**
> 1. Perdi Ruhiyat - 23552011207
> 2. Syaiful Fathur Rozaq - 23552011282

---

## 📋 Daftar Isi

- [Deskripsi Proyek](#-deskripsi-proyek)
- [Fitur Utama](#-fitur-utama)
- [Arsitektur Sistem](#-arsitektur-sistem)
- [Hardware yang Dibutuhkan](#-hardware-yang-dibutuhkan)
- [Diagram Koneksi Pin](#-diagram-koneksi-pin)
- [Library yang Digunakan](#-library-yang-digunakan)
- [Konfigurasi](#-konfigurasi)
- [Struktur FreeRTOS](#-struktur-freertos)
- [Logika Deteksi](#-logika-deteksi)
- [MQTT Topics & Commands](#-mqtt-topics--commands)
- [Status Sistem](#-status-sistem)
- [Format Payload Telemetry](#-format-payload-telemetry)
- [Cara Upload & Penggunaan](#-cara-upload--penggunaan)
- [Troubleshooting](#-troubleshooting)
- [Demo Prototype](#demo-prototype)

---

## 📌 Deskripsi Proyek

**Smart Helmet Monitoring System (SHM-001)** adalah perangkat IoT yang dipasang pada helm untuk mendeteksi kecelakaan secara otomatis. Sistem ini menggunakan sensor **MPU6050** (accelerometer + gyroscope) untuk memantau kemiringan dan percepatan helm secara real-time.

Apabila terdeteksi benturan keras atau kemiringan ekstrem diikuti kondisi diam (tidak bergerak), sistem akan menganggap pengendara mengalami kecelakaan dan mengirimkan **alert darurat** melalui protokol **MQTT** ke server cloud. Seluruh task dijalankan secara paralel menggunakan **FreeRTOS** pada ESP32 dual-core.

---

## ✨ Fitur Utama

| Fitur                        | Deskripsi                                                |
| ---------------------------- | -------------------------------------------------------- |
| 🔍 **Deteksi Kemiringan**    | Memantau sudut kemiringan helm secara real-time          |
| 💥 **Deteksi Benturan**      | Mendeteksi impact berdasarkan nilai percepatan (g-force) |
| 🚨 **Deteksi Kecelakaan**    | Menilai risiko kecelakaan jika diam setelah impact keras |
| 📡 **Telemetry MQTT**        | Mengirim data sensor setiap 500ms ke broker MQTT cloud   |
| ⚡ **FreeRTOS Multitasking** | 4 task paralel pada 2 core ESP32                         |
| 💾 **Penyimpanan NVS**       | Konfigurasi WiFi tersimpan di NVS (Non-Volatile Storage) |
| 🎛️ **Mode Otomatis/Manual**  | Dapat dikontrol via perintah MQTT                        |
| 🔘 **Tombol Reset Fisik**    | Reset alarm langsung dari perangkat                      |
| 🔴🟡🟢 **Indikator LED**     | Status visual melalui tiga LED berwarna                  |
| 🔔 **Buzzer**                | Alarm audio untuk kondisi bahaya                         |

---

## 🏗️ Arsitektur Sistem

![Arsitektur Sistem](https://cdn.discordapp.com/attachments/1099859505891790959/1525345718062026962/ars.png?ex=6a530c66&is=6a51bae6&hm=d55d1fba53e235e49422d53f5fc26fb548bac12290ffce05c36db6d927ee1565&)

---

## 🔧 Hardware yang Dibutuhkan

| Komponen                      | Jumlah     | Keterangan                       |
| ----------------------------- | ---------- | -------------------------------- |
| **ESP32** (WROOM-32 / DevKit) | 1          | Mikrokontroller utama            |
| **MPU6050**                   | 1          | Accelerometer + Gyroscope (I2C)  |
| **LED Hijau**                 | 1          | Indikator status NORMAL          |
| **LED Kuning**                | 1          | Indikator status WARNING         |
| **LED Merah**                 | 1          | Indikator status FALL / ACCIDENT |
| **Buzzer Aktif**              | 1          | Alarm audio                      |
| **Push Button**               | 1          | Tombol reset alarm               |
| **Resistor 220Ω**             | 3          | Resistor pembatas arus LED       |
| **Breadboard + Kabel**        | Secukupnya | Untuk koneksi                    |
| **Sumber Daya**               | 1          | USB / Baterai 3.7V LiPo          |

---

## 🔌 Diagram Koneksi Pin

### MPU6050 → ESP32

| MPU6050 | ESP32       | Keterangan    |
| ------- | ----------- | ------------- |
| VCC     | 3.3V        | Tegangan daya |
| GND     | GND         | Ground        |
| SDA     | GPIO **21** | I2C Data      |
| SCL     | GPIO **22** | I2C Clock     |

### Komponen Output → ESP32

| Komponen     | GPIO ESP32  | Mode         |
| ------------ | ----------- | ------------ |
| LED Hijau    | GPIO **18** | OUTPUT       |
| LED Merah    | GPIO **19** | OUTPUT       |
| LED Kuning   | GPIO **23** | OUTPUT       |
| Buzzer       | GPIO **25** | OUTPUT (PWM) |
| Tombol Reset | GPIO **26** | INPUT_PULLUP |

> **Catatan:** Tombol Reset terhubung antara GPIO 26 dan GND. Mode INPUT_PULLUP menjaga pin HIGH saat tombol tidak ditekan.

---

## 📦 Library yang Digunakan

Instal semua library berikut melalui **Arduino IDE Library Manager** atau **PlatformIO**:

| Library         | Versi Dianjurkan | Fungsi                |
| --------------- | ---------------- | --------------------- |
| `WiFi`          | Built-in ESP32   | Koneksi WiFi          |
| `PubSubClient`  | ≥ 2.8.0          | Client MQTT           |
| `ArduinoJson`   | ≥ 6.x            | Serialisasi JSON      |
| `Wire`          | Built-in ESP32   | Komunikasi I2C        |
| `MPU6050_light` | ≥ 1.1.0          | Driver sensor MPU6050 |
| `Preferences`   | Built-in ESP32   | Penyimpanan NVS       |
| `FreeRTOS`      | Built-in ESP32   | Multitasking          |

---

## ⚙️ Konfigurasi

### Konfigurasi WiFi (via NVS)

Kredensial WiFi disimpan di **Non-Volatile Storage (NVS)** ESP32 dengan namespace `wifi_config`. Jika belum pernah dikonfigurasi, sistem menggunakan nilai default:

```cpp
// Nilai default jika NVS kosong
wifiSsid     = "Munyenyo";   // Ganti dengan SSID Anda
wifiPassword = "jangkrik";   // Ganti dengan password Anda
```

Untuk mengubah konfigurasi WiFi, gunakan sketch terpisah atau Serial Monitor untuk menulis ke NVS:

```cpp
preferences.begin("wifi_config", false);
preferences.putString("ssid", "NamaWiFiAnda");
preferences.putString("password", "PasswordAnda");
preferences.end();
```

### Konfigurasi MQTT

```cpp
const char* MQTT_HOST = "xxxxxxxx.cloud.shiftr.io";
const uint16_t MQTT_PORT = 1883;
const char* MQTT_USER = "xxxxxxxx";
const char* DEVICE_ID  = "SHM-001";
```

### Parameter Deteksi

| Parameter             | Nilai Default | Keterangan                                |
| --------------------- | ------------- | ----------------------------------------- |
| `WARNING_TILT_DEG`    | **55.0°**     | Ambang batas kemiringan peringatan        |
| `FALL_TILT_DEG`       | **70.0°**     | Ambang batas kemiringan jatuh             |
| `IMPACT_THRESHOLD_G`  | **2.5 G**     | Ambang batas benturan normal              |
| `HARD_IMPACT_G`       | **3.5 G**     | Ambang batas benturan keras               |
| `STILL_ACCEL_DELTA_G` | **0.12 G**    | Toleransi akselerasi saat diam            |
| `STILL_GYRO_DPS`      | **12.0 dps**  | Toleransi gyroscope saat diam             |
| `STILL_TIME_MS`       | **4000 ms**   | Durasi diam sebelum dinyatakan kecelakaan |
| `SEND_INTERVAL`       | **500 ms**    | Interval pengiriman telemetry             |

---

## 🧵 Struktur FreeRTOS

Sistem menggunakan **4 FreeRTOS Task** yang didistribusikan pada 2 core ESP32:

### Core 1

| Task         | Prioritas     | Stack      | Interval | Fungsi                          |
| ------------ | ------------- | ---------- | -------- | ------------------------------- |
| `SensorTask` | 4 (Tertinggi) | 4096 bytes | 10 ms    | Baca data MPU6050               |
| `LogicTask`  | 3             | 4096 bytes | 20 ms    | Analisis dan deteksi status     |
| `OutputTask` | 2             | 3072 bytes | 25 ms    | Kontrol LED, buzzer, dan tombol |

### Core 0

| Task          | Prioritas | Stack      | Interval | Fungsi                       |
| ------------- | --------- | ---------- | -------- | ---------------------------- |
| `NetworkTask` | 2         | 8192 bytes | 10 ms    | WiFi, MQTT, telemetry, alert |

### Shared Resources (Thread Safety)

| Resource      | Tipe            | Digunakan Oleh                      |
| ------------- | --------------- | ----------------------------------- |
| `sensorMutex` | Mutex           | SensorTask ↔ LogicTask, NetworkTask |
| `stateMutex`  | Mutex           | LogicTask ↔ OutputTask, NetworkTask |
| `alertQueue`  | Queue (12 item) | LogicTask, OutputTask → NetworkTask |

---

## 🧠 Logika Deteksi

Sistem menerapkan state machine dengan 4 status:

![logika Deteksi](https://cdn.discordapp.com/attachments/1099859505891790959/1525361534304260186/loki.png?ex=6a531b21&is=6a51c9a1&hm=797dd0ab5e641a8892bfb2f09d605458f514d962d60a4399ec943900941dc009&)

### Kondisi Deteksi Detail

| Kondisi                  | Trigger                                    | Status                          |
| ------------------------ | ------------------------------------------ | ------------------------------- |
| Kemiringan ringan        | tilt ≥ 55°                                 | `WARNING`                       |
| Kemiringan ekstrem       | tilt ≥ 70° **atau** impact ≥ 2.5G          | `FALL_DETECTED`                 |
| Benturan sangat keras    | acceleration ≥ 3.5G                        | `FALL_DETECTED` + pending check |
| Benturan keras + ekstrem | impact ≥ 2.5G **dan** tilt ≥ 70°           | `FALL_DETECTED` + pending check |
| Diam pasca impact keras  | diam selama 4 detik setelah benturan keras | `ACCIDENT_RISK` ⚠️              |

---

## 📡 MQTT Topics & Commands

### Topics

| Topic                           | Tipe               | Keterangan                    |
| ------------------------------- | ------------------ | ----------------------------- |
| `smarthelmet/SHM-001/telemetry` | Publish            | Data sensor lengkap (500ms)   |
| `smarthelmet/SHM-001/status`    | Publish (retained) | Status teks saat ini          |
| `smarthelmet/SHM-001/alert`     | Publish (retained) | Event alert darurat           |
| `smarthelmet/SHM-001/command`   | Subscribe          | Perintah dari server/aplikasi |

### Perintah yang Didukung (`/command`)

Kirim JSON atau teks plain ke topic command:

```json
{ "command": "RESET_ALARM" }
```

| Perintah      | Fungsi                                                |
| ------------- | ----------------------------------------------------- |
| `RESET_ALARM` | Reset alarm, kembali ke status NORMAL                 |
| `MODE_AUTO`   | Aktifkan mode otomatis (deteksi otomatis)             |
| `MODE_MANUAL` | Aktifkan mode manual (alarm dikendalikan via MQTT)    |
| `ALARM_ON`    | Aktifkan alarm secara manual _(hanya di mode manual)_ |
| `ALARM_OFF`   | Matikan alarm secara manual _(hanya di mode manual)_  |

---

## 🚦 Status Sistem

| Status          | LED               | Buzzer                | Keterangan                              |
| --------------- | ----------------- | --------------------- | --------------------------------------- |
| `NORMAL`        | 🟢 Hijau menyala  | Mati                  | Sistem berfungsi normal                 |
| `WARNING`       | 🟡 Kuning menyala | Bip berselang (500ms) | Kemiringan melebihi 55°                 |
| `FALL_DETECTED` | 🔴 Merah menyala  | Nada kontinu 2500Hz   | Terdeteksi jatuh atau benturan          |
| `ACCIDENT_RISK` | 🔴 Merah menyala  | Nada kontinu 2500Hz   | Risiko kecelakaan tinggi, alert dikirim |

---

## 📊 Format Payload Telemetry

Telemetry dikirim setiap **500ms** ke topic `smarthelmet/SHM-001/telemetry`:

```json
{
  "device_id": "SHM-001",
  "status": "NORMAL",
  "mode": "AUTO",
  "alarm": false,
  "tilt": 3.14,
  "acceleration_g": 1.02,
  "accelerometer": {
    "x": 0.1,
    "y": -0.05,
    "z": 9.81
  },
  "accelerometer_g": {
    "x": 0.01,
    "y": -0.005,
    "z": 1.0
  },
  "gyroscope": {
    "x": 0.3,
    "y": -0.1,
    "z": 0.05
  },
  "angles": {
    "x": 2.1,
    "y": 1.5,
    "z": 0.0
  },
  "rssi": -65,
  "uptime_ms": 12345
}
```

### Format Payload Alert

Alert dikirim ke topic `smarthelmet/SHM-001/alert`:

```json
{
  "device_id": "SHM-001",
  "event": "ACCIDENT_RISK_DETECTED",
  "status": "ACCIDENT_RISK",
  "timestamp_ms": 12345
}
```

| Event                    | Keterangan                            |
| ------------------------ | ------------------------------------- |
| `DEVICE_CONNECTED`       | Perangkat terhubung ke MQTT           |
| `STATUS_NORMAL`          | Status kembali normal                 |
| `WARNING_DETECTED`       | Kemiringan terdeteksi                 |
| `FALL_DETECTED`          | Jatuh terdeteksi                      |
| `ACCIDENT_RISK_DETECTED` | Risiko kecelakaan — butuh pertolongan |
| `ALARM_RESET`            | Alarm direset                         |
| `MODE_AUTO`              | Mode diubah ke otomatis               |
| `MODE_MANUAL`            | Mode diubah ke manual                 |

---

## 🚀 Cara Upload & Penggunaan

### 1. Persiapan Arduino IDE

1. Instal **Arduino IDE** versi 2.x atau lebih baru
2. Tambahkan board ESP32 melalui **Board Manager**:
   - URL: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
3. Pilih board: **ESP32 Dev Module**

### 2. Instalasi Library

Buka **Library Manager** (`Ctrl+Shift+I`) dan instal:

- `PubSubClient` by Nick O'Leary
- `ArduinoJson` by Benoit Blanchon
- `MPU6050_light` by rfetick

### 3. Konfigurasi & Upload

1. Buka file `UAS-Helm.ino`
2. Sesuaikan kredensial WiFi di bagian NVS atau ubah nilai default pada baris:
   ```cpp
   wifiSsid     = preferences.getString("ssid", "NamaWiFiAnda");
   wifiPassword = preferences.getString("password", "PasswordAnda");
   ```
3. Pilih port COM yang sesuai
4. Klik **Upload** (`Ctrl+U`)

### 4. Verifikasi

1. Buka **Serial Monitor** pada baud rate **115200**
2. Pastikan output menunjukkan:
   ```
   Memuat konfigurasi WiFi NVS...
   Jangan gerakkan helm. Kalibrasi MPU6050...
   Kalibrasi MPU6050 selesai.
   Membuat task FreeRTOS...
   Smart Helmet FreeRTOS siap.
   Menghubungkan WiFi ke SSID: ...
   MQTT terhubung.
   ```
3. LED hijau menyala → sistem siap

### 5. Pemantauan via MQTT

Gunakan MQTT client seperti **MQTT Explorer** atau **mosquitto_sub** untuk memantau data:

```bash
mosquitto_sub -h perdiruhiyat.cloud.shiftr.io -p 1883 \
  -u perdiruhiyat -P <password> \
  -t "smarthelmet/SHM-001/#" -v
```

---

## 🔨 Troubleshooting

| Masalah                      | Kemungkinan Penyebab           | Solusi                                                               |
| ---------------------------- | ------------------------------ | -------------------------------------------------------------------- |
| LED merah berkedip saat boot | MPU6050 tidak terdeteksi       | Periksa koneksi SDA/SCL dan tegangan                                 |
| LED merah berkedip saat boot | FreeRTOS resource gagal dibuat | Restart ESP32, periksa heap memory                                   |
| Tidak terhubung WiFi         | SSID/Password salah di NVS     | Update konfigurasi NVS atau ubah nilai default                       |
| MQTT gagal (`rc=-2`)         | Broker tidak terjangkau        | Pastikan WiFi terhubung dan MQTT host benar                          |
| Data sensor tidak akurat     | Kalibrasi gagal                | Pastikan helm diam saat kalibrasi startup                            |
| Alert tidak terkirim         | Queue alert penuh              | Queue terisi, event dilewati — ini normal pada kondisi burst         |
| Alarm tidak bisa direset     | Alarm ter-latch                | Tekan tombol reset fisik (GPIO 26) atau kirim `RESET_ALARM` via MQTT |

---

## 📁 Struktur File

```
UAS-Helm/
├── UAS-Helm.ino    # Source code utama (Arduino sketch)
└── README.md       # Dokumentasi proyek ini
```

---

## 👨‍💻 Informasi Proyek

|                         |                         |
| ----------------------- | ----------------------- |
| **Mata Kuliah**         | Sistem Mikrokontroller  |
| **Semester**            | 6                       |
| **Platform**            | ESP32 (FreeRTOS)        |
| **Protokol Komunikasi** | MQTT (shiftr.io)        |
| **Bahasa Pemrograman**  | C++ (Arduino Framework) |
| **Device ID**           | SHM-001                 |

---

##  Demo Prototype
[![Demo - Smart Helmet Monitoring with ESP32](https://cdn-icons-png.flaticon.com/128/3670/3670147.png)](https://youtu.be/TesJzKEsMRk?si=Y5mwEJAeZnArwNwn)

---
