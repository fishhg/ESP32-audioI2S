
/*****************************************************************************************************************************************************
    audio.cpp

    Created on: 28.10.2018                                                                                                  */
char audioI2SVers[] = "\
    Version 4.0.0                                                                                                                          ";
/*  Updated on: Aug 06, 2026

    Author: Wolle (schreibfaul1)
    Audio library for ESP32, ESP32-S3 or ESP32-P4
    Arduino Vers. V3 is mandatory
    PSRAM is mandatory
    external DAC is mandatory

*****************************************************************************************************************************************************/

#include "Audio.h"
#include "aac_decoder/aac_decoder.h"
#include "flac_decoder/flac_decoder.h"
#include "mp3_decoder/mp3_decoder.h"
#include "opus_decoder/opus_decoder.h"
#include "psram_unique_ptr.hpp"
#include "vorbis_decoder/vorbis_decoder.h"
#include "wav_decoder/wav_decoder.h"

// constants
constexpr size_t m_frameSizeWav = 4096;
constexpr size_t m_frameSizeMP3 = 18000;         //  more than one icy-metaint
constexpr size_t m_frameSizeAAC = 18000;         //  more than one icy-metaint
constexpr size_t m_frameSizeFLAC = 24576;      // practical FLAC frame threshold (not UINT16_MAX which deadlocks InBuff)
constexpr size_t m_frameSizeOPUS = UINT16_MAX;   // max ogg size
constexpr size_t m_frameSizeVORBIS = UINT16_MAX; // OGG length is normally 4080 bytes, but can be reach 64KB in the metadata block
constexpr size_t m_outbuffSize = 8192 * 2;       // expanded for Hi-Res 96k/176.4k FLAC blocks
constexpr size_t m_resamplesBuffSize = m_outbuffSize * 8; // SRmin: 6KHz -> SRmax: 48K

constexpr size_t AUDIO_STACK_SIZE = 32768;

// static allocations for Audio task
StaticTask_t __attribute__((unused)) xAudioTaskBuffer;
StackType_t __attribute__((unused))  xAudioStack[AUDIO_STACK_SIZE];
//-------------------------------------------------------------------------------------------------------------------
// weak default implementation - can be overridden by user
__attribute__((weak)) void audio_process_i2s(int32_t* outBuff, int16_t validSamples, bool* continueI2S) {
    // Default: do nothing. User can provide their own implementation to process audio data.
}
__attribute__((weak)) void audio_process_raw_samples(int32_t* outBuff, int16_t validSamples) {
    // Default: do nothing. User can provide their own implementation to process audio data.
}
//-------------------------------------------------------------------------------------------------------------------
static bool IRAM_ATTR i2s_tx_sent_callback(i2s_chan_handle_t handle, i2s_event_data_t* event, void* user_ctx) {
    Audio* audio = static_cast<Audio*>(user_ctx);
    audio->m_dmaFreeDesc.fetch_add(1);
    return false; // keine höhere Task geweckt
}

// —————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————
// 📌📌📌  A U D I O B U F F E R  📌📌📌
// —————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————

// Main buffer:
// +-----------------------------------------------------------+

// Reserve buffer:
// +-------------------------+

// The reserve buffer always contains a copy of the first
// reserveSize bytes of the main buffer.

// This guarantees that every decoder receives one contiguous
// memory block even if reading crosses the end of the main
// buffer.

// Only one memcpy() is required per wrap.
//
//  m_bufferBegin     m_readPtr                  m_writePtr
//   |                  |<------maxAvailableBytes----->|<--------------------- writeSpace ----------------------->|
//   ▼                  ▼                              ▼                                                          ▼
//   ---------------------------------------------------------------------------------------------------------------
//   |                           <--m_mainSize-->                                 |      <--m_reserveSize -->     |
//   ---------------------------------------------------------------------------------------------------------------
//   |<---freeSpace---->|<------------filled---------->|<-------freeSpace-------->|
//
//
//
//  m_bufferBegin                  m_writePtr                 m_readPtr
//   |                                 |<-------writeSpace------>|<---------------maxAvailableBytes-------------->|
//   ▼                                 ▼                         ▼                ▼                               ▼
//   ---------------------------------------------------------------------------------------------------------------
//   |                          <--m_mainSize-->                                  |      <--m_reserveSize -->     |
//   ---------------------------------------------------------------------------------------------------------------
//   |<------------filled------------->|<-------freeSpace------->|<----filled---->|
//
//

