/*

ESP8266SoftwareSerial.cpp - Implementation of the Arduino software serial for ESP8266.
Copyright (c) 2015-2016 Peter Lerup. All rights reserved.

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

*/
#if defined(ESP8266)
#include <Arduino.h>

// The Arduino standard GPIO routines are not enough,
// must use some from the Espressif SDK as well
extern "C" {
#include "gpio.h"
}

#include "ESP8266SoftwareSerial.h"

#define MAX_PIN 15

// As the Arduino attachInterrupt has no parameter, lists of objects
// and callbacks corresponding to each possible GPIO pins have to be defined
ESP8266SoftwareSerial *ObjList[MAX_PIN + 1];

void IRAM_ATTR sws_isr_0() { ObjList[0]->rxRead(); };
void IRAM_ATTR sws_isr_1() { ObjList[1]->rxRead(); };
void IRAM_ATTR sws_isr_2() { ObjList[2]->rxRead(); };
void IRAM_ATTR sws_isr_3() { ObjList[3]->rxRead(); };
void IRAM_ATTR sws_isr_4() { ObjList[4]->rxRead(); };
void IRAM_ATTR sws_isr_5() { ObjList[5]->rxRead(); };
// Pin 6 to 11 can not be used
void IRAM_ATTR sws_isr_12() { ObjList[12]->rxRead(); };
void IRAM_ATTR sws_isr_13() { ObjList[13]->rxRead(); };
void IRAM_ATTR sws_isr_14() { ObjList[14]->rxRead(); };
void IRAM_ATTR sws_isr_15() { ObjList[15]->rxRead(); };

static void(*ISRList[MAX_PIN + 1])() = {
	  sws_isr_0,
	  sws_isr_1,
	  sws_isr_2,
	  sws_isr_3,
	  sws_isr_4,
	  sws_isr_5,
	  NULL,
	  NULL,
	  NULL,
	  NULL,
	  NULL,
	  NULL,
	  sws_isr_12,
	  sws_isr_13,
	  sws_isr_14,
	  sws_isr_15
};

ESP8266SoftwareSerial::ESP8266SoftwareSerial(int receivePin, int transmitPin, bool inverse_logic, unsigned int buffSize, bool edge_triggered) {
	m_oneWire = (receivePin == transmitPin);
	m_rxValid = m_txValid = m_txEnableValid = false;
	m_buffer = NULL;
	m_invert = inverse_logic;
	m_edge = edge_triggered;
	m_overflow = false;
	m_rxEnabled = false;
	if (isValidGPIOpin(receivePin)) {
		m_rxPin = receivePin;
		m_buffSize = buffSize;
		m_buffer = (uint8_t*)malloc(m_buffSize);
		if (m_buffer != NULL) {
			m_rxValid = true;
			m_inPos = m_outPos = 0;
			pinMode(m_rxPin, INPUT);
			ObjList[m_rxPin] = this;
		}
	}
	if (isValidGPIOpin(transmitPin) || (!m_oneWire && (transmitPin == 16))) {
		m_txValid = true;
		m_txPin = transmitPin;
		if (!m_oneWire) {
			pinMode(m_txPin, OUTPUT);
			digitalWrite(m_txPin, !m_invert);
		}
	}
	// Default speed
	begin(9600);
}

ESP8266SoftwareSerial::~ESP8266SoftwareSerial() {
	enableRx(false);
	if (m_rxValid)
		ObjList[m_rxPin] = NULL;
	if (m_buffer)
		free(m_buffer);
}

bool ESP8266SoftwareSerial::isValidGPIOpin(int pin) {
	return (pin >= 0 && pin <= 5) || (pin >= 12 && pin <= MAX_PIN);
}

void ESP8266SoftwareSerial::begin(long speed) {
	// Use getCycleCount() loop to get as exact timing as possible
	m_bitTime = F_CPU / speed;
	// By default enable interrupt during tx only for low speed
	m_intTxEnabled = speed < 9600;

	if (!m_rxEnabled)
		enableRx(true);
}

