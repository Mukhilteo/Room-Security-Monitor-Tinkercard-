// SIT315 M1.T1 - Interrupt-driven room security monitor (Sense-Think-Act)
//
// SENSE : PIR motion sensor (external interrupt), arm switch (polled),
//         door + window contacts (pin change interrupts, same port group)
// THINK : motion only raises the alarm if the system is armed;
//         door/window are grouped into one "zone open" state
// ACT   : alarm LED, zone LED, heartbeat LED, Serial log
//
// Every ISR only sets a flag or copies a value. All decisions, LED writes
// and Serial output happen in loop(), so no ISR ever blocks.

// ---------- Pin map ----------
const uint8_t PIN_MOTION    = 2;   // PIR signal  -> INT0 (attachInterrupt)
const uint8_t PIN_ARM       = 4;   // slide switch, LOW = armed (polled)
const uint8_t PIN_DOOR      = 8;   // pushbutton  -> PCINT0 (PORTB group)
const uint8_t PIN_WINDOW    = 9;   // pushbutton  -> PCINT1 (PORTB group)

const uint8_t PIN_LED_ALARM = 5;   // red:    motion detected while armed
const uint8_t PIN_LED_ZONE  = 6;   // yellow: door OR window open
const uint8_t PIN_LED_BEAT  = 7;   // green:  1 Hz heartbeat from Timer1

const uint8_t DOOR_MASK   = (1 << PCINT0);
const uint8_t WINDOW_MASK = (1 << PCINT1);
const uint8_t STATUS_EVERY_N_TICKS = 5;   // status report period (seconds)

// ---------- Shared between ISRs and loop() (must be volatile) ----------
volatile bool    motionFlag   = false;  // set by INT0 ISR
volatile bool    zoneFlag     = false;  // set by PCINT0 ISR
volatile uint8_t zoneSnapshot = 0;      // PINB captured inside the ISR
volatile uint8_t tickTotal    = 0;      // counted by Timer1 ISR (wraps at 255)

// ---------- State owned by loop() only ----------
bool armed       = false;
bool alarmActive = false;
bool doorOpen    = false;
bool windowOpen  = false;
bool beatState   = false;
uint8_t  handledTicks = 0;              // how many timer ticks loop() has processed
uint8_t  tickCount   = 0;
uint16_t motionCount = 0;

// ---------- Prototypes ----------
void setupPinChangeInterrupts();
void setupTimer1();
void isrMotion();
void handleArmSwitch();
void handleMotion();
void handleZones();
void handleTimer();
void updateOutputs();
void printStatus();

void setup()
{
  Serial.begin(9600);

  pinMode(PIN_MOTION, INPUT);          // PIR drives its own output
  pinMode(PIN_ARM,    INPUT_PULLUP);   // LOW when switched to GND
  pinMode(PIN_DOOR,   INPUT_PULLUP);   // LOW while pressed (= open)
  pinMode(PIN_WINDOW, INPUT_PULLUP);

  pinMode(PIN_LED_ALARM, OUTPUT);
  pinMode(PIN_LED_ZONE,  OUTPUT);
  pinMode(PIN_LED_BEAT,  OUTPUT);

  armed = (digitalRead(PIN_ARM) == LOW);

  // External interrupt: PIR output goes HIGH when motion starts
  attachInterrupt(digitalPinToInterrupt(PIN_MOTION), isrMotion, RISING);

  setupPinChangeInterrupts();
  setupTimer1();

  Serial.println(F("[BOOT] Security monitor ready."));
  printStatus();
}

// PCI works per port group. D8 and D9 are both on PORTB, which is
// group 0, so they share the single vector PCINT0_vect.
void setupPinChangeInterrupts()
{
  PCICR  |= (1 << PCIE0);               // enable the PORTB group
  PCMSK0 |= DOOR_MASK | WINDOW_MASK;    // only D8 and D9 may trigger it
}

// Timer1 in CTC mode: counts to OCR1A, fires the ISR, resets to 0.
// 16 MHz / 1024 prescaler = 15625 counts per second -> OCR1A = 15624 = 1 Hz
void setupTimer1()
{
  noInterrupts();
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1  = 0;
  OCR1A  = 15624;
  TCCR1B |= (1 << WGM12);               // CTC mode
  TCCR1B |= (1 << CS12) | (1 << CS10);  // prescaler 1024
  TIMSK1 |= (1 << OCIE1A);              // enable compare-match A interrupt
  interrupts();
}

// ---------- ISRs: short, no delay(), no Serial ----------

