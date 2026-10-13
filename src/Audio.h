/*
 * Audio.h
 *
 */

#pragma once
#pragma GCC optimize("Ofast")
#include "audiolib_structs.hpp"
#include "esp_arduino_version.h"
#include "esp_dsp.h"
#include "psram_unique_ptr.hpp"
#include <Arduino.h>
#include <FFat.h>
#include <FS.h>
#include <NetworkClient.h>
#include <NetworkClientSecure.h>
#include <SD.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include <atomic>
#include <charconv>
#include <codecvt>
#include <deque>
#include <driver/i2s_std.h>
#include <esp32-hal-log.h>
#include <functional>
#include <libb64/cencode.h>
#include <locale>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#ifndef I2S_GPIO_UNUSED
    #define I2S_GPIO_UNUSED -1 // = I2S_PIN_NO_CHANGE in IDF < 5
#endif

extern __attribute__((weak)) void audio_process_raw_samples(int32_t* outBuff, int16_t validSamples);            // before volume, gain and equalizer, record audiodata
extern __attribute__((weak)) void audio_process_i2s(int32_t* outBuff, int16_t validSamples, bool* continueI2S); // after volume, gain and equalizer, send via BT
extern char                       audioI2SVers[];
class Decoder; // prototype

//----------------------------------------------------------------------------------------------------------------------

//  The reserve area contains a copy of the first bytes of the main buffer.
//  Therefore every decoder always receives one contiguous memory block, even if the read position crosses the end of the main buffer.
//
//  Only one memcpy() is required during wrap-around.
//
//  Writer:
//      WiFi / SD
//
//  Reader:
//      Audio decoder
//
//  Reader and writer may work simultaneously.
// ——————————————————————————————————————————————————————————————————————————————

class AudioBuffer {
#define ANSI_ESC_RED   "\033[31m"
#define ANSI_ESC_RESET "\033[0m"
  public:
    // ------------------------------------------------------------
    // Configuration
    // ------------------------------------------------------------

    static constexpr size_t DEFAULT_MAIN_BUFFER_SIZE = UINT16_MAX * 10;
    static constexpr size_t DEFAULT_RESERVE_BUFFER_SIZE = UINT16_MAX;
    // Maximum contiguous block returned by readSpace()/writeSpace().
    // All supported decoders use uint16_t (bytesLeft, bytesAvail, ...). Returning larger blocks has no benefit and would only increase decoder latency.
    static constexpr size_t MAX_TRANSFER_SIZE = UINT16_MAX;

    // ------------------------------------------------------------
    // Construction
    // ------------------------------------------------------------

    AudioBuffer();
    AudioBuffer(size_t mainSize, size_t reserveSize);

    ~AudioBuffer();

    // ------------------------------------------------------------
    // Initialization
    // ------------------------------------------------------------

    size_t init();
    bool   isInitialized() const { return m_initialized; }
    void   reset();

    // ------------------------------------------------------------
    // Configuration
    // ------------------------------------------------------------

    void   setMaxBlocksize(size_t size);
    size_t getMaxBlockSize() const;
    size_t getBufsize() const;

    // ------------------------------------------------------------
    // Buffer information
    // ------------------------------------------------------------

    size_t bufferFilled();
    size_t freeSpace();
    size_t readSpace();
    size_t writeSpace();

    // ------------------------------------------------------------
    // Pointer access
    // ------------------------------------------------------------

    uint8_t* getReadPtr() { return m_readPtr; }   // Pointer remains valid until the next call to bytesWasRead() or reset().
    uint8_t* getWritePtr() { return m_writePtr; } // Pointer remains valid until the next call to bytesWritten() or reset().

    // ------------------------------------------------------------
    // Update pointers
    // ------------------------------------------------------------

    void bytesWritten(size_t bytes);
    void bytesWasRead(size_t bytes);

    // ------------------------------------------------------------
    // Debug
    // ------------------------------------------------------------

    void showStatus();

  protected:
    //-------------------------------------------------------------
    // Buffer configuration
    //-------------------------------------------------------------

    size_t m_mainSize;
    size_t m_reserveSize;
    size_t m_totalSize;
    size_t m_maxBlockSize = 0;
    size_t m_readSpace = 0;
    size_t m_writeSpace = 0;

    //-------------------------------------------------------------
    // Memory
    //-------------------------------------------------------------

    ps_ptr<uint8_t> m_buffer;

    //-------------------------------------------------------------
    // Pointer layout
    //-------------------------------------------------------------

    uint8_t* m_bufferBegin = nullptr;
    uint8_t* m_mainEnd = nullptr;
    uint8_t* m_bufferEnd = nullptr;
    uint8_t* m_readPtr = nullptr;
    uint8_t* m_writePtr = nullptr;

    //-------------------------------------------------------------
    // State
    //-------------------------------------------------------------

