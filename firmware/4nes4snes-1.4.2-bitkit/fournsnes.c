/* Name: fournsnes.c
 * Project: Multiple NES/SNES to USB converter
 * Author: Raphael Assenat <raph@raphnet.net>
 * Copyright: (C) 2007-2012 Raphael Assenat <raph@raphnet.net>
 * License: GPLv2
 * Tabsize: 4
 */
#define F_CPU   12000000L  

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <avr/pgmspace.h>
#include <string.h>
#include "gamepad.h"
#include "fournsnes.h"

#define GAMEPAD_BYTES	8	/* 2 byte per snes controller * 4 controllers */

/******** IO port definitions **************/
#define SNES_LATCH_DDR	DDRC
#define SNES_LATCH_PORT	PORTC
#define SNES_LATCH_BIT	(1<<4)

#define SNES_CLOCK_DDR	DDRC
#define SNES_CLOCK_PORT	PORTC
#define SNES_CLOCK_BIT	(1<<5)

#define SNES_DATA_PORT	PORTC
#define SNES_DATA_DDR	DDRC
#define SNES_DATA_PIN	PINC
#define SNES_DATA_BIT1	(1<<3)	/* controller 1 */
#define SNES_DATA_BIT2	(1<<2)	/* controller 2 */
#define SNES_DATA_BIT3	(1<<1)	/* controller 3 */
#define SNES_DATA_BIT4	(1<<0)	/* controller 4 */

#define MULTITAP_SELECT_PORT	PORTB
#define MULTITAP_SELECT_DDR		DDRB
#define MULTITAP_SELECT_PIN		PINB
#define MULTITAP_SELECT_BIT		(1<<5)

/********* IO port manipulation macros **********/
#define SNES_LATCH_LOW()	do { SNES_LATCH_PORT &= ~(SNES_LATCH_BIT); } while(0)
#define SNES_LATCH_HIGH()	do { SNES_LATCH_PORT |= SNES_LATCH_BIT; } while(0)
#define SNES_CLOCK_LOW()	do { SNES_CLOCK_PORT &= ~(SNES_CLOCK_BIT); } while(0)
#define SNES_CLOCK_HIGH()	do { SNES_CLOCK_PORT |= SNES_CLOCK_BIT; } while(0)

#define SNES_GET_DATA1()	(SNES_DATA_PIN & SNES_DATA_BIT1)
#define SNES_GET_DATA2()	(SNES_DATA_PIN & SNES_DATA_BIT2)
#define SNES_GET_DATA3()	(SNES_DATA_PIN & SNES_DATA_BIT3)
#define SNES_GET_DATA4()	(SNES_DATA_PIN & SNES_DATA_BIT4)

#define MTAP_SELECT_HIGH()	do { MULTITAP_SELECT_PORT |= MULTITAP_SELECT_BIT; } while(0)
#define MTAP_SELECT_LOW()	do { MULTITAP_SELECT_PORT &= ~MULTITAP_SELECT_BIT; } while(0)

/*********** prototypes *************/
static void fournsnesInit(void);
static void fournsnesUpdate(void);
static char fournsnesChanged(unsigned char report_id);
static char fournsnesBuildReport(unsigned char *reportBuffer, char report_id);


// the most recent bytes we fetched from the controller
static unsigned char last_read_controller_bytes[GAMEPAD_BYTES];

// the most recently reported bytes
static unsigned char last_reported_controller_bytes[GAMEPAD_BYTES];

// indicates if a controller is in NES mode
static unsigned char nesMode=0;	/* Bit0: controller 1, Bit1: controller 2...*/
static unsigned char fourscore_mode = 0;
static unsigned char multitap_mode = 0; // SNES
static unsigned char live_autodetect = 1;

void disableLiveAutodetect(void)
{
	live_autodetect = 0;
}