AudioBuffer::AudioBuffer() : AudioBuffer(DEFAULT_MAIN_BUFFER_SIZE, DEFAULT_RESERVE_BUFFER_SIZE) {}
AudioBuffer::AudioBuffer(size_t mainSize, size_t reserveSize) : m_mainSize(mainSize), m_reserveSize(reserveSize), m_totalSize(mainSize + reserveSize) {}
AudioBuffer::~AudioBuffer() {}
//----------------------------------------------------------------------------------------------------------------------------------------------------
size_t AudioBuffer::init() {
    if (m_initialized) return m_mainSize;
    m_buffer.alloc(m_totalSize, "AudioBuffer");
    if (!m_buffer) return 0;
    m_mutex = xSemaphoreCreateBinary();
    if (!m_mutex) {
        m_buffer.reset();
        return 0;
    }
    xSemaphoreGive(m_mutex);
    m_log.set_name("\nAudioBuffer_Log");
    m_bufferBegin = m_buffer.get();
    m_mainEnd = m_bufferBegin + m_mainSize;
    m_bufferEnd = m_bufferBegin + m_totalSize;
    reset();
    m_initialized = true;
    return m_mainSize;
}
//----------------------------------------------------------------------------------------------------------------------------------------------------
void AudioBuffer::reset() {

    m_readPtr = m_bufferBegin;
    m_writePtr = m_bufferBegin;

    m_isEmpty = true;
    m_isFull = false;

    m_readSpace = 0;
    m_writeSpace = std::min(MAX_TRANSFER_SIZE, m_mainSize);
}
//----------------------------------------------------------------------------------------------------------------------------------------------------
void AudioBuffer::setMaxBlocksize(size_t size) {
    m_maxBlockSize = size;
}

size_t AudioBuffer::getMaxBlockSize() const {
    return m_maxBlockSize;
}

size_t AudioBuffer::getBufsize() const {
    return m_mainSize;
}
//----------------------------------------------------------------------------------------------------------------------------------------------------
size_t AudioBuffer::bufferFilled() {
    if (!m_initialized) return 0;

    xSemaphoreTake(m_mutex, portMAX_DELAY);
    size_t filled = 0;

    if (m_readPtr == m_writePtr) {

        if (m_isEmpty) {
            filled = 0;
            goto exit;
        }

        if (m_isFull) {
            filled = m_mainSize;
            goto exit;
        }

        // should never happen
        m_log.assignf(ANSI_ESC_RED "[{}:{}] writePtr == readPtr, invalid state" ANSI_ESC_RESET, __FILE__, __LINE__);
        m_log.println();
        goto exit;
    }

    if (m_writePtr > m_readPtr) {
        filled = m_writePtr - m_readPtr;
    } else {
        filled = (m_mainEnd - m_readPtr) + (m_writePtr - m_bufferBegin);
    }

exit:

    xSemaphoreGive(m_mutex);

    return filled;
}
//----------------------------------------------------------------------------------------------------------------------------------------------------
size_t AudioBuffer::freeSpace() {
    if (!m_initialized) return 0;

    xSemaphoreTake(m_mutex, portMAX_DELAY);

    size_t free = 0;

    if (m_readPtr == m_writePtr) {

        if (m_isEmpty) {
            free = m_mainSize;
            goto exit;
        }

        if (m_isFull) {
            free = 0;
            goto exit;
        }

        m_log.assignf(ANSI_ESC_RED "[{}:{}] writePtr == readPtr, invalid state" ANSI_ESC_RESET, __FILE__, __LINE__);
        m_log.println();
        goto exit;
    }

    if (m_readPtr > m_writePtr) {
        free = m_readPtr - m_writePtr;
    } else {
        free = (m_mainEnd - m_writePtr) + (m_readPtr - m_bufferBegin);
    }

exit:
    xSemaphoreGive(m_mutex);
    return free;
}
//----------------------------------------------------------------------------------------------------------------------------------------------------
size_t AudioBuffer::writeSpace() {
    if (!m_initialized) return 0;
    m_writeSpace = 0;

    xSemaphoreTake(m_mutex, portMAX_DELAY);

    //------------------------------------------------------------
    // Space until physical buffer end
    //------------------------------------------------------------

    size_t spaceToEnd = m_bufferEnd - m_writePtr;

    //------------------------------------------------------------
    // Buffer full?
    //------------------------------------------------------------

    if (m_isFull) goto exit;

    //------------------------------------------------------------
    // Empty buffer
    //------------------------------------------------------------

    if (m_isEmpty) {
        m_writeSpace = std::min(MAX_TRANSFER_SIZE, spaceToEnd);
        goto exit;
    }

    //------------------------------------------------------------
    // Reserve buffer completely written?
    //------------------------------------------------------------

    if (spaceToEnd == 0) {
        // Copy reserve area to beginning only if the reader
        // has already released these bytes.

        if (m_readPtr > (m_bufferBegin + m_reserveSize)) {
            memcpy(m_bufferBegin, m_mainEnd, m_reserveSize);
            m_writePtr = m_bufferBegin + m_reserveSize;
        }
        spaceToEnd = m_bufferEnd - m_writePtr;
    }

    //------------------------------------------------------------
    // Writer before reader
    //------------------------------------------------------------

    if (m_writePtr < m_readPtr) {

        // Reader is still inside reserve area.
        // Writer must not enter reserve.

        if (m_readPtr >= m_mainEnd) {
            m_writeSpace = std::min(MAX_TRANSFER_SIZE, (size_t)(m_mainEnd - m_writePtr));
        } else {
            m_writeSpace = std::min(MAX_TRANSFER_SIZE, (size_t)(m_readPtr - m_writePtr));
        }
        goto exit;
    }

    //------------------------------------------------------------
    // Writer behind reader
    //------------------------------------------------------------

    m_writeSpace = std::min(MAX_TRANSFER_SIZE, spaceToEnd);
exit:
    xSemaphoreGive(m_mutex);
    return m_writeSpace;
}

