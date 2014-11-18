/* Nes/Snes/Genesis/SMS/Atari to USB
 * Copyright (C) 2006-2011 Raphaël Assénat
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * The author may be contacted at raph@raphnet.net
 */
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>

#include <util/delay.h>
#include <string.h>

#include "gamepad.h"
#include "leds.h"
#include "db9.h"

#define REPORT_SIZE		3
#define GAMEPAD_BYTES	3

#define CTL_ID_ATARI	0x00 // up/dn/lf/rt/btn_b
#define CTL_ID_SMS		0x01 // up/dn/lf/rt/btn_b/btn_c
#define CTL_ID_GENESIS3	0x02 // up/dn/lf/rt/btn_a/btn_b/btn_c/start
#define CTL_ID_GENESIS6 0x03 // all bits
#define CTL_ID_PADDLE	0x04	// HPD-200 Japanese

#define isGenesis(a) ( (a) >= CTL_ID_GENESIS3)

static unsigned char cur_id = CTL_ID_GENESIS3;

static inline unsigned char SAMPLE()
{
	unsigned char c;
	unsigned char b;
	unsigned char res;

	c = PINC;
	b = PINB;

	/* The rest of the code was developped on a different PCB which
	 * maps the DB9 pins differently. This code is to 'emulate'
	 * this different mapping so I dont have to change auto-detection
	 * and report descriptor building stuff. */

	res = (c & 0x20) >> 5; // Up/Up/Z
	res |= (c & 0x10) >> 3; // Down/Down/Y
	res |= (c & 0x08) >> 1; // Left/0/X
	res |= (c & 0x04) << 1; // Right/0
	res |= (c & 0x02) << 3; // BtnB/BtnA
	res |= (b & 0x20); // BtnC/BtnStart
	
	if (cur_id == CTL_ID_PADDLE) {
		res |= (b & 0x10) << 2; // extra button for paddle (PORTD3)
	}

	return res;
}


#define SET_SELECT()	PORTC |= 1;
#define CLR_SELECT()	PORTC &= 0xfe;

/*********** prototypes *************/
static char db9Init(void);
static void db9Update(void);





#define PADDLE_MAP_HORIZ	0
#define PADDLE_MAP_VERT		1
#define PADDLE_MAP_1BTN		0
#define PADDLE_MAP_2BTN		2

static int paddle_mapping = PADDLE_MAP_HORIZ | PADDLE_MAP_1BTN;

// the most recent bytes we fetched from the controller
static unsigned char last_read_controller_bytes[REPORT_SIZE];
// the most recently reported bytes
static unsigned char last_reported_controller_bytes[REPORT_SIZE];

#define READ_CONTROLLER_SIZE 5
static void readController(unsigned char bits[READ_CONTROLLER_SIZE])
{
	unsigned char a,b,c,d,e;

	// total delays: 160 microseconds.

	/* |   1 |  2  |  3  |  4  | 5 ...
	 * ___    __    __    __    __
	 *    |__|  |__|  |__|  |__|
	 *  ^  ^     ^     ^   ^
	 *  A  B     D     E   C
	 *
	 *  ABC are used when reading controllers.
	 *  D and E are used for auto-detecting the genesis 6 btn controller.
	 *
	 */

	/* 1 */
	SET_SELECT();
	_delay_us(20.0);
	a = SAMPLE();

	if (cur_id == CTL_ID_ATARI ||
		cur_id == CTL_ID_SMS) {
		
		bits[0] = a;
		bits[1] = 0xff;
		bits[2] = 0xff;

		return;
	}

	CLR_SELECT();
	_delay_us(20.0);		
	b = SAMPLE();

	/* 2 */
	SET_SELECT();
	_delay_us(20.0);		
	CLR_SELECT();
	_delay_us(20.0);		
	d = SAMPLE();

	/* 3 */
	SET_SELECT();
	_delay_us(20.0);		
	CLR_SELECT();
	_delay_us(20.0);			
	e = SAMPLE();

	/* 4 */
	SET_SELECT();
	_delay_us(20.0);			
	c = SAMPLE();

	CLR_SELECT();
	_delay_us(20.0);			

	/* 5 */
	SET_SELECT();

	bits[0] = a;
	bits[1] = b;
	bits[2] = c;
	bits[3] = d;
	bits[4] = e;
}

