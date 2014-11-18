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
 *
 * 2011-11-05: Added famicon microphone controller support with mappings.
 */
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/delay.h>

#include <string.h>

#include "usbconfig.h"
#include "usbdrv.h"
#include "gamepad.h"
#include "leds.h"
#include "nes.h"

#define REPORT_SIZE		3
#define GAMEPAD_BYTES	1

/******** IO port definitions **************/
#define NES_LATCH_DDR	DDRC
#define NES_LATCH_PORT	PORTC
#define NES_LATCH_BIT	(1<<4)

#define NES_CLOCK_DDR	DDRC
#define NES_CLOCK_PORT	PORTC
#define NES_CLOCK_BIT	(1<<5)

#define NES_DATA_PORT	PORTC
#define NES_DATA_DDR	DDRC
#define NES_DATA_PIN	PINC
#define NES_DATA_BIT	(1<<3)

#define FAMICON_MIC_PORT	PORTC
#define FAMICON_MIC_DDR		DDRC
#define FAMICON_MIC_PIN		PINC
#define FAMICON_MIC_BIT		(1<<2)

/********* IO port manipulation macros **********/
#define NES_LATCH_LOW()	do { NES_LATCH_PORT &= ~(NES_LATCH_BIT); } while(0)
#define NES_LATCH_HIGH()	do { NES_LATCH_PORT |= NES_LATCH_BIT; } while(0)
#define NES_CLOCK_LOW()	do { NES_CLOCK_PORT &= ~(NES_CLOCK_BIT); } while(0)
#define NES_CLOCK_HIGH()	do { NES_CLOCK_PORT |= NES_CLOCK_BIT; } while(0)

#define NES_GET_DATA()	(NES_DATA_PIN & NES_DATA_BIT)

#define FAMICON_GET_MIC()	(FAMICON_MIC_PIN & FAMICON_MIC_BIT)

/* Different mappings for famicon support */ 
#define FAMICON_MODE_OFF		0 // not in famicon mode
#define FAMICON_MODE_DEFAULT	1
#define FAMICON_MODE_A			2
#define FAMICON_MODE_B			3


/* Controller button bits, as stored in last_read_controller_bytes[0] */
#define BTN_BIT_A		0x80
#define BTN_BIT_B		0x40
#define BTN_BITS_AB		(BTN_BIT_A|BTN_BIT_B)
#define BTN_BIT_SELECT	0x20
#define BTN_BIT_START	0x10


static char isFamicon = FAMICON_MODE_OFF;
static char famiconMicStatus = 0;
static char famiconMicChanged = 0;

/*********** prototypes *************/
static char nesInit(void);
static void nesUpdate(void);

// the most recent bytes we fetched from the controller
static unsigned char last_read_controller_bytes[GAMEPAD_BYTES];
// the most recently reported bytes
static unsigned char last_reported_controller_bytes[GAMEPAD_BYTES];

static void famiconPollMic(void)
{
	if (!FAMICON_GET_MIC()) {
		famiconMicStatus = 1;
		famiconMicChanged = 1;
	}
}

static char nesInit(void)
{
	// clock and latch as output
	NES_LATCH_DDR |= NES_LATCH_BIT;
	NES_CLOCK_DDR |= NES_CLOCK_BIT;
	
	// data as input
	NES_DATA_DDR &= ~(NES_DATA_BIT);
	// enable pullup. Prevents rapid random toggling of pins
	// when no controller is connected.
	NES_DATA_PORT |= NES_DATA_BIT;

	// clock is normally high
	NES_CLOCK_PORT |= NES_CLOCK_BIT;

	// LATCH is Active HIGH
	NES_LATCH_PORT &= ~(NES_LATCH_BIT);

	// microphone as input with pull up
	FAMICON_MIC_DDR &= ~(FAMICON_MIC_BIT);
	FAMICON_MIC_PORT |= FAMICON_MIC_BIT;

	// use PC 1 and 0 to detect famicon mode.
	// PC0 is an input with pull-up
	// PC1 is an output driven low.
	DDRC &= ~0x03; // first both are inputs
	PORTC |= 0x01;  // PC0 high
	PORTC &= ~0x02; // PC1 low
	DDRC |= 0x02; // Enable PC1 output
	_delay_ms(1);

	// Now if PC0 is low, it means PC1 and PC0 
	// are tied. Therefore, enable famicon mode.
	if (!(PINC & 0x01)) {

		// But which mode? This depends on the initial 
		// status of A and B
		nesUpdate();

		if (last_read_controller_bytes[0] & BTN_BIT_A) {
			isFamicon = FAMICON_MODE_A;
		} else if (last_read_controller_bytes[0] & BTN_BIT_B) {
			isFamicon = FAMICON_MODE_B;
		} else {
			isFamicon = FAMICON_MODE_DEFAULT;
		}
	}

	return 0;
}

