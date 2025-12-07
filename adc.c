/**
 * Copyright (c) 2021 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "adc.h"

// This example uses the DMA to capture many samples from the ADC.
//
// - We are putting the ADC in free-running capture mode at 0.5 Msps
//
// - A DMA channel will be attached to the ADC sample FIFO
//
// - Configure the ADC to right-shift samples to 8 bits of significance, so we
//   can DMA into a byte buffer
//
// This could be extended to use the ADC's round robin feature to sample two
// channels concurrently at 0.25 Msps each.
//
// It would be nice to have some analog samples to measure! This example also
// drives waves out through a 5-bit resistor DAC, as found on the reference
// VGA board. If you have that board, you can take an M-F jumper wire from
// GPIO 26 to the Green pin on the VGA connector (top row, next-but-rightmost
// hole). Or you can ignore that part of the code and connect your own signal
// to the ADC input.

//uint8_t capture_buf[CAPTURE_DEPTH];
// Global
#define CAPTURE_DEPTH  8192
uint8_t  capture_buf[CAPTURE_DEPTH];
int      dma_chan;
volatile uint32_t samples_captured = 0;
volatile bool capture_done = false;

void core1_main();

//static uint dma_chan;

int adc_init_test() {

    //stdio_init_all();

    // // Send core 1 off to start driving the "DAC" whilst we configure the ADC.
    // multicore_launch_core1(core1_main);

        // Init GPIO for analogue use: hi-Z, no pulls, disable digital input buffer.
    adc_gpio_init(26 + CAPTURE_CHANNEL);

    adc_init();
    adc_select_input(CAPTURE_CHANNEL);
    adc_fifo_setup(
        true,    // Write each completed conversion to the sample FIFO
        true,    // Enable DMA data request (DREQ)
        1,       // DREQ (and IRQ) asserted when at least 1 sample present
        false,   // We won't see the ERR bit because of 8 bit reads; disable.
        true     // Shift each sample to 8 bits when pushing to FIFO
    );

    // Divisor of 0 -> full speed. Free-running capture with the divider is
    // equivalent to pressing the ADC_CS_START_ONCE button once per `div + 1`
    // cycles (div not necessarily an integer). Each conversion takes 96
    // cycles, so in general you want a divider of 0 (hold down the button
    // continuously) or > 95 (take samples less frequently than 96 cycle
    // intervals). This is all timed by the 48 MHz ADC clock.
    float target_hz   = 20000.0f;        // 20 kSps is plenty for piezo hits
    float clk_adc_hz  = 48000000.0f;
    float div         = clk_adc_hz / target_hz - 1.0f;
    adc_set_clkdiv(div);


    // Set up the DMA to start transferring data as soon as it appears in FIFO
    dma_chan = dma_claim_unused_channel(true);
    dma_channel_config cfg = dma_channel_get_default_config(dma_chan);

    // Reading from constant address, writing to incrementing byte addresses
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, true);

    // Pace transfers based on availability of ADC samples
    channel_config_set_dreq(&cfg, DREQ_ADC);

    dma_channel_configure(dma_chan, &cfg,
        capture_buf,    // dst
        &adc_hw->fifo,  // src
        CAPTURE_DEPTH,  // transfer count
        false            // start immediately
    );

    adc_run(true);
}

void read_adc()
{
    printf("Starting capture\n");

    adc_fifo_drain();
    // Re-arm DMA: reset write address and restart
    dma_channel_set_write_addr(dma_chan, capture_buf, true);
    
    adc_run(true);
        // Once DMA finishes, stop any new conversions from starting, and clean up
    // the FIFO in case the ADC was still mid-conversion.
    dma_channel_wait_for_finish_blocking(dma_chan);
    adc_run(false);
    adc_fifo_drain();

    //printf("Capture finished\n");

    // Print samples to stdout so you can display them in pyplot, excel, matlab
    for (int i = 0; i < CAPTURE_DEPTH; ++i) {
        printf("%-3d, ", capture_buf[i]);
        if (i % 10 == 9)
            printf("\n");
    }

    
}

volatile bool adc_start = false;
volatile bool adc_stop  = false;

void adc_start_func()
{
        // 1) flush old samples so burst aligns to edge
        while (!adc_fifo_is_empty()) {
            (void)adc_hw->fifo;
        }

        // 2) clear any ADC error/overrun flags
        adc_hw->fcs |= ADC_FCS_ERR_BITS;

        // 3) re-arm DMA for a fresh capture
        dma_channel_abort(dma_chan);  // cancel any previous transfer

        dma_channel_set_write_addr(dma_chan, capture_buf, false);
        dma_channel_set_trans_count(dma_chan, CAPTURE_DEPTH, true);
        // ^ 'true' starts the DMA immediately
}

void adc_stop_func(void)
{
    // Read remaining transfers *before* aborting
    uint32_t remaining = dma_hw->ch[dma_chan].transfer_count;

    // Now stop DMA
    dma_channel_abort(dma_chan);

    // Compute how many samples were actually written this run
    samples_captured = CAPTURE_DEPTH - remaining;

    capture_done = true;
}


typedef struct {
    uint8_t baseline;      // running baseline (idle value)
    uint8_t envelope;      // smoothed absolute level
    uint8_t peak;          // detected peak (output)
} peakdet_t;

void peakdet_init(peakdet_t *pd, uint8_t initial)
{
    pd->baseline = initial;
    pd->envelope = 0;
    pd->peak = 0;
}

uint8_t peakdet_process(peakdet_t *pd, uint8_t sample)
{
    // --- 1. Update baseline slowly (low-pass) ---
    // tracks idle level but ignores fast hits
    pd->baseline = (pd->baseline * 15 + sample) >> 4; // ~1/16 update

    // --- 2. Absolute deviation from baseline ---
    int16_t diff = (int16_t)sample - (int16_t)pd->baseline;
    if (diff < 0) diff = -diff;
    uint8_t level = (uint8_t)diff;

    // --- 3. Envelope follower (attack fast, decay slow) ---
    if (level > pd->envelope) {
        pd->envelope = level;            // instant attack
    } else {
        pd->envelope = pd->envelope - (pd->envelope >> 4);  // ~1/16 decay
    }

    // --- 4. Peak detection (holds max until reset) ---
    if (pd->envelope > pd->peak) {
        pd->peak = pd->envelope;
    }

    return pd->peak;
}

void peakdet_reset(peakdet_t *pd)
{
    pd->peak = 0;
}

// remap raw peak (0–255) so that:
//   100 -> 0
//   200 -> 127
static inline uint8_t map_peak(uint8_t peak)
{
    // clamp to [100,200]
    if (peak <= 120) return 5;
    if (peak >= 200) return 127;

    // linear map (peak - 100) from 0–100 into 0–127
    return (uint8_t)(((uint16_t)(peak - 120) * 127) / 120);
}

uint8_t remap(float x)
{
    if (x <= 20.0)  return 1;
    if (x >= 110.0) return 127;

    // scale (x - 20) from 0–100 into 1–127
    return (uint8_t)(1 + (float)(x - 20.0) * 116.0 / 100.0);
}

void process_samples(const uint8_t *buf, uint32_t n)
{
    uint32_t sum = 0;
    uint32_t count = 0;
    uint8_t  max_sample = 0;

    // 1) compute average over non-zero samples
    //    and find raw peak
    for (uint32_t i = 0; i < n; ++i) {
        uint8_t s = buf[i];
        if (s <= 1) continue;        // skip silence / noise
        //putchar_raw(buf[i]);
        if (s > max_sample) {
            max_sample = s;
        }

        sum   += s;
        count += 1;
    }

    float avg = (count > 0) ? (float)sum / (float)count : 0.0f;

    // 2) Convert peak to MIDI velocity (0–127)
    uint8_t peak = max_sample;   // raw peak
    float avg_of_all = (((float)avg*0.2 + (float)peak))/2.0;
    uint8_t vel  = remap(avg_of_all);

    if(vel > 2)
    {
        send_midi(vel);
    }

    //printf("Avg: %.3f, Peak: %u, Vel: %u, samples: %d, avgofall: %f\n", avg, peak, vel, count, avg_of_all);
}


void adc_task(void)
{
    if (capture_done) {
        capture_done = false;

        uint32_t n = samples_captured;
        //if (n > CAPTURE_DEPTH) n = CAPTURE_DEPTH;  // paranoia

        //static uint32_t start_ms = 0;

        //if (board_millis() - start_ms < 40) return; // not enough time
        //start_ms = board_millis();
        process_samples(capture_buf, n);
    }
        //     // Flush any old junk
        // adc_fifo_drain();

        // // Re-arm DMA for a full buffer
        // dma_channel_set_write_addr(dma_chan, capture_buf, true);           // start DMA
        // dma_channel_set_trans_count(dma_chan, CAPTURE_DEPTH, true);       // (true) starts it

        // // Wait until buffer is full
        // dma_channel_wait_for_finish_blocking(dma_chan);

        // // Just in case ADC was mid-conversion
        // adc_fifo_drain();

        // // Process/print this block
        // process_samples(capture_buf, CAPTURE_DEPTH);

}