int detectPaddle(void)
{
	unsigned char a;
	int i;
	int count_low=0, count_high=0;

	// 0x20 : TR
	// 0x10 : TL
	
	// TR frequecny: ~8khz. Period :  124uS
	for (i=0; i<100; i++)
	{
		a = SAMPLE();
		if (a & 0x20)
			count_high++;
		else
			count_low++;

		_delay_us(10);
	}

	if ((count_low > 33) && (count_high > 33)) {
		return 1;
	}


	return 0;
}

static char db9Init(void)
{
	unsigned char sreg;
	unsigned char bits[READ_CONTROLLER_SIZE];
	
	sreg = SREG;
	cli();

	DDRB |= 0x02; // Bit 1 out
	PORTB &= ~0x02; // 0

	DDRB &= ~0x30; // PB5, BtnC/BtnStart  PB4: Extra button on paddle
	PORTB |= 0x30;

	
	DDRC |= 1;	// SELECT
	PORTC |= 1;

	DDRC &= ~0x3E; // direction and buttons
	PORTC |= 0x3E;

	// The multi-tap is not detected properly without
	// a delay here. Its internal 4 bit mcu might be
	// initializing or maybe it is configured by some
	// glitches on the IO pins at power up. 
	//
	// Detection with 50ms delay was stable, use 100ms
	// for safety.
	_delay_ms(100);

	if (detectPaddle()) {
		cur_id = CTL_ID_PADDLE;
		paddle_mapping = PADDLE_MAP_2BTN;
		db9Update();

		paddle_mapping = 0;

		// BTN1
		if (last_read_controller_bytes[2] & 0x01) {
			paddle_mapping |= PADDLE_MAP_VERT;
		}

		if (last_read_controller_bytes[2] & 0x02) {
			paddle_mapping |= PADDLE_MAP_2BTN;
		}

		SREG = sreg;
		return 0;
	}

	readController(bits);

	cur_id = CTL_ID_SMS;

	if ((bits[0]&0xf) == 0x3) {
		SREG = sreg;
		return -1; // MULTI-TAP
	}

	if ((bits[0]&0xf) == 0xf) {
		if ((bits[1]&0xf) == 0x3) 
		{
			if (	((bits[3] & 0xf) != 0x3)  ||
					((bits[4] & 0xf) != 0x3) ) {
				/* 6btn controllers return 0x0 and 0xf here. But
				 * for greater compatibility, I only test
				 * if it is different from a 3 button controller. */
				cur_id = CTL_ID_GENESIS6;
			}
			else {
				cur_id = CTL_ID_GENESIS3;
			}
		}
	}


	/* Force 6 Button genesis controller. Useful if auto-detection
	 * fails for some reason. */
	if (!(bits[1] & 0x20)) { // if start button initially held down
		cur_id = CTL_ID_GENESIS6;
	}
	

	db9Update();


	SREG = sreg;

	return 0;
}

static unsigned char getPaddleNibble(void)
{
	unsigned char a, bits;
	a = SAMPLE();
	bits = (a & 0x0F);
	return bits;
}

