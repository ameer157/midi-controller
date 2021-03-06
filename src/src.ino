/*
    MIDI Controller on Arduino (Pro) Micro 5V/16MHz

       This is a generic MIDI controller for DJs and music producers built on ATmega32u4 microcontroller.
       The controller has been tested on Native Instrument's Traktor Pro 2 and Ableton Live 10 and should work on other versions alike.

       It doesn't require any external software such as "The Hairless MIDI" since this is MIDI USB Class Compliant device.

      MIDI mapping:

       MIDI In can be assigned from the software to any knob, slide or button you map it to,
       but MIDI Out has to be configured in the readMIDI() method in the code.

      Traktor Pro 2:
       MIDI Channel = 4
       MIDI CC 80 = Filter On (Deck A)
       MIDI CC 81 = Monitor Cue On (Deck A)
       MIDI CC 84 = Deck Focus Selector (Deck A)
       MIDI CC 82 = Filter On (Deck B)
       MIDI CC 83 = Monitor Cue On (Deck B)
       MIDI CC 85 = Deck Focus Selector (Deck B)

     Mappings file: https://goo.gl/JWAb5h55pk

     Electronic components used:
       1. Arduino (Pro) Micro 5V/16MHz (ATmega32u4) (1x)
       2. Rotary encoder (1x)
       3. 10k Rotary potentiometer (8x)
       4. 10k Slide potentiometer (3x)
       5. Momentary pushbutton switch (6x)
       6. Standard (3501) Blue 3mm diffused LEDs (4x blue, 2x green)
       7. 16-Channel Analog/Digital CD74HC4067 Multiplexer (1x)

      Fritzing schematics file: https://goo.gl/JW5b5455pk

     USB device name change:

       If you want to change your MIDI controller device name go ahead and read this: http://liveelectronics.musinou.net/MIDIdeviceName.php

     Created 09/05/2018
     By Amir Off
     Modified 19/06/2018
     By Amir Off
*/

///////////////////////
/// Libraries Used ///
/////////////////////
// Hypereasy CD4067 multiplexer library
// https://github.com/sumotoy/Multiplexer4067
#include "Multiplexer4067.h"
// Arduino interrupt library, designed for Arduino Uno/Mega 2560/Leonardo/Due
// https://github.com/GreyGnome/EnableInterrupt
#include "EnableInterrupt.h"
// A MIDI library over USB, based on PluggableUSB
// https://github.com/arduino-libraries/MIDIUSB
#include "MIDIUSB.h"


///////////////////////
/// Encoders Setup ///
/////////////////////
const int NEncoders = 1; //*
const int encoderPins[NEncoders][2] = {{14, 16}}; // The amount of encoders and their corresponding pin numbers
int encoderCounter = 0;
int encoderState[NEncoders] = {0};
int encoderLastState[NEncoders] = {0};


//////////////////////
/// Buttons Setup ///
////////////////////
const int NButtons = 1; //*
const int buttonPin[NButtons] = {6}; //* The amount of pushbuttons and their corresponding pin numbers
int buttonLastState[NButtons];
int buttonState[NButtons] = {HIGH}; // pull-up resistor's button initial state is HIGH (normally open when not pressed)


////////////////////
/// Multiplexer ///
//////////////////
Multiplexer4067 muxPots = Multiplexer4067(2, 3, 4, 5, A0);


///////////////////
/// LEDs Setup ///
/////////////////
const int NLEDs = 2; //*
const int LedPin[NLEDs] = {8, 9}; //* The amount of LEDs and their corresponding pin numbers
int ledLastState[NLEDs];
int ledState[NLEDs] = {LOW};


/////////////////////////////
/// Potentiometers Setup ///
///////////////////////////
const byte NPots = 2; //*
const byte NMuxPots = 2; //**
const byte totalPots = NPots + NMuxPots;

const byte potPin[NPots] = {A2, A3}; //* The amount of potentiometer and their pin numbers
const byte muxPotPin[NMuxPots] = {0, 1}; //** The amount of multiplexer potentiometer and their pin numbers

int potState[totalPots] = {0};
int potLastState[totalPots] = {0};
int potVar = 0; // Difference between the current and previous state of the potentiometer
int midiCState[totalPots] = {0}; // Current MIDI value of the potentiometer
int midiPState[totalPots] = {0}; // Previous MIDI value of the potentiometer
int TIMEOUT = 300; //* Amount of time the potentiometer will be read after it exceeds the varThreshold
int varThreshold = 6; //* Threshold for the potentiometer signal variation
boolean isPotMoving = true; // If the potentiometer is moving
unsigned long PTime[totalPots] = {0}; // Previously stored time
unsigned long timer[totalPots] = {0}; // Stores the time that has elapsed since the timer was reset