long ESP8266SoftwareSerial::baudRate() {
	return F_CPU / m_bitTime;
}

void ESP8266SoftwareSerial::setTransmitEnablePin(int transmitEnablePin) {
	if (isValidGPIOpin(transmitEnablePin)) {
		m_txEnableValid = true;
		m_txEnablePin = transmitEnablePin;
		pinMode(m_txEnablePin, OUTPUT);
		digitalWrite(m_txEnablePin, LOW);
	}
	else {
		m_txEnableValid = false;
	}
}

void ESP8266SoftwareSerial::enableIntTx(bool on) {
	m_intTxEnabled = on;
}

void ESP8266SoftwareSerial::enableTx(bool on) {
	if (m_oneWire && m_txValid) {
		if (on) {
			enableRx(false);
			digitalWrite(m_txPin, !m_invert);
			pinMode(m_rxPin, OUTPUT);
		}
		else {
			digitalWrite(m_txPin, !m_invert);
			pinMode(m_rxPin, INPUT);
			enableRx(true);
		}
		delay(1); // it's important to have a delay after switching
	}
}

void ESP8266SoftwareSerial::enableRx(bool on) {
	if (m_rxValid) {
		if (on) {
			if (m_edge)
				attachInterrupt(m_rxPin, ISRList[m_rxPin], CHANGE); // fire at rising and falling edges
			else
				attachInterrupt(m_rxPin, ISRList[m_rxPin], m_invert ? RISING : FALLING);
		}
		else
			detachInterrupt(m_rxPin);
		m_rxEnabled = on;
	}
}

int ESP8266SoftwareSerial::read() {
	if (!m_rxValid || (m_inPos == m_outPos)) return -1;
	uint8_t ch = m_buffer[m_outPos];
	m_outPos = (m_outPos + 1) % m_buffSize;
	return ch;
}

int ESP8266SoftwareSerial::available() {
	if (!m_rxValid) return 0;
	int avail = m_inPos - m_outPos;
	if (avail < 0) avail += m_buffSize;
	return avail;
}

#define WAIT { while (ESP.getCycleCount()-start < wait) if (m_intTxEnabled) optimistic_yield(1); wait += m_bitTime; }

size_t ESP8266SoftwareSerial::write(uint8_t b) {
	if (!m_txValid) return 0;

	if (m_invert) b = ~b;
	if (!m_intTxEnabled)
		// Disable interrupts in order to get a clean transmit
		cli();
	if (m_txEnableValid) digitalWrite(m_txEnablePin, HIGH);
	unsigned long wait = m_bitTime;
	digitalWrite(m_txPin, HIGH);
	unsigned long start = ESP.getCycleCount();
	// Start bit;
	digitalWrite(m_txPin, LOW);
	WAIT;
	for (int i = 0; i < 8; i++) {
		digitalWrite(m_txPin, (b & 1) ? HIGH : LOW);
		WAIT;
		b >>= 1;
	}
	// Stop bit
	digitalWrite(m_txPin, HIGH);
	WAIT;
	if (m_txEnableValid) digitalWrite(m_txEnablePin, LOW);
	if (!m_intTxEnabled)
		sei();
	return 1;
}

void ESP8266SoftwareSerial::flush() {
	m_inPos = m_outPos = 0;
}

bool ESP8266SoftwareSerial::overflow() {
	bool res = m_overflow;
	m_overflow = false;
	return res;
}

int ESP8266SoftwareSerial::peek() {
	if (!m_rxValid || (m_inPos == m_outPos)) return -1;
	return m_buffer[m_outPos];
}

inline bool ESP8266SoftwareSerial::propgateBits(bool level, int pulseBitLength)
{
	for (int i = 0; i < pulseBitLength; i++)
	{
		m_rec >>= 1;
		if (level)
			m_rec |= 0x80;
		m_bitNo++;
		if (m_bitNo == 8)
		{
			return true;
		}
	}

	return false;
}

inline void ESP8266SoftwareSerial::setWaitingForStart()
{
	m_getByteState = awaitingStart;
}

