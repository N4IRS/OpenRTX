/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * 
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "core/ui.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "core/event.h"
#include "core/state.h"
#include "core/battery.h"
#include "hwconfig.h"
#include "interfaces/platform.h"
#include "interfaces/nvmem.h"
#include "interfaces/delays.h"

state_t state;
pthread_mutex_t state_mutex;
static long long int lastUpdate = 0;

#ifdef NVMEM_DEBOUNCED_WRITE
    #ifndef NVM_WRITE_DEBOUNCE_MS
    #define NVM_WRITE_DEBOUNCE_MS 3000
    #endif

static settings_t lastSavedSettings;
static settings_t lastSeenSettings;
static channel_t  lastSavedChannel;
static channel_t  lastSeenChannel;
static long long  lastChangeTick = 0;

/**
 * Debounced persistence for platforms without a graceful shutdown.
 *
 * Settings and VFO changes are stored once they differ from what was last
 * written and have been stable for NVM_WRITE_DEBOUNCE_MS. Comparing against
 * snapshots means no "mark dirty" calls have to be scattered across every UI
 * path that touches the state, and continuous adjustment (a knob, a held key)
 * keeps postponing the write instead of triggering one per interval.
 */
static void persistDebounced(void)
{
    settings_t settings;
    channel_t  channel;

    pthread_mutex_lock(&state_mutex);
    settings = state.settings;
    channel  = state.channel;
    pthread_mutex_unlock(&state_mutex);

    // Any difference from the last value observed means the user is still
    // adjusting something: restart the stability timer and wait.
    if ((memcmp(&settings, &lastSeenSettings, sizeof(settings_t)) != 0) ||
        (memcmp(&channel,  &lastSeenChannel,  sizeof(channel_t))  != 0))
    {
        lastSeenSettings = settings;
        lastSeenChannel  = channel;
        lastChangeTick   = getTick();
        return;
    }

    bool differsFromSaved =
        (memcmp(&settings, &lastSavedSettings, sizeof(settings_t)) != 0) ||
        (memcmp(&channel,  &lastSavedChannel,  sizeof(channel_t))  != 0);

    if (!differsFromSaved)
        return;

    if ((getTick() - lastChangeTick) < NVM_WRITE_DEBOUNCE_MS)
        return;

    nvm_writeSettingsAndVfo(&settings, &channel);
    lastSavedSettings = settings;
    lastSavedChannel  = channel;
}
#endif

// Commonly used frequency steps, expressed in Hz
const uint32_t freq_steps[] = { 1000,  5000,  6250,  10000, 12500,
                                15000, 20000, 25000, 50000, 100000 };
const size_t n_freq_steps = sizeof(freq_steps) / sizeof(freq_steps[0]);

void state_init()
{
    pthread_mutex_init(&state_mutex, NULL);

    /*
     * Try loading settings from nonvolatile memory and default to sane values
     * in case of failure.
     */
    if (nvm_readSettings(&state.settings) < 0) {
        state.settings = default_settings;
    }

    /*
     * Try loading VFO configuration from nonvolatile memory and default to sane
     * values in case of failure.
     */
    if (nvm_readVfoChannelData(&state.channel) < 0) {
        state.channel = cps_getDefaultChannel();
    }

/*
     * Initialise remaining fields
     */
#ifdef CONFIG_RTC
    state.time = platform_getCurrentTime();
#endif
    state.v_bat = platform_getVbat();
    state.charge = battery_getCharge(state.v_bat);
    state.rssi = -127.0f;
    state.volume = platform_getVolumeLevel();

    state.channel_index = 0; // Set default channel index (it is 0-based)
    state.bank_enabled = false;
    state.rtxStatus = RTX_OFF;
    state.emergency = false;
    state.txDisable = false;
    state.step_index = 4; // Default frequency step 12.5kHz

    // Force brightness field to be in range 0 - 100
    if (state.settings.brightness > 100) {
        state.settings.brightness = 100;
    }

#ifdef NVMEM_DEBOUNCED_WRITE
    // Snapshot what the radio considers persisted, so the debounced writer
    // only triggers on real changes.
    lastSavedSettings = state.settings;
    lastSavedChannel  = state.channel;
    lastSeenSettings  = state.settings;
    lastSeenChannel   = state.channel;
#endif
}

void state_terminate()
{
    // Never store a brightness of 0 to avoid booting with a black screen
    if (state.settings.brightness == 0) {
        state.settings.brightness = 5;
    }

    nvm_writeSettingsAndVfo(&state.settings, &state.channel);
    pthread_mutex_destroy(&state_mutex);
}

void state_task()
{
    // Update radio state once every 100ms
    if ((getTick() - lastUpdate) < 100)
        return;

    lastUpdate = getTick();

    pthread_mutex_lock(&state_mutex);

    /*
     * Low-pass filtering with a time constant of 10s when updated at 1Hz
     * Original computation: state.v_bat = 0.02*vbat + 0.98*state.v_bat
     * Peak error is 18mV when input voltage is 49mV.
     *
     * NOTE: GD77 and DM-1801 already have an hardware low-pass filter on the
     * vbat pin. Adding also the digital one seems to cause more troubles than
     * benefits.
     */
    uint16_t vbat = platform_getVbat();
#if defined(PLATFORM_GD77) || defined(PLATFORM_DM1801)
    state.v_bat = vbat;
#else
    state.v_bat -= (state.v_bat * 2) / 100;
    state.v_bat += (vbat * 2) / 100;
#endif

    /*
     * Update volume level, as a 50% average between previous value and a new
     * read of the knob position. This gives a good reactivity while preventing
     * the volume level to jitter when the knob is not being moved.
     */
    uint16_t vol = platform_getVolumeLevel() + state.volume;
    state.volume = vol / 2;

    state.charge = battery_getCharge(state.v_bat);
    state.rssi = rtx_getRssi();

#ifdef CONFIG_RTC
    state.time = platform_getCurrentTime();
#endif

    pthread_mutex_unlock(&state_mutex);

#ifdef NVMEM_DEBOUNCED_WRITE
    persistDebounced();
#endif

    ui_pushEvent(EVENT_STATUS, 0);
}

void state_resetSettingsAndVfo()
{
    state.settings = default_settings;
    state.channel = cps_getDefaultChannel();
}
