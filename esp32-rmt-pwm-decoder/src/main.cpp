#include "esp32-rmt-pwm-decoder.h"
#include "RidiculouslySmallDfPlayer.h"
#include "EyeMovements.h"
//#include "MSGEQ7.h"
#include <Arduino.h>

RxDecoder rfReceiver;
static const uint8_t RF_RECEIVER_PIN = GPIO_NUM_11;

//DF Player
static const uint8_t DFPLAYER_TX = GPIO_NUM_37;  
static const uint8_t DFPLAYER_RX = GPIO_NUM_36; 
static const uint8_t DFPLAYER_BUSY = GPIO_NUM_38;
static const uint8_t DFPLAYER_DAC_L = GPIO_NUM_41; // for FFT processing
static const uint32_t DFPLAYER_BAUD = 9600;
RidiculouslySmallDfPlayer dfPlayer;
HardwareSerial DfPlayerSerial(2);

#if 0
//MSGEQ7
static const uint8_t  MSGEQ7_MULTIPLEX_OUT = GPIO_NUM_7; // msgeq7 pin 3
static const uint8_t MSGEQ7_STROBE_PIN = GPIO_NUM_16 ; // msgeq7 pin 4
//static const uint8_t  MSGEQ7_AUDIO_IN_FROM_DFPLAYER = 0; // not needed
static const uint8_t MSGEQ7_RESET_PIN = GPIO_NUM_15;  // msgeq7 pin 7
static const uint8_t MSGEQ7_OSC_CLK_PIN = GPIO_NUM_6; // msgeq7 pin 8
static const uint32_t MSGEQ7_OSC_CLK_FREQ = 160000; //a number in range of optimal freq of msgeq7
                                                    // divisible by 80MHz, thus avoiding jitter.
MSGEQ7 msgeq7;
#endif

//eyes
static const uint8_t LEFT_EYE = 21;
static const uint8_t RIGHT_EYE = 47;
EyeMovements ledEyes;

void setup() {
  Serial.begin(115200);
  //rf receiver
  rfReceiver.setRxPin(RF_RECEIVER_PIN);

  //df player
  DfPlayerSerial.begin(DFPLAYER_BAUD, SERIAL_8N1, DFPLAYER_RX, DFPLAYER_TX);
  dfPlayer.begin(DfPlayerSerial, DFPLAYER_BUSY);
  //I don't know if I need these delays...
  delay(3000);
  dfPlayer.resetPlayer();
  delay(3000);
  dfPlayer.setVolume(1);
  
  ledEyes.setEyePins(LEFT_EYE,RIGHT_EYE);
  //let the user know the system is online.
  ledEyes.bothEyesOn();
  delay(2000);
  ledEyes.eyesOff();


#if 0
  msgeq7.initialize(MSGEQ7_MULTIPLEX_OUT /*msgeq7 pin 3*/, MSGEQ7_STROBE_PIN /*msgeq7 pin 4*/,
                    MSGEQ7_RESET_PIN /* msgeq7 pin 7*/ , MSGEQ7_OSC_CLK_PIN /*pin 8 */, 
                    MSGEQ7_OSC_CLK_FREQ /*in hz*/);

  msgeq7.beginOscClock();
#endif
  delay(5000);
  xTaskCreatePinnedToCore(rfReceiver.rxSignalHandler, "rxSignalHandler", 4096, NULL, 10, NULL, 1);
  Serial.printf("setup complete\n"); 
}

uint32_t decoded;
bool isBusy = false;
uint8_t dude = 0;
void loop() {
  isBusy = dfPlayer.isBusy();
  if(isBusy) {
    ledEyes.bothEyesOn();    
    dude = 1;
  } else if (!(isBusy) && (dude == 1)) {
    ledEyes.eyesOff();
    Serial.printf("player is done\n");
    dude = 0;
  }

  if (rfReceiver.available()) {
    decoded = rfReceiver.getReceivedValue();
    switch (decoded) {   //translate fob codes (first 4)
      case 0x4C8568:
        Serial.printf("button 1\n");
        dfPlayer.playTrack(1);
        break;
      case 0x4C8564:
        Serial.printf("button 2\n");   
        dfPlayer.playTrack(2);       
        break;
      case 0x4C856C:
        Serial.printf("button 3\n");
        dfPlayer.playTrack(3);
        break;
      case 0x4C8562:
        Serial.printf("button 4\n");
        dfPlayer.playTrack(4);
        break;
      case 0x4C856A  :
        Serial.printf("button 5\n");
        break;
      case 0x4C8566:
        Serial.printf("button 6\n");          
        break;
      case 0x4C856E:
        Serial.printf("button 7\n");
        break;
      case 0x4C8561:
        Serial.printf("button 8\n");
        dfPlayer.stopPlaying();
        break;
      default:
        Serial.printf("Unknown signal: 0x%X bit length: %d\n", 
          rfReceiver.getReceivedValue(), rfReceiver.getReceivedBitlength() );
          break;
    } // end of switch
    rfReceiver.resetAvailable();
  }
}
