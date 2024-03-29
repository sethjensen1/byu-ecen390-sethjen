#include "transmitter.h"
#include "buttons.h"
#include "filter.h"
#include "mio.h"
#include "switches.h"
#include "utils.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define TRANSMITTER_TEST_BUTTON

#define TRANSMITTER_HIGH_VALUE 1
#define TRANSMITTER_LOW_VALUE 0

#define TRANSMITTER_TEST_TICK_PERIOD_IN_MS 10
#define BOUNCE_DELAY 5

#define TRANSMITTER_NONCONTINUOUS_DELAY_MS 500

// The transmitter state machine generates a square wave output at the chosen
// frequency as set by transmitter_setFrequencyNumber(). The step counts for the
// frequencies are provided in filter.h

static enum transmitter_st_t {
  init_st,
  wait_st,
  sig_high_st,
  sig_low_st,
} currentState = init_st;

static uint16_t signalTimer = 0;
static bool continuousModeOn = false;
volatile static bool running = false;

static uint32_t pulseWidth = TRANSMITTER_PULSE_WIDTH;
static bool debugOn = false;

static uint8_t currentFrequency = 0;
static uint16_t period = 0;

void transmitter_setDebug(bool on) {
  debugOn = on;
}

void transmitter_setPulseWidth(uint32_t width) {
  pulseWidth = width;
}

void transmitter_setContinuousModeOn(bool on) {
  continuousModeOn = on;
}

// Standard init function.
void transmitter_init() {
  currentState = init_st;

  signalTimer = 0;
  running = false;

  period = filter_frequencyTickTable[currentFrequency];

  mio_init(debugOn);
  mio_setPinAsOutput(TRANSMITTER_OUTPUT_PIN);
}

// Standard tick function.
void transmitter_tick() {
  // Transition logic
  switch (currentState) {
  case init_st:
    currentState = wait_st;
    break;
  case wait_st:
    // if the run next flag is true, then move on to sending our signal
    if (running) {
      if (!continuousModeOn)
        running = false; // only run once, unluss continuous mode is on
      mio_writePin(TRANSMITTER_OUTPUT_PIN, TRANSMITTER_HIGH_VALUE);
      period = filter_frequencyTickTable[currentFrequency]; // get the most
                                                            // recent tick count
      signalTimer = 0;
      currentState = sig_high_st;
    }
    break;
  case sig_high_st:
    // For the first half of the period, stay in the high state. Otherwise move
    // to low.
    if ((signalTimer % period) > (period / 2)) {
      currentState = sig_low_st;
      mio_writePin(TRANSMITTER_OUTPUT_PIN, TRANSMITTER_LOW_VALUE);
    }
    break;
  case sig_low_st:
    // If the timer has sent a full transmit pulse, go back to the wait state.
    // If continuous mode is on, then the wait state will handle resetting the
    // period with only a small delay.
    if (signalTimer > pulseWidth) {
      currentState = wait_st;
      // For the second half of the period, stay in the low state. Otherwise
      // move to high.
    } else if ((signalTimer % period) <
               (period / 2)) { // Since there is a modulo, I'm fine with using a
                               // < operator even for a timer, since we still
                               // won't end up in an infinite loop if the timer
                               // somehow skips over the change condition
      currentState = sig_high_st;
      mio_writePin(TRANSMITTER_OUTPUT_PIN, TRANSMITTER_HIGH_VALUE);
    }
    break;
  }

  // Action logic
  switch (currentState) {
  case init_st:
    break;
  case wait_st:
    break;
  case sig_high_st:
    signalTimer++;
    break;
  case sig_low_st:
    signalTimer++;
    break;
  }
}

// Activate the transmitter.
void transmitter_run() { running = true; }

// Returns true if the transmitter is still running.
bool transmitter_running() {
  return running;
}

// Sets the frequency number. If this function is called while the
// transmitter is running, the frequency will not be updated until the
// transmitter stops and transmitter_run() is called again.
void transmitter_setFrequencyNumber(uint16_t frequencyNumber) {
  currentFrequency = frequencyNumber;
}

// Returns the current frequency setting.
uint16_t transmitter_getFrequencyNumber() { return currentFrequency; }

