#include "esp32-rmt-pwm-decoder.h"
#include <Arduino.h>
#include "esp_timer.h"

RxDecoder rfReceiver;
static const gpio_num_t RF_RECEIVER_PIN = GPIO_NUM_11;

volatile uint8_t fifty_hz_timer_flag = 0;
#define FLAG_50HZ 0 
static void fifty_hz_timer_callback(void *arg) {
  /* This pushes handling into loop(). For this reason avoid using delay() */
 /* Waits may be implemented with countdowns in loop() using the 50Hz tick */
  fifty_hz_timer_flag |= (1 << FLAG_50HZ);
}
static esp_timer_handle_t fifty_hz_timer_handle = NULL;
const esp_timer_create_args_t fifty_hz_timer_config = {
  .callback = &fifty_hz_timer_callback, // The callback function to be executed
  .arg = NULL, // Optional argument to pass to the callback
  .name = "fifty_hz_timer" // Optional name for debugging
};

void setup() {
  Serial.begin(115200);
  //rf receiver
  rfReceiver.setRxPin(RF_RECEIVER_PIN);

  delay(5000); // not really needed, but it leaves time to look at the serial port to watch everything get initialized
  xTaskCreatePinnedToCore(rfReceiver.rxSignalHandler, "rxSignalHandler", 4096, NULL, 10, NULL, 1);

  esp_timer_create(&fifty_hz_timer_config, &fifty_hz_timer_handle);
  esp_timer_start_periodic(fifty_hz_timer_handle, 20000); // 50hz - 5 times per second
  Serial.printf("setup complete\n"); 
}

uint32_t decoded;
bool isBusy = false;

void loop() {

  if (fifty_hz_timer_flag & (1 << FLAG_50HZ)){// runs 50 times per sec
    fifty_hz_timer_flag &= ~(1 << FLAG_50HZ); //reset.

    if (rfReceiver.available()) {
      decoded = rfReceiver.getReceivedValue();
      switch (decoded) {   //translate fob codes (first 4)
        case 0x4C8568:
          Serial.printf("button 1\n");    
          break;
        case 0x4C8564:
          Serial.printf("button 2\n");      
          break;
        case 0x4C856C:
          Serial.printf("button 3\n");      
          break;
        case 0x4C8562:
          Serial.printf("button 4\n");
          Serial.printf("## code: 0x%X bit length: %d\n", 
            rfReceiver.getReceivedValue(), rfReceiver.getReceivedBitlength() );    
          break;
        case 0x4C856A  :
          Serial.printf("button 5\n");
          Serial.printf("## code: 0x%X bit length: %d\n", 
            rfReceiver.getReceivedValue(), rfReceiver.getReceivedBitlength() );          
          break;
        case 0x4C8566:
          Serial.printf("button 6\n");          
          break;
        case 0x4C856E:
          Serial.printf("button 7\n");
          break;
        case 0x4C8561:
          Serial.printf("button 8\n");
          Serial.printf("## code: 0x%X bit length: %d\n", 
            rfReceiver.getReceivedValue(), rfReceiver.getReceivedBitlength() );
          delay(50); // give the player 50 ms to stop
          break;
        default:
          Serial.printf("Unknown signal: 0x%X bit length: %d\n", 
            rfReceiver.getReceivedValue(), rfReceiver.getReceivedBitlength() );
            break;
      } // end of switch
      rfReceiver.resetAvailable();    
    } //if receiver

  } //if (fifty_hz_timer_flag
  vTaskDelay(500); // non-blocking delay. Not needed if more stuff is happening in loop
}  //loop
