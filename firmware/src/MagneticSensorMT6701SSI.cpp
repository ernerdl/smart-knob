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

#include "./MagneticSensorMT6701SSI.h"
#include "common/foc_utils.h"
#include "common/time_utils.h"

MagneticSensorMT6701SSI::MagneticSensorMT6701SSI(int nCS, SPISettings settings) : settings(settings), nCS(nCS) {

}


MagneticSensorMT6701SSI::~MagneticSensorMT6701SSI() {

}

void MagneticSensorMT6701SSI::init(SPIClass* _spi) {
    this->spi=_spi;
    if (nCS >= 0) {
        pinMode(nCS, OUTPUT);
        digitalWrite(nCS, HIGH);
    }
    this->spi->begin();
    this->Sensor::init();
}

// check 40us delay between each read?
float MagneticSensorMT6701SSI::getSensorAngle() {
    float angle_data = readRawAngleSSI();
    angle_data = ( angle_data / (float)MT6701_CPR ) * _2PI;
    // return the shaft angle
    return angle_data;
}


uint16_t MagneticSensorMT6701SSI::readRawAngleSSI() {
    spi->beginTransaction(settings);
    if (nCS >= 0)
        digitalWrite(nCS, LOW);
    uint16_t value = spi->transfer16(0x0000);
    if (nCS >= 0)
        digitalWrite(nCS, HIGH);
    spi->endTransaction();
    return (value>>MT6701_DATA_POS)&0x3FFF;
};
