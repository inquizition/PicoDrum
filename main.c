#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "pico/binary_info.h"

#include "bsp/board.h"
#include "tusb.h"
#include "midi.h"
#include "adc.h"

enum  {
  BLINK_NOT_MOUNTED = 250,
  BLINK_MOUNTED = 1000,
  BLINK_SUSPENDED = 2500,
};

#define GPIO_WATCH_PIN 4

static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;
const uint LED_PIN = PICO_DEFAULT_LED_PIN;
const uint TEST_PIN = 16;

static uint32_t last_edge_time;
static bool adc_gated = false;
static bool high_pulse = false;
const uint32_t debounce_ms = 1;

void led_blinking_task(void);
void midi_task(void);

void gpio_callback(uint gpio, uint32_t events) {
  if (events & GPIO_IRQ_EDGE_RISE) 
  {
    if(!adc_gated && high_pulse)
    {
      adc_stop_func();
      gpio_put(TEST_PIN, 0);   // low
      adc_gated = true;
      last_edge_time = board_millis();
      high_pulse = false;
    }
  }
  if (events & GPIO_IRQ_EDGE_FALL)
  {
    if(!adc_gated && !high_pulse)
    {
      high_pulse = true;
      adc_start_func();
      gpio_put(TEST_PIN, 1);   // low
    }
  }
}

int main() {
  board_init();
  stdio_init_all();
  tusb_init();
  gpio_init(GPIO_WATCH_PIN);
  gpio_init(TEST_PIN);
  gpio_set_dir(TEST_PIN, GPIO_OUT);
  gpio_put(TEST_PIN, 0);   // low

  gpio_set_irq_enabled_with_callback(GPIO_WATCH_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
  gpio_pull_up(GPIO_WATCH_PIN);

  printf("Hello!\n");
  adc_init_test();

  while (1)
  {
    tud_task(); // tinyusb device task
    led_blinking_task();
    adc_task();

    if(adc_gated)
    {
      if (board_millis() - last_edge_time > 30)
      {
        adc_gated = false;
      }
    }
    
  }
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
  blink_interval_ms = BLINK_MOUNTED;
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
  blink_interval_ms = BLINK_NOT_MOUNTED;
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
  (void) remote_wakeup_en;
  blink_interval_ms = BLINK_SUSPENDED;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
  blink_interval_ms = BLINK_MOUNTED;
}

//--------------------------------------------------------------------+
// BLINKING TASK
//--------------------------------------------------------------------+
void led_blinking_task(void)
{
  static uint32_t start_ms = 0;
  static bool led_state = false;

  // Blink every interval ms
  if ( board_millis() - start_ms < blink_interval_ms) return; // not enough time
  start_ms += blink_interval_ms;

  board_led_write(led_state);
  led_state = 1 - led_state; // toggle
}
