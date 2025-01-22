/*
  esp32-rmt-pwm-decoder - Arduino libary for decoding an RF keyfob with an ESP32
  Copyright (c) 2025 Doug Brann.  All right reserved. 
  Please see Readme file for list of credits, howto, etc.
  
  Project home: https://github.com/TheDougMiester/esp32-rmt-pwm-decoder

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "esp32-rmt-pwm-decoder.h"

void RxDecoder::rxSignalHandler(void* param){
	rmt_rx_done_event_data_t rx_data;
	QueueHandle_t rx_queue = xQueueCreate(1, sizeof(rx_data));

	while(1) {
		rmt_channel_handle_t rx_channel = NULL;
		rmt_symbol_word_t symbols[64];
		
		rmt_receive_config_t rx_config = {
		    .signal_range_min_ns = 1250,     // my pulse widths are around 350µs, 1250 ns < 350 µs, so valid pulses not treated as noise
											// I'd like to have a higher minimum (like ~200µs), but if it's much larger than 1250ns
											// the buffer overflows and there's a core dump.
    		.signal_range_max_ns = 12000000, // the longest duration for any signal above is the PT2262 (350*31=10850) 12000000 ns > 10850 µs, 
											// this ensures the receive does not stop early    
		};
		//rx_config.flags.en_partial_rx=1;// for some reason the compiler dumps core on this
		// That's too bad. This flag can keep the buffer from overflowing, but overflow isn't a memory issue, 
		// so it may not matter, but I hate reading all those warning messages.
											
		rmt_rx_channel_config_t rx_ch_conf = {
			.gpio_num = static_cast<gpio_num_t>(RxDecoder::rxPin),
			.clk_src = RMT_CLK_SRC_DEFAULT,
			.resolution_hz = 1000000,
			.mem_block_symbols = 64
		};
		rx_ch_conf.flags.invert_in = rxProtocol.INVERSE_LEVEL; //is the protocol inverse?
		rx_ch_conf.flags.with_dma = false;         // do not need DMA backend

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
			if (xQueueReceive(rx_queue, &rx_data, pdMS_TO_TICKS(1000)) == pdPASS){
				size_t len = rx_data.num_symbols;
				rmt_symbol_word_t *rx_items = rx_data.received_symbols;
				//make sure the length is long enough (everything I've used has 24 bits)
                //and ensure at least 500 usecs have passed (eliminates duplicates and some noise)
				if((len > 22) && ((thisTime - lastTime) > 500)){					
					uint32_t rcode = 0;
					rcode = validateSignal(rx_items, len);
					if (rcode !=0) {
						//Serial.printf("this time: %d, last time: %d, delta: %d\n", thisTime, lastTime, thisTime-lastTime);
						RxDecoder::nReceivedValue = rcode;
        				RxDecoder::nReceivedBitlength = len;
					}
					lastTime = thisTime;
#ifdef DEBUG					
					rxDataDump(len, rx_items);
#endif			
				} //if len..
				ESP_ERROR_CHECK(rmt_receive(rx_channel, symbols, sizeof(symbols), &rx_config));
			} //if xQueue
		} //while(1)
		rmt_disable(rx_channel);
		rmt_del_channel(rx_channel);
	} //outer while(1)
	vQueueDelete(rx_queue);
	vTaskDelete(NULL);
}

void RxDecoder::setRxPin(uint8_t l_rxPin){
	RxDecoder::rxPin = l_rxPin;
}

void RxDecoder::rxDataDump(size_t len, rmt_symbol_word_t *item) { 
	Serial.printf("Rx len (# of bits) %d: \n", len);							
	for (uint8_t i=0; i < len ; i++ ) {
		Serial.printf("%d,%d,", item[i].duration0, item[i].duration1); 
	}
	Serial.println();								
}

uint32_t RxDecoder::validateSignal(rmt_symbol_word_t *item, size_t &len){
	if(len < 23 || len > 64 ){
		return 0;
	}

    uint32_t code = 0x00000000;
	int32_t bit_value = -1;
	size_t i;
	size_t header_size = 0;

	if(!checkHeaderWord(item[0])){
		header_size = 1; //Serial.print("value = "); Serial.println((!checkbit(item[0], proto[PT2262].header_high, proto[PT2262].header_low) ));
	}
	//Serial.printf("\nheader: %d\n", header_size);
	for(i = header_size; i < len - 1; i++){
		if (i > 24) {break;}

		bit_value = validateRmtWord(item[i]);
		if(bit_value >= 0) {
			// Shift num1 left by 1 bit and OR with returned value. If m was 11100 and bit_value ==1, m would become 111001
            code = (code << 1) | bit_value; 
		} else {
//			Serial.printf("BitError %d\n",i); 
//			Serial.printf("d0:%d, d1:%d, L0:%d L1:%d\n", item[i].duration0, item[i].duration1, item[i].level0, item[i].level1);
			return 0;
		}
	}
		//Serial.printf("len: %d, i:%d\nhex: %X, binary:", len, i, m); Serial.println(m, BIN);

	return code;
}

int32_t RxDecoder::validateRmtWord(rmt_symbol_word_t &item) {
	if	((item.duration0 < rxProtocol.uSecOneHigh_upperBound) && (item.duration0 > rxProtocol.uSecOneHigh_lowerBound) &&
		(item.duration1 < rxProtocol.uSecOneLow_upperBound) && (item.duration1 > rxProtocol.uSecOneLow_lowerBound)) {
			return(1);
		} else if ((item.duration0 < (rxProtocol.uSecZeroHigh_upperBound) && (item.duration0 > rxProtocol.uSecZeroHigh_lowerBound)) &&
		(item.duration1 < (rxProtocol.uSecZeroLow_upperBound)) && (item.duration1 > rxProtocol.uSecZeroLow_lowerBound)) {
			return(0);
		}else {
			return(-1);
		}
}

bool RxDecoder::checkHeaderWord(rmt_symbol_word_t &item){
	return item.level0 == 0 && item.level1 != 0 &&
		(item.duration0 < rxProtocol.usecSynchHigh_upperBound) && (item.duration0 > rxProtocol.usecSynchHigh_lowerBound) &&
		(item.duration1 < rxProtocol.usecSynchLow_upperBound) && (item.duration1 > rxProtocol.uSecZeroLow_lowerBound);
}


bool RxDecoder::rxDone(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *udata){
	BaseType_t h = pdFALSE;
	QueueHandle_t q = (QueueHandle_t)udata;
	xQueueSendFromISR(q, edata, &h);
	return h == pdTRUE;
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