inline void ESP8266SoftwareSerial::setStartBit(unsigned long start)
{
	// mark - the start of a pulse
	// set the timers and wait for the next pulse
	m_byteStart = start;
	m_pulseStart = start;
	m_rec = 0;
	m_bitNo = 0;
	m_getByteState = gotStart;
}

void ICACHE_RAM_ATTR ESP8266SoftwareSerial::rxRead() {
	// Advance the starting point for the samples but compensate for the
	// initial delay which occurs before the interrupt is delivered
	unsigned long wait = m_bitTime + m_bitTime / 3 - 500;
	unsigned long start = ESP.getCycleCount();
	if (m_edge) {

		// Get the rxLevel and convert for inverted input
		bool rxLevel;

		if (m_invert)
			rxLevel = !digitalRead(m_rxPin);
		else
			rxLevel = digitalRead(m_rxPin);

		unsigned long byteTime;
		float byteBitLengthFloat;
		int byteBitLength;

		unsigned long pulseTime;
		float pulseBitLengthFloat;
		int pulseBitLength;

		bool previousBit = !rxLevel;

		switch (m_getByteState)
		{

		case awaitingStart:
			if (!rxLevel)
			{
				setStartBit(start);
			}
			break;

		case gotStart:

			byteTime = start - m_byteStart;
			byteBitLengthFloat = (float)byteTime / m_bitTime;
			byteBitLength = (int)(byteBitLengthFloat + 0.5);

			pulseTime = start - m_pulseStart;
			pulseBitLengthFloat = (float)pulseTime / m_bitTime;
			pulseBitLength = (int)(pulseBitLengthFloat + 0.5);

			// don't want to add the start bit into the value
			if (propgateBits(previousBit, pulseBitLength - 1))
			{
				// store byte in buffer
				unsigned int next = (m_inPos + 1) % m_buffSize;
				if (next != m_outPos) {
					m_buffer[m_inPos] = m_rec;
					m_inPos = next;
				}
				else {
					m_overflow = true;
				}
				setWaitingForStart();
				return;
			}
			m_pulseStart = start;
			m_getByteState = readingBits;
			break;

		case readingBits:

			byteTime = start - m_byteStart;
			byteBitLengthFloat = (float)byteTime / m_bitTime;
			byteBitLength = (int)(byteBitLengthFloat + 0.5);
			pulseTime = start - m_pulseStart;
			pulseBitLengthFloat = (float)pulseTime / m_bitTime;
			pulseBitLength = (int)(pulseBitLengthFloat + 0.5);

			if (byteBitLength > 9)
			{
				// fill in the last bits with high values
				// because this is the start bit of the next byte
				propgateBits(true, 8);

				// store the byte in the buffer
				unsigned int next = (m_inPos + 1) % m_buffSize;
				if (next != m_outPos) {
					m_buffer[m_inPos] = m_rec;
					m_inPos = next;
				}
				else {
					m_overflow = true;
				}
				setStartBit(start);
			}
			else
			{
				if (propgateBits(previousBit, pulseBitLength))
				{
					// Store the received value in the buffer unless we have an overflow
					unsigned int next = (m_inPos + 1) % m_buffSize;
					if (next != m_outPos) {
						m_buffer[m_inPos] = m_rec;
						m_inPos = next;
					}
					else {
						m_overflow = true;
					}
					setWaitingForStart();
				}
			}
			m_pulseStart = start;
			break;
		}
	}
	else
	{
		uint8_t rec = 0;
		for (int i = 0; i < 8; i++) {
			WAIT;
			rec >>= 1;
			if (digitalRead(m_rxPin))
				rec |= 0x80;
		}
		if (m_invert) rec = ~rec;
		// Stop bit
		WAIT;
		// Store the received value in the buffer unless we have an overflow
		unsigned int next = (m_inPos + 1) % m_buffSize;
		if (next != m_inPos) {
			m_buffer[m_inPos] = rec;
			m_inPos = next;
		}
	}

	// Must clear this bit in the interrupt register,
	// it gets set even when interrupts are disabled
	GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, 1 << m_rxPin);
}
#endif