    bool m_initialized = false;
    bool m_isEmpty = true;
    bool m_isFull = false;

    //-------------------------------------------------------------
    // Debug
    //-------------------------------------------------------------

    ps_ptr<char> m_log;

    //-------------------------------------------------------------
    // Synchronisation
    //-------------------------------------------------------------

    SemaphoreHandle_t m_mutex = nullptr;

  private:
    //-------------------------------------------------------------
    // Internal helper
    //-------------------------------------------------------------
    void sanityCheck();
};

//----------------------------------------------------------------------------------------------------------------------

class RingBuffer {

  public:
    RingBuffer();
    ~RingBuffer();

    void          setBufsize(size_t size);
    size_t        getBufsize() const;
    size_t        init();
    bool          isInitialized() { return m_init; };
    void          clear();
    void          reset();
    size_t        freeSpace() const;
    size_t        bufferFilled() const;
    size_t        writeSpace() const;
    size_t        readSpace() const;
    int32_t*      getWritePtr();
    int32_t*      getReadPtr();
    bool          bytesWritten(size_t bytes);
    bool          bytesRead(size_t bytes);
    size_t        write(const int32_t* src, size_t words);
    size_t        read(int32_t* dst, size_t words);
    size_t        peek(int32_t* dst, size_t words);
    inline size_t advanceIndex(size_t index, size_t words) const;
    void          showStatus();

  private:
    ps_ptr<int32_t>      m_buffer;
    size_t               m_bufSize = 0;
    size_t               m_readIndex = 0;
    size_t               m_writeIndex = 0;
    size_t               m_used = 0;
    bool                 m_init = false;
    mutable ps_ptr<char> m_log;
};

//----------------------------------------------------------------------------------------------------------------------

class Audio {
  private:
    AudioBuffer InBuff; // instance of input buffer
    RingBuffer  SamplesBuff;

  public:
    Audio(uint8_t i2sPort = I2S_NUM_0);
    ~Audio();
    std::mutex mutex_info; // mutex_info as member

    // callbacks ---------------------------------------------------------
    typedef enum {
        evt_info = 0,
        evt_id3data,
        evt_eof,
        evt_name,
        evt_icydescription,
        evt_streamtitle,
        evt_bitrate,
        evt_icyurl,
        evt_icylogo,
        evt_genre,
        evt_lasthost,
        evt_image,
        evt_lyrics,
        evt_log,
        evt_vu,
        evt_spectrum,
    } event_t;

    // Audio event type descriptions
    static constexpr std::array<const char*, 16> eventStr = {
        "info",            // evt_info
        "id3data",         // evt_id3data
        "eof",             // evt_eof
        "station_name",    // evt_name
        "icy_description", // evt_icydescription
        "streamtitle",     // evt_streamtitle
        "bitrate (b/s)",   // evt_bitrate
        "icy_url",         // evt_icyurl
        "icy_logo",        // evt_icylogo
        "genre",           // evt_genre
        "lasthost",        // evt_lasthost
        "cover_image",     // evt_image
        "lyrics",          // evt_lyrics
        "log",             // evt_log
        "VU",              // evt_vu
        "BANDS",           // evt_spectrum
    };

    typedef struct _msg { // used in info(audio_info_callback());
        const char*           msg = nullptr;
        const char*           s = nullptr;
        event_t               e = (event_t)0; // event type
        int32_t               i2s_num = 0;
        int32_t               arg1 = 0;
        int32_t               arg2 = 0;
        std::vector<uint32_t> vec1 = {}; // apic [pos, len, pos, len, pos, len, ....]
        std::vector<uint32_t> vec2 = {};
    } msg_t;
    inline static std::function<void(msg_t i)> audio_info_callback;
    using VolumeCurveFn = std::function<float(float t)>;
    // -------------------------------------------------------------------
    typedef enum : uint32_t { SR_ORIGIN = 0, SR_44100 = 44100, SR_48000 = 48000 } OutputSR_t;

