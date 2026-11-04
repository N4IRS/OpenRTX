/*
 * SPDX-FileCopyrightText: Copyright 2022-2026 OpenRTX Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/***************************************************************************
 *   Copyright (C) 2022 - 2023 by Federico Amedeo Izzo IU2NUO,             *
 *                                Niccolò Izzo IU2KIN                      *
 *                                Frederik Saraci IU2NRO                   *
 *                                Silvano Seva IU2KWO                      *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, see <http://www.gnu.org/licenses/>   *
 *                                                                         *
 *   (2025) Modified by KD0OSS for P25 on Module17/OpenRTX                 *
 ***************************************************************************/

#include <audio_stream.h>
#include <imbe_audio_codec.h>
#include <interfaces/delays.h>
#include <pthread.h>
#include <stdint.h>
#include <threads.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <zephyr/kernel.h>
#include <errno.h>
#include <dsp.h>
#include <cstdint>
#include <P25/vendor/RingBuffer.h>
#include <imbe_vocoder_api.h>

static pathId audioPath;

static uint8_t initCnt = 0;
static bool running;

static bool reqStop;
static pthread_t codecThread;
static pthread_attr_t codecAttr;
static void *codecStack = NULL;
static pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;
static int16_t decodeTable[256];

/*
 * Multiply an audio sample by a gain with saturation.  On speech peaks the
 * plain int16 products used before this helper overflowed and wrapped (sign
 * inversion), which is audible as hard distortion.
 */
static inline int16_t scaleSample(int16_t sample, int gain)
{
    int32_t value = static_cast<int32_t>(sample) * gain;

    if (value > 32767)
        value = 32767;
    else if (value < -32768)
        value = -32768;

    return static_cast<int16_t>(value);
}

#ifdef PLATFORM_MOD17
//#include <drivers/USART3_MOD17.h> // used for debugging
#include <drivers/usb_vcom.h>
static const uint8_t micGainPre = 4;
static const uint8_t micGainPost = 3;
#else
#ifndef PLATFORM_LINUX
/*
 * Single microphone gain for the IMBE encoder.  The M17 codec path applies
 * CONFIG_MIC_GAIN to the same ADC stream, so match it: that is the level the
 * vocoder was designed for.  Deviation is set by the P25 TX Level control,
 * not by overdriving the vocoder input.
 */
#ifndef CONFIG_MIC_GAIN
#define CONFIG_MIC_GAIN 13
#endif
static const int micGain = CONFIG_MIC_GAIN;

/*
 * Transmit audio shaping for IMBE.
 *
 * IMBE codes a 3.4 kHz-wide spectrum, so rumble below the speech band only
 * consumes spectral bits: this microphone picks up a lot of it (handling,
 * breath, proximity effect), and the received audio came out boomy and
 * dull.  A second-order high-pass at 200 Hz makes the coded spectrum
 * closer to speech, which the receiver renders as noticeably clearer
 * audio.
 *
 * A high shelf was also tried, because the microphone rolls off towards
 * the top of the band: +5 dB at 1.8 kHz was clearer but harsh, and
 * +2.5 dB was still harsh, while the high-pass alone is clear.  So only
 * the high-pass ships; do not raise the shelf without an on-air A/B.
 *
 * The filter is an RBJ biquad designed for fs = 8 kHz; the coefficients
 * below are their normalised values.
 */
struct biquad {
    float b0, b1, b2, a1, a2;
    float x1, x2, y1, y2;
};

static inline float biquadProcess(struct biquad *q, float x)
{
    float y = q->b0 * x + q->b1 * q->x1 + q->b2 * q->x2 - q->a1 * q->y1
            - q->a2 * q->y2;

    q->x2 = q->x1;
    q->x1 = x;
    q->y2 = q->y1;
    q->y1 = y;

    return y;
}

/* 200 Hz Butterworth high-pass: -3 dB at 200 Hz, -24 dB at 50 Hz. */
static const struct biquad txHighPass = {
    0.89485775f, -1.78971550f, 0.89485775f, -1.77863008f, 0.80080093f,
    0.0f,        0.0f,         0.0f,        0.0f
};
#endif

/*
 * The IMBE decoder already emits full-scale s16 speech: measured peaks on
 * real frames sit around -3 dBFS, and the reference decoders (dvmhost,
 * op25) feed the samples to the DAC at unity gain.  Keep this named and
 * saturating so the level can be raised on air if the speaker path needs
 * it, without ever wrapping.
 */
static const int rxGain = 1;
#endif

extern CRingBuffer<uint8_t> p25PacketBuffer;
imbe_vocoder vocoder;