static void autoDetectSNESMultiTap(void)
{
	// Detection is done by observing that DATA2 becomes 
	// low when LATCH is high.
	//
	// Not sure which state of MTAP_SELECT_HIGH is reliable
	// so I'm simply trying with both states.

	MTAP_SELECT_LOW();

	if (SNES_GET_DATA2()) {
		SNES_LATCH_HIGH();
		_delay_us(12);

		if (!SNES_GET_DATA2()) {
			SNES_LATCH_LOW();
			_delay_us(12);
			if (SNES_GET_DATA2()) {
				multitap_mode = 1;
			}
		}
	}

	MTAP_SELECT_HIGH();

	if (SNES_GET_DATA2()) {
		SNES_LATCH_HIGH();
		_delay_us(12);

		if (!SNES_GET_DATA2()) {
			SNES_LATCH_LOW();
			_delay_us(12);
			if (SNES_GET_DATA2()) {
				multitap_mode = 1;
			}
		}
	}
}

static void autoDetectFourScore(void)
{
	unsigned char dat18th_low = 0;
	unsigned char hc=0;
	int i;

	SNES_LATCH_HIGH();
	_delay_us(12);
	SNES_LATCH_LOW();

	for (i=0; i<24; i++)
	{
		_delay_us(6);
		SNES_CLOCK_LOW();
		
		if (!SNES_GET_DATA1()) {
			if (i==19) {
				dat18th_low = 1;
			}
		}
		else {
			hc++;
		}


		_delay_us(6);
		SNES_CLOCK_HIGH();
	}

	if (dat18th_low && hc == 23) {
		// only 18th data bit was low. Looks like a FOUR SCORE.
		fourscore_mode = 1;
	}
		
	return;

}

static void fournsnesInit(void)
{
	unsigned char sreg;
	sreg = SREG;
	cli();
	
	// clock and latch as output
	SNES_LATCH_DDR |= SNES_LATCH_BIT;
	SNES_CLOCK_DDR |= SNES_CLOCK_BIT;
	
	// data as input
	SNES_DATA_DDR &= ~(SNES_DATA_BIT1 | SNES_DATA_BIT2 | SNES_DATA_BIT3 | SNES_DATA_BIT4 );
	// enable pullup. This should prevent random toggling of pins
	// when no controller is connected.
	SNES_DATA_PORT |= (SNES_DATA_BIT1 | SNES_DATA_BIT2 | SNES_DATA_BIT3 | SNES_DATA_BIT4 );

	// clock is normally high
	SNES_CLOCK_PORT |= SNES_CLOCK_BIT;

	// LATCH is Active HIGH
	SNES_LATCH_PORT &= ~(SNES_LATCH_BIT);

	
	MULTITAP_SELECT_DDR |= MULTITAP_SELECT_BIT;
	MULTITAP_SELECT_PORT |= MULTITAP_SELECT_BIT;


	nesMode = 0;
	fournsnesUpdate();

	if (!live_autodetect) {	
		/* Snes controller buttons are sent in this order:
		 * 1st byte: B Y SEL START UP DOWN LEFT RIGHT 
		 * 2nd byte: A X L R 1 1 1 1
		 *
		 * Nes controller buttons are sent in this order:
		 * One byte: A B SEL START UP DOWN LEFT RIGHT
		 *
		 * When an additional byte is read from a NES controller,
		 * all bits are 0. Because the data signal is active low,
		 * this corresponds to pressed buttons. When we read
		 * from the controller for the first time, detect NES
		 * controllers by checking those 4 bits.
		 **/
		if (last_read_controller_bytes[1]==0xFF)
			nesMode |= 1;

		if (last_read_controller_bytes[3]==0xFF)
			nesMode |= 2;

		if (last_read_controller_bytes[5]==0xFF)
			nesMode |= 4;

		if (last_read_controller_bytes[7]==0xFF)
			nesMode |= 8;
	}

	autoDetectFourScore();
	autoDetectSNESMultiTap();

	SREG = sreg;
}


