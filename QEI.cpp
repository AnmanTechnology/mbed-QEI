#include "QEI.h"

QEI::QEI(PinName channelA, PinName channelB, PinName index, Encoding encoding) : _channelA(channelA, PullUp), _channelB(channelB, PullUp), _index(index)
{
    _pulses = 0;
    _revolutions = 0;
    _fSpeedFactor = 1.0;
    _fPositionFactor = 1.0;

    _nSpeedLastTimer = 0;
    _nSpeedAvrTimeSum = 0;
    _nSpeedAvrTimeCount = -1;
    _fLastSpeed = 0;
    _nSpeedTimeoutMax = 10;
    _nSpeedTimeoutCount = 0;

    _SpeedTimer.reset();
    _SpeedTimer.start();

    //Workout what the current state is.
    int chanA = _channelA.read();
    int chanB = _channelB.read();

    //2-bit state
    _currState = (chanA << 1) | (chanB);
    _prevState = _currState;

    //X2 encoding uses interrupts on only channel A.
    //X4 encoding uses interrupts on both channel A and B.
    _encoding = encoding;
    _channelA.rise(callback(this, &QEI::encode));
    _channelA.fall(callback(this, &QEI::encode));

    if (_encoding == X4_ENCODING)
    {
        _channelB.rise(callback(this, &QEI::encode));
        _channelB.fall(callback(this, &QEI::encode));
    }

    //Index is optional.
    if (index != NC)
    {
        _index.rise(callback(this, &QEI::index));
    }
}

QEI::~QEI()
{
    _channelA.rise(NULL);
    _channelA.fall(NULL);
    _channelB.rise(NULL);
    _channelB.fall(NULL);
}

void QEI::reset()
{
    _pulses = 0;
    _revolutions = 0;
}

int QEI::read()
{
    return _pulses;
}

void QEI::write(int pulses)
{
    _pulses = pulses;
}

void QEI::setSpeedFactor(float fSpeedFactor)
{
    _fSpeedFactor = fSpeedFactor;
}

float QEI::getSpeed()
{
    // float fSpeed;
    int avrTimeSum = 0;
    int avrTimeCount = 0;

    __disable_irq();
    avrTimeSum = _nSpeedAvrTimeSum;
    avrTimeCount = _nSpeedAvrTimeCount;
    _nSpeedAvrTimeSum = 0;
    _nSpeedAvrTimeCount = 0;
    __enable_irq();

    if (avrTimeCount == 0)
    {
        if (_nSpeedTimeoutCount++ > _nSpeedTimeoutMax)
            _fLastSpeed *= 0.5f;
        _fSpeed = _fLastSpeed;
    }
    else if (avrTimeCount < 0 || avrTimeSum == 0)
    {
        _fSpeed = 0;
        _nSpeedTimeoutCount = 0;
    }
    else
    {
        _fSpeed = 1000000.0f * _fSpeedFactor / ((float)avrTimeSum / (float)avrTimeCount);
        _nSpeedTimeoutCount = 0;
    }
    _fLastSpeed = _fSpeed;

    return _fSpeed;
}

void QEI::setPositionFactor(float fPositionFactor)
{
    _fPositionFactor = fPositionFactor;
}

float QEI::getPosition()
{
    return (float)_pulses * _fPositionFactor;
}

// +-------------+
// | X2 Encoding |
// +-------------+
//
// When observing states two patterns will appear:
//
// Counter clockwise rotation:
//
// 10 -> 01 -> 10 -> 01 -> ...
//
// Clockwise rotation:
//
// 11 -> 00 -> 11 -> 00 -> ...
//
// We consider counter clockwise rotation to be "forward" and
// counter clockwise to be "backward". Therefore pulse count will increase
// during counter clockwise rotation and decrease during clockwise rotation.
//
// +-------------+
// | X4 Encoding |
// +-------------+
//
// There are four possible states for a quadrature encoder which correspond to
// 2-bit gray code.
//
// A state change is only valid if of only one bit has changed.
// A state change is invalid if both bits have changed.
//
// Clockwise Rotation ->
//
//    00 01 11 10 00
//
// <- Counter Clockwise Rotation
//
// If we observe any valid state changes going from left to right, we have
// moved one pulse clockwise [we will consider this "backward" or "negative"].
//
// If we observe any valid state changes going from right to left we have
// moved one pulse counter clockwise [we will consider this "forward" or
// "positive"].
//
// We might enter an invalid state for a number of reasons which are hard to
// predict - if this is the case, it is generally safe to ignore it, update
// the state and carry on, with the error correcting itself shortly after.

void QEI::encode()
{
    int change = -1;
    int chanA = _channelA.read();
    int chanB = _channelB.read();

    //2-bit state
    _currState = (chanA << 1) | chanB;

    if (_encoding == X2_ENCODING)
    {
        //11->00->11->00 is counter clockwise rotation or "forward".
        if ((_prevState == 0x03 && _currState == 0x00) || (_prevState == 0x00 && _currState == 0x02))
        {
            _pulses++;
            change = 0;
        }
        //10->01->10->01 is clockwise rotation or "backward".
        else if ((_prevState == 0x02 && _currState == 0x01) || (_prevState == 0x01 && _currState == 0x02))
        {
            _pulses--;
            change = 1;
        }
    }
    else if (_encoding == X4_ENCODING)
    {
        //Entered a new valid state.
        if (((_currState ^ _prevState) != INVALID) && (_currState != _prevState))
        {
            //2 bit state. Right hand bit of prev XOR left hand bit of current
            //give 0 if counter clockwise rotation and 1 if clockwise rotation
            change = (_prevState & PREV_MASK) ^ ((_currState & CURR_MASK) >> 1);
            if (change == 0)
            {
                _pulses++;
            }
            else if (change == 1)
            {
                _pulses--;
            }
        }
    }
    _prevState = _currState;

    if (change != -1)
    {
        unsigned int act = _SpeedTimer.read_us();
        unsigned int diff = act - _nSpeedLastTimer;
        _nSpeedLastTimer = act;

        if (_nSpeedAvrTimeCount < 0)
        {
            _nSpeedAvrTimeSum = 0;
            _nSpeedAvrTimeCount = 0;
        }
        else
        {
            if (change == 0)
                _nSpeedAvrTimeSum += diff;
            else
                _nSpeedAvrTimeSum -= diff;
            _nSpeedAvrTimeCount++;
        }
    }
}

void QEI::index()
{
    _revolutions++;
}