//----------------------------------------------------------------------------------------------------------------------------------------------------
void AudioBuffer::bytesWritten(size_t bytes) {
    if (!m_initialized) return;

    if (!bytes) return;
    xSemaphoreTake(m_mutex, portMAX_DELAY);

    //------------------------------------------------------------
    // The caller must never report more bytes than previously
    // returned by writeSpace().
    //------------------------------------------------------------

    if (bytes > m_writeSpace) {
        m_log.assignf(ANSI_ESC_RED "[{}:{}] writeSpace {}, bytes {}" ANSI_ESC_RESET, __FILE__, __LINE__, m_writeSpace, bytes);
        m_log.println();
        bytes = m_writeSpace;
    }

    //------------------------------------------------------------
    // Prevent writer from overtaking reader.
    //------------------------------------------------------------

    if (m_writePtr < m_readPtr) {
        if (m_writePtr + bytes > m_readPtr) {
            m_log.assignf(ANSI_ESC_RED "[{}:{}] writePtr overruns readPtr "
                                       "(write {}, read {}, bytes {})" ANSI_ESC_RESET,
                          __FILE__, __LINE__, m_writePtr - m_bufferBegin, m_readPtr - m_bufferBegin, bytes);
            m_log.println();
            bytes = m_readPtr - m_writePtr;
        }
    }

    //------------------------------------------------------------
    // Never write behind the physical buffer.
    //------------------------------------------------------------

    if (m_writePtr + bytes > m_bufferEnd) {
        m_log.assignf(ANSI_ESC_RED "[{}:{}] writePtr overruns bufferEnd "
                                   "(write {}, end {}, bytes {})" ANSI_ESC_RESET,
                      __FILE__, __LINE__, m_writePtr - m_bufferBegin, m_bufferEnd - m_bufferBegin, bytes);

        m_log.println();
        bytes = m_bufferEnd - m_writePtr;
    }

    //------------------------------------------------------------
    // Advance write pointer.
    //------------------------------------------------------------

    m_writePtr += bytes;

    //------------------------------------------------------------
    // Update state flags.
    //------------------------------------------------------------

    m_isEmpty = false;
    if (m_writePtr == m_readPtr) m_isFull = true;
    xSemaphoreGive(m_mutex);

#ifdef DEBUG
    sanityCheck();
#endif
}
//----------------------------------------------------------------------------------------------------------------------------------------------------
size_t AudioBuffer::readSpace() {
    if (!m_initialized) return 0;

    xSemaphoreTake(m_mutex, portMAX_DELAY);

    //------------------------------------------------------------
    // Reader reached reserve area?
    // Move it back into the beginning of the main buffer.
    //------------------------------------------------------------

    if (m_readPtr >= m_mainEnd && m_writePtr <= m_mainEnd) {
        const size_t offset = m_readPtr - m_mainEnd;

        if (m_writePtr > (m_bufferBegin + offset)) { m_readPtr = m_bufferBegin + offset; }
    }

    m_readSpace = 0;

    //------------------------------------------------------------
    // Empty buffer?
    //------------------------------------------------------------

    if (m_isEmpty) goto exit;

    //------------------------------------------------------------
    // Buffer completely full.
    //------------------------------------------------------------

    if (m_isFull) {
        m_readSpace = std::min(MAX_TRANSFER_SIZE, (size_t)(m_bufferEnd - m_readPtr));
        goto exit;
    }

    //------------------------------------------------------------
    // Writer is ahead.
    //------------------------------------------------------------

    if (m_writePtr > m_readPtr) {
        m_readSpace = std::min(MAX_TRANSFER_SIZE, (size_t)(m_writePtr - m_readPtr));
        goto exit;
    }

    //------------------------------------------------------------
    // Writer wrapped around.
    //------------------------------------------------------------

    m_readSpace = std::min(MAX_TRANSFER_SIZE, (size_t)(m_bufferEnd - m_readPtr));

exit:
    xSemaphoreGive(m_mutex);
    return m_readSpace;
}