static void fournsnesUpdate_fourscore(void)
{
	int i;
	unsigned char tmp1=0;
	unsigned char tmp2=0;
	unsigned char tmp3=0;
	unsigned char tmp4=0;

	SNES_LATCH_HIGH();
	_delay_us(12);
	SNES_LATCH_LOW();

	/* Nes controller buttons are sent in this order:
	 * One byte: A B SEL START UP DOWN LEFT RIGHT */

	for (i=0; i<8; i++)
	{
		_delay_us(6);
		SNES_CLOCK_LOW();
	
		// FourScore to be connected to ports 1 and 2	
		tmp1 <<= 1;	
		tmp2 <<= 1;	
		if (!SNES_GET_DATA1()) { tmp1 |= 1; }
		if (!SNES_GET_DATA2()) { tmp2 |= 1; }

		_delay_us(6);
		SNES_CLOCK_HIGH();
	}

	for (i=0; i<8; i++)
	{
		_delay_us(6);
		SNES_CLOCK_LOW();
	
		// FourScore to be connected to ports 1 and 2	
		tmp3 <<= 1;	
		tmp4 <<= 1;	
		if (!SNES_GET_DATA1()) { tmp3 |= 1; }
		if (!SNES_GET_DATA2()) { tmp4 |= 1; }

		_delay_us(6);
		SNES_CLOCK_HIGH();
	}

	for (i=0; i<8; i++)
	{
		_delay_us(6);
		SNES_CLOCK_LOW();
	
		_delay_us(6);
		SNES_CLOCK_HIGH();
	}
	last_read_controller_bytes[0] = tmp1;
	last_read_controller_bytes[1] = tmp2;
	last_read_controller_bytes[2] = tmp3;
	last_read_controller_bytes[3] = tmp4;

	return;

}


/*
 *
       Clock Cycle     Button Reported
        ===========     ===============
        1               B
        2               Y
        3               Select
        4               Start
        5               Up on joypad
        6               Down on joypad
        7               Left on joypad
        8               Right on joypad
        9               A
        10              X
        11              L
        12              R
        13              none (always high)
        14              none (always high)
        15              none (always high)
        16              none (always high)
 *
 */

static void fournsnesUpdate(void)
{
	int i;
	unsigned char tmp1=0;
	unsigned char tmp2=0;
	unsigned char tmp3=0;
	unsigned char tmp4=0;

	if (fourscore_mode) 
	{
		fournsnesUpdate_fourscore();
		return;
	}


	if (multitap_mode) 
	{
		SNES_LATCH_HIGH();
		_delay_us(12);
		SNES_LATCH_LOW();
		_delay_us(12);

		MTAP_SELECT_HIGH();
		_delay_us(6);
		for (i=0; i<8; i++)
		{
			SNES_CLOCK_LOW();
			_delay_us(6);

			tmp1 <<= 1;	
			tmp2 <<= 1;	

			if (!SNES_GET_DATA1()) { tmp1 |= 1; }
			if (!SNES_GET_DATA2()) { tmp2 |= 1; }
			
			SNES_CLOCK_HIGH();
			_delay_us(6);
		}
		last_read_controller_bytes[0] = tmp1;
		last_read_controller_bytes[2] = tmp2;
		for (i=0; i<8; i++)
		{
			SNES_CLOCK_LOW();
			_delay_us(6);

			tmp1 >>= 1;	
			tmp2 >>= 1;	

			if (!SNES_GET_DATA1()) { tmp1 |= 0x80; }
			if (!SNES_GET_DATA2()) { tmp2 |= 0x80; }
			
			SNES_CLOCK_HIGH();
			_delay_us(6);
		}


		MTAP_SELECT_LOW();
		_delay_us(6);
		for (i=0; i<8; i++)
		{
			SNES_CLOCK_LOW();
			_delay_us(6);

			tmp3 <<= 1;	
			tmp4 <<= 1;	

			if (!SNES_GET_DATA1()) { tmp3 |= 1; }
			if (!SNES_GET_DATA2()) { tmp4 |= 1; }
			
			SNES_CLOCK_HIGH();
			_delay_us(6);
		}
		last_read_controller_bytes[4] = tmp3;
		last_read_controller_bytes[6] = tmp4;
		for (i=0; i<8; i++)
		{
			SNES_CLOCK_LOW();
			_delay_us(6);

			tmp3 >>= 1;	
			tmp4 >>= 1;	

			if (!SNES_GET_DATA1()) { tmp3 |= 0x80; }
			if (!SNES_GET_DATA2()) { tmp4 |= 0x80; }
			
			SNES_CLOCK_HIGH();
			_delay_us(6);
		}


	}
	else // standard mode (not multitap)
	{			
		SNES_LATCH_HIGH();
		_delay_us(12);
		SNES_LATCH_LOW();

		for (i=0; i<8; i++)
		{
			_delay_us(6);
			SNES_CLOCK_LOW();
			
			tmp1 <<= 1;	
			tmp2 <<= 1;	
			tmp3 <<= 1;	
			tmp4 <<= 1;
			if (!SNES_GET_DATA1()) { tmp1 |= 1; }
			if (!SNES_GET_DATA2()) { tmp2 |= 1; }
			if (!SNES_GET_DATA3()) { tmp3 |= 1; }
			if (!SNES_GET_DATA4()) { tmp4 |= 1; }

			_delay_us(6);
			SNES_CLOCK_HIGH();
		}
		last_read_controller_bytes[0] = tmp1;
		last_read_controller_bytes[2] = tmp2;
		last_read_controller_bytes[4] = tmp3;
		last_read_controller_bytes[6] = tmp4;

		for (i=0; i<8; i++)
		{
			_delay_us(6);

			SNES_CLOCK_LOW();

			// notice that this is different from above. We
			// want the bits to be in reverse-order
			tmp1 >>= 1;	
			tmp2 >>= 1;	
			tmp3 >>= 1;	
			tmp4 >>= 1;	
			if (!SNES_GET_DATA1()) { tmp1 |= 0x80; }
			if (!SNES_GET_DATA2()) { tmp2 |= 0x80; }
			if (!SNES_GET_DATA3()) { tmp3 |= 0x80; }
			if (!SNES_GET_DATA4()) { tmp4 |= 0x80; }
			
			_delay_us(6);
			SNES_CLOCK_HIGH();
		}
	}


	if (live_autodetect) {	
		if (tmp1==0xFF)
			nesMode |= 1;
		else
			nesMode &= ~1;

		if (tmp2==0xFF)
			nesMode |= 2;
		else
			nesMode &= ~2;

		if (tmp3==0xFF)
			nesMode |= 4;
		else
			nesMode &= ~4;

		if (tmp4==0xFF)
			nesMode |= 8;
		else
			nesMode &= ~8;

	}

	/* Force extra bits to 0 when in NES mode. Otherwise, if
	 * we read zeros on the wire, we will have permanantly 
	 * pressed buttons */
	last_read_controller_bytes[1] = (nesMode & 1) ? 0x00 : tmp1;
	last_read_controller_bytes[3] = (nesMode & 2) ? 0x00 : tmp2;
	last_read_controller_bytes[5] = (nesMode & 4) ? 0x00 : tmp3;
	last_read_controller_bytes[7] = (nesMode & 8) ? 0x00 : tmp4;

}

