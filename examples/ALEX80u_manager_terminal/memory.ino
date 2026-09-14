// SPI SRAM service for Z80 memory cycles.
// Command-byte signals are active low; see ALEX80u::read_CMD().

enum MemoryCycleState : uint8_t {
  MEMORY_CYCLE_IDLE,
  MEMORY_CYCLE_READ,
  MEMORY_CYCLE_WRITE,
};

struct Z80MemoryServiceState {
  MemoryCycleState cycle;
  bool dataBusIsOutput;
  bool previousRd;
  bool previousWr;
};

Z80MemoryServiceState z80MemoryService = {
  MEMORY_CYCLE_IDLE,
  false,
  HIGH,
  HIGH,
};

static bool isZ80MemoryCycle(uint8_t command) {
  const bool rfsh = bitRead(command, 1);
  const bool iorq = bitRead(command, 3);
  const bool mreq = bitRead(command, 4);
  const bool busack = bitRead(command, 6);

  // Ignore refresh, I/O/interrupt acknowledge, and a bus granted via BUSRQ/BUSACK.
  return rfsh == HIGH && iorq == HIGH && mreq == LOW && busack == HIGH;
}

static void releaseZ80DataBus() {
  if (z80MemoryService.dataBusIsOutput) {
    a80u.pinMode_DATA(INPUT);
    z80MemoryService.dataBusIsOutput = false;
  }
}

void resetZ80MemoryService() {
  releaseZ80DataBus();
  z80MemoryService.cycle = MEMORY_CYCLE_IDLE;
  z80MemoryService.previousRd = HIGH;
  z80MemoryService.previousWr = HIGH;
}

void serviceZ80Memory() {
  uint16_t address;
  uint8_t command;
  a80u.read_BUS(address, command);

  const bool rd = bitRead(command, 7);
  const bool wr = bitRead(command, 5);
  const bool validMemoryCycle = isZ80MemoryCycle(command);

  // Keep the byte driven until a read cycle ends. For writes, the state avoids
  // duplicate samples when WR remains low across multiple loop iterations.
  if (z80MemoryService.cycle == MEMORY_CYCLE_READ) {
    if (validMemoryCycle && rd == LOW && wr == HIGH) {
      z80MemoryService.previousRd = rd;
      z80MemoryService.previousWr = wr;
      return;
    }
    releaseZ80DataBus();
    z80MemoryService.cycle = MEMORY_CYCLE_IDLE;
  } else if (z80MemoryService.cycle == MEMORY_CYCLE_WRITE) {
    if (validMemoryCycle && wr == LOW && rd == HIGH) {
      z80MemoryService.previousRd = rd;
      z80MemoryService.previousWr = wr;
      return;
    }
    z80MemoryService.cycle = MEMORY_CYCLE_IDLE;
  }

  if (validMemoryCycle && rd == LOW && wr == HIGH) {
    const uint8_t data = a80u.read_RAM(address);
    a80u.pinMode_DATA(OUTPUT);
    z80MemoryService.dataBusIsOutput = true;
    a80u.write_DATA(data);
    z80MemoryService.cycle = MEMORY_CYCLE_READ;
  } else if (validMemoryCycle && wr == LOW && rd == HIGH) {
    // Arduino must be an input before sampling data written by the CPU. The
    // state assignment makes the write idempotent.
    releaseZ80DataBus();
    const uint8_t data = a80u.read_DATA();
    a80u.write_RAM(address, data);
    z80MemoryService.cycle = MEMORY_CYCLE_WRITE;
  }

  z80MemoryService.previousRd = rd;
  z80MemoryService.previousWr = wr;
}
