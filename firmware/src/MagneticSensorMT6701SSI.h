/*
 * MT6701 SSI magnetic sensor driver for SimpleFOC.
 *
 * Taken unmodified from the SimpleFOC drivers library:
 *   https://github.com/simplefoc/Arduino-FOC-drivers
 *   src/encoders/mt6701/
 *
 * MIT License
 * Copyright (c) 2021 Richard Unger
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED. See the LICENSE file at the repository root for the full text.
 */

#ifndef __MAGNETIC_SENSOR_MT6701_SSI_H__
#define __MAGNETIC_SENSOR_MT6701_SSI_H__

#include "Arduino.h"
#include "SPI.h"
#include "common/base_classes/Sensor.h"


#define MT6701_CPR 16384.0f

#define MT6701_BITORDER MSBFIRST

#define MT6701_DATA_POS 1

// Use SPI mode 2, capture on falling edge. First bit is not valid data, so have to read 25 bits to get a full SSI frame.
// SSI frame is 1 bit ignore, 14 bits angle, 4 bit status and 6 bit CRC.
static SPISettings MT6701SSISettings(1000000, MT6701_BITORDER, SPI_MODE2); // @suppress("Invalid arguments")



class MagneticSensorMT6701SSI : public Sensor {
public:
    MagneticSensorMT6701SSI(int nCS = -1, SPISettings settings = MT6701SSISettings);
    virtual ~MagneticSensorMT6701SSI();

    virtual void init(SPIClass* _spi = &SPI);

    float getSensorAngle() override; // angle in radians, return current value

protected:
    uint16_t readRawAngleSSI();

    SPISettings settings;
    SPIClass* spi;
	int nCS = -1;
};


#endif