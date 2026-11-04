/*
 *   Copyright (C) 2009-2016 by Jonathan Naylor G4KLX
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *   (2025) Modified by KD0OSS for P25 use in Module17/OpenRTX
 */
#if !defined(P25_H)
#define P25_H

#ifdef PLATFORM_CS7000P
#include "stm32h7xx.h"
#elif defined(PLATFORM_MOD17) || defined(PLATFORM_MD3x0) \
    || defined(PLATFORM_MDUV3x0)
#include "stm32f4xx.h"
#else
#include <stdint.h> /* host builds need no MCU header */
#endif
#include <stdint.h>
#include <audio_path.h>
#include <audio_stream.h>
#include <cstddef>
#include <memory>
#include <array>
#include <dsp.h>
#include <P25/P25Data.h>
#include <P25/P25Audio.h>
#include <P25/P25NID.h>
#include <P25/P25LowSpeedData.h>

#include "P25RX.h"
#include "P25TX.h"

#define ARM_MATH_CM4
#include <P25/vendor/arm_math.h>

void update_tx(uint16_t length);

static constexpr size_t P25_RX_SAMPLE_RATE = 24000;
static constexpr size_t P25_TX_SAMPLE_RATE = 24000;
static constexpr size_t P25_SAMPLE_BUF_SIZE = 8;

class P25_IO
{
public:
    P25_IO();
    ~P25_IO();

    // Hardware specific routines
    void initInt();
    void startInt();
    void terminate();
    void start();
    void reset();
    void process();
    void startBasebandSampling();
    void stopBasebandSampling();
    bool isLocked();
    bool isValid();
    bool update(const bool invertPhase);
    void process_tx();
    void getData(uint8_t *);
    void init_tx();
    void terminate_tx();
    bool start_tx(const bool invertP);
    void stop_tx();
    void setTxHeader(const uint8_t *header, uint8_t len);
    void setTxData(uint8_t *data, uint8_t len);
    void writeEOT();
    void setTXDelay(uint8_t delay);
    void addP25Sync(unsigned char *data);
    void createTxHeader();
    void addBusyBits(unsigned char *data, unsigned int length, bool b1,
                     bool b2);
    void createTxLDU1();
    void createTxLDU2();
    void createTxTerminator();
    void decodeLDUAudio(unsigned char *buffer);
    unsigned char decodeNID(unsigned char *data);
    void decode();
    void writeSyncFrames();
    unsigned int getSrcId();
    unsigned int getDstId();
    uint8_t getLcf()
    {
        return m_rfData.getLCF();
    }
    void setSrcId(unsigned int id);
    void setDstId(unsigned int id);
    void setNac(unsigned int nac);
    bool startThread(const bool invertP, void *(*func)(void *));
    void stopThread();
    void setTxLevel(uint8_t level);

    uint16_t getRxLduCount() const
    {
        return m_rxLduCount;
    }

private:
    std::unique_ptr<int16_t[]>
        baseband_rxbuffer; ///< Buffer for baseband audio handling.
    pathId basebandPath;
    streamId basebandId;   ///< Id of the baseband input stream.
    bool locked;
    bool txRunning;        ///< Transmission running.
    bool invPhase;         ///< Invert signal phase
    bool rx_started;
    bool dataValid;

    bool m_pttInvert;
    bool m_regen;
    uint32_t m_srcId;
    uint32_t m_dstId;
    uint8_t m_rxLevel;
    uint8_t m_txLevel;

    // Bring-up diagnostics counters
    uint16_t m_bsFail;
    uint16_t m_rxLduCount;

    bool dac_running;
    pthread_t dacThread;
    pthread_attr_t dacAttr;

    struct dcBlock dcrState;

    CP25RX p25RX;
    CP25TX p25TX;
    CP25NID m_nid;
    CP25Audio m_audio;
    CP25Data m_rfData;
    CP25LowSpeedData m_txLSD;
    bool m_started;

    arm_fir_instance_q15 m_boxcar5Filter;
    q15_t m_boxcar5State[30U]; // NoTaps + BlockSize - 1,  6 + 20 - 1 plus some spare
    uint16_t m_rssi;
};
#endif
