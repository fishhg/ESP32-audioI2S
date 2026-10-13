# ESP32-P4 Hi-Res Digital Audio Player (DAP) & Internet Radio

[![ESP32-P4](https://img.shields.io/badge/Chip-ESP32--P4%20RISC--V%20Dual%20Core%20360MHz-orange.svg)](https://www.espressif.com/)
[![Arduino ESP32 3.x](https://img.shields.io/badge/Arduino--ESP32-v3.x-blue.svg)](https://github.com/espressif/arduino-esp32)
[![Audio Hi-Res](https://img.shields.io/badge/Audio-Hi--Res%20192kHz%20%2F%2024--bit-brightgreen.svg)]()
[![UI FPS](https://img.shields.io/badge/UI%20Rendering-85%20FPS%20LERP-purple.svg)]()
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

[English](#english) | [Türkçe](#türkçe)

---

<a name="english"></a>
## English

An advanced, dual-core, hardware-accelerated **Hi-Res Digital Audio Player (DAP) and Internet Radio** built on the **Waveshare ESP32-P4-WIFI6** development board, featuring a **PCM5102A 32-bit I2S DAC**, **ST7920 128x64 Graphic LCD**, and onboard **4-bit SDIO 3.0 SDMMC** microSD slot.

### 🌟 Key Features

* **🚀 Asymmetric Dual-Core Architecture:**
  * **Core 0 (Dedicated Audio Processor):** Real-time decoder execution (FLAC, OPUS, WAV, MP3, AAC), 8-Band Hardware DF2T Biquad DSP filter engine, and I2S DMA streaming.
  * **Core 1 (Application & UI Processor):** SDMMC / Wi-Fi I/O tasks, 85 FPS smartphone-grade LERP graphic rendering, button debounce, and rotary/cursor animations.
* **🎛️ 8-Band Hardware DSP Equalizer (Transposed Direct Form II):**
  * Precision tone control at 60Hz, 150Hz, 400Hz, 1kHz, 2.5kHz, 6kHz, 10kHz, 15kHz (-12dB to +12dB).
  * 9 studio presets (*Flat, Bass Boost, Bass Heavy, Treble, Vocal, Rock, Pop, Jazz, Classic*) plus persistent Custom band memory.
  * Zero-click parameter interpolation during live adjustment (filter state continuity preserved).
* **📊 Real-time 512-Point Hardware FFT Spectrum Analyzer:**
  * Computes 512-point Radix-2 FFT directly from raw 32-bit audio samples (not simulated/pseudo random waveforms).
  * 9 musical frequency bands with pink-noise (1/f) acoustic compensation, adaptive dynamic range tracking (AGC), Smoothstep contrast curve, and 88% fast-attack / smooth-decay ballistics.
* **✨ 85 FPS Ultra-Fluid UI & Visuals:**
  * Sub-pixel directional horizontal slide transitions between screens.
  * Realistic 8-phase rotating turntable vinyl record animation with tonearm placement.
  * 48 px/s hardware-timed smooth marquee text scroller.
* **🔤 Comprehensive Typography & Character Support:**
  * Turkish Latin Extended-A: `ş, Ş, ı, İ, ğ, Ğ, ç, Ç, ö, Ö, ü, Ü` with automatic CP1254/ISO-8859-9 to UTF-8 transcoding.
  * Japanese & CJK glyph rendering: Hiragana, Katakana, and Kanji characters.
* **📻 Dual Playback Modes (SD Card & Internet Radio):**
  * Seamless playback from local microSD or live HTTP/Icecast streaming stations (*Power FM, Kral FM, SomaFM, Classic FM, etc.*).
  * Instant non-blocking boot (<50ms) with background asynchronous Wi-Fi connection; full offline usability when Wi-Fi is unavailable.
* **💾 Non-Volatile Flash Memory (NVS / Preferences):**
  * Volume, screen state, last played track, and custom EQ profiles are saved with a debounced write cycle to protect SPI Flash lifespan.

---

### 🎵 Supported Audio Codecs

| Format | Decoder Engine | Sample Rate & Bit Depth |
|:---|:---|:---|
| **FLAC** | Native Hardware FLAC | 44.1 kHz – **192 kHz** / 16-bit, **24-bit Hi-Res** |
| **WAV** | Uncompressed PCM | 44.1 kHz – **192 kHz** / 16-bit, **24-bit**, 32-bit |
| **OPUS** | Hardware CELT / SILK | 48.0 kHz / 16-bit Stereo |
| **OGG / OGA** | Ogg Vorbis / Opus | 44.1 kHz – 48.0 kHz |
| **MP3** | MPEG 1/2 Layer 3 | 32 – 320 kbps CBR/VBR |
| **AAC / M4A** | Advanced Audio Coding | LC-AAC / HE-AAC |

---

### 🔌 Hardware Wiring & Pinout

#### 1. ST7920 128x64 Graphic LCD (Serial / SPI Mode)
> **Note:** The **PSB** pin on the back of the ST7920 module MUST be connected to GND to select SPI mode.

| ST7920 Pin | ESP32-P4 GPIO | Description |
|:---|:---|:---|
| **GND** | GND | Ground |
| **VCC** | 5V or 3.3V | Logic Power |
| **E / SCK** | **GPIO 27** | Serial Clock |
| **R/W / MOSI** | **GPIO 32** | Serial Data (SID) |
| **RS / CS** | **GPIO 33** | Chip Select |
| **RST** | Unconnected | Handled in software |
| **PSB** | **GND** | Selects SPI (Serial) Mode |
| **BLA** | 3.3V | Backlight Anode (+) |
| **BLK** | GND | Backlight Cathode (-) |

---

#### 2. PCM5102A 32-bit I2S DAC
The PCM5102A does not require external I2C configuration or an MCLK line; it derives its master clock via internal PLL.

| PCM5102A Pin | ESP32-P4 GPIO | Description |
|:---|:---|:---|
| **VCC / VIN** | 3.3V or 5V | Power Supply |
| **GND** | GND | Ground |
| **BCK** | **GPIO 49** | Bit Clock (BCLK) |
| **DIN** | **GPIO 4** | Data In (I2S DOUT) |
| **LCK / LRCK** | **GPIO 5** | Left/Right Clock (Word Select) |
| **SCK** | **GND** | Internal PLL Mode (No MCLK required) |

##### PCM5102A Solder Jumper Configuration (Back of PCB):
* **SCK:** GND (Bridged to GND)
* **FMT:** GND / L (Standard Philips I2S)
* **XSMT:** 3.3V / H (Soft-mute disabled, audio unmuted)
* **FLT:** GND / L (Normal latency FIR filter)
* **DEMP:** GND / L (De-emphasis filter off)

> **Important:** Audio output is Line-Level. Connect to headphones through an amplifier, active speakers, or an AUX input.

---

#### 3. Onboard MicroSD Slot (Waveshare ESP32-P4-WIFI6)
The microSD slot on the board connects directly to the hardware 4-bit SDIO 3.0 controller:

| Signal | ESP32-P4 GPIO | Configuration |
|:---|:---|:---|
| **CLK** | **GPIO 43** | Floating (Driven by Host) |
| **CMD** | **GPIO 44** | Pull-Up |
| **D0** | **GPIO 39** | Pull-Up |
| **D1** | **GPIO 40** | Pull-Up |
| **D2** | **GPIO 41** | Pull-Up |
| **D3** | **GPIO 42** | Pull-Up |
| **POWER** | **GPIO 45** | Active Low (LOW) |

* The slot is locked to **20 MHz DEFAULT mode** (10 MB/s bandwidth). This provides ~9x the maximum throughput required by 192kHz/24-bit audio while preventing high-frequency DMA timeouts and bus collisions with the onboard ESP32-C6 Wi-Fi co-processor.

---

#### 4. Navigation Buttons (Opposite Pin to GND)

| Button | GPIO | List Screen | Now Playing Screen | Volume & EQ Screen | Main Menu Screen |
|:---|:---|:---|:---|:---|:---|
| **UP** | **GPIO 2** | Previous Song | Skip to Previous Track | Volume + / Gain +1dB | Cursor Up |
| **DOWN** | **GPIO 3** | Next Song | Skip to Next Track | Volume − / Gain -1dB | Cursor Down |
| **OK** | **GPIO 23** | Play Selected | Pause / Resume | Cycle Band (B0..B7) | Enter Selection |
| **BACK** | **GPIO 26** | Open Menu | Return to Song List | Step Back / Exit | Return to List |

---

### 🛠️ Arduino IDE Setup

1. **Board Settings (Tools Menu):**
   * **Board:** `ESP32P4 Dev Module` (requires Arduino-ESP32 v3.x core)
   * **Flash Size:** `32MB (256Mb)`
   * **Partition Scheme:** `32M Flash (13MB APP/6.75MB SPIFFS)` or `4.8MB APP/22MB FATFS`
   * **PSRAM:** **`Enabled`** *(Mandatory for Hi-Res audio buffers and OPUS)*
   * **CPU Frequency:** `360MHz`
2. **Library Installation:**
   * **U8g2:** Install from Arduino Library Manager (`U8g2` by *olikraus*).
   * **ESP32-audioI2S (Patched):** Navigate to `Sketch -> Include Library -> Add .ZIP Library...` and select the **`libraries/ESP32-audioI2S-master.zip`** file provided in this repository.
   > **Note:** Do NOT install the upstream version from the Library Manager, as it lacks the hardware audio hook and suffers from OPUS/Hi-Res stack panics on ESP32-P4.

---

### 📁 SD Card & Wi-Fi Configuration

1. Format the microSD card as **FAT32**.
2. Copy your audio files into the root directory (`/`) or `/music` folder.
3. **Wi-Fi Setup:**
   * Create a text file named `wifi.txt` in the root of the microSD card:
     ```text
     Your_WiFi_SSID
     Your_WiFi_Password
     ```
   * Line 1 is the network name; Line 2 is the password.
   * If `wifi.txt` is missing, the player falls back to `DEFAULT_WIFI_SSID` and `DEFAULT_WIFI_PASS` defined in the sketch.

---

<a name="türkçe"></a>
## Türkçe

Waveshare **ESP32-P4-WIFI6** geliştirme kartı, **PCM5102A 32-bit I2S DAC**, **ST7920 128x64 Grafik LCD** ve yerleşik **4-bit SDIO 3.0 SDMMC** microSD yuvası kullanılarak geliştirilmiş, çift çekirdekli, donanım hızlandırmalı üst düzey Hi-Res Taşınabilir Müzik Çalar (DAP) ve İnternet Radyosu.

### 🌟 Öne Çıkan Özellikler

* **🚀 Çift Çekirdekli (Dual-Core) Asimetrik Yük Dağılımı:**
  * **Core 0 (Özel Ses İşlemcisi):** Ses çözücü motoru (FLAC, OPUS, MP3, WAV vb.), 8-Bant Donanım Biquad IIR filtreleme ve I2S DMA aktarımı.
  * **Core 1 (Arayüz & Sistem İşlemcisi):** SDIO / WiFi I/O görevleri, 85 FPS akıllı telefon seviyesi LERP arayüz çizimi ve tuş yönetimi.
* **🎛️ 8-Bant Donanımsal DSP Ekolayzer (DF2T Biquad):**
  * Transposed Direct Form II mimarisi ile 60Hz, 150Hz, 400Hz, 1kHz, 2.5kHz, 6kHz, 10kHz, 15kHz frekanslarında -12dB .. +12dB hassas kazanç ayarı.
  * 9 hazır önayar (*Flat, Bass Boost, Bass Heavy, Treble, Vokal, Rock, Pop, Jazz, Klasik*) ve Özel (*Custom*) bant hafızası.
  * Filtre katsayıları değişirken seste sıfır çıtırtı/kopma (Click-free parameter interpolation).
* **📊 Gerçek Donanım DSP 512-Nokta FFT Spektrum Analizörü:**
  * Sahte/algoritmik dalga yerine çalınan gerçek PCM örneklerinden hesaplanan 9 bantlı canlı stüdyo spektrumu.
  * Akustik pembe gürültü kompanzasyonu, adaptif dinamik aralık takibi (AGC), Smoothstep kontrast eğrisi ve %88 hızlı atak/canlı sönümleme balistiği.
* **✨ 85 FPS Ultra Akıcı Arayüz & Görseller:**
  * Ekranlar arası directional slide (yatay kayma) geçişleri.
  * 8 açılı dönen vinil pikap plağı ve iğne kolu (tonearm) animasyonu.
  * 48 px/sn ipeksi kayan yazı (marquee) motoru.
* **🔤 Geniş Karakter Desteği:**
  * Türkçe karakterler: `ş, Ş, ı, İ, ğ, Ğ, ç, Ç, ö, Ö, ü, Ü` (Windows-1254 CP1254 otomatik UTF-8 dönüştürme).
  * Japonca / CJK Desteği (Hiragana, Katakana, Kanji).
* **📻 Çift Mod (SD Kart & İnternet Radyo):**
  * Yerel SD kart müziği ve çevrim içi canlı radyo istasyonları (*Power FM, Kral FM, Show Radyo, SomaFM vb.*).
  * Arka planda beklemesiz (<50ms) asenkron WiFi bağlantısı; WiFi veya SD kart yokken bile kesintisiz menüye erişim.
* **💾 Akıllı Kalıcı Hafıza (NVS / Preferences):**
  * Ses seviyesi, seçili ekran, en son çalınan parça ve özel ekolayzer ayarları debounced mimariyle SPI Flash'a otomatik kaydedilir.

---

### 🎵 Desteklenen Ses Formatları

| Format | Çözücü | Örnekleme Hızı & Bit Derinliği |
|:---|:---|:---|
| **FLAC** | Yerel Donanım FLAC | 44.1 kHz – **192 kHz** / 16-bit, **24-bit Hi-Res** |
| **WAV** | PCM Uncompressed | 44.1 kHz – **192 kHz** / 16-bit, **24-bit**, 32-bit |
| **OPUS** | SILK / CELT Decoders | 48.0 kHz / 16-bit Stereo |
| **OGG / OGA** | Ogg Vorbis / Opus | 44.1 kHz – 48.0 kHz |
| **MP3** | MPEG 1/2 Layer 3 | 32 – 320 kbps VBR/CBR |
| **AAC / M4A** | Advanced Audio Coding | LC-AAC / HE-AAC |

---

### 🔌 Donanım Bağlantı Şeması

#### 1. ST7920 128x64 Grafik LCD (SPI / Seri Mod)
> **Önemli:** LCD arkasındaki **PSB** pini GND'ye bağlanarak SPI (seri) moduna alınmalıdır.

| ST7920 Pin | ESP32-P4 GPIO | Açıklama |
|:---|:---|:---|
| **GND** | GND | Toprak |
| **VCC** | 5V veya 3.3V | Besleme |
| **E / SCK** | **GPIO 27** | Seri Saat (Clock) |
| **R/W / MOSI** | **GPIO 32** | Seri Veri (MOSI / SID) |
| **RS / CS** | **GPIO 33** | Çip Seçimi (CS) |
| **RST** | Boşta | Donanım Reset gerekmez |
| **PSB** | **GND** | Seri/SPI Mod Seçimi |
| **BLA** | 3.3V | Arka Işık Pozitif (+) |
| **BLK** | GND | Arka Işık Negatif (-) |

---

#### 2. PCM5102A I2S DAC (32-bit / 384kHz Destekli)
PCM5102A harici I2C veya MCLK gerektirmez; dahili PLL ile saat üretir.

| PCM5102A Pin | ESP32-P4 GPIO | Açıklama |
|:---|:---|:---|
| **VCC / VIN** | 3.3V veya 5V | Güç Girişi |
| **GND** | GND | Toprak |
| **BCK** | **GPIO 49** | Bit Clock (BCLK) |
| **DIN** | **GPIO 4** | Data In (I2S DOUT) |
| **LCK / LRCK** | **GPIO 5** | Left/Right Clock (Word Select) |
| **SCK** | **GND** | Harici MCLK kapalı (Dahili PLL Modu) |

##### PCM5102A Arkasındaki Lehim Köprüleri (Jumper Ayarları):
* **SCK:** GND (Köprü lehimli veya kablo GND'de)
* **FMT:** GND / L (Standart Philips I2S formatı)
* **XSMT:** 3.3V / H (Soft-Mute kapalı, ses açık)
* **FLT:** GND / L (Normal FIR filtresi)
* **DEMP:** GND / L (De-emphasis kapalı)

> **Not:** Çıkış hat seviyesindedir (Line-Out). Pasif hoparlörü doğrudan sürmez; kulaklık amfisi, aktif hoparlör veya AUX girişe bağlayınız.

---

#### 3. Onboard MicroSD Yuvası (Waveshare ESP32-P4-WIFI6)
Waveshare kartında yerleşik microSD yuvası doğrudan **4-bit SDIO 3.0 SDMMC** hatlarına bağlıdır:

| Sinyal | ESP32-P4 GPIO | Ayar |
|:---|:---|:---|
| **CLK** | **GPIO 43** | Floating (Host Driven) |
| **CMD** | **GPIO 44** | Pull-Up |
| **D0** | **GPIO 39** | Pull-Up |
| **D1** | **GPIO 40** | Pull-Up |
| **D2** | **GPIO 41** | Pull-Up |
| **D3** | **GPIO 42** | Pull-Up |
| **POWER** | **GPIO 45** | Aktif Düşük (LOW) |

* Kart **20 MHz DEFAULT** kararlı modunda çalışır (10 MB/sn). 40 MHz yüksek frekanstaki sinyal bozulmaları ve ESP32-C6 WiFi SDIO çekişmeleri engellenmiştir.

---

#### 4. Butonlar (Diğer Uçları GND'ye Bağlı)

| Buton | ESP32-P4 GPIO | Liste Ekranı | Çalıyor Ekranı | Ses & EQ Ekranı | Menü Ekranı |
|:---|:---|:---|:---|:---|:---|
| **YUKARI** | **GPIO 2** | Önceki Şarkı | Önceki Parçaya Geç | Ses + / Kazanç + (+1dB) | Menüde Yukarı |
| **AŞAĞI** | **GPIO 3** | Sonraki Şarkı | Sonraki Parçaya Geç | Ses − / Kazanç − (-1dB) | Menüde Aşağı |
| **OK** | **GPIO 23** | Seçileni Çal | Duraklat / Devam Et | Sonraki Bant (B0..B7) | Seçime Gir |
| **GERİ** | **GPIO 26** | Ana Menüye Dön | Şarkı Listesine Dön | Ses / Preset / Menü | Geri Dön |

---

### 🛠️ Arduino IDE Kurulumu

1. **Kart Ayarları (Araçlar / Tools Menüsü):**
   * **Kart (Board):** `ESP32P4 Dev Module` (Arduino-ESP32 v3.x kurulu olmalıdır)
   * **Flash Size:** `32MB (256Mb)`
   * **Partition Scheme:** `32M Flash (13MB APP/6.75MB SPIFFS)` veya `4.8MB APP/22MB FATFS`
   * **PSRAM:** **`Enabled`** *(Hi-Res ve OPUS tamponları için ZORUNLUDUR)*
   * **CPU Frequency:** `360MHz`
2. **Kütüphanelerin Yüklenmesi:**
   * **U8g2 Kütüphanesi:** Arduino IDE -> Kütüphane Yöneticisi -> `U8g2` aratıp kurun.
   * **Özel ESP32-audioI2S Kütüphanesi:** Bu depoda `libraries/` klasörü içinde yer alan **`ESP32-audioI2S-master.zip`** dosyasını kullanın. Arduino IDE menüsünden: **Taslak (Sketch) -> Kütüphane Ekle (Include Library) -> .ZIP Kütüphanesi Ekle... (Add .ZIP Library...)** seçip bu zip dosyasını yükleyin.

---

### 📁 SD Kart & Wi-Fi Yapılandırması

1. SD kartınızı **FAT32** olarak biçimlendirin.
2. Şarkılarınızı SD kartın ana dizinine (`/`) veya `/music` klasörüne kopyalayın.
3. **Wi-Fi Ayarları:**
   * SD kartın ana dizinine `wifi.txt` adında bir dosya oluşturun:
     ```text
     WiFi_Ag_Adiniz
     WiFi_Sifreniz
     ```
   * 1. Satır ağ adı, 2. satır şifredir.
   * Eğer SD kartta `wifi.txt` bulunamazsa, kodun başındaki `DEFAULT_WIFI_SSID` ve `DEFAULT_WIFI_PASS` tanımları kullanılır.

---

## 📜 Lisans

Bu proje [MIT Lisansı](LICENSE) kapsamında açık kaynak olarak lisanslanmıştır.
Modifiye edilen ses kütüphanesi [ESP32-audioI2S (schreibfaul1)](https://github.com/schreibfaul1/ESP32-audioI2S) GPL-3.0 lisansına tabidir.