/*
 * Enhanced encoder mode: leashed-continuity pitch tracking (prefers the
 * previous frame's pitch among near-equal candidates), slightly wider
 * voicing thresholds and a spectral noise gate with a 3 s minimum-
 * statistics floor.
 *
 * Offline A/B on speech: identical level and spectral centroid on
 * continuous speech, no gated speech frames, onsets re-open within one
 * frame (20 ms) after a noisy pause, and quiet speech without background
 * noise is never gated.  The pitch-tracking part is perceptual and cannot
 * be judged from these measurements, so it is being evaluated on air.
 */
static const int txEnh = 1;

static void alawDecode(void);

static void *encodeFunc(void *arg);
static void *decodeFunc(void *arg);
static bool startThread(const pathId path, void *(*func)(void *));
static void stopThread();

void imbe_init()
{
    pthread_mutex_lock(&init_mutex);
    initCnt += 1;
    pthread_mutex_unlock(&init_mutex);
#if defined(PLATFORM_MOD17)
    alawDecode();
#endif
    if (initCnt > 0)
        return;

    running = false;
}

void imbe_terminate()
{
    pthread_mutex_lock(&init_mutex);
    initCnt -= 1;
    pthread_mutex_unlock(&init_mutex);

    if (initCnt > 0)
        return;

    if (running)
        stopThread();
}

bool imbe_startEncode(const pathId path)
{
    return startThread(path, encodeFunc);
}

bool imbe_startDecode(const pathId path)
{
    return startThread(path, decodeFunc);
}

void imbe_stop(const pathId path)
{
    if (running == false)
        return;

    if (audioPath != path)
        return;

    stopThread();
}

bool imbe_running()
{
    return running;
}

void encodeIMBE(int16_t *pcm, uint8_t *imbe)
{
#ifndef PLATFORM_MOD17
    vocoder.encode_4400(pcm, imbe);
#endif
}

void decodeIMBE(int16_t *pcm, uint8_t *imbe)
{
    vocoder.decode_4400(pcm, imbe);
}
#ifdef PLATFORM_MOD17
static unsigned char alawEncode(int16_t sample)
{
    uint16_t samp = (sample & 0xFFFF);
    int sign = (samp & 0x8000) >> 8;
    if (sign != 0) {
        samp = (int16_t)-samp;
        sign = 0x80;
    }

    if (samp > 32635)
        samp = 32635;

    int exp = 7;
    int expMask;
    for (expMask = 0x4000; (samp & expMask) == 0 && exp > 0;
         exp--, expMask >>= 1)
        ;
    int mantis = (samp >> ((exp == 0) ? 4 : (exp + 3))) & 0x0f;
    unsigned char alaw = (unsigned char)(sign | exp << 4 | mantis);
    return (unsigned char)(alaw ^ 0xD5);
} // end alawEncode