// External interrupt on D2 (event-based)
void isrMotion()
{
  motionFlag = true;
}

// One vector for both D8 and D9. We capture the whole port here so
// loop() can work out which pin(s) changed from the snapshot.
ISR(PCINT0_vect)
{
  zoneSnapshot = PINB;
  zoneFlag = true;
}

// Timer1 compare match, once per second (time-based).
// A counter is used instead of a bool so ticks are not lost if loop()
// is briefly busy printing.
ISR(TIMER1_COMPA_vect)
{
  tickTotal++;
}

// ---------- Main loop: think + act ----------
void loop()
{
  handleArmSwitch();
  handleMotion();
  handleZones();
  handleTimer();
  updateOutputs();
}

// Polled sensor: detect changes of the arm switch
void handleArmSwitch()
{
  bool nowArmed = (digitalRead(PIN_ARM) == LOW);
  if (nowArmed == armed) return;

  armed = nowArmed;
  Serial.print(F("[ARM] System "));
  Serial.println(armed ? F("ARMED") : F("DISARMED"));

  // The motion interrupt only fires on a RISING edge. If the PIR output
  // is already HIGH when the system is armed there will be no new edge,
  // so check the current level once here.
  if (armed && digitalRead(PIN_MOTION) == HIGH)
  {
    alarmActive = true;
    Serial.println(F("  -> Motion already present when armed: ALARM ON."));
  }

  if (!armed && alarmActive)
  {
    alarmActive = false;                // disarming clears the alarm
    Serial.println(F("  -> Alarm cleared."));
  }
}

// Conditional logic: motion AND armed -> alarm
void handleMotion()
{
  if (!motionFlag) return;
  motionFlag = false;
  motionCount++;

  Serial.print(F("[INT0] Motion detected. System is "));
  Serial.println(armed ? F("ARMED") : F("disarmed"));

  if (armed)
  {
    alarmActive = true;
    Serial.println(F("  -> Motion AND armed: ALARM ON."));
  }
  else
  {
    Serial.println(F("  -> Ignored, system not armed."));
  }
}

// Grouped logic for the two PCI pins
void handleZones()
{
  if (!zoneFlag) return;

  // Copy the shared data with interrupts off so the ISR cannot change
  // it halfway through
  noInterrupts();
  uint8_t snap = zoneSnapshot;
  zoneFlag = false;
  interrupts();

  bool doorNow   = !(snap & DOOR_MASK);     // LOW = pressed = open
  bool windowNow = !(snap & WINDOW_MASK);

  // Comparing against the stored state tells us which pin changed and
  // filters out repeat interrupts (e.g. contact bounce) with no real change
  if (doorNow != doorOpen)
  {
    doorOpen = doorNow;
    Serial.print(F("[PCI] Door (D8) -> "));
    Serial.println(doorOpen ? F("OPEN") : F("closed"));
  }
  if (windowNow != windowOpen)
  {
    windowOpen = windowNow;
    Serial.print(F("[PCI] Window (D9) -> "));
    Serial.println(windowOpen ? F("OPEN") : F("closed"));
  }
}

// Periodic task: heartbeat every second, status report every 5 seconds
void handleTimer()
{
  // The ISR only ever increments tickTotal and loop() only reads it.
  // It is a single byte, so the read is atomic on the Uno and no
  // interrupt disabling is needed. loop() catches up one tick at a time.
  uint8_t total = tickTotal;

  while (handledTicks != total)
  {
    handledTicks++;
    beatState = !beatState;
    tickCount++;
    if (tickCount >= STATUS_EVERY_N_TICKS)
    {
      tickCount = 0;
      printStatus();
    }
  }
}

// Actuation in one place
void updateOutputs()
{
  digitalWrite(PIN_LED_ALARM, alarmActive ? HIGH : LOW);
  digitalWrite(PIN_LED_ZONE,  (doorOpen || windowOpen) ? HIGH : LOW);
  digitalWrite(PIN_LED_BEAT,  beatState ? HIGH : LOW);
}

void printStatus()
{
  Serial.print(F("[TIMER] Status | armed="));
  Serial.print(armed ? F("yes") : F("no"));
  Serial.print(F(" alarm="));
  Serial.print(alarmActive ? F("ON") : F("off"));
  Serial.print(F(" door="));
  Serial.print(doorOpen ? F("open") : F("closed"));
  Serial.print(F(" window="));
  Serial.print(windowOpen ? F("open") : F("closed"));
  Serial.print(F(" motionEvents="));
  Serial.println(motionCount);
}