//----------------------------------------------------------------------------------------------------------------------------------------------------
void AudioBuffer::bytesWasRead(size_t bytes) {
    if (!m_initialized) return;

    if (!bytes) return;

    xSemaphoreTake(m_mutex, portMAX_DELAY);

    //------------------------------------------------------------
    // The caller must never report more bytes than previously
    // m_readSpace contains.
    //------------------------------------------------------------

    if (bytes > m_readSpace) {
        m_log.assignf(ANSI_ESC_RED "[{}:{}] readSpace {}, bytes {}" ANSI_ESC_RESET, __FILE__, __LINE__, m_readSpace, bytes);
        m_log.println();
        bytes = m_readSpace;
    }

    //------------------------------------------------------------
    // Prevent reader from overtaking writer.
    //------------------------------------------------------------

    if (m_readPtr < m_writePtr) {
        if (m_readPtr + bytes > m_writePtr) {
            m_log.assignf(ANSI_ESC_RED "[{}:{}] readPtr overruns writePtr "
                                       "(read {}, write {}, bytes {})" ANSI_ESC_RESET,
                          __FILE__, __LINE__, m_readPtr - m_bufferBegin, m_writePtr - m_bufferBegin, bytes);

            m_log.println();
            bytes = m_writePtr - m_readPtr;
        }
    }

    //------------------------------------------------------------
    // Never read behind the physical buffer.
    //------------------------------------------------------------

    if (m_readPtr + bytes > m_bufferEnd) {

        m_log.assignf(ANSI_ESC_RED "[{}:{}] readPtr overruns bufferEnd "
                                   "(read {}, end {}, bytes {})" ANSI_ESC_RESET,
                      __FILE__, __LINE__, m_readPtr - m_bufferBegin, m_bufferEnd - m_bufferBegin, bytes);

        m_log.println();
        bytes = m_bufferEnd - m_readPtr;
    }

    //------------------------------------------------------------
    // Advance read pointer.
    //------------------------------------------------------------

    m_readPtr += bytes;

    //------------------------------------------------------------
    // Update state flags.
    //------------------------------------------------------------

    m_isFull = false;
    if (m_readPtr == m_writePtr) m_isEmpty = true;
    xSemaphoreGive(m_mutex);

#ifdef DEBUG
    sanityCheck();
#endif
}

void AudioBuffer::showStatus() {
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    m_log.assignf("\nfilled {}, free {}\n", bufferFilled(), freeSpace());
    m_log.appendf("writeSpace {}, readSpace {}\n", writeSpace(), readSpace());
    m_log.appendf("writePtr {}, readPtr {}\n", m_writePtr - m_bufferBegin, m_readPtr - m_bufferBegin);
    m_log.appendf("isEmpty {}, isFull {}\n\n", m_isEmpty, m_isFull);
    m_log.print();
    xSemaphoreGive(m_mutex);
}

void AudioBuffer::sanityCheck() {
    assert(m_bufferBegin);
    assert(m_mainEnd == m_bufferBegin + m_mainSize);
    assert(m_bufferEnd == m_mainEnd + m_reserveSize);
    assert(m_readPtr >= m_bufferBegin);
    assert(m_readPtr <= m_bufferEnd);
    assert(m_writePtr >= m_bufferBegin);
    assert(m_writePtr <= m_bufferEnd);
    assert(!(m_isEmpty && m_isFull));
    assert(m_readSpace <= MAX_TRANSFER_SIZE);
    assert(m_writeSpace <= MAX_TRANSFER_SIZE);
    if (m_readPtr == m_writePtr) { assert(m_isEmpty || m_isFull); }
}

// —————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————
// 📌📌📌  R I N G B U F F E R  📌📌📌
// —————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————————

// RingBuffer will be allocated in PSRAM
//
//  start            m_readPtr                    m_writePtr                                                     end
//   |                  |<----------- m_used --------->|<--------------------- writeSpace ----------------------->|
//   ▼                  ▼                              ▼                                                          ▼
//   ---------------------------------------------------------------------------------------------------------------
//   |                                           <--m_bufSize-->                                                   |
//   ----------------------------------------------------------------------------------------------------------