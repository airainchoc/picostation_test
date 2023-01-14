#include <stdio.h>
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include "main.pio.h"

#include "utils.h"
#include "values.h"
#include "subq.h"

extern volatile uint subq_offset;
extern volatile uint subq_offset;
extern volatile int track;
extern volatile int sector;
extern volatile int sector_for_track_update;
extern volatile int subq_start;
extern uint8_t tracksubq[12];
extern int *logical_track_to_sector;
extern int num_logical_tracks;

void printf_subq(uint8_t *subqdata){
	for (int i=0;i<12;i++) {
		printf("%02X ", subqdata[i]);
	}
}

static inline void send_subq(uint8_t *subqdata) {
	pio_sm_put_blocking(pio1, SCOR_SM, 1);
	subq_program_init(pio1, SUBQ_SM, subq_offset, SQSO, SQCK);    
	pio_sm_set_enabled(pio1, SUBQ_SM, true);

	uint sub1 = (reverseBits(subqdata[0],8) << 24) | 
				(reverseBits(subqdata[1],8) << 16) | 
				(reverseBits(subqdata[2],8) << 8) | 
				(reverseBits(subqdata[3],8));
	uint sub2 = (reverseBits(subqdata[4],8) << 24) | 
				(reverseBits(subqdata[5],8) << 16) | 
				(reverseBits(subqdata[6],8) << 8) | 
				(reverseBits(subqdata[7],8));
	uint sub3 = (reverseBits(subqdata[8],8) << 24) | 
				(reverseBits(subqdata[9],8) << 16) | 
				(reverseBits(subqdata[10],8) << 8) | 
				(reverseBits(subqdata[11],8));
	pio_sm_put_blocking(pio1, SUBQ_SM, reverseBits(sub1,32));
	pio_sm_put_blocking(pio1, SUBQ_SM, reverseBits(sub2,32));
	pio_sm_put_blocking(pio1, SUBQ_SM, reverseBits(sub3,32));
}

void start_subq() {
	if (sector < 4500) {
		int subq_entry = sector%(3+num_logical_tracks);
		
		if (subq_entry == 0) {
			tracksubq[0] = 0x01;
			tracksubq[1] = 0x00;
			tracksubq[2] = 0xA0;
			tracksubq[7] = 0x01;
			tracksubq[8] = 0x00;
			tracksubq[9] = 0x00;
		} else if (subq_entry == 1) {
			tracksubq[0] = 0x01;
			tracksubq[1] = 0x00;
			tracksubq[2] = 0xA1;
			tracksubq[7] = tobcd(num_logical_tracks);
			tracksubq[8] = 0x00;
			tracksubq[9] = 0x00;
		} else if (subq_entry == 2) {
			int sector_lead_out = logical_track_to_sector[num_logical_tracks+1] - 4500;
			tracksubq[0] = 0x01;
			tracksubq[1] = 0x00;
			tracksubq[2] = 0xA2;
			tracksubq[7] = tobcd(sector_lead_out/75/60);
			tracksubq[8] = tobcd((sector_lead_out/75) % 60); 
			tracksubq[9] = tobcd(sector_lead_out % 75);
		} else if (subq_entry > 2) {
			int sector_track;
			int logical_track = subq_entry-2;
			if (logical_track == 1) {
				sector_track = 150;
			} else {
				sector_track = logical_track_to_sector[logical_track] - 4500;
			}
			tracksubq[0] = 0x01;
			tracksubq[1] = 0x00;
			tracksubq[2] = tobcd(logical_track);
			tracksubq[7] = tobcd(sector_track/75/60);
			tracksubq[8] = tobcd((sector_track/75) % 60); 
			tracksubq[9] = tobcd(sector_track % 75);
		}
		
		tracksubq[3] = tobcd(sector/75/60);
		tracksubq[4] = tobcd((sector/75) % 60);
		tracksubq[5] = tobcd(sector % 75);
		tracksubq[6] = 0x00;
		tracksubq[10] = 0x00;
		tracksubq[11] = 0x00;
		
		send_subq(tracksubq);
	} else {
		int logical_track = num_logical_tracks+1; // in case seek overshoots past end of disc
		for (int i=0;i<num_logical_tracks+2;i++) { // + 2 for lead in & lead out
			if (logical_track_to_sector[i+1] > sector) {
				logical_track = i;
				break;
			}
		}
		int sector_track = sector - logical_track_to_sector[logical_track];
		int sector_abs = (sector - 4500);
		int sector_track_after_pause;
		
		if (logical_track == 1) {
			sector_track_after_pause = sector_track - 150;
		} else {
			sector_track_after_pause = sector_track;
		}
		
		tracksubq[0] = 0x01;
		
		if (logical_track == num_logical_tracks+1) {
			tracksubq[1] = 0xAA;
		} else {
			tracksubq[1] = tobcd(logical_track);
		}
		if (sector_track < 150 && logical_track == 1) { // 2 sec pause track
			tracksubq[2] = 0x00;
			tracksubq[3] = 0x00; // min
			tracksubq[4] = tobcd(1-(sector_track/75)); // sec (count down) 
			tracksubq[5] = tobcd(74 - (sector_track % 75)); // frame (count down)
		} else {
			tracksubq[2] = 0x01;
			tracksubq[3] = tobcd(sector_track_after_pause/75/60); // min
			tracksubq[4] = tobcd((sector_track_after_pause/75) % 60); // sec
			tracksubq[5] = tobcd(sector_track_after_pause % 75); // frame
		}
		tracksubq[6] = 0x00;
		tracksubq[7] = tobcd(sector_abs/75/60); // amin
		tracksubq[8] = tobcd((sector_abs/75) % 60); // asec
		tracksubq[9] = tobcd(sector_abs % 75); // aframe
		tracksubq[10] = 0x00;
		tracksubq[11] = ((sector % 2) == 0) ? 0x00 : 0x80;
		
		send_subq(tracksubq);
	}
}

void wait_end_subq() {
	while(pio_sm_is_rx_fifo_empty(pio1, SUBQ_SM));
	if (sector % (50+num_logical_tracks) == 0) {
		printf_subq(tracksubq);
		printf("%d\n",sector);
	}
	pio_sm_set_enabled(pio1, SUBQ_SM, false);
	sleep_us(500);
	gpio_init(SQSO);
	gpio_set_dir(SQSO, GPIO_OUT);
	gpio_put(SQSO, 1);
	sector++;
	if ((sector - sector_for_track_update) > sectors_per_track(track)) {
		sector_for_track_update = sector;
		track++;
	}
}
