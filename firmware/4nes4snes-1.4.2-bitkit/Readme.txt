This is 4nes4snes Readme.txt file. 

Table of contents:

1) What is 4nes4snes?
2) USB Implementation
3) Compilation and installation
4) License
5) About the vendor id/product id pair:
6) Where do I get more information and updates?


1) What is 4nes4snes?
   --------------------
	4nes4snes if a firmware for Atmel ATmega8 and Atmega168* 
	which allows one to connect up to 4 NES and/or SNES 
	controllers to a PC using a single circuit. 

	The device connects to an USB port and appears to the
	PC as standard HID joystick with 4 report Id's. This means
	that it looks like 4 controllers in Windows's 
	control_panel->game_controllers window.

* Other devices from the same family are probably supported, but
not tested.


2) USB Implementation
   ------------------
	4nes4snes uses the software-only usb driver from Objective Development.
	See http://www.obdev.at/products/avrusb/index.html

	A good portion of 4nes4snes is based on Objective Development's
	HIDKeys example.


3) Compilation and installation
   ----------------------------
	First, you must compile it. To compile, you need a working avr-gcc and
	avr-libc. Under linux or cygwin, simply type make in the project directory.
	(assuming avr-gcc is in your path). 

	Next, you must upload the generated file (main.hex) to the Atmega8 using
	whatever tools you like. Personally, I use uisp. The 'flash' and 'fuse'
	targets in the makefile is a good example about how to use it. 

	The Atmega fuse bits must also be set properly. The idea behind this is to
	enable the external 12mhz crystal instead of the internal clock. Check the
	makefile for good fuse bytes values.


4) License
   -------
	4nes4snes is released under the GPLv2 license. See License.txt


5) About the vendor id/product id pair:
   ------------------------------------
	Please do not re-use them for other projects. Instead,
	obtain your own.


6) Where can I get more information and updates?
   --------------------------------------------
	Visit the 4nes4snes webpage:
	http://www.raphnet.net/electronique/4nes4snes/index_en.php
	you may also contact me by email:
	Raphael Assenat <raph@raphnet.net>
	