static void alawDecode(void)
{
    for (int i = 0; i < 256; i++) {
        int input = (i & 0xFF) ^ 85;
        int mantissa = (input & 15) << 4;
        int segment = (input & 112) >> 4;
        int value = mantissa + 8;
        if (segment >= 1)
            value += 256;
        if (segment > 1)
            value <<= (segment - 1);
        if ((input & 128) == 0)
            value = -value;
        decodeTable[i] = (int16_t)value;
    }
} // end alawDecode
#endif
static void *encodeFunc(void *arg)
{
    streamId iStream;
    pathId iPath = *((pathId *)arg);
    stream_sample_t audioBuf[320];
    struct dcBlock encDcBlock;

    iStream = audioStream_start(iPath, audioBuf, 320, 8000,
                                STREAM_INPUT | BUF_CIRC_DOUBLE);
    if (iStream < 0) {
        pthread_detach(pthread_self());
        running = false;
        return NULL;
    }

    dsp_resetState(encDcBlock);

    vocoder.set_enh(txEnh);

    int byte_count = 0;
#ifndef PLATFORM_MOD17
    /* Fresh filter state for every transmission. */
    struct biquad txHpf = txHighPass;

    uint8_t imbe[11] = { 0x04U, 0x0CU, 0xFDU, 0x7BU, 0xFBU, 0x7DU,
                         0xF2U, 0x7BU, 0x3DU, 0x9EU, 0x45U };
    int16_t cRxBuffer[160 * sizeof(int16_t)];
#else
    uint8_t cRxBuffer[166 * sizeof(uint8_t)];
#endif
    p25PacketBuffer.reset();

    while (reqStop == false) {
        // Invalid path, quit
        if (audioPath_getStatus(iPath) != PATH_OPEN)
            break;

#ifndef PLATFORM_MOD17
        // Bound how far the encoder may run ahead of the transmitter.  The
        // RTX thread converts p25PacketBuffer contents into LDU frames; if
        // several seconds of speech are queued here, releasing PTT discards
        // the backlog and the receiving radio hears little or nothing.
        // ~2 LDU pairs is enough to keep the transmitter fed.
        if (p25PacketBuffer.getData() > 396) {
            sleepFor(0, 5);
            continue;
        }
#endif

        dataBlock_t audio = inputStream_getData(iStream);
        if (audio.data == NULL) {
            // Transient sync failure: keep running, real stream/path
            // termination is detected at the top of the loop.  Breaking
            // here would kill the encoder for the whole transmission and
            // silently substitute the P25 silence frame for all audio.
            continue;
        }

#ifndef PLATFORM_LINUX
        // Pre-amplification stage.  The ADC delivers samples with a DC
        // pedestal (raw 12-bit data, 0..4095): remove it before applying
        // the mic gain, as the M17 codec path does.  The gain is a single
        // saturating multiply, so a pedestal residual can no longer wrap
        // around int16 and bury speech at the vocoder input.
#ifndef PLATFORM_MOD17
        for (size_t i = 0; i < audio.len; i++) {
            float sample = dsp_dcBlockFilter(&encDcBlock, audio.data[i]);
            sample = biquadProcess(&txHpf, sample);
            sample *= micGain;
            if (sample > 32767.0f)
                sample = 32767.0f;
            else if (sample < -32768.0f)
                sample = -32768.0f;
            audio.data[i] = static_cast<int16_t>(sample);
        }
#else
        for (size_t i = 0; i < audio.len; i++)
            audio.data[i] *= micGainPre;
#endif
#ifdef PLATFORM_MOD17
        if (cRxBuffer != NULL) {
            // Six header bytes
            cRxBuffer[byte_count++] = 0x61;
            cRxBuffer[byte_count++] = 0x00;
            cRxBuffer[byte_count++] = 0xA2;
            cRxBuffer[byte_count++] = 0x04; // Type = audio for imbeEncoder
            cRxBuffer[byte_count++] = 0x00;
            cRxBuffer[byte_count++] = 0xA0;

            // Post-amplification stage
            for (size_t i = 0; i < audio.len; i++) {
                cRxBuffer[byte_count++] =
                    alawEncode(audio.data[i] * micGainPost);
            }
            vcom_writeBlock(cRxBuffer, 166);
            byte_count = 0;
        }
#else
        if (cRxBuffer != NULL && p25PacketBuffer.getSpace() >= 11) {
            // Samples are already at the encoder's nominal level.
            for (size_t i = 0; i < audio.len; i++) {
                cRxBuffer[byte_count++] = audio.data[i];
                if (byte_count == 160) {
                    encodeIMBE(cRxBuffer, imbe);
                    for (size_t j = 0; j < 11; j++)
                        p25PacketBuffer.put(imbe[j]);
                    byte_count = 0;
                }
            }
            byte_count = 0;
        }
#endif
#endif
    }

    audioStream_terminate(iStream);

    // In case thread terminates due to invalid path or stream error, detach it
    // to ensure that its memory gets freed by the OS.
    if (reqStop == false)
        pthread_detach(pthread_self());

    running = false;
    return NULL;
}

static void *decodeFunc(void *arg)
{
    streamId oStream;
    pathId oPath = *((pathId *)arg);
    stream_sample_t audioBuf[320];

    // Open output stream
    memset(audioBuf, 0x00, 320 * sizeof(stream_sample_t));
    oStream = audioStream_start(oPath, audioBuf, 320, 8000,
                                STREAM_OUTPUT | BUF_CIRC_DOUBLE);

    if (oStream < 0) {
        pthread_detach(pthread_self());
        running = false;
        return NULL;
    }

    //    uint8_t  *siAudioBuf = (uint8_t*)malloc(320);
    uint8_t siAudioBuf[320];
#ifdef PLATFORM_MOD17
    uint8_t cRxBuffer[166];
#else
    int16_t pcm[160];
    uint8_t imbe[11] = { 0x04U, 0x0CU, 0xFDU, 0x7BU, 0xFBU, 0x7DU,
                         0xF2U, 0x7BU, 0x3DU, 0x9EU, 0x45U }; // silence

#endif
    // Ensure that thread start is correctly synchronized with the output
    // stream to avoid having the decode function writing in a memory area
    // being read at the same time by the output stream system causing cracking
    // noises at speaker output. Behavior observed on both Module17
    outputStream_sync(oStream, false);
    int frame = -1;

    while (reqStop == false) {
        // Invalid path, quit
        if (audioPath_getStatus(oPath) != PATH_OPEN)
            break;

        bool newData = false;
        stream_sample_t *audioBuf = outputStream_getIdleBuffer(oStream);
#ifdef PLATFORM_MOD17
        int byte_count = 0;
        if (vcom_bytesReady() >= 1) {
            // look for start of audio packet
            vcom_readBlock((uint8_t *)cRxBuffer, 1);
            if (cRxBuffer[0] != 0x61)
                continue;
        }
        // Read next 165 bytes of PCM audio from host
        if (vcom_bytesReady() >= 165) {
            byte_count = vcom_readBlock((uint8_t *)cRxBuffer, 165);
            if (byte_count == 165) {
                for (int i = 0; i < byte_count; i++) {
                    if (frame >= 0) {
                        int16_t audio = decodeTable[cRxBuffer[i]];
                        audio *= 2.0F;
                        siAudioBuf[frame++] = audio & 0x00ff;
                        siAudioBuf[frame++] = (audio >> 8) & 0xff;
                        //		audioBuf[frame++] = audio & 0x00ff;
                        //		audioBuf[frame++] = (audio >> 8) & 0xff;
                    }

                    // Check for header bytes
                    if (cRxBuffer[2] == 0x02 && frame < 0) {
                        frame = 0; // start of PCM audio packet
                        i = i + 4; // advance past second header byte
                    }

                    if (frame == 320) {
                        memcpy(audioBuf, (uint8_t *)siAudioBuf, 320);
                        newData = true;
                        frame = -1;
                    }
                }
            }
            memset(cRxBuffer, 0x00, 166);
        }
#else
        if (p25PacketBuffer.getData() >= 11) {
            for (int i = 0; i < 11; i++)
                p25PacketBuffer.get(imbe[i]);
            decodeIMBE(pcm, imbe);
            for (int i = 0; i < 160; i++) {
#ifdef PLATFORM_MOD17
                pcm[i] *= 1.5F;
#else
                // Unity by default; see the rxGain declaration above.
                pcm[i] = scaleSample(pcm[i], rxGain);
#endif
                siAudioBuf[frame++] = pcm[i] & 0x00ff;
                siAudioBuf[frame++] = (pcm[i] >> 8) & 0xff;
            }
            memcpy(audioBuf, (uint8_t *)siAudioBuf, 320);
            newData = true;
            frame = 0;
        }
#endif
        if (audioBuf == NULL)
            break;

        if (!newData) {
            memset(audioBuf, 0x00, 160 * sizeof(stream_sample_t));
        }

        outputStream_sync(oStream, true);
    }

    // Stop stream and wait until its effective termination
    audioStream_stop(oStream);

#ifdef PLATFORM_MOD17
    while (vcom_readBlock((uint8_t *)cRxBuffer, 166) > 0)
        ;
#endif

    // In case thread terminates due to invalid path or stream error, detach it
    // to ensure that its memory gets freed by the OS.
    if (reqStop == false)
        pthread_detach(pthread_self());

    running = false;
    return NULL;
}

static bool startThread(const pathId path, void *(*func)(void *))
{
    // Bad incoming path
    if (audioPath_getStatus(path) != PATH_OPEN)
        return false;

    // Handle access contention when starting the codec thread to ensure that
    // only one call at a time can effectively start the thread.
    pthread_mutex_lock(&init_mutex);
    if (running) {
        // Same path as before, path open, codec already running: all good.
        if (path == audioPath) {
            pthread_mutex_unlock(&init_mutex);
            return true;
        }

        // New path takes over the current one only if it has an higher priority
        // or the current one is closed/suspended.
        pathInfo_t newPath = audioPath_getInfo(path);
        pathInfo_t curPath = audioPath_getInfo(audioPath);
        if ((curPath.status == PATH_OPEN) && (curPath.prio >= newPath.prio)) {
            pthread_mutex_unlock(&init_mutex);
            return false;
        } else {
            stopThread();
        }
    }

    running = true;
    audioPath = path;
    pthread_mutex_unlock(&init_mutex);

    reqStop = false;

    pthread_attr_init(&codecAttr);

#if defined(_MIOSIX)
    // Set stack size of IMBE thread to 16kB.
    pthread_attr_setstacksize(&codecAttr, IMBE_THREAD_STKSIZE);

    // Set priority of IMBE thread to the maximum one, the same of RTX thread.
    struct sched_param param;
    param.sched_priority = THREAD_PRIO_HIGH;
    pthread_attr_setschedparam(&codecAttr, &param);
#elif defined(__ZEPHYR__)
    // Allocate and set the stack for AMBE thread
    codecStack = malloc(IMBE_THREAD_STKSIZE * sizeof(uint8_t));
    pthread_attr_setstack(&codecAttr, codecStack, IMBE_THREAD_STKSIZE);
#endif

    // Start thread
    int ret = pthread_create(&codecThread, &codecAttr, func, &audioPath);
    if (ret < 0)
        running = false;

    return running;
}

static void stopThread()
{
    reqStop = true;
    pthread_join(codecThread, NULL);
    running = false;

#ifdef __ZEPHYR__
    if (codecStack != NULL) {
        free(codecStack);
        codecStack = NULL;
    }
#endif
}
