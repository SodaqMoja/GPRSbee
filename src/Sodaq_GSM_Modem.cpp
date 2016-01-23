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
    _baudRateChangeCallbackPtr(0)
{
    this->_isBufferInitialized = false;
}

bool Sodaq_GSM_Modem::on()
{
    if (!isOn()) {
        if (_onoff) {
            _onoff->on();
        }
    }

    // Make sure it responds
    if (!isAlive()) {
        // Oh, no answer, maybe it's off
        // Fall through and rely on the status pin
    } else {
        // It's alive, it answered with an OK
    }

    return isOn();
}

bool Sodaq_GSM_Modem::off()
{
    // No matter if it is on or off, turn it off.
    if (_onoff) {
        _onoff->off();
    }

    // TODO _echoOff = false;
    return !isOn();
}

bool Sodaq_GSM_Modem::isOn()
{
    if (_onoff) {
        return _onoff->isOn();
    }

    // No onoff. Let's assume it is on.
    return true;
}

// TODO is the result really needed?
size_t Sodaq_GSM_Modem::write(const char* buffer)
{
    debugPrint("[write]");
    debugPrint(buffer);
    
    return _modemStream->print(buffer);
}

size_t Sodaq_GSM_Modem::write(uint8_t value)
{
    debugPrint("[write]");
    debugPrint(value);

    return _modemStream->print(value);
}

size_t Sodaq_GSM_Modem::write(uint32_t value)
{
    debugPrint("[write]");
    debugPrint(value);

    return _modemStream->print(value);
}

size_t Sodaq_GSM_Modem::write(char value)
{
    debugPrint("[write]");
    debugPrint(value);

    return _modemStream->print(value);
};

// TODO is the result really needed?
size_t Sodaq_GSM_Modem::writeLn(const char* buffer)
{
    size_t i = write(buffer);
    return i + write(SODAQ_GSM_TERMINATOR);
}

size_t Sodaq_GSM_Modem::writeLn(uint8_t value)
{
    size_t i = write(value);
    return i + write(SODAQ_GSM_TERMINATOR);
}

size_t Sodaq_GSM_Modem::writeLn(uint32_t value)
{
    size_t i = write(value);
    return i + write(SODAQ_GSM_TERMINATOR);
}

size_t Sodaq_GSM_Modem::writeLn(char value)
{
    size_t i = write(value);
    return i + write(SODAQ_GSM_TERMINATOR);
}

void Sodaq_GSM_Modem::initBuffer()
{
    debugPrintLn("[initBuffer]");

    // make sure the buffers are only initialized once
    if (!_isBufferInitialized) {
        this->_inputBuffer = static_cast<char*>(malloc(this->_inputBufferSize));

        _isBufferInitialized = true;
    }
}

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

size_t Sodaq_GSM_Modem::readBytesUntil(char terminator, char *buffer, size_t length)
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

size_t Sodaq_GSM_Modem::readBytes(char *buffer, size_t length)
{
    size_t count = 0;
    while (count < length) {
        int c = timedRead();
        if (c < 0) {
            break;
        }
        
        *buffer++ = static_cast<char>(c);
        count++;
    }

    // TODO distinguise timeout from empty string?
    // TODO return error for overflow?
    return count;
}

// Reads a line from the device stream into the "buffer" and returns it starting at the "start" position of the buffer received, without the terminator.
// Returns the number of bytes read.
// terminates string
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
