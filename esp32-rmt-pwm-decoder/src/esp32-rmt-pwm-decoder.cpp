/*
  esp32-rmt-pwm-decoder - Arduino libary for decoding an RF keyfob with an ESP32
  Copyright (c) 2025 Doug Brann.  All right reserved. 
  Please see Readme file for list of credits, howto, etc.
  
  Project home: https://github.com/TheDougMiester

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.B00b00


  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/
#include "esp32-rmt-pwm-decoder.h"

#define NUMBER_OF_RMT_SYMBOLS 64 //128
#define BIT_LENGTH 24 // number of ones and zeros in the signal we're trying to read.
void RxDecoder::rxSignalHandler(void* param){
	
	rmt_symbol_word_t symbols[NUMBER_OF_RMT_SYMBOLS];
	rmt_rx_done_event_data_t rx_data = {
		.received_symbols = symbols,
		.num_symbols = NUMBER_OF_RMT_SYMBOLS,
	};
	
	QueueHandle_t rx_queue = xQueueCreate(1, sizeof(rx_data));

	rmt_receive_config_t rx_config = {

	// The max value the register can hold is 255/(rx_ch_conf.clk_src)... a 1MHz 
	// clock would be perfect, since PT2262 pulse widths are around 350µs, 
	// but the standard build won't allow anything larger than 3.1µs, using the
	// RMT_CLK_SRC_RC_FAST clock (17MHz) on the esp32s3
	// I needed a much a higher minimum (like ~200µs), 
	// but anything larger than 255/.clk_src,
	// the buffer overflows and there's a core dump. So, I modified 
	// the driver code in rmt_common.c and put a #define in rmt_common.h
	// I tried the guidance given near the bottom of
	// https://github.com/espressif/esp-idf/issues/14760 and rebuilt 
	// the driver using https://github.com/espressif/esp32-arduino-lib-builder
	// (I used the GUI tools/config_editor/app.py). But that had other side-effects
	// that I still don't understand.
		.signal_range_min_ns =1250,  // 1.25µs	
		.signal_range_max_ns = 15000000, // the longest duration for any signal above is the PT2262 (350*31=10850) 12000000 ns > 10850 µs, 
							   		// this ensures the receive does not stop early
		.flags {
			.en_partial_rx = 0,	// for some reason the compiler dumps core on this when en_partial_rx = 1
						// That's too bad. This flag can keep the buffer from overflowing, 
						// but overflow isn't a memory issue, 
						// so it may not matter, but I hate reading all those warning messages.
		}    
	};

	rmt_rx_channel_config_t rx_ch_conf = {
		.gpio_num = static_cast<gpio_num_t>(RxDecoder::rxPin), // GPIO number
		.clk_src = RMT_CLK_SRC_DEFAULT, //RMT_CLK_SRC_DEFAULT (80MHz),       // select source clock
		.resolution_hz = 1000000, // 1MHz tick resolution, i.e. 1 tick = 1us		
		.mem_block_symbols = NUMBER_OF_RMT_SYMBOLS,          // memory block size, 64 * 4 = 256Bytes
		.flags = {
			.invert_in = rxProtocol.INVERSE_LEVEL,         // don't invert input signal
			.with_dma = 0,  //if set to true, data stops coming through. No idea why.
		}
	};

	rmt_channel_handle_t rx_channel = NULL;
	rmt_new_rx_channel(&rx_ch_conf, &rx_channel);   
	rmt_rx_event_callbacks_t cbs = {
		.on_recv_done = rxDone,
	};

	ESP_ERROR_CHECK(rmt_enable(rx_channel));
	ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx_channel, &cbs, rx_queue));
	ESP_ERROR_CHECK(rmt_receive(rx_channel, symbols, sizeof(symbols), &rx_config));
	uint32_t lastTime=0;
	uint32_t thisTime= millis();

	while(1) {	
		uint32_t thisTime= millis();
		// wait for RX done signal but set timeout to 0 to prevent the Serial port from being flooded with buffer errors.
		if (xQueueReceive(rx_queue, &rx_data, 0)) { 
			//size_t len = rx_data.num_symbols;
			rmt_symbol_word_t *rx_items = rx_data.received_symbols;
			if(((thisTime - lastTime) > 500) && (rx_data.num_symbols == NUMBER_OF_RMT_SYMBOLS)) {									
				uint32_t rcode = 0;
				rcode = validateSignal(rx_items, rx_data.num_symbols);
				if (rcode !=0) {
					RxDecoder::nReceivedValue = rcode;
					RxDecoder::nReceivedBitlength = rx_data.num_symbols;
					//Serial.printf("Code: 0x%X\n", rcode);
				} //else {  //debug
					//rxDataDump(rx_data.num_symbols, rx_items);
					//Serial.printf("----------------------\n");
				//}
				lastTime = thisTime; //reset the timer				
			} //if xQueueReceive(...)			
			//start receiver again					
			ESP_ERROR_CHECK(rmt_receive(rx_channel, symbols, sizeof(symbols), &rx_config));
		} //if xQueue
		vTaskDelay(1); //delay 1 clock tick. Without this, the scheduler can't grab
					   // control and the entire program gets stuck in this while loop
					   // if you ask me, that's crappy software engineering.
	} //while(1)
	
	rmt_disable(rx_channel);
	rmt_del_channel(rx_channel);
	vQueueDelete(rx_queue);
	vTaskDelete(NULL);
}

void RxDecoder::setRxPin(uint8_t l_rxPin){
	RxDecoder::rxPin = l_rxPin;
	pinMode(RxDecoder::rxPin, INPUT);
}

void RxDecoder::rxDataDump(size_t len, rmt_symbol_word_t *item) { 
	Serial.printf("Rx len (# of bits) %d: \n", len);							
	for (uint8_t i=0; i < len ; i++ ) {
		Serial.printf("%d,%d,", item[i].duration0, item[i].duration1); 
	}
	Serial.println();								
}

uint32_t RxDecoder::validateSignal(rmt_symbol_word_t *item, size_t &len){
/*
	if (len !=NUMBER_OF_RMT_SYMBOLS) {
		return 0;
	}
*/
    uint32_t code = 0x00000000;
	int32_t bit_value = -1;
	size_t bit_size = 0;
	size_t i;
	int8_t header_pos = 0;
	int8_t num_errors = 0;
	header_pos = checkHeaderWord(item, len );
	for(i = header_pos+1, bit_size = 0; i < len - 1, bit_size < 24; i++, bit_size++){
		bit_value = validateRmtWord(item[i]);
		if(bit_value >= 0) {
			// Shift num1 left by 1 bit and OR with returned value. If m was 11100 and bit_value ==1, m would become 111001
            code = (code << 1) | bit_value; 
		} else {
			//debug
			//Serial.printf("FAIL: i:%d, bit_size: %d, d0:%d, d1:%d, L0:%d L1:%d\n",
			// i, bit_size, item[i].duration0, item[i].duration1, item[i].level0, item[i].level1);
			return 0;
		}
	}
	//Serial.printf("len: %d, i:%d\nhex: %X", len, bit_size, code); //Serial.printf(m, BIN);

	return code;
}

