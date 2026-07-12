#include "ALEX80u.h"


#ifdef ARDUINO_AVR_UNO  // Uno R3
#define SER_SPEED 9600
#define MCP_SPEED 8000000
#define RAM_SPEED 8000000
#endif

#ifdef ARDUINO_UNOR4_WIFI  // Uno R4 WIFI
#define SER_SPEED 115200
#define MCP_SPEED 10000000
#define RAM_SPEED 20000000
#endif

#ifdef ARDUINO_MINIMA  // Uno R4 MINIMA
#define SER_SPEED 115200
#define MCP_SPEED 10000000
#define RAM_SPEED 20000000
#endif

ALEX80u a80u(RAM_SPEED, MCP_SPEED);


uint8_t z_data = 0x00;

void setup() {
    Serial.begin(SER_SPEED);
    const unsigned long serialTimeout = millis() + 1000;
    while (!Serial && millis() < serialTimeout) {}  // Timeout needed for R4 if a serial terminal is not connected

    a80u.begin_UNO();  // Configures Arduino Uno inputs and outputs
    a80u.begin_RAM();  // Initializes the SRAM
    a80u.begin_MCP();  // Configures inputs and outputs of both MCPs

    a80u.set_RST(LOW);  // HIGH | LOW - Sets the RST pin state
    a80u.set_CLK(LOW);  // HIGH | LOW - Sets the CLK pin state

    a80u.set_INT(HIGH);    // HIGH | LOW - Sets the INT pin state
    a80u.set_NMI(HIGH);    // HIGH | LOW - Sets the NMI pin state
    a80u.set_BUSRQ(HIGH);  // HIGH | LOW - Sets the BUSRQ pin state
    a80u.set_WAIT(HIGH);   // HIGH | LOW - Sets the WAIT pin state

    uint16_t z_addr = 0x0000;

    uint8_t z_cmd;
    a80u.read_BUS(z_addr, z_cmd);  // Unified snapshot of AddressBus and CommandBus
    const bool z_halt = bitRead(z_cmd, 0);
    const bool z_rfsh = bitRead(z_cmd, 1);
    const bool z_m1 = bitRead(z_cmd, 2);
    const bool z_iorq = bitRead(z_cmd, 3);
    const bool z_mreq = bitRead(z_cmd, 4);
    const bool z_wr = bitRead(z_cmd, 5);
    const bool z_busack = bitRead(z_cmd, 6);
    const bool z_rd = bitRead(z_cmd, 7);

    uint8_t z_data = a80u.read_RAM(z_addr);  // Reads SRAM
    a80u.write_RAM(z_addr, z_data);  // Writes SRAM

    a80u.pinMode_DATA(OUTPUT);  // INPUT | OUTPUT - Sets the data-bus direction
    a80u.write_DATA(z_data);      // Writes the data bus

    a80u.pinMode_DATA(INPUT);  // INPUT | OUTPUT - Sets the data-bus direction
    z_data = a80u.read_DATA();   // Reads the data bus
}

void loop() {

}
