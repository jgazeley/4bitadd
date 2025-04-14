# Makefile for building count.c for an AVR (ATmega328p)

# Build parameters
F_CPU       = 16000000UL
MMCU        = atmega328p
SOURCE      = count.c
OUTPUT_ELF  = build\count.elf
OUTPUT_HEX  = build\count.hex

# Default target: build the HEX file.
all: $(OUTPUT_HEX)

# Compile the source file to an ELF file
$(OUTPUT_ELF): $(SOURCE)
	avr-gcc -DF_CPU=$(F_CPU) -mmcu=$(MMCU) -Os -o $(OUTPUT_ELF) $(SOURCE)

# Convert the ELF file to a HEX file
$(OUTPUT_HEX): $(OUTPUT_ELF)
	avr-objcopy -O ihex -R .eeprom $(OUTPUT_ELF) $(OUTPUT_HEX)

# Clean up build artifacts
clean:
	del $(OUTPUT_ELF) $(OUTPUT_HEX)

# -------------------------------------------------------------------
# Flash parameters (modify paths as needed)
AVRDUDE      = avrdude.exe
AVRDUDE_CONF = D:\Programs\Arduino\Arduino IDE Portable\hardware\tools\avr\etc\avrdude.conf
MCU          = atmega328p
PROGRAMMER   = arduino
BAUD         = 115200

!IF "$(COM_PORT)" == ""
COM_PORT = COM_INVALID
!ENDIF

flash: $(OUTPUT_HEX)
	@echo Flashing $(OUTPUT_HEX) to MCU using COM port: $(COM_PORT)
	$(AVRDUDE) -v -C "$(AVRDUDE_CONF)" -p $(MCU) -c $(PROGRAMMER) -P $(COM_PORT) -b $(BAUD) -D -U flash:w:"$(OUTPUT_HEX)":i

# -------------------------------------------------------------------
# Create a batch file (flash.bat) that prompts for a COM port, fixes it
# if necessary, and then calls nmake flash.
flash_bat: $(OUTPUT_HEX)
	@echo @echo off > flash.bat
	@echo set /p COMPort=Enter COM port (e.g. COM13): >> flash.bat
	@echo echo %%COMPort%% ^| findstr /b /i "COM" >nul >> flash.bat
	@echo if errorlevel 1 ^( >> flash.bat
	@echo     set COMPort=COM%%COMPort%% >> flash.bat
	@echo ^) >> flash.bat
	@echo nmake flash COM_PORT=%%COMPort%% >> flash.bat
	@echo pause >> flash.bat
	@echo Batch file created at flash.bat

