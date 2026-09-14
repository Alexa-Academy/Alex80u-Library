// Serial memory commands. Dump output is buffered a small record at a time so
// no long command monopolises the serial TX buffer or allocates a large RAM area.

const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
const uint32_t MANUAL_WRITE_TIMEOUT_MS = 30000UL;

enum PendingMemoryOperation : uint8_t {
  MEMORY_OPERATION_NONE,
  MEMORY_OPERATION_TEXT_DUMP,
  MEMORY_OPERATION_BINARY_DUMP,
  MEMORY_OPERATION_MESSAGE,
  MEMORY_OPERATION_INFO,
};

struct MemoryCommandState {
  PendingMemoryOperation operation;
  uint32_t address;
  uint32_t remaining;
  bool restoreClock;
  bool startStatusPending;
  bool finishStatusPending;
  const char *message;
  uint8_t messageStage;
  char tx[80];
  uint8_t txLength;
  uint8_t txPosition;
};

MemoryCommandState memoryCommand = {
  MEMORY_OPERATION_NONE, 0, 0, false, false, false, NULL, 0, { 0 }, 0, 0
};

bool xmodemReceiveActive = false;
uint32_t manualWriteLastActivityMs = 0;

static char hexDigit(uint8_t value) {
  value &= 0x0F;
  return value < 10 ? '0' + value : 'A' + value - 10;
}

static void queueTxChar(char value) {
  memoryCommand.tx[memoryCommand.txLength++] = value;
}

static void queueTxText(const char *value) {
  while (*value != '\0') queueTxChar(*value++);
}

static void queueTextDumpLine() {
  const uint8_t byteCount = memoryCommand.remaining < 16UL
      ? static_cast<uint8_t>(memoryCommand.remaining) : 16;
  uint8_t data[16];
  for (uint8_t i = 0; i < byteCount; ++i) {
    data[i] = a80u.read_RAM(static_cast<uint16_t>(memoryCommand.address + i));
  }

  memoryCommand.txLength = 0;
  memoryCommand.txPosition = 0;
  const uint16_t address = static_cast<uint16_t>(memoryCommand.address);
  queueTxChar(hexDigit(address >> 12));
  queueTxChar(hexDigit(address >> 8));
  queueTxChar(hexDigit(address >> 4));
  queueTxChar(hexDigit(address));
  queueTxText(":  ");
  for (uint8_t i = 0; i < 16; ++i) {
    if (i < byteCount) {
      queueTxChar(hexDigit(data[i] >> 4));
      queueTxChar(hexDigit(data[i]));
    } else {
      queueTxText("  ");
    }
    queueTxChar(' ');
    if (i == 7) queueTxChar(' ');
  }
  queueTxChar(' ');
  for (uint8_t i = 0; i < 16; ++i) {
    if (i < byteCount) {
      queueTxChar(data[i] >= 0x20 && data[i] <= 0x7E ? data[i] : '.');
    } else {
      queueTxChar(' ');
    }
  }
  queueTxText("\r\n");
  memoryCommand.address += byteCount;
  memoryCommand.remaining -= byteCount;
}

static void queueBinaryFrame() {
  const uint8_t byteCount = memoryCommand.remaining < 3UL
      ? static_cast<uint8_t>(memoryCommand.remaining) : 3;
  uint8_t input[3] = { 0, 0, 0 };
  for (uint8_t i = 0; i < byteCount; ++i) {
    input[i] = a80u.read_RAM(static_cast<uint16_t>(memoryCommand.address + i));
  }

  memoryCommand.txLength = 0;
  memoryCommand.txPosition = 0;
  queueTxText("!DATA");
  queueTxChar(b64_table[(input[0] & 0xFC) >> 2]);
  queueTxChar(b64_table[((input[0] & 0x03) << 4) | ((input[1] & 0xF0) >> 4)]);
  queueTxChar(byteCount > 1 ? b64_table[((input[1] & 0x0F) << 2) | ((input[2] & 0xC0) >> 6)] : '=');
  queueTxChar(byteCount > 2 ? b64_table[input[2] & 0x3F] : '=');
  queueTxChar(';');
  memoryCommand.address += byteCount;
  memoryCommand.remaining -= byteCount;
}

static void completeMemoryOperation() {
  const bool restartClock = memoryCommand.restoreClock;
  memoryCommand.operation = MEMORY_OPERATION_NONE;
  memoryCommand.txLength = 0;
  memoryCommand.txPosition = 0;
  memoryCommand.startStatusPending = false;
  memoryCommand.finishStatusPending = false;
  memoryCommand.message = NULL;
  memoryCommand.messageStage = 0;
  if (restartClock) startClock();
}

bool isMemoryOperationActive() {
  return memoryCommand.operation != MEMORY_OPERATION_NONE;
}

bool isXmodemReceiveActive() {
  return xmodemReceiveActive;
}

void beginMemoryDump(uint16_t start, uint32_t length, bool binary) {
  memoryCommand.operation = binary ? MEMORY_OPERATION_BINARY_DUMP : MEMORY_OPERATION_TEXT_DUMP;
  memoryCommand.address = start;
  memoryCommand.remaining = min(static_cast<uint32_t>(length), 0x10000UL - start);
  memoryCommand.restoreClock = stopClockAndWait();
  memoryCommand.startStatusPending = binary && isComputerConnected;
  memoryCommand.finishStatusPending = binary;
  memoryCommand.txLength = 0;
  memoryCommand.txPosition = 0;
}

void beginHelpOutput() {
  static const char helpText[] PROGMEM =
      "Usage:\r\n"
      "\r\n"
      "r start length                      read hexadecimal\r\n"
      "rb start length                     read Base64 frames\r\n"
      "w start                             write hexadecimal; end/cancel, 30 s timeout\r\n"
      "wb start                            write binary with XMODEM\r\n"
      "sclk freq                           set clock frequency\r\n"
      "clk start/stop/arduino              control/select Arduino clock\r\n"
      "clk external, mem external          not available\r\n"
      "mem arduino                         select shield SRAM\r\n"
      "cc                                  set computer connected\r\n"
      "hc                                  set human connected\r\n"
      "echo on/off                         set echo on/off\r\n"
      "log on/off                          not available\r\n"
      "info                                get config infos\r\n"
      "reset                               reset Z80\r\n"
      "?                                   this help\r\n";

  memoryCommand.operation = MEMORY_OPERATION_MESSAGE;
  memoryCommand.address = 0;
  memoryCommand.remaining = 0;
  memoryCommand.restoreClock = false;
  memoryCommand.startStatusPending = false;
  memoryCommand.finishStatusPending = false;
  memoryCommand.message = helpText;
  memoryCommand.messageStage = 0;
  memoryCommand.txLength = 0;
  memoryCommand.txPosition = 0;
}

void beginInfoOutput() {
  memoryCommand.operation = MEMORY_OPERATION_INFO;
  memoryCommand.restoreClock = false;
  memoryCommand.startStatusPending = false;
  memoryCommand.finishStatusPending = false;
  memoryCommand.message = NULL;
  memoryCommand.messageStage = 0;
  memoryCommand.txLength = 0;
  memoryCommand.txPosition = 0;
}

static bool queueInfoChunk() {
  memoryCommand.txLength = 0;
  memoryCommand.txPosition = 0;
  if (isComputerConnected) {
    if (memoryCommand.messageStage != 0) return false;
    memoryCommand.txLength = snprintf(
        memoryCommand.tx, sizeof(memoryCommand.tx),
        "!INFO:CARD:ALEX80\xC2\xB5,SCLK:A,FCLK:%u,ECLK:%u,SMEM:A,DEBUG:OFF,MONITOR:OFF;\r\n",
        freq, clockEnabled ? 1 : 0);
    ++memoryCommand.messageStage;
    return true;
  }

  if (memoryCommand.messageStage == 0) {
    memoryCommand.txLength = snprintf(
        memoryCommand.tx, sizeof(memoryCommand.tx),
        "Settings\r\n  Card: ALEX80\xC2\xB5\r\n  Clock: Arduino\r\n  Clock freq: %u Hz\r\n", freq);
    ++memoryCommand.messageStage;
    return true;
  }
  if (memoryCommand.messageStage == 1) {
    memoryCommand.txLength = snprintf(
        memoryCommand.tx, sizeof(memoryCommand.tx),
        "  Clock: %s\r\n  Memory: Arduino\r\n  Debug: Off\r\n  Monitor: Off\r\n",
        clockEnabled ? "On" : "Off");
    ++memoryCommand.messageStage;
    return true;
  }
  return false;
}

