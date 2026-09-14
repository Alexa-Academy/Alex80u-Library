#include "Arduino.h"

#define CMD_READ  "r"
#define CMD_READ_BIN  "rb"
#define CMD_WRITE "w"
#define CMD_WRITE_BIN "wb"
#define CMD_SET_ECHO "echo"
#define CMD_SET_COMPUTER_CONNECTED "cc"
#define CMD_SET_HUMAN_CONNECTED "hc"
#define CMD_SET_CLOCK_FREQ "sclk"
#define CMD_CLOCK "clk"
#define CMD_LOG "log"
#define CMD_DEBUG "debug"
#define CMD_STEP "s"
#define CMD_RESET "reset"
#define CMD_DUMP "d"
#define CMD_MEM_SEL "mem"
#define CMD_GET_INFO "info"
#define CMD_NMI "nmi"
#define CMD_MONITOR "monitor"
#define CMD_HELP "?"

#define BACKSPACE 0x08
#define DEL 0x7F

#define COMMANDSIZE 100
char cmdbuf[COMMANDSIZE];
uint8_t cmdIndex = 0;
bool lineOverflow = false;
bool lastInputWasCR = false;

#define MAX_COMMAND_ARGS 6
#define SERIAL_BYTES_PER_LOOP 8

void replyError(const __FlashStringHelper *computer, const __FlashStringHelper *human) {
  if (isComputerConnected) Serial.println(computer); else Serial.println(human);
}

static void replyOk() {
  if (isComputerConnected) Serial.println(F("!OK;")); else Serial.println(F("OK"));
}

static bool parseHex16(const char *text, uint16_t &value) {
  if (text == NULL || *text == '\0' || *text == '-' || *text == '+') return false;
  char *end = NULL;
  unsigned long parsed = strtoul(text, &end, 16);
  if (end == text || *end != '\0' || parsed > 0xFFFFUL) return false;
  value = (uint16_t)parsed;
  return true;
}

static bool parseDec16(const char *text, uint16_t &value) {
  if (text == NULL || *text == '\0' || *text == '-' || *text == '+') return false;
  char *end = NULL;
  unsigned long parsed = strtoul(text, &end, 10);
  if (end == text || *end != '\0' || parsed > 0xFFFFUL) return false;
  value = (uint16_t)parsed;
  return true;
}

static bool parseMemoryLength(const char *text, uint32_t &value) {
  if (text == NULL || *text == '\0' || *text == '-' || *text == '+') return false;
  char *end = NULL;
  unsigned long parsed = strtoul(text, &end, 10);
  if (end == text || *end != '\0' || parsed > 0x10000UL) return false;
  value = parsed;
  return true;
}

bool parseByteHex(const char *text, uint8_t &value) {
  uint16_t parsed;
  if (!parseHex16(text, parsed) || parsed > 0xFF) return false;
  value = (uint8_t)parsed;
  return true;
}


// Set the command callback.
void setCommandHandler(CommandHandler handler) {
  onCommand = handler;
}

// Call from loop() to process a bounded number of input bytes.
void readCommandNonBlocking() {
  uint8_t processed = 0;
  while (Serial.available() > 0 && processed++ < SERIAL_BYTES_PER_LOOP) {
    char c = Serial.read();

    if (c == '\n' || c == '\r') {
      if (c == '\n' && lastInputWasCR) {
        lastInputWasCR = false;
        continue; // CRLF: treat the pair as one line ending.
      }
      lastInputWasCR = (c == '\r');
      if (lineOverflow) {
        lineOverflow = false;
        cmdIndex = 0;
        replyError(F("!ERROR:COMMAND TOO LONG;"), F("COMMAND TOO LONG"));
      } else if (cmdIndex > 0) { // Non-empty command.
        cmdbuf[cmdIndex] = '\0';
        if (onCommand) {
          onCommand(cmdbuf);   // Invoke the callback.
        }
        cmdIndex = 0;
      }
    } else if (c == BACKSPACE || c == DEL) {
      if (cmdIndex > 0 && !lineOverflow) {
        --cmdIndex;
        if (isEchoOn) Serial.print(F("\b \b"));
      }
    } else {
      lastInputWasCR = false;
      if (isEchoOn) Serial.write(c);
      if (cmdIndex < COMMANDSIZE - 1) {
        cmdbuf[cmdIndex++] = c;
      } else {
        lineOverflow = true;
      }
    }
  }
}