int32_t RxDecoder::validateRmtWord(rmt_symbol_word_t &item) {
	if	((item.duration0 < rxProtocol.uSecOneHigh_upperBound) && (item.duration0 > rxProtocol.uSecOneHigh_lowerBound) &&
		(item.duration1 < rxProtocol.uSecOneLow_upperBound) && (item.duration1 > rxProtocol.uSecOneLow_lowerBound)) {
		return(1);
	} else if ((item.duration0 < (rxProtocol.uSecZeroHigh_upperBound) && (item.duration0 > rxProtocol.uSecZeroHigh_lowerBound)) &&
		(item.duration1 < (rxProtocol.uSecZeroLow_upperBound)) && (item.duration1 > rxProtocol.uSecZeroLow_lowerBound)) {
		return(0);
	} else {
		return(-1);
	}
}

uint8_t RxDecoder::checkHeaderWord(rmt_symbol_word_t *item, size_t &len) {
	//sometimes you get a synch bit in the 0th or first element, sometimes you have to go out a few
	//but if there isn't enough left in the array to have a header and the word, stop trying.
	for (uint8_t i=0; i < len-BIT_LENGTH-1 ; i++ ) {
		if ((item[i].duration0 < rxProtocol.usecSynchHigh_upperBound) && (item[i].duration0 > rxProtocol.usecSynchHigh_lowerBound) &&
		(item[i].duration1 < rxProtocol.usecSynchLow_upperBound) && (item[i].duration1 > rxProtocol.usecSynchLow_lowerBound)) {
			//debug
			//Serial.printf("HEADER AT:%d, d0:%d, d1:%d, L0:%d L1:%d\n", i, item[i].duration0, item[i].duration1, item[i].level0, item[i].level1);
			return(i);
		}
	}
	//if we got here, well...we never found the header. Return a value greater than the length.
	//Serial.printf("header not found\n");
	return(len+1);
}


bool RxDecoder::rxDone(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *udata){
	BaseType_t task_wakeup = pdFALSE;
	QueueHandle_t queueHandle = (QueueHandle_t)udata;
	// send the received RMT symbols to the parser task
	xQueueSendFromISR(queueHandle, edata, &task_wakeup);
	return task_wakeup == pdTRUE;
}

bool RxDecoder::available() {
  return RxDecoder::nReceivedValue != 0;
}

void RxDecoder::resetAvailable() {
  RxDecoder::nReceivedValue = 0;
}
uint32_t RxDecoder::getReceivedValue() {
  return RxDecoder::nReceivedValue;
}

uint8_t RxDecoder::getReceivedBitlength() {
  return RxDecoder::nReceivedBitlength;
}

//required stupidness because these variable need to be static. Otherwise, the program won't link.. :(
uint8_t RxDecoder::rxPin; 
uint32_t RxDecoder::nReceivedValue;
uint8_t RxDecoder::nReceivedBitlength;
