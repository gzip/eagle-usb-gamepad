CC=avr-gcc
AS=$(CC)
LD=$(CC)
PROGNAME=4nes4snes-m8
CPU=atmega8

CFLAGS=-Wall -Os -Iusbdrv -I. -mmcu=$(CPU) -DF_CPU=12000000L #-DDEBUG_LEVEL=1
LDFLAGS=-Wl,-Map=$(PROGNAME).map -mmcu=$(CPU) 
AVRDUDE=avrdude -p m8 -c usbasp

OBJS=usbdrv/usbdrv.o usbdrv/usbdrvasm.o usbdrv/oddebug.o main.o fournsnes.o devdesc.o

HEXFILE=$(PROGNAME).hex
ELFFILE=$(PROGNAME).elf

# symbolic targets:
all: $(HEXFILE)

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

.S.o:
	$(CC) $(CFLAGS) -x assembler-with-cpp -c $< -o $@
# "-x assembler-with-cpp" should not be necessary since this is the default
# file type for the .S (with capital S) extension. However, upper case
# characters are not always preserved on Windows. To ensure WinAVR
# compatibility define the file type manually.

.c.s:
	$(CC) $(CFLAGS) -S $< -o $@


clean:
	rm -f $(HEXFILE) main.lst main.obj main.cof main.list main.map main.eep.hex main.bin *.o usbdrv/*.o main.s usbdrv/oddebug.s usbdrv/usbdrv.s

# file targets:
$(ELFFILE): $(OBJS)
	$(LD) $(LDFLAGS) -o $(ELFFILE) $(OBJS)

$(HEXFILE):	$(ELFFILE)
	rm -f $(HEXFILE) 
	avr-objcopy -j .text -j .data -O ihex $(ELFFILE) $(HEXFILE)
	./checksize $(ELFFILE)


flash: $(HEXFILE)
	$(AVRDUDE) -Uflash:w:$(HEXFILE) -B 1.0

flashall: flash fuse

# Fuse high byte:
# 0xc9 = 1 1 0 0   1 0 0 1 <-- BOOTRST (boot reset vector at 0x0000)
#        ^ ^ ^ ^   ^ ^ ^------ BOOTSZ0
#        | | | |   | +-------- BOOTSZ1
#        | | | |   + --------- EESAVE (don't preserve EEPROM over chip erase)
#        | | | +-------------- CKOPT (full output swing)
#        | | +---------------- SPIEN (allow serial programming)
#        | +------------------ WDTON (WDT not always on)
#        +-------------------- RSTDISBL (reset pin is enabled)
# Fuse low byte:
# 0x9f = 1 0 0 1   1 1 1 1
#        ^ ^ \ /   \--+--/
#        | |  |       +------- CKSEL 3..0 (external >8M crystal)
#        | |  +--------------- SUT 1..0 (crystal osc, BOD enabled)
#        | +------------------ BODEN (BrownOut Detector enabled)
#        +-------------------- BODLEVEL (2.7V)

LFUSE=0x9F
HFUSE=0xC9

fuse:
	$(AVRDUDE) -e -Uhfuse:w:$(HFUSE):m -Ulfuse:w:$(LFUSE):m -B 20.0 -v

chip_erase:
	$(AVRDUDE) -e -B 1.0 -F

reset:
	$(AVRDUDE) -B 1.0 -F
	