static void db9Update(void)
{
	unsigned char data[READ_CONTROLLER_SIZE];
	int x=128,y=128;

	if (cur_id == CTL_ID_PADDLE) {
		unsigned char a;
		unsigned char bits;
		int retries;
		int t;
		int axis_target_idx;
		int axis_inactive_idx;
		int invert_axis;

		if (paddle_mapping & PADDLE_MAP_VERT) {
			// Map to vertical axis
			axis_target_idx = 1;
			axis_inactive_idx = 0;
			invert_axis = 1;
		} else {
			axis_target_idx = 0;
			axis_inactive_idx = 1;
			invert_axis = 0;
		}

		last_read_controller_bytes[axis_inactive_idx] = 128;
		last_read_controller_bytes[2] = 0;
		a = SAMPLE();

		if (!(a & 0x10)) { // TL
			last_read_controller_bytes[2] |= 0x01;
		}

		if (paddle_mapping & PADDLE_MAP_2BTN) {
			if (!(a & 0x40)) // EXTRA BTN
				last_read_controller_bytes[2] |= 0x02;
		} else { // PADDLE_MAP_2BTN
			if (!(a & 0x40)) // EXTRA BTN
				last_read_controller_bytes[2] |= 0x01;
		}
		
		for (retries =0; retries<4; retries++)
		{
			unsigned char value = 0;

			// Right/Left/Down/Up
			//  3     2    1   0
		
			for (t=0; t<100; t++)
			{
				a = SAMPLE();
				if (a & 0x20)
					break;
				_delay_us(1);
			}
			if (a & 0x20) { // TR
				bits = getPaddleNibble();
				_delay_us(2);
				a = SAMPLE();
				if (a & 0x20) {
					value |= bits << 4;
				} else {
					continue;
				}
				
				// delay is based on measured 124us TR period
				_delay_us(62);

				a = SAMPLE();
				if (!(a & 0x20)) {
					bits = getPaddleNibble();
					_delay_us(2);
					a = SAMPLE();
					if (a & 0x20) {
						continue;
					} else {
						value |= bits;
						if (invert_axis)
							value ^= 0xff;
						last_read_controller_bytes[axis_target_idx] = value;
						return;
					}
				}

			}
			else {
				// TODO : read from TR low
				_delay_us(62);
				continue;
			}

		}

		return;
	}
	
	/* 0: Up//Z
	 * 1: Down//Y
	 * 2: Left//X
	 * 3: Right//Mode
	 * 4: Btn B/A
	 * 5: Btn C/Start/
	 */
	readController(data);

	/* Buttons are active low. Invert the bits
	 * here to simplify subsequent 'if' statements... */
	data[0] = data[0] ^ 0xff;
	data[1] = data[1] ^ 0xff;
	data[2] = data[2] ^ 0xff;

	if (data[0] & 1) { y = 0; } // up
	if (data[0] & 2) { y = 255; } //down
	if (data[0] & 4) { x = 0; }  // left
	if (data[0] & 8) { x = 255; } // right

	last_read_controller_bytes[0]=x;
	last_read_controller_bytes[1]=y;
 
 	last_read_controller_bytes[2]=0;

	if (isGenesis(cur_id)) {
		
		if (data[1]&0x10) { last_read_controller_bytes[2] |= 0x01; } // A
		if (data[0]&0x10) { last_read_controller_bytes[2] |= 0x02; } // B
		if (data[0]&0x20) { last_read_controller_bytes[2] |= 0x04; } // C
		if (data[1]&0x20) { last_read_controller_bytes[2] |= 0x08; } // Start
		if (cur_id == CTL_ID_GENESIS6) {
			if (data[2]&0x04) { last_read_controller_bytes[2] |= 0x10; } // X
			if (data[2]&0x02) { last_read_controller_bytes[2] |= 0x20; } // Y
			if (data[2]&0x01) { last_read_controller_bytes[2] |= 0x40; } // Z

			if (data[2]&0x08) { last_read_controller_bytes[2] |= 0x80; } // Mode
		}
	}
	else {
		/* The button IDs for 1 and 2 button joysticks should start
		 * at '1'. Some Atari emulators don't support button remapping so
		 * this is pretty important! */
		if (data[0]&0x10) { last_read_controller_bytes[2] |= 0x01; } // Button 1
		if (data[0]&0x20) { last_read_controller_bytes[2] |= 0x02; } // Button 2
	}
}	

static char db9Changed(char id)
{
	static int first = 1;
	if (first) { first = 0;  return 1; }
	
	return memcmp(last_read_controller_bytes, 
					last_reported_controller_bytes, GAMEPAD_BYTES);
}

static char db9BuildReport(unsigned char *reportBuffer, char id)
{
	if (reportBuffer != NULL)
	{
		memcpy(reportBuffer, last_read_controller_bytes, GAMEPAD_BYTES);
	}
	memcpy(last_reported_controller_bytes, 
			last_read_controller_bytes, 
			GAMEPAD_BYTES);	

	return REPORT_SIZE;
}

#include "snes_descriptor.c"

Gamepad db9Gamepad = {
	.num_reports			=	1,
	.reportDescriptorSize	=	sizeof(snes_usbHidReportDescriptor),
	.init					=	db9Init,
	.update					=	db9Update,
	.changed				=	db9Changed,
	.buildReport			=	db9BuildReport
};

Gamepad *db9GetGamepad(void)
{
	db9Gamepad.reportDescriptor = (void*)snes_usbHidReportDescriptor;

	return &db9Gamepad;
}

