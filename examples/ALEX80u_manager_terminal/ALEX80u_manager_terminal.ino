/*
 * ALEX80u Manager Terminal example
 *
 * This sketch runs an Arduino UNO as the Z80 clock and memory controller.
 * The current example intentionally supports only the Arduino clock and the
 * shield SRAM; external clock and external memory paths are not included.
 *
 * A small demo program is loaded into SRAM at startup so the Z80 can be tested
 * immediately (it increments A and writes the value to the output LEDs).
 * Serial commands are handled cooperatively together with clock and bus
 * servicing. See serial.ino for the command parser and clock.ino/memory.ino
 * for the timing and memory-cycle state machines.
 */

#include <SPI.h>
#include <XModem.h>
#include <ALEX80u.h>

typedef void (*CommandHandler)(const char*);
CommandHandler onCommand = nullptr;
void setCommandHandler(CommandHandler handler);

#if defined(ARDUINO_AVR_UNO)  // Arduino UNO R3
#define SER_SPEED 9600
#define MCP_SPEED 8000000
#define RAM_SPEED 8000000
#elif defined(ARDUINO_UNOR4_WIFI) || defined(ARDUINO_UNOWIFIR4)  // Arduino UNO R4 WiFi
#define SER_SPEED 115200
#define MCP_SPEED 10000000
#define RAM_SPEED 20000000
#elif defined(ARDUINO_UNOR4_MINIMA) || defined(ARDUINO_MINIMA)  // Arduino UNO R4 Minima
#define SER_SPEED 115200
#define MCP_SPEED 10000000
#define RAM_SPEED 20000000
#else
#error "ALEX80u_manager_terminal supporta solo Arduino UNO R3, UNO R4 Minima e UNO R4 WiFi"
#endif

ALEX80u a80u(RAM_SPEED, MCP_SPEED);

const byte demo_prog[] PROGMEM = {0xAF, 0xD3, 0x00, 0x3C, 0xC3, 0x01, 0x00 };

XModem xmodem;
bool xmodem_process_block(void *blk_id, size_t idSize, byte *data, size_t dataSize);  // XMODEM file-transfer callback

bool isEchoOn = true;
bool isComputerConnected = false;
bool clockEnabled = false;
bool isWriting = false;
//bool monitorEnabled=false;
//bool debug_mode = false;
//bool busGranted=false; // Indicates that the Z80 buses have been granted
bool clockwasEnabled;

uint16_t current_add = 0;
uint32_t bytesRemaining = 0;

unsigned int freq = 50;


void enableLog() {
  
}

void disableLog() {
  
}


void setup() {
  delay(1);
  Serial.begin(SER_SPEED); 
  const unsigned long serialTimeout = millis() + 1000;
  while (!Serial && millis() < serialTimeout) {}  // Timeout needed for R4 if a serial terminal is not connected

  setCommandHandler(myCommandHandler);

  xmodem.begin(Serial, XModem::ProtocolType::XMODEM);
  xmodem.setRecieveBlockHandler(xmodem_process_block);

  a80u.begin_UNO();
  a80u.begin_RAM();
  a80u.begin_MCP();
  delay(1);

  for (int i=0; i<sizeof(demo_prog); ++i) a80u.write_RAM(i, pgm_read_byte(&demo_prog[i]));

  beginReset(false);
  resetZ80MemoryService();
}

void replyResetComplete() {
  if (isComputerConnected) Serial.println(F("!OK;")); else Serial.println(F("OK"));
}

bool xmodem_process_block(void *blk_id, size_t idSize, byte *data, size_t dataSize) {
  if (!isXmodemReceiveActive() || dataSize > bytesRemaining) return false;
  for(int i = 0; i < dataSize; ++i) {
    a80u.write_RAM(current_add, data[i]);
    --bytesRemaining;
    if (current_add != 0xFFFF) ++current_add;
  }

  return true; 
}

void loop() {
  serviceReset();
  serviceClock();
  servicePendingOperation();
  serviceManualWriteTimeout();
  if (!isMemoryOperationActive() && !isXmodemReceiveActive()) readCommandNonBlocking();
  serviceReset();
  serviceClock();
  servicePendingOperation();
}