/*
 *        Clock Cycle     Button Reported
        ===========     ===============
        1               B
        2               Y
        3               Select
        4               Start
        5               Up on joypad
        6               Down on joypad
        7               Left on joypad
        8               Right on joypad
 * 
 */

static void nesUpdate(void)
{
	int i;
	unsigned char tmp=0;

	NES_LATCH_HIGH();
	_delay_us(12);
	NES_LATCH_LOW();

	for (i=0; i<8; i++)
	{
		_delay_us(6);
		NES_CLOCK_LOW();
		
		tmp <<= 1;	
		if (!NES_GET_DATA()) { tmp |= 1; }

		_delay_us(6);
		
		NES_CLOCK_HIGH();
	}
	last_read_controller_bytes[0] = tmp;
}

static char nesChanged(char id)
{
	return famiconMicChanged || memcmp(last_read_controller_bytes, 
					last_reported_controller_bytes, GAMEPAD_BYTES);
}

static char nesBuildReport(unsigned char *reportBuffer, char id)
{
	int x, y;
	unsigned char lrcb;

	memcpy(last_reported_controller_bytes, 
			last_read_controller_bytes, GAMEPAD_BYTES);	

	if (reportBuffer == NULL) {
		return REPORT_SIZE;
	}

	lrcb = last_read_controller_bytes[0];
	y = x = 0x80;
	if (lrcb&0x1) { x = 0xff; }
	if (lrcb&0x2) { x = 0; }
	if (lrcb&0x4) { y = 0xff; }
	if (lrcb&0x8) { y = 0; }
	reportBuffer[0]=x;
	reportBuffer[1]=y;
	reportBuffer[2]=0;

	if (isFamicon) 
	{
		switch(isFamicon)
		{
			default:
			case 1: // Normal mode. A(1) B(2) AB(3) MIC(4)
				reportBuffer[2] = (lrcb & BTN_BIT_A) >> 7;
				reportBuffer[2] |=(lrcb & BTN_BIT_B) >> 5;

				if ((lrcb & BTN_BITS_AB) == BTN_BITS_AB) { // A + B
					reportBuffer[2] |= 0x04;
				}

				if (famiconMicStatus) {
					reportBuffer[2] |= 0x08;
					famiconMicStatus = 0;
					famiconMicChanged = 1;
				}
				break;

			case 2: // A held. Mic(1), B(2), AB(3), A(4)
				if (famiconMicStatus) {
					reportBuffer[2] |= 0x01;
					famiconMicStatus = 0;
					famiconMicChanged = 1;
				}

				if ((lrcb & BTN_BITS_AB) == BTN_BITS_AB) { // A + B
					reportBuffer[2] |= 0x04;
				} else {
					reportBuffer[2] |= (lrcb & BTN_BIT_B) >> 5;
					reportBuffer[2] |= (lrcb & BTN_BIT_A) >> 4;
				}
				break;

			case 3:
				if (famiconMicStatus) {
					reportBuffer[2] |= 0x02;
					famiconMicStatus = 0;
					famiconMicChanged = 1;
				}

				if ((lrcb & BTN_BITS_AB) == BTN_BITS_AB) { // A + B
					reportBuffer[2] |= 0x04;
				} else {
					reportBuffer[2] |= (lrcb & BTN_BIT_A) >> 7;
					reportBuffer[2] |= (lrcb & BTN_BIT_B) >> 3;
				}

				break;
		}

	}
	else 
	{
		// swap buttons so they get reported in a more
		// logical order (A-B-Select-Start)
		reportBuffer[2] = (lrcb & BTN_BIT_A) >> 7;
		reportBuffer[2] |= (lrcb & BTN_BIT_B) >> 5;
		reportBuffer[2] |= (lrcb & BTN_BIT_SELECT) >> 3;
		reportBuffer[2] |= (lrcb & BTN_BIT_START) >> 1;
	}


	return REPORT_SIZE;
}