// Called from loop. It writes at most one byte to the UART at a time. Some
// supported cores can transiently report zero from availableForWrite() even
// though write() accepts a byte; advance only after the byte is accepted.
void servicePendingOperation() {
  if (!isMemoryOperationActive()) return;

  if (memoryCommand.txPosition < memoryCommand.txLength) {
    if (Serial.write(memoryCommand.tx[memoryCommand.txPosition]) == 1) {
      ++memoryCommand.txPosition;
    }
    return;
  }

  if (memoryCommand.operation == MEMORY_OPERATION_MESSAGE) {
    memoryCommand.txLength = 0;
    memoryCommand.txPosition = 0;
    while (memoryCommand.txLength < sizeof(memoryCommand.tx)) {
      const char value = pgm_read_byte(memoryCommand.message + memoryCommand.address);
      if (value == '\0') {
        completeMemoryOperation();
        return;
      }
      queueTxChar(value);
      ++memoryCommand.address;
    }
    return;
  }

  if (memoryCommand.operation == MEMORY_OPERATION_INFO) {
    if (!queueInfoChunk()) completeMemoryOperation();
    return;
  }

  if (memoryCommand.startStatusPending) {
    memoryCommand.txLength = 0;
    memoryCommand.txPosition = 0;
    queueTxText("!STATUS1;");
    memoryCommand.startStatusPending = false;
    return;
  }

  if (memoryCommand.remaining > 0) {
    if (memoryCommand.operation == MEMORY_OPERATION_TEXT_DUMP) queueTextDumpLine();
    else queueBinaryFrame();
    return;
  }

  if (memoryCommand.finishStatusPending) {
    memoryCommand.txLength = 0;
    memoryCommand.txPosition = 0;
    if (isComputerConnected) queueTxText("!STATUS2;\r\n");
    else queueTxText("\r\nOK\r\n");
    memoryCommand.finishStatusPending = false;
    return;
  }

  completeMemoryOperation();
}

void beginManualWrite(uint16_t start) {
  current_add = start;
  bytesRemaining = 0x10000UL - start;
  clockwasEnabled = stopClockAndWait();
  manualWriteLastActivityMs = millis();
  isWriting = true;
}

static void finishManualWrite(bool completed) {
  const bool restartClock = clockwasEnabled;
  isWriting = false;
  if (restartClock) startClock();
  if (completed) {
    if (isComputerConnected) Serial.println(F("!OK;")); else Serial.println(F("OK"));
  } else {
    if (isComputerConnected) Serial.println(F("!ERROR:WRITE CANCELLED;")); else Serial.println(F("WRITE CANCELLED"));
  }
}

bool serviceManualWriteLine(const char *line) {
  if (!isWriting) return false;
  manualWriteLastActivityMs = millis();
  if (strcmp(line, "end") == 0) {
    finishManualWrite(true);
    return true;
  }
  if (strcmp(line, "cancel") == 0) {
    finishManualWrite(false);
    return true;
  }

  uint8_t data;
  if (!parseByteHex(line, data)) {
    replyError(F("!ERROR:INVALID BYTE;"), F("INVALID BYTE"));
    return true;
  }
  if (bytesRemaining == 0) {
    replyError(F("!ERROR:ADDRESS RANGE EXCEEDED;"), F("ADDRESS RANGE EXCEEDED"));
    return true;
  }

  a80u.write_RAM(current_add, data);
  --bytesRemaining;
  if (current_add != 0xFFFF) ++current_add;
  return true;
}

void serviceManualWriteTimeout() {
  if (isWriting && static_cast<uint32_t>(millis() - manualWriteLastActivityMs) >= MANUAL_WRITE_TIMEOUT_MS) {
    finishManualWrite(false);
  }
}

bool receiveXmodem(uint16_t start) {
  current_add = start;
  bytesRemaining = 0x10000UL - start;
  const bool restartClock = stopClockAndWait();
  xmodemReceiveActive = true;
  const bool completed = xmodem.receive();
  xmodemReceiveActive = false;
  if (restartClock) startClock();
  return completed;
}
