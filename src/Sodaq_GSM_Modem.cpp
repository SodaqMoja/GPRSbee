/*
 * Copyright (c) 2013-2015 Kees Bakker.  All rights reserved.
 *
 * This file is part of GPRSbee.
 *
 * GPRSbee is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or(at your option) any later version.
 *
 * GPRSbee is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GPRSbee.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include "Sodaq_GSM_Modem.h"

#define DEBUG

#ifdef DEBUG
#define debugPrintLn(...) { if (this->_diagStream) this->_diagStream->println(__VA_ARGS__); }
#define debugPrint(...) { if (this->_diagStream) this->_diagStream->print(__VA_ARGS__); }
#warning "Debug mode is ON"
#else
#define debugPrintLn(...)
#define debugPrint(...)
#endif

#define CR "\r"
#define LF "\n"
#define CRLF "\r\n"

// TODO this needs to be set in the compiler directives. Find something else to do
#define SODAQ_GSM_TERMINATOR CRLF

#ifndef SODAQ_GSM_TERMINATOR
#warning "SODAQ_GSM_TERMINATOR is not set"
#define SODAQ_GSM_TERMINATOR CRLF
#endif

#define SODAQ_GSM_TERMINATOR_LEN (sizeof(SODAQ_GSM_TERMINATOR) - 1) // without the NULL terminator

#define SODAQ_GSM_MODEM_DEFAULT_INPUT_BUFFER_SIZE 128

// Constructor
Sodaq_GSM_Modem::Sodaq_GSM_Modem() :
    _modemStream(0),
    _diagStream(0),
    _inputBufferSize(SODAQ_GSM_MODEM_DEFAULT_INPUT_BUFFER_SIZE),
    _inputBuffer(0),
    _apn(0),
    _apnUser(0),
    _apnPass(0),
    _timeout(DEFAULT_TIMEOUT),
    _onoff(0),
    _baudRateChangeCallbackPtr(0),
    _appendCommand(false),
    _lastCSQ(0),
    _CSQtime(0),
    _minSignalQuality(10)
{
    this->_isBufferInitialized = false;
}

// Turns the modem on and returns true if successful.
bool Sodaq_GSM_Modem::on()
{
    if (!isOn()) {
        if (_onoff) {
            _onoff->on();
        }
    }

    // wait for power up
    bool timeout = true;
    for (uint8_t i = 0; i < 10; i++) {
        if (isAlive()) {
            timeout = false;
            break;
        }
    }

    if (timeout) {
        debugPrintLn("Error: No Reply from Modem");
        return false;
    }

    return isOn(); // this essentially means isOn() && isAlive()
}

// Turns the modem off and returns true if successful.
bool Sodaq_GSM_Modem::off() const
{
    // No matter if it is on or off, turn it off.
    if (_onoff) {
        _onoff->off();
    }

    return !isOn();
}

// Returns true if the modem is on.
bool Sodaq_GSM_Modem::isOn() const
{
    if (_onoff) {
        return _onoff->isOn();
    }

    // No onoff. Let's assume it is on.
    return true;
}

void Sodaq_GSM_Modem::writeProlog()
{
    if (!_appendCommand) {
        debugPrint(">> ");
        _appendCommand = true;
    }
}

// TODO is the result really needed?
size_t Sodaq_GSM_Modem::write(const char* buffer)
{
    writeProlog();
    debugPrint(buffer);
    
    return _modemStream->print(buffer);
}

// Write a byte, as binary data
size_t Sodaq_GSM_Modem::writeByte(uint8_t value)
{
    return _modemStream->write(value);
}

size_t Sodaq_GSM_Modem::write(uint8_t value)
{
    writeProlog();
    debugPrint(value);

    return _modemStream->print(value);
}

size_t Sodaq_GSM_Modem::write(uint32_t value)
{
    writeProlog();
    debugPrint(value);

    return _modemStream->print(value);
}

size_t Sodaq_GSM_Modem::write(char value)
{
    writeProlog();
    debugPrint(value);

    return _modemStream->print(value);
};

// TODO is the result really needed?
size_t Sodaq_GSM_Modem::writeLn(const char* buffer)
{
    size_t i = write(buffer);
    return i + writeLn();
}

size_t Sodaq_GSM_Modem::writeLn(uint8_t value)
{
    size_t i = write(value);
    return i + writeLn();
}

size_t Sodaq_GSM_Modem::writeLn(uint32_t value)
{
    size_t i = write(value);
    return i + writeLn();
}

size_t Sodaq_GSM_Modem::writeLn(char value)
{
    size_t i = write(value);
    return i + writeLn();
}

size_t Sodaq_GSM_Modem::writeLn()
{
    debugPrintLn();
    size_t i = write('\r');
    _appendCommand = false;
    return i;
}

// Initializes the input buffer and makes sure it is only initialized once. 
// Safe to call multiple times.
void Sodaq_GSM_Modem::initBuffer()
{
    debugPrintLn("[initBuffer]");

    // make sure the buffers are only initialized once
    if (!_isBufferInitialized) {
        this->_inputBuffer = static_cast<char*>(malloc(this->_inputBufferSize));

        _isBufferInitialized = true;
    }
}

// Sets the modem stream.
void Sodaq_GSM_Modem::setModemStream(Stream& stream)
{
    this->_modemStream = &stream;
}

void Sodaq_GSM_Modem::setApn(const char * apn)
{
    size_t len = strlen(apn);
    _apn = static_cast<char*>(realloc(_apn, len + 1));
    strcpy(_apn, apn);
}

void Sodaq_GSM_Modem::setApnUser(const char * user)
{
    size_t len = strlen(user);
    _apnUser = static_cast<char*>(realloc(_apnUser, len + 1));
    strcpy(_apnUser, user);
}

void Sodaq_GSM_Modem::setApnPass(const char * pass)
{
    size_t len = strlen(pass);
    _apnPass = static_cast<char*>(realloc(_apnPass, len + 1));
    strcpy(_apnPass, pass);
}

// Returns a character from the modem stream if read within _timeout ms or -1 otherwise.
int Sodaq_GSM_Modem::timedRead() const
{
    int c;
    uint32_t _startMillis = millis();
    
    do {
        c = _modemStream->read();
        if (c >= 0) {
            return c;
        }
    } while (millis() - _startMillis < _timeout);
    
    return -1; // -1 indicates timeout
}

// Fills the given "buffer" with characters read from the modem stream up to "length"
// maximum characters and until the "terminator" character is found or a character read
// times out (whichever happens first).
// The buffer does not contain the "terminator" character or a null terminator explicitly.
// Returns the number of characters written to the buffer, not including null terminator.
size_t Sodaq_GSM_Modem::readBytesUntil(char terminator, char* buffer, size_t length)
{
    if (length < 1) {
        return 0;
    }

    size_t index = 0;

    while (index < length) {
        int c = timedRead();

        if (c < 0 || c == terminator) {
            break;
        }

        *buffer++ = static_cast<char>(c);
        index++;
    }

    // TODO distinguise timeout from empty string?
    // TODO return error for overflow?
    return index; // return number of characters, not including null terminator
}

// Fills the given "buffer" with up to "length" characters read from the modem stream.
// It stops when a character read timesout or "length" characters have been read.
// Returns the number of characters written to the buffer.
size_t Sodaq_GSM_Modem::readBytes(uint8_t* buffer, size_t length)
{
    size_t count = 0;

    while (count < length) {
        int c = timedRead();

        if (c < 0) {
            break;
        }
        
        *buffer++ = static_cast<uint8_t>(c);
        count++;
    }

    // TODO distinguise timeout from empty string?
    // TODO return error for overflow?
    return count;
}

// Reads a line (up to the SODAQ_GSM_TERMINATOR) from the modem stream into the "buffer".
// The buffer is terminated with null.
// Returns the number of bytes read, not including the null terminator.
size_t Sodaq_GSM_Modem::readLn(char* buffer, size_t size, long timeout)
{
    _timeout = timeout;
    size_t len = readBytesUntil(SODAQ_GSM_TERMINATOR[SODAQ_GSM_TERMINATOR_LEN - 1], buffer, size);

    // check if the terminator is more than 1 characters, then check if the first character of it exists 
    // in the calculated position and terminate the string there
    if ((SODAQ_GSM_TERMINATOR_LEN > 1) && (_inputBuffer[len - (SODAQ_GSM_TERMINATOR_LEN - 1)] == SODAQ_GSM_TERMINATOR[0])) {
        len -= SODAQ_GSM_TERMINATOR_LEN - 1;
    }

    // terminate string
    _inputBuffer[len] = 0;

    return len;
}