static char fournsnesChanged(unsigned char report_id)
{
	report_id--; // first report is 1

	if (fourscore_mode) {
		return last_read_controller_bytes[report_id] != last_reported_controller_bytes[report_id];
	}

	return memcmp(	&last_read_controller_bytes[report_id<<1], 
					&last_reported_controller_bytes[report_id<<1], 
					2);
}

static char getX(unsigned char nesByte1)
{
	char x = 128;
	if (nesByte1&0x1) { x = 255; }
	if (nesByte1&0x2) { x = 0; }
	return x;
}

static char getY(unsigned char nesByte1)
{
	char y = 128;
	if (nesByte1&0x4) { y = 255; }
	if (nesByte1&0x8) { y = 0; }
	return y;
}

static unsigned char snesReorderButtons(unsigned char bytes[2])
{
	unsigned char v;

	/* pack the snes button bits, which are on two bytes, in
	 * one single byte. */
	v =	(bytes[0]&0x80)>>7;
	v |= (bytes[0]&0x40)>>5;
	v |= (bytes[0]&0x20)>>3;
	v |= (bytes[0]&0x10)>>1;
	v |= (bytes[1]&0x0f)<<4;

	return v;
}

static char fournsnesBuildReport(unsigned char *reportBuffer, char id)
{
	int idx;

	if (id < 0 || id > 4)
		return 0;

	/* last_read_controller_bytes[] structure:
	 *
	 * [0] : controller 1, 8 first bits (dpad + start + sel + y|a + b)
	 * [1] : controller 1, 8 snes extra bits (4 lower bits are buttons)
	 *
	 * [2] : controller 2, 8 first bits
	 * [3] : controller 2, 4 extra snes buttons
	 *
	 * [4] : controller 3, 8 first bits
	 * [5] : controller 3, 4 extra snes buttons
	 *
	 * [6] : controller 4, 8 first bits
	 * [7] : controller 4, 4 extra snes buttons
	 *
	 *
	 * last_read_controller_bytes[] structure in FOUR SCORE mode:
	 *
	 *  A B SEL START UP DOWN LEFT RIGHT 
	 *
	 * [0] : NES controller 1 data
	 * [1] : NES controller 2 data
	 * [2] : NES controller 3 data
	 * [3] : NES controller 4 data
	 *
	 */

	if (fourscore_mode) {
		idx = id - 1;
		if (reportBuffer != NULL)
		{
			reportBuffer[0]=id;
			reportBuffer[1]=getX(last_read_controller_bytes[idx]);
			reportBuffer[2]=getY(last_read_controller_bytes[idx]);
			reportBuffer[3] = last_read_controller_bytes[idx] & 0xf0;
		}

		last_reported_controller_bytes[idx] = last_read_controller_bytes[idx];

		return 4;
	}

	idx = id - 1;
	if (reportBuffer != NULL)
	{
		reportBuffer[0]=id;
		reportBuffer[1]=getX(last_read_controller_bytes[idx*2]);
		reportBuffer[2]=getY(last_read_controller_bytes[idx*2]);

		if (nesMode & (0x01<<idx))
			reportBuffer[3] = last_read_controller_bytes[idx*2] & 0xf0;
		else
			reportBuffer[3] = snesReorderButtons(&last_read_controller_bytes[idx*2]);
	}

	memcpy(&last_reported_controller_bytes[idx*2], 
			&last_read_controller_bytes[idx*2], 
			2);

	return 4;
}