//////////////////////////////////////
/// Buttons Debounce Funcionality ///
////////////////////////////////////
unsigned long lastDebounceTime[NButtons] = {0};  // The last time the button was toggled
unsigned long debounceDelay = 5;    //* The debounce time; increases if the output flickers


//////////////////////
/// MIDI Settings ///
////////////////////
byte midiChannel = 0; //* MIDI channel to be used (0 -  15)
byte midiNote = 36; //* Lowest MIDI note to be used
byte midiCC = 0; //* Lowest MIDI CC to be used (0 -  119)


void setup() {

  Serial.begin(115200);

  // Initialize multiplexers
  muxPots.begin();

  // Initialize encoders
  for (int i = 0; i < NEncoders; i++) {
    enableInterrupt(encoderPins[i][0], encoder, CHANGE);
    enableInterrupt(encoderPins[i][1], encoder, CHANGE);
    pinMode(encoderPins[i][0], INPUT_PULLUP);
    pinMode(encoderPins[i][1], INPUT_PULLUP);
    // Reads the initial state of the encoder's output A pin
    encoderLastState[i] = digitalRead(encoderPins[i][0]);
  }

  // Initialize buttons
  for (int i = 0; i < NButtons; i++) {
    // Check for latching switch button pin (index of NButtons) to set an external pull-down resistor
    switch (i) {
      case -1:
        pinMode(buttonPin[i], INPUT); // Normally closed switch (pull-down)
        break;
      default:
        pinMode(buttonPin[i], INPUT_PULLUP); // Normally open switch
        break;
    }
  }

  // Initialize LEDs
  for (int i = 0; i < NLEDs; i++) {
    pinMode(LedPin[i], OUTPUT);
  }

}


void loop() {
  potentiometers();
  buttons();
  leds();
  readMIDI();
}


//////////////////////////
/// MIDIUSB Functions ///
////////////////////////

// First parameter is the event type (0x09 = note on, 0x08 = note off).
// Second parameter is note-on/note-off, combined with the channel.
// Channel can be anything between 0-15. Typically reported to the user as 1-16.
// Third parameter is the note number (48 = middle C).
// Fourth parameter is the velocity (64 = normal, 127 = fastest).

void noteOn(byte channel, byte pitch, byte velocity) {
  midiEventPacket_t noteOn = {0x09, 0x90 | channel, pitch, velocity};
  MidiUSB.sendMIDI(noteOn);
}

void noteOff(byte channel, byte pitch, byte velocity) {
  midiEventPacket_t noteOff = {0x08, 0x80 | channel, pitch, velocity};
  MidiUSB.sendMIDI(noteOff);
}


// First parameter is the event type (0x0B = control change).
// Second parameter is the event type, combined with the channel.
// Third parameter is the control number number (0-119).
// Fourth parameter is the control value (0-127).

void controlChange(byte channel, byte control, byte value) {
  midiEventPacket_t event = {0x0B, 0xB0 | channel, control, value};
  MidiUSB.sendMIDI(event);
}

void readMIDI() {
  midiEventPacket_t rx;
  do {
    rx = MidiUSB.read();
    if (rx.header != 0) {
      Serial.print("Received: ");
      Serial.print(rx.header, HEX); // MIDI message type (B = Control change)
      Serial.print("-");
      Serial.print(rx.byte1, HEX); // MIDI message type combined with channel (B0 - B15)
      Serial.print("-");
      Serial.print(rx.byte2, DEC); // MIDI CC number (0 - 119)
      Serial.print("-");
      Serial.println(rx.byte3, DEC); // MIDI CC value (0 - 127)

      // Check MIDI Channel (Decimal  value)
      if (rx.byte1 == 179) { // 179 Dec to Hex Value = "B3" Channel 4 in MIDI (LEDs signal feedback channel)

        // Check MIDI CC number
        switch (rx.byte2) {
          case 80:
            // Check MIDI CC value to determine whether to switch the corresponding LED on/off (index of NLEDs)
            if (rx.byte3 == 0) { // 0 (min MIDI range in Traktor)
              digitalWrite(LedPin[0], LOW);
            } else { // 127 (Max MIDI range in Traktor)
              digitalWrite(LedPin[0], HIGH);
            }
            break;
          case 82:
            if (rx.byte3 == 0) {
              digitalWrite(LedPin[2], HIGH);
            } else { // 127 (max MIDI range in Traktor)
              digitalWrite(LedPin[2], LOW);
            }
            break;
          case 83:
            if (rx.byte3 == 0) {
              digitalWrite(LedPin[1], HIGH);
            } else { // 127 (max MIDI range in Traktor)
              digitalWrite(LedPin[1], LOW);
            }
            break;
        }
      }
    }
  } while (rx.header != 0);
}


