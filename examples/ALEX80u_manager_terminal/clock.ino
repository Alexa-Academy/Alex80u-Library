// Cooperative clock and reset sequence. No interrupts are used: each call
// can produce at most one edge and never catches up by compressing edges.

enum ClockPhase : uint8_t {
  CLOCK_PHASE_LOW,
  CLOCK_PHASE_HIGH,
};

enum ResetState : uint8_t {
  RESET_IDLE,
  RESET_WAIT_CLOCK_STOP,
  RESET_ASSERTED,
  RESET_PULSE_HIGH,
  RESET_PULSE_LOW,
};

const uint8_t RESET_CLOCK_PULSES = 16;

ClockPhase clockPhase = CLOCK_PHASE_LOW;
ResetState resetState = RESET_IDLE;
bool clockStopRequested = false;
bool resetRestoreClock = false;
bool resetReplyPending = false;
bool clockFrequencyPending = false;
uint16_t pendingFrequency = 50;
uint8_t resetPulseCount = 0;
uint32_t clockHalfPeriodUs = 10000UL;
uint32_t clockNextTransitionUs = 0;
uint32_t resetNextTransitionUs = 0;

static bool timeReached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

static uint32_t halfPeriodForFrequency(uint16_t frequency) {
  uint32_t halfPeriod = 500000UL / frequency;
  return halfPeriod == 0 ? 1 : halfPeriod;
}

static void applyClockFrequency(uint16_t frequency) {
  freq = frequency;
  clockHalfPeriodUs = halfPeriodForFrequency(frequency);
}

static void applyPendingClockFrequency() {
  if (clockFrequencyPending) {
    applyClockFrequency(pendingFrequency);
    clockFrequencyPending = false;
  }
}

bool isResetInProgress() {
  return resetState != RESET_IDLE;
}

void startClock() {
  if (isResetInProgress()) return;

  if (clockEnabled) {
    clockStopRequested = false;
    return;
  }

  applyPendingClockFrequency();
  a80u.set_CLK(LOW);
  clockPhase = CLOCK_PHASE_LOW;
  clockEnabled = true;
  clockStopRequested = false;
  clockNextTransitionUs = micros() + clockHalfPeriodUs;
}

void stopClock() {
  if (clockEnabled) clockStopRequested = true;
}

bool stopClockAndWait() {
  const bool wasEnabled = clockEnabled;
  stopClock();
  while (clockEnabled) {
    serviceClock();
  }
  return wasEnabled;
}

void setClockFrequency(unsigned int newFrequency) {
  const uint16_t frequency = static_cast<uint16_t>(newFrequency);
  freq = frequency;
  pendingFrequency = frequency;

  // Change the duration only while CLK is low, without shortening HIGH.
  if (!clockEnabled || clockPhase == CLOCK_PHASE_LOW) {
    applyClockFrequency(frequency);
    clockFrequencyPending = false;
  } else {
    clockFrequencyPending = true;
  }
}

void serviceClock() {
  // During RESET_WAIT_CLOCK_STOP the clock must complete the last high edge;
  // reset pulses in all other reset states are handled by serviceReset().
  if (!clockEnabled || (isResetInProgress() && resetState != RESET_WAIT_CLOCK_STOP)) return;

  // Stopping with CLK low completes the last high edge and holds a known level;
  // no pulse is truncated.
  if (clockStopRequested && clockPhase == CLOCK_PHASE_LOW) {
    clockEnabled = false;
    clockStopRequested = false;
    return;
  }

  const uint32_t now = micros();
  if (!timeReached(now, clockNextTransitionUs)) return;

  // Memory servicing precedes both edges. In particular, read data and WAIT
  // (in future phases) are stable before the Z80 sampling window at falling edge.
  serviceZ80Memory();

  if (clockPhase == CLOCK_PHASE_LOW) {
    applyPendingClockFrequency();
    a80u.set_CLK(HIGH);
    clockPhase = CLOCK_PHASE_HIGH;
  } else {
    a80u.set_CLK(LOW);
    clockPhase = CLOCK_PHASE_LOW;
    applyPendingClockFrequency();
  }

  // Restart from the current time instead of chasing a past deadline: a delay
  // lengthens the current phase and never creates closely spaced edges.
  clockNextTransitionUs = micros() + clockHalfPeriodUs;

  if (clockStopRequested && clockPhase == CLOCK_PHASE_LOW) {
    clockEnabled = false;
    clockStopRequested = false;
  }
}

bool beginReset(bool replyWhenComplete) {
  if (isResetInProgress()) return false;

  resetRestoreClock = clockEnabled;
  resetReplyPending = replyWhenComplete;
  resetState = clockEnabled ? RESET_WAIT_CLOCK_STOP : RESET_ASSERTED;
  if (clockEnabled) stopClock();
  return true;
}

void serviceReset() {
  if (resetState == RESET_IDLE) return;

  if (resetState == RESET_WAIT_CLOCK_STOP) {
    if (clockEnabled) return;
    resetState = RESET_ASSERTED;
  }

  const uint32_t now = micros();
  if (resetState == RESET_ASSERTED) {
    // Put reset and the data bus in a known state before the 16 pulses.
    a80u.set_CLK(LOW);
    a80u.set_RST(LOW);
    resetZ80MemoryService();
    resetPulseCount = 0;
    resetNextTransitionUs = now + clockHalfPeriodUs;
    resetState = RESET_PULSE_LOW;
    return;
  }

  if (!timeReached(now, resetNextTransitionUs)) return;

  if (resetState == RESET_PULSE_LOW) {
    a80u.set_CLK(HIGH);
    resetState = RESET_PULSE_HIGH;
  } else {  // RESET_PULSE_HIGH
    a80u.set_CLK(LOW);
    ++resetPulseCount;
    if (resetPulseCount == RESET_CLOCK_PULSES) {
      a80u.set_RST(HIGH);
      resetZ80MemoryService();
      resetState = RESET_IDLE;
      if (resetRestoreClock) startClock();
      if (resetReplyPending) replyResetComplete();
      resetReplyPending = false;
      return;
    }
    resetState = RESET_PULSE_LOW;
  }

  resetNextTransitionUs = micros() + clockHalfPeriodUs;
}
