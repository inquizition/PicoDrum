#include <stdio.h>
#include "pico/stdlib.h"
// For ADC input:
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "midi.h"
// For resistor DAC output:
#include "pico/multicore.h"

// Channel 0 is GPIO26
#define CAPTURE_CHANNEL 0
//#define CAPTURE_DEPTH 1000

int adc_init_test(void);
void read_adc(void);

void adc_start_func(void);
void adc_stop_func(void);
void adc_task(void);
void process_samples(const uint8_t *buf, uint32_t n);