void myCommandHandler(const char* cmd) {
  if (isResetInProgress()) {
    replyError(F("!ERROR:RESET IN PROGRESS;"), F("RESET IN PROGRESS"));
    return;
  }

  if (isWriting) {
    serviceManualWriteLine(cmd);
  } else {
    uint8_t index = 0;
    char *strings[MAX_COMMAND_ARGS];
    char *ptr = NULL;
    ptr = strtok((char *)cmd, " \t");
    while (ptr != NULL) {
      if (index == MAX_COMMAND_ARGS) {
        replyError(F("!ERROR:TOO MANY PARAMETERS;"), F("TOO MANY PARAMETERS"));
        return;
      }
      strings[index++] = ptr;
      ptr = strtok(NULL, " ");
    }

    if (index > 0) {
      if (strcmp(strings[0], CMD_READ) == 0) {
        word startAdd = 0;
        uint32_t len = 16;
        if (index > 3) { replyError(F("!ERROR:TOO MANY PARAMETERS;"), F("TOO MANY PARAMETERS")); return; }
        if (index > 1 && !parseHex16(strings[1], startAdd)) { replyError(F("!ERROR:INVALID ADDRESS;"), F("INVALID ADDRESS")); return; }
        if (index > 2 && !parseMemoryLength(strings[2], len)) { replyError(F("!ERROR:INVALID LENGTH;"), F("INVALID LENGTH")); return; }
        if (len == 0) { replyError(F("!ERROR:LENGTH MUST BE GREATER THAN 0;"), F("LENGTH MUST BE GREATER THAN 0")); return; }

        beginMemoryDump(startAdd, len, false);
      } else if (strcmp(strings[0], CMD_READ_BIN) == 0) {
        uint16_t startAdd = 0;
        uint32_t len = 16;
        if (index > 3) { replyError(F("!ERROR:TOO MANY PARAMETERS;"), F("TOO MANY PARAMETERS")); return; }
        if (index > 1 && !parseHex16(strings[1], startAdd)) { replyError(F("!ERROR:INVALID ADDRESS;"), F("INVALID ADDRESS")); return; }
        if (index > 2 && !parseMemoryLength(strings[2], len)) { replyError(F("!ERROR:INVALID LENGTH;"), F("INVALID LENGTH")); return; }
        if (len == 0) { replyError(F("!ERROR:LENGTH MUST BE GREATER THAN 0;"), F("LENGTH MUST BE GREATER THAN 0")); return; }

        beginMemoryDump(startAdd, len, true);
      } else if (strcmp(strings[0], CMD_WRITE) == 0) {
        uint16_t startAdd = 0;
        if (index != 2 || !parseHex16(strings[1], startAdd)) { replyError(F("!ERROR:INVALID ADDRESS;"), F("INVALID ADDRESS")); return; }
        beginManualWrite(startAdd);
      } else if (strcmp(strings[0], CMD_WRITE_BIN) == 0) {
        uint16_t startAdd = 0;
        if (index != 2 || !parseHex16(strings[1], startAdd)) { replyError(F("!ERROR:INVALID ADDRESS;"), F("INVALID ADDRESS")); return; }
        if (!isComputerConnected) Serial.println(F("SEND FILE (XMODEM PROTOCOL)"));

        const bool transferCompleted = receiveXmodem(startAdd);

        if (transferCompleted) {
          if (!isComputerConnected) {
            Serial.println(F("OK"));
          }
        } else {
          if (isComputerConnected) Serial.println(F("!ERROR:AN ERROR OCCURRED DURING FILE TRANSFER;")); else Serial.println(F("AN ERROR OCCURRED DURING FILE TRANSFER"));
        }

      } else if (strcmp(strings[0], CMD_SET_ECHO) == 0) {
        if (index == 2) {
          if (strcmp(strings[1], "on") == 0) {
            isEchoOn = true;
            if (isComputerConnected) Serial.println(F("!OK;")); else Serial.println(F("OK"));
          } else if (strcmp(strings[1], "off") == 0) {
            isEchoOn = false;
            if (isComputerConnected) Serial.println(F("!OK;")); else Serial.println(F("OK"));
          } else {
            if (isComputerConnected) Serial.println(F("!ERROR:WRONG PARAMETER;")); else Serial.println(F("WRONG PARAMETER"));
          }
        } else {
          if (isComputerConnected) Serial.println(F("!ERROR:MISSING PARAMETERS;")); else Serial.println(F("MISSING PARAMETERS"));
        }
      } else if (strcmp(strings[0], CMD_SET_COMPUTER_CONNECTED) == 0) {
        isEchoOn = false;
        isComputerConnected = true;
      } else if (strcmp(strings[0], CMD_SET_HUMAN_CONNECTED) == 0) {
        isEchoOn = true;
        isComputerConnected = false;
      } else if (strcmp(strings[0], CMD_SET_CLOCK_FREQ) == 0) {  
        if (index == 2) {
          uint16_t freq_val;
          if (!parseDec16(strings[1], freq_val) || freq_val == 0) {
            replyError(F("!ERROR:INVALID FREQUENCY;"), F("INVALID FREQUENCY"));
            return;
          }
          setClockFrequency(freq_val);
          if (isComputerConnected) {
            replyOk();
          } else {
            Serial.print(F("OK frequency set to "));
            Serial.print(freq);
            Serial.println(F(" Hz"));
          }
        } else {
          if (isComputerConnected) Serial.println(F("!ERROR:MISSING PARAMETERS;")); else Serial.println(F("MISSING PARAMETERS"));
        }
      } else if (strcmp(strings[0], CMD_CLOCK) == 0) { 
        if (index == 2) {
          if (strcmp(strings[1], "start") == 0) {
            startClock();
            replyOk();
          } else if (strcmp(strings[1], "stop") == 0) {
            stopClockAndWait();
            replyOk();
          } else if (strcmp(strings[1], "arduino") == 0) {
            // This is the only implemented source; retaining the historical
            // command makes source selection explicit and idempotent.
            replyOk();
          } else if (strcmp(strings[1], "external") == 0) {
            replyError(F("!ERROR:EXTERNAL CLOCK NOT AVAILABLE;"), F("EXTERNAL CLOCK NOT AVAILABLE"));
          } else {
            if (isComputerConnected) Serial.println(F("!ERROR:WRONG PARAMETER;")); else Serial.println(F("WRONG PARAMETER"));
          }
        } else {
          if (isComputerConnected) Serial.println(F("!ERROR:MISSING PARAMETERS;")); else Serial.println(F("MISSING PARAMETERS"));
        }
      } else if (strcmp(strings[0], CMD_LOG) == 0) {
        if (index == 2) {
          if (strcmp(strings[1], "on") == 0) {
            replyError(F("!ERROR:LOG NOT AVAILABLE;"), F("LOG NOT AVAILABLE"));
          } else if (strcmp(strings[1], "off") == 0) {
            replyError(F("!ERROR:LOG NOT AVAILABLE;"), F("LOG NOT AVAILABLE"));
          } else {
            if (isComputerConnected) Serial.println(F("!ERROR:WRONG PARAMETER;")); else Serial.println(F("WRONG PARAMETER"));
          }
        } else {
          if (isComputerConnected) Serial.println(F("!ERROR:MISSING PARAMETERS;")); else Serial.println(F("MISSING PARAMETERS"));
        }
      } /*else if (strcmp(strings[0], CMD_DEBUG) == 0) {
        if (index == 2) {
          if (strcmp(strings[1], "on") == 0) {
            enableDebug();
            if (isComputerConnected) Serial.println(F("!OK;")); else Serial.println(F("OK"));
          } else if (strcmp(strings[1], "off") == 0) {
            disableDebug();
            if (isComputerConnected) Serial.println(F("!OK;")); else Serial.println(F("OK"));
          } else {
            if (isComputerConnected) Serial.println(F("!ERROR:WRONG PARAMETER;")); else Serial.println(F("WRONG PARAMETER"));
          }
        } else {
          if (isComputerConnected) Serial.println(F("!ERROR:MISSING PARAMETERS;")); else Serial.println(F("MISSING PARAMETERS"));
        }
      } *//*else if (strcmp(strings[0], CMD_STEP) == 0) {
        in_wait = false;
        first_wait_logged = false;
        setWAIT(false);
      } */else if (strcmp(strings[0], CMD_RESET) == 0) {
        if (!beginReset(true)) {
          replyError(F("!ERROR:RESET IN PROGRESS;"), F("RESET IN PROGRESS"));
        }
      } else if (strcmp(strings[0], CMD_MEM_SEL) == 0) {
        if (index == 2) {
          if (strcmp(strings[1], "arduino") == 0) {
            replyOk();
          } else if (strcmp(strings[1], "external") == 0) {
            replyError(F("!ERROR:EXTERNAL MEMORY NOT AVAILABLE;"), F("EXTERNAL MEMORY NOT AVAILABLE"));
          } else {
            if (isComputerConnected) Serial.println(F("!ERROR:WRONG PARAMETER;")); else Serial.println(F("WRONG PARAMETER"));
          }
        } else {
          if (isComputerConnected) Serial.println(F("!ERROR:MISSING PARAMETERS;")); else Serial.println(F("MISSING PARAMETERS"));
        }
      } else if (strcmp(strings[0], CMD_GET_INFO) == 0) {
        beginInfoOutput();
      } else if (strcmp(strings[0], CMD_HELP) == 0) {
        beginHelpOutput();
      } else {
        if (isComputerConnected) Serial.println(F("!ERROR:WRONG COMMAND;")); else Serial.println(F("WRONG COMMAND"));
      }
    } else {
      if (isComputerConnected) Serial.println(F("!ERROR:WRONG COMMAND;")); else Serial.println(F("WRONG COMMAND"));
    }
  }
}