    bool             openai_speech(const char* api_key, const char* model, const char* input, const char* instructions, const char* voice, const char* response_format, const char* speed);
    audiolib::hwoe_t dismantle_host(const char* host);
    bool             connecttohost(const char* host, const char* user = nullptr, const char* pwd = nullptr);
    bool             connecttospeech(const char* speech, const char* lang);
    bool             connecttoFS(fs::FS& fs, const char* path, int32_t fileStartTime = -1);
    void             setConnectionTimeout(uint16_t timeout_ms, uint16_t timeout_ms_ssl);
    bool             setAudioPlayTime(uint16_t sec);
    bool             setTimeOffset(int sec);
    bool             setPinout(uint8_t BCLK, uint8_t LRC, uint8_t DOUT, int8_t MCLK = I2S_GPIO_UNUSED);
    bool             pauseResume();
    bool             isRunning() { return m_f_running; }
    void             loop();
    uint32_t         stopSong();
    void             forceMono(bool m);
    void             setOutputSampleRate(OutputSR_t sr);
    void             setBalance(float balance = 0.0f);
    void             setVolumeSteps(uint8_t steps);
    uint8_t          getVolumeSteps();
    void             setVolume(uint8_t vol, uint8_t curve = 0);
    void             setVolumeCurve(VolumeCurveFn curve);
    uint8_t          getVolume();
    void             setMute(bool mute);
    bool             getMute();
    int32_t          getI2sPort();
    uint32_t         getFileSize();
    uint32_t         getSampleRate();
    uint8_t          getBitsPerSample();
    uint8_t          getChannels();
    uint32_t         getBitRate();
    uint32_t         getAudioFileDuration();
    uint32_t         getAudioCurrentTime();
    uint32_t         getAudioFilePosition();
    bool             setAudioFilePosition(uint32_t pos);
    uint32_t         inBufferFilled();  // returns the number of stored bytes in the inputbuffer
    uint32_t         inBufferFree();    // returns the number of free bytes in the inputbuffer
    uint32_t         getInBufferSize(); // returns the size of the inputbuffer in bytes
    void             inBufferStatus() { InBuff.showStatus(); }
    void             samplesBufferStatus() { SamplesBuff.showStatus(); }
    void             setTone(float gainLowPass, float gainBandPass, float gainHighPass);
    void             setI2SCommFMT_LSB(bool commFMT);
    int              getCodec() { return m_codec; }
    const char*      getCodecname() { return codecname[m_codec]; }
    const char*      getVersion();
    // —————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————

  private:
    // ------- PRIVATE MEMBERS ----------------------------------------

    enum class HeaderResult {
        Continue,
        ContentTypeSeen,
        Redirect,
        Error,
    };

    bool                      i2s_config();
    std::unique_ptr<Decoder>  createDecoder(const std::string& type);
    void                      destroy_decoder();
    void                      latinToUTF8(ps_ptr<char>& buff, bool UTF8check = true);
    void                      htmlToUTF8(char* str);
    void                      setDefaults(); // free buffers and set defaults
    int32_t                   audioFileRead();
    int32_t                   audioFileRead(uint16_t timeout_ms);
    int32_t                   audioFileRead(uint8_t* buff, size_t len);
    int32_t                   audioFileRead(uint8_t* buff, size_t len, uint16_t timeout_ms);
    int32_t                   audioFileSeek(uint32_t position, size_t len = 0);
    void                      initInBuff();
    bool                      httpPrint(const char* host);
    bool                      httpRange(uint32_t range, uint32_t length = UINT32_MAX);
    void                      processLocalFile();
    void                      processWebStream();
    void                      processWebFile();
    void                      processWebStreamTS();
    void                      processWebStreamHLS();
    void                      playAudioData();
    bool                      readPlayListData();
    ps_ptr<char>              parsePlaylist_M3U();
    ps_ptr<char>              parsePlaylist_PLS();
    ps_ptr<char>              parsePlaylist_ASX();
    ps_ptr<char>              parsePlaylist_M3U8();
    uint16_t                  accomplish_m3u8_url();
    int16_t                   prepare_first_m3u8_url(ps_ptr<char>& playlistBuff);
    ps_ptr<char>              m3u8redirection(uint8_t* codec);
    void                      showCodecParams();
    int                       findNextSync(uint8_t* data, size_t len);
    uint32_t                  decodeError(int8_t res, uint8_t* data, int32_t bytesDecoded);
    uint32_t                  decodeContinue(int8_t res, uint8_t* data, int32_t bytesDecoded, int32_t* bytesLeft);
    int                       sendBytes(uint8_t* data, size_t len);
    void                      setDecoderItems();
    void                      calculateAudioTime(uint16_t bytesDecoderIn, uint16_t bytesDecoderOut);
    void                      showID3Tag(const char* tag, const char* val);
    size_t                    readAudioHeader(uint32_t bytes);
    int                       read_WAV_Header(uint8_t* data, size_t len);
    int                       read_FLAC_Header(uint8_t* data, size_t len);
    int                       read_ID3_Header(uint8_t* data, size_t len);
    int                       read_M4A_Header(uint8_t* data, size_t len);
    size_t                    process_m3u8_ID3_Header(uint8_t* packet);
    bool                      setSampleRate(uint32_t hz);
    bool                      setBitsPerSample(int bits);
    bool                      setChannels(int channels);
    uint32_t                  resampleI2Soutput(audiolib::resampler_t& resampler, int32_t* input, uint32_t inputSamples, int32_t* output);
    void                      cacheSamples();
    void                      playChunk();
    void                      calculateVUlevel(int32_t* buff, size_t len);
    void                      calculateSpectrum(int32_t* buff, size_t len);
    void                      Gain(int32_t* buff, size_t len);
    void                      stereo2mono(int32_t* buff, size_t len);
    void                      IIR_filter(int32_t* buff, size_t len);
    void                      gain_ramp();
    void                      calculateVolumeLimits();
    void                      showstreamtitle(char* ml);
    bool                      parseContentType(ps_ptr<char> ct);
    std::vector<ps_ptr<char>> readHeader();
    HeaderResult              parseHeaderLine(ps_ptr<char> name, ps_ptr<char> value, ps_ptr<char>& redirectUrl);
    bool                      parseHttpResponseHeader();
    bool                      parseHttpRangeHeader();
    bool                      initializeDecoder();
    esp_err_t                 I2Sstart();
    esp_err_t                 I2Sstop();
    void                      zeroI2Sbuff();
    void                      reconfigI2S();
    void                      IIR_calculateCoefficients();
    uint32_t                  streamavail() { return m_client ? m_client->available() : 0; }
    bool                      ts_parsePacket(uint8_t* packet, uint8_t* packetStart, uint8_t* packetLength);
    uint64_t                  getLastGranulePosition(uint8_t codec);

    //+++ create a T A S K  for playAudioData(), output via I2S +++
  public:
    void     setAudioTaskCore(uint8_t coreID);
    uint32_t getHighWatermark();

  private:
    void        startAudioTask(); // starts a task for decode and play
    void        stopAudioTask();  // stops task for audio
    static void audioTaskWrapper(void* param);
    void        audioTask();
    void        performAudioTask();

    //+++ H E L P   F U N C T I O N S +++
    bool                   readMetadata(uint32_t b, uint16_t* readedBytes, bool first = false);
    int32_t                getChunkSize(uint16_t* readedBytes, bool first = false);
    bool                   readID3V1Tag();
    int32_t                newInBuffStart(int32_t resumeFilePos);
    boolean                streamDetection(uint32_t bytesAvail);
    uint32_t               m4a_correctResumeFilePos();
    int32_t                ogg_correctResumeFilePos();
    int32_t                flac_correctResumeFilePos();
    int32_t                mp3_correctResumeFilePos();
    int32_t                wav_correctResumeFilePos();
    uint8_t                determineCodec(uint8_t presumed_codec);
    bool                   get_info();
    void                   trim(char* str);
    bool                   startsWith(const char* base, const char* str);
    int                    indexOf(const char* base, const char* str, int startIndex = 0);
    int                    indexOf(const char* base, char ch, int startIndex = 0);
    int                    specialIndexOf(uint8_t* base, const char* str, int baselen, bool exact = false);
    int32_t                min3(int32_t a, int32_t b, int32_t c);
    uint64_t               bigEndian(uint8_t* base, uint8_t numBytes, uint8_t shiftLeft = 8);
    bool                   b64encode(const char* source, uint16_t sourceLength, char* dest);
    uint32_t               simpleHash(const char* str);
    ps_ptr<char>           urlencode(const char* str, bool spacesOnly);
    audiolib::BiquadCoeffs makeButterworthLPF_Q31(float fs);

  private:
    enum : int { APLL_AUTO = -1, APLL_ENABLE = 1, APLL_DISABLE = 0 };
    enum : int { EXTERNAL_I2S = 0, INTERNAL_DAC = 1, INTERNAL_PDM = 2 };
    enum : int { FORMAT_NONE = 0, FORMAT_M3U = 1, FORMAT_PLS = 2, FORMAT_ASX = 3, FORMAT_M3U8 = 4 }; // playlist formats
    const char* plsFmtStr[5] = {"NONE", "M3U", "PLS", "ASX", "M3U8"};                                // playlist format string
    enum : int { AUDIO_NONE, HTTP_RESPONSE_HEADER, HTTP_RANGE_HEADER, AUDIO_DATA, AUDIO_LOCALFILE, AUDIO_PLAYLISTINIT, AUDIO_PLAYLISTHEADER, AUDIO_PLAYLISTDATA };
    const char* dataModeStr[8] = {"AUDIO_NONE", "HTTP_RESPONSE_HEADER", "HTTP_RANGE_HEADER", "AUDIO_DATA", "AUDIO_LOCALFILE", "AUDIO_PLAYLISTINIT", "AUDIO_PLAYLISTHEADER", "AUDIO_PLAYLISTDATA"};
    enum : int { FLAC_BEGIN = 0, FLAC_MAGIC = 1, FLAC_MBH = 2, FLAC_SINFO = 3, FLAC_PADDING = 4, FLAC_APP = 5, FLAC_SEEK = 6, FLAC_VORBIS = 7, FLAC_CUESHEET = 8, FLAC_PICTURE = 9, FLAC_OKAY = 100 };