const char nes_usbHidReportDescriptor[] PROGMEM = {
    0x05, 0x01,                    // USAGE_PAGE (Generic Desktop)
    0x09, 0x05,                    // USAGE (Game Pad)
    0xa1, 0x01,                    //   COLLECTION (Application)
    0x09, 0x01,                    //     USAGE (Pointer)
    0xa1, 0x00,                    //     COLLECTION (Physical)
    0x09, 0x30,                    //       USAGE (X)
    0x09, 0x31,                    //       USAGE (Y)
    0x15, 0x00,                    //       LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x00,				//      LOGICAL_MAXIMUM (255)
    0x75, 0x08,                    //       REPORT_SIZE (8)
    0x95, 0x02,                    //       REPORT_COUNT (2)
    0x81, 0x02,                    //       INPUT (Data,Var,Abs)
    0xc0,                          //     END_COLLECTION
    0x05, 0x09,                    //     USAGE_PAGE (Button)
    0x19, 0x01,                    //     USAGE_MINIMUM (Button 1)
    0x29, 0x04,                    //     USAGE_MAXIMUM (Button 4)
    0x15, 0x00,                    //     LOGICAL_MINIMUM (0)
    0x25, 0x01,                    //     LOGICAL_MAXIMUM (1)
    0x75, 0x01,                    //     REPORT_SIZE (1)
    0x95, 0x04,                    //     REPORT_COUNT (4)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)

	/* Padding.*/
	0x75, 0x01,                    //     REPORT_SIZE (1)
	0x95, 0x04,                    //     REPORT_COUNT (4)
	0x81, 0x03,                    //     INPUT (Constant,Var,Abs)
    0xc0,                          //   END_COLLECTION
};

#define USBDESCR_DEVICE         1

// This is the same descriptor as in devdesc.c, but the product id is 0x0a99 

const char nes_usbDescrDevice[] PROGMEM = {    /* USB device descriptor */
    18,         /* sizeof(usbDescrDevice): length of descriptor in bytes */
    USBDESCR_DEVICE,    /* descriptor type */
    0x01, 0x01, /* USB version supported */
    USB_CFG_DEVICE_CLASS,
    USB_CFG_DEVICE_SUBCLASS,
    0,          /* protocol */
    8,          /* max packet size */
    USB_CFG_VENDOR_ID,  /* 2 bytes */
    0x99, 0x0a,  /* 2 bytes */
    USB_CFG_DEVICE_VERSION, /* 2 bytes */
    USB_CFG_DESCR_PROPS_STRING_VENDOR != 0 ? 1 : 0,         /* manufacturer string index */
    USB_CFG_DESCR_PROPS_STRING_PRODUCT != 0 ? 2 : 0,        /* product string index */
    USB_CFG_DESCR_PROPS_STRING_SERIAL_NUMBER != 0 ? 3 : 0,  /* serial number string index */
    1,          /* number of configurations */
};



Gamepad NesGamepad = {
	.num_reports			=	1,
	.reportDescriptorSize	=	sizeof(nes_usbHidReportDescriptor),
	.deviceDescriptorSize	=	sizeof(nes_usbDescrDevice),
	.init					=	nesInit,
	.update					=	nesUpdate,
	.changed				=	nesChanged,
	.buildReport			=	nesBuildReport,
	.ultraPoll				=	famiconPollMic,
};

Gamepad *nesGetGamepad(void)
{
	NesGamepad.reportDescriptor = (void*)nes_usbHidReportDescriptor;
	NesGamepad.deviceDescriptor = (void*)nes_usbDescrDevice;
	
	return &NesGamepad;
}