//////////////////////////
/// Encoders Function ///
////////////////////////
void encoder() {
  for (int i = 0; i < NEncoders; i++) {

    // Reads the current state of the encoder's output A pin
    encoderState[i] = digitalRead(encoderPins[i][0]);

    // If the previous state and the current state of the output A pin are different, it means that a pulse occurred
    if (encoderState[i] != encoderLastState[i]) {
      // If the state of the encoder's output B pin is different from the output A pin it means that the Encoder is rotating clockwise
      if (digitalRead(encoderPins[i][1]) != encoderState[i]) {
        encoderCounter = 127;
      } else {
        encoderCounter = 1;
      }
      controlChange(midiChannel, midiCC + i, encoderCounter);
      MidiUSB.flush();
    }
    encoderLastState[i] = encoderState[i]; // Stores the actual value in the previous value
  }
}


/////////////////////////
/// Buttons Function ///
///////////////////////
void buttons() {
  for (int i = 0; i < NButtons; i++) {

    // Reads the current state of the button
    buttonState[i] = digitalRead(buttonPin[i]);
    if ((millis() - lastDebounceTime[i]) > debounceDelay) {

      // Function loops only if the button state is LOW (button pressed)
      if (buttonLastState[i] != buttonState[i]) {

        lastDebounceTime[i] = millis();

        if (buttonState[i] == HIGH) {
          controlChange(midiChannel + 1, midiCC + i, 127);
          MidiUSB.flush();

          // Check for button pin (index of NButtons) to turn on / off its corresponding LED pin (index of NLEDs)
          switch (i) {
            case 0:
              ledState[0] = !ledLastState[0];
              digitalWrite(LedPin[0], ledState[0]);
              break;
            case 2:
              ledState[1] = !ledLastState[1];
              digitalWrite(LedPin[1], ledState[1]);
              break;
            case 3:
              ledState[2] = !ledLastState[2];
              digitalWrite(LedPin[2], ledState[2]);
              break;
          }
        }
        else {
          controlChange(midiChannel + 1, midiCC + i, 0);
          MidiUSB.flush();
        }
      }
    }
    buttonLastState[i] = buttonState[i];
  }
}


//////////////////////
/// LEDs Function ///
////////////////////
void leds() {
  for (int i = 0; i < NLEDs; i++) {

    // Reads the current state of the LED
    ledState[i] = digitalRead(LedPin[i]);
    ledLastState[i] = ledState[i];
  }
}


////////////////////////////////
/// Potentiometers Function ///
//////////////////////////////
void potentiometers() {
  for (int i = 0; i < NMuxPots; i++) {
    // Reads the state of multiplexer potentiometers
    potState[i] = muxPots.readChannel(muxPotPin[i]);
  }

  for (int i = 0; i < NPots; i++) {
    // Reads  the state of the Arduino potentiometers then store it in the potState variable after extending its index number
    potState[i + NMuxPots] = analogRead(potPin[i]);
  }

  for (int i = 0; i < totalPots; i++) {
    midiCState[i] = map(potState[i], 0, 1023, 0, 127); // Maps the reading of the potState to a value usable in midi
    potVar = abs(potState[i] - potLastState[i]); // Calculates the absolute value between the difference between the current and previous state of the pot

    if (potVar > varThreshold) { // Opens the gate if the potentiometer variation is greater than the threshold
      PTime[i] = millis(); // Stores the previous time
    }

    timer[i] = millis() - PTime[i]; // Resets the timer 11000 - 11000 = 0ms

    if (timer[i] < TIMEOUT) { // If the timer is less than the maximum allowed time it means that the potentiometer is still moving
      isPotMoving = true;
    }
    else {
      isPotMoving = false;
    }

    if (isPotMoving == true) { // If the potentiometer is still moving, send the change control
      if (midiPState[i] != midiCState[i]) {
        controlChange(midiChannel + 2, midiCC + i, midiCState[i]);
        MidiUSB.flush();

        potLastState[i] = potState[i]; // Stores the current reading of the potentiometer to compare with the next
        midiPState[i] = midiCState[i];
      }
    }

  }
}