// Runs the transmitter continuously.
// if continuousModeFlag == true, transmitter runs continuously, otherwise, it
// transmits one burst and stops. To set continuous mode, you must invoke
// this function prior to calling transmitter_run(). If the transmitter is
// currently in continuous mode, it will stop running if this function is
// invoked with continuousModeFlag == false. It can stop immediately or wait
// until a 200 ms burst is complete. NOTE: while running continuously,
// the transmitter will only change frequencies in between 200 ms bursts.
void transmitter_setContinuousMode(bool continuousModeFlag) {
  continuousModeOn = continuousModeFlag;
}

/******************************************************************************
***** Test Functions
******************************************************************************/

// Prints out the clock waveform to stdio. Terminates when BTN3 is pressed.
// Does not use interrupts, but calls the tick function in a loop.
void transmitter_runTest() {
  printf("Running transmitter_runTest()\n");

  transmitter_setDebug(true);
  transmitter_setPulseWidth(200); // shorten pulse width for readability

  transmitter_init();

  // Finish loop when button 3 is pressed
  while (!(buttons_read() & BUTTONS_BTN3_MASK)) {
    uint16_t switchValue = switches_read() % FILTER_FREQUENCY_COUNT; // Compute a safe number from the switches.

    transmitter_setFrequencyNumber(switchValue); // set the frequency number based upon switch value.
    transmitter_run();                           // Start the transmitter.
    while (transmitter_running()) {
      transmitter_tick();                                // tick.
      utils_msDelay(TRANSMITTER_TEST_TICK_PERIOD_IN_MS); // short delay between ticks.
    }
  }

  // don't continue until all buttons are lifted
  do {
    utils_msDelay(BOUNCE_DELAY);
  } while (buttons_read());

  transmitter_setDebug(false);
  transmitter_setPulseWidth(TRANSMITTER_PULSE_WIDTH);

  printf("Exiting transmitter_runTest()\n");
}

// Tests the transmitter in non-continuous mode.
// The test runs until BTN3 is pressed.
// To perform the test, connect the oscilloscope probe
// to the transmitter and ground probes on the development board
// prior to running this test. You should see about a 300 ms dead
// spot between 200 ms pulses.
// Should change frequency in response to the slide switches.
// Depends on the interrupt handler to call tick function.
void transmitter_runTestNoncontinuous() {
  printf("Running transmitter_runTestNoncontinuous\n");

  transmitter_setDebug(true);
  transmitter_setContinuousModeOn(false);

  transmitter_init();

  // Finish loop when button 3 is pressed
  while (!(buttons_read() & BUTTONS_BTN3_MASK)) {
    utils_msDelay(TRANSMITTER_NONCONTINUOUS_DELAY_MS); // short delay between reading buttons, sending new run command
    uint16_t switchValue = switches_read() % FILTER_FREQUENCY_COUNT; // Compute a safe number from the switches.

    transmitter_setFrequencyNumber(switchValue); // set the frequency number based upon switch value.

    transmitter_run();                           // Start the transmitter.
    while (transmitter_running());
  }

  // don't continue until all buttons are lifted
  do {
    utils_msDelay(BOUNCE_DELAY);
  } while (buttons_read());

  transmitter_setDebug(false);

  printf("Exiting transmitter_runTestNoncontinuous()\n");
}

// Tests the transmitter in continuous mode.
// To perform the test, connect the oscilloscope probe
// to the transmitter and ground probes on the development board
// prior to running this test.
// Transmitter should continuously generate the proper waveform
// at the transmitter-probe pin and change frequencies
// in response to changes in the slide switches.
// Test runs until BTN3 is pressed.
// Depends on the interrupt handler to call tick function.
void transmitter_runTestContinuous() {
  printf("Running transmitter_runTestContinuous\n");

  transmitter_setDebug(true);
  transmitter_setContinuousModeOn(true);

  transmitter_init();

  transmitter_run(); // Give an initial run command

  // Finish loop when button 3 is pressed
  while (!(buttons_read() & BUTTONS_BTN3_MASK)) {
    uint16_t switchValue = switches_read() % FILTER_FREQUENCY_COUNT; // Compute a safe number from the switches.

    transmitter_setFrequencyNumber(switchValue); // set the frequency number based upon switch value.
  }

  // don't continue until all buttons are lifted
  do {
    utils_msDelay(BOUNCE_DELAY);
  } while (buttons_read());

  transmitter_setContinuousModeOn(false);
  transmitter_setDebug(false);

  printf("Exiting transmitter_runTestContinuous()\n");
}