const char fournsnes_usbHidReportDescriptor[] PROGMEM = {

	/* Controller and report_id 1 */
    0x05, 0x01,			// USAGE_PAGE (Generic Desktop)
    0x09, 0x04,			// USAGE (Joystick)
    0xa1, 0x01,			//	COLLECTION (Application)
    0x09, 0x01,			//		USAGE (Pointer)
    0xa1, 0x00,			//		COLLECTION (Physical)
	0x85, 0x01,			//			REPORT_ID (1)
	0x09, 0x30,			//			USAGE (X)
    0x09, 0x31,			//			USAGE (Y)
    0x15, 0x00,			//			LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x00,	//			LOGICAL_MAXIMUM (255)
    0x75, 0x08,			//			REPORT_SIZE (8)
    0x95, 0x02,			//			REPORT_COUNT (2)
    0x81, 0x02,			//			INPUT (Data,Var,Abs)

    0x05, 0x09,			//			USAGE_PAGE (Button)
    0x19, 1,			//   		USAGE_MINIMUM (Button 1)
    0x29, 8,			//   		USAGE_MAXIMUM (Button 8)
    0x15, 0x00,			//   		LOGICAL_MINIMUM (0)
    0x25, 0x01,			//   		LOGICAL_MAXIMUM (1)
    0x75, 1,			// 			REPORT_SIZE (1)
    0x95, 8,			//			REPORT_COUNT (8)
    0x81, 0x02,			//			INPUT (Data,Var,Abs)
	0xc0,				//		END_COLLECTION
    0xc0,				// END_COLLECTION

	/* Controller and report_id 2 */
    0x05, 0x01,			// USAGE_PAGE (Generic Desktop)
    0x09, 0x04,			// USAGE (Joystick)
    0xa1, 0x01,			//	COLLECTION (Application)
    0x09, 0x01,			//		USAGE (Pointer)
    0xa1, 0x00,			//		COLLECTION (Physical)
	0x85, 0x02,			//			REPORT_ID (2)
	0x09, 0x30,			//			USAGE (X)
    0x09, 0x31,			//			USAGE (Y)
    0x15, 0x00,			//			LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x00,	//			LOGICAL_MAXIMUM (255)
    0x75, 0x08,			//			REPORT_SIZE (8)
    0x95, 0x02,			//			REPORT_COUNT (2)
    0x81, 0x02,			//			INPUT (Data,Var,Abs)
    0x05, 0x09,			//			USAGE_PAGE (Button)
    0x19, 1,			//   		USAGE_MINIMUM (Button 1)
    0x29, 8,			//   		USAGE_MAXIMUM (Button 8)
    0x15, 0x00,			//   		LOGICAL_MINIMUM (0)
    0x25, 0x01,			//   		LOGICAL_MAXIMUM (1)
    0x75, 1,			// 			REPORT_SIZE (1)
    0x95, 8,			//			REPORT_COUNT (8)
    0x81, 0x02,			//			INPUT (Data,Var,Abs)
	0xc0,				//		END_COLLECTION
    0xc0,				// END_COLLECTION

	/* Controller and report_id 3 */
    0x05, 0x01,			// USAGE_PAGE (Generic Desktop)
    0x09, 0x04,			// USAGE (Joystick)
    0xa1, 0x01,			//	COLLECTION (Application)
    0x09, 0x01,			//		USAGE (Pointer)
    0xa1, 0x00,			//		COLLECTION (Physical)
	0x85, 0x03,			//			REPORT_ID (3)
	0x09, 0x30,			//			USAGE (X)
    0x09, 0x31,			//			USAGE (Y)
    0x15, 0x00,			//			LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x00,	//			LOGICAL_MAXIMUM (255)
    0x75, 0x08,			//			REPORT_SIZE (8)
    0x95, 0x02,			//			REPORT_COUNT (2)
    0x81, 0x02,			//			INPUT (Data,Var,Abs)
    0x05, 0x09,			//			USAGE_PAGE (Button)
    0x19, 1,			//   		USAGE_MINIMUM (Button 1)
    0x29, 8,			//   		USAGE_MAXIMUM (Button 8)
    0x15, 0x00,			//   		LOGICAL_MINIMUM (0)
    0x25, 0x01,			//   		LOGICAL_MAXIMUM (1)
    0x75, 1,			// 			REPORT_SIZE (1)
    0x95, 8,			//			REPORT_COUNT (8)
    0x81, 0x02,			//			INPUT (Data,Var,Abs)
	0xc0,				//		END_COLLECTION
    0xc0,				// END_COLLECTION

	/* Controller and report_id 4 */
    0x05, 0x01,			// USAGE_PAGE (Generic Desktop)
    0x09, 0x04,			// USAGE (Joystick)
    0xa1, 0x01,			//	COLLECTION (Application)
    0x09, 0x01,			//		USAGE (Pointer)
    0xa1, 0x00,			//		COLLECTION (Physical)
	0x85, 0x04,			//			REPORT_ID (4)
	0x09, 0x30,			//			USAGE (X)
    0x09, 0x31,			//			USAGE (Y)
    0x15, 0x00,			//			LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x00,	//			LOGICAL_MAXIMUM (255)
    0x75, 0x08,			//			REPORT_SIZE (8)
    0x95, 0x02,			//			REPORT_COUNT (2)
    0x81, 0x02,			//			INPUT (Data,Var,Abs)
    0x05, 0x09,			//			USAGE_PAGE (Button)
    0x19, 1,			//   		USAGE_MINIMUM (Button 1)
    0x29, 8,			//   		USAGE_MAXIMUM (Button 8)
    0x15, 0x00,			//   		LOGICAL_MINIMUM (0)
    0x25, 0x01,			//   		LOGICAL_MAXIMUM (1)
    0x75, 1,			// 			REPORT_SIZE (1)
    0x95, 8,			//			REPORT_COUNT (8)
    0x81, 0x02,			//			INPUT (Data,Var,Abs)
	0xc0,				//		END_COLLECTION
    0xc0,				// END_COLLECTION


};

Gamepad SnesGamepad = {
	.num_reports 			= 4,
	.reportDescriptorSize	= sizeof(fournsnes_usbHidReportDescriptor),
	.init					= fournsnesInit,
	.update					= fournsnesUpdate,
	.changed				= fournsnesChanged,
	.buildReport			= fournsnesBuildReport
};

Gamepad *fournsnesGetGamepad(void)
{
	SnesGamepad.reportDescriptor = (void*)fournsnes_usbHidReportDescriptor;

	return &SnesGamepad;
}

