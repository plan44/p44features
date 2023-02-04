//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2018 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  This file is part of p44features.
//
//  p44featured is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  p44featured is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with p44featured. If not, see <http://www.gnu.org/licenses/>.
//

// File scope debugging options
// - Set ALWAYS_DEBUG to 1 to enable DBGLOG output even in non-DEBUG builds of this file
#define ALWAYS_DEBUG 0
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL 7

#include "mixloop.hpp"

#if ENABLE_FEATURE_MIXLOOP

#include "application.hpp"

using namespace p44;

MixLoop::MixLoop(const string aLedChain1Name, const string aLedChain2Name, bool doStart) :
  ledChain1Name(aLedChain1Name),
  ledChain2Name(aLedChain2Name),
  inherited("mixloop"),
  // params
  interval(33*MilliSecond),
  accelThreshold(1),
  accelChangeCutoff(10),
  accelMaxChange(50),
  accelIntegrationGain(0.16),
  integralFadeOffset(1.5),
  integralFadeScaling(0.95),
  maxIntegral(300),
  hitStartMinIntegral(15),
  hitWindowStart(1.2*Second),
  hitWindowDuration(2.5*Second),
  hitMinAccelChange(300), ///< how much change in acceleration a hit needs to be
  numLeds(52),
  integralDispOffset(0),
  integralDispScaling(0.01),
  hitFlashTime(666*MilliSecond),
  hitDispTime(5*Second),
  // state
  accelIntegral(0),
  accelStart(Never),
  hitDetectorActive(false),
  hitShowing(false)
{
  // check for commandline-triggered standalone operation
  if (doStart) {
    initOperation();
  }
}

// MARK: ==== light API

ErrorPtr MixLoop::initialize(JsonObjectPtr aInitData)
{
  initOperation();
  return Error::ok();
}


ErrorPtr MixLoop::processRequest(ApiRequestPtr aRequest)
{
  ErrorPtr err;
  JsonObjectPtr data = aRequest->getRequest();
  JsonObjectPtr o = data->get("cmd");
  if (o) {
    string cmd = o->stringValue();
    if (cmd=="hit") {
      showHit();
      return Error::ok();
    }
    else {
      return inherited::processRequest(aRequest);
    }
  }
  else {
    // decode properties
    if (data->get("accelThreshold", o, true)) {
      accelThreshold = o->int32Value();
    }
    if (data->get("interval", o, true)) {
      interval = o->doubleValue()*Second;
    }
    if (data->get("accelChangeCutoff", o, true)) {
      accelChangeCutoff = o->doubleValue();
    }
    if (data->get("accelMaxChange", o, true)) {
      accelMaxChange = o->doubleValue();
    }
    if (data->get("accelIntegrationGain", o, true)) {
      accelIntegrationGain = o->doubleValue();
    }
    if (data->get("integralFadeOffset", o, true)) {
      integralFadeOffset = o->doubleValue();
    }
    if (data->get("integralFadeScaling", o, true)) {
      integralFadeScaling = o->doubleValue();
    }
    if (data->get("maxIntegral", o, true)) {
      maxIntegral = o->doubleValue();
    }
    if (data->get("hitStartMinIntegral", o, true)) {
      hitStartMinIntegral = o->doubleValue();
    }
    if (data->get("hitWindowStart", o, true)) {
      hitWindowStart = o->doubleValue()*Second;
    }
    if (data->get("hitWindowDuration", o, true)) {
      hitWindowDuration = o->doubleValue()*Second;
    }
    if (data->get("hitMinAccelChange", o, true)) {
      hitMinAccelChange = o->doubleValue();
    }
    if (data->get("numLeds", o, true)) {
      numLeds = o->int32Value();
    }
    if (data->get("integralDispOffset", o, true)) {
      integralDispOffset = o->doubleValue();
    }
    if (data->get("integralDispScaling", o, true)) {
      integralDispScaling = o->doubleValue();
    }
    if (data->get("hitFlashTime", o, true)) {
      hitFlashTime = o->doubleValue()*Second;
    }
    if (data->get("hitDispTime", o, true)) {
      hitDispTime = o->doubleValue()*Second;
    }
    return err ? err : Error::ok();
  }
}


JsonObjectPtr MixLoop::status()
{
  JsonObjectPtr answer = inherited::status();
  if (answer->isType(json_type_object)) {
    answer->add("accelThreshold", JsonObject::newInt32(accelThreshold));
    answer->add("interval", JsonObject::newDouble((double)interval/Second));
    answer->add("accelChangeCutoff", JsonObject::newDouble(accelChangeCutoff));
    answer->add("accelMaxChange", JsonObject::newDouble(accelMaxChange));
    answer->add("accelIntegrationGain", JsonObject::newDouble(accelIntegrationGain));
    answer->add("integralFadeOffset", JsonObject::newDouble(integralFadeOffset));
    answer->add("integralFadeScaling", JsonObject::newDouble(integralFadeScaling));
    answer->add("maxIntegral", JsonObject::newDouble(maxIntegral));
    answer->add("hitStartMinIntegral", JsonObject::newDouble(hitStartMinIntegral));
    answer->add("hitWindowStart", JsonObject::newDouble((double)hitWindowStart/Second));
    answer->add("hitWindowDuration", JsonObject::newDouble((double)hitWindowDuration/Second));
    answer->add("hitMinAccelChange", JsonObject::newDouble(hitMinAccelChange));
    answer->add("numLeds", JsonObject::newInt32(numLeds));
    answer->add("integralDispOffset", JsonObject::newDouble(integralDispOffset));
    answer->add("integralDispScaling", JsonObject::newDouble(integralDispScaling));
    answer->add("hitFlashTime", JsonObject::newDouble((double)hitFlashTime/Second));
    answer->add("hitDispTime", JsonObject::newDouble((double)hitDispTime/Second));
  }
  return answer;
}


// MARK: ==== hermel operation


void MixLoop::initOperation()
{
  ledChain1 = LEDChainCommPtr(new LEDChainComm("WS2813.GRB", ledChain1Name, 100));
  ledChain2 = LEDChainCommPtr(new LEDChainComm("WS2813.GRB", ledChain2Name, 100));
  ledChain1->begin();
  ledChain1->show();
  ledChain2->begin();
  ledChain2->show();
  setInitialized();
  // ADXL345 accelerometer @ SPI bus 1.0 (/dev/spidev1.0 software SPI)
  accelerometer = SPIManager::sharedManager().getDevice(10, "generic-HP@00");
  measureTicket.executeOnce(boost::bind(&MixLoop::accelInit, this), 1*Second);
}



static bool adxl345_writebyte(SPIDevicePtr aSPiDev, uint8_t aReg, uint8_t aValue)
{
  // Bit7: 0=write, 1=read
  // Bit6: 0=singlebyte, 1=multibyte
  // Bit5..0: register address
  uint8_t msg[2];
  msg[0] = aReg & 0x3F; // single byte register write
  msg[1] = aValue;
  return aSPiDev->getBus().SPIRawWriteRead(aSPiDev.get(), 2, msg, 0, NULL);
}


static bool adxl345_readbyte(SPIDevicePtr aSPiDev, uint8_t aReg, uint8_t &aValue)
{
  // Bit7: 0=write, 1=read
  // Bit6: 0=singlebyte, 1=multibyte
  // Bit5..0: register address
  uint8_t wr;
  wr = (aReg & 0x3F) | 0x80; // single byte register read
  return aSPiDev->getBus().SPIRawWriteRead(aSPiDev.get(), 1, &wr, 1, &aValue);
}


static bool adxl345_readword(SPIDevicePtr aSPiDev, uint8_t aReg, uint16_t &aValue)
{
  // Bit7: 0=write, 1=read
  // Bit6: 0=singlebyte, 1=multibyte
  // Bit5..0: register address
  uint8_t wr;
  uint8_t res[2];
  wr = (aReg & 0x3F) | 0xC0; // multi byte register read
  bool ok = aSPiDev->getBus().SPIRawWriteRead(aSPiDev.get(), 1, &wr, 2, res);
  if (ok) {
    aValue = res[0] + (res[1]<<8);
  }
  return ok;
}


void MixLoop::accelInit()
{
  // null previous
  lastaccel[0] = 0;
  lastaccel[1] = 0;
  lastaccel[2] = 0;
  // - set power register
  adxl345_writebyte(accelerometer, 0x2D, 0x28);
  uint8_t b;
  if (adxl345_readbyte(accelerometer, 0x2D, b)) {
    if (b==0x28) {
      // 4-wire SPI, full resolution, justfy right (LSB mode), 2G range
      adxl345_writebyte(accelerometer, 0x31, 0x08);
      // set FIFO mode
      adxl345_writebyte(accelerometer, 0x38, 0x00); // FIFO bypassed
      // set data rate
      adxl345_writebyte(accelerometer, 0x2C, 0x09); // high power, 50 Hz data rate / 25Hz bandwidth
      // ready now, start sampling
      OLOG(LOG_NOTICE, "accelerometer ready -> start sampling now");
      accelMeasure();
      return;
    }
  }
  // retry later again
  FOCUSOLOG("waiting for accelerometer to get ready, reg 0x2D=0x%02x", b);
  measureTicket.executeOnce(boost::bind(&MixLoop::accelInit, this), 1*Second);
}



void MixLoop::accelMeasure()
{
  // measure
  bool changed = false;
  double changeamount = 0;
  for (int ai=0; ai<3; ai++) {
    // 0x32, 0x34 and 0x36 are accel data registers, 1/256g = LSB
    uint16_t uw;
    adxl345_readword(accelerometer, 0x32+2*ai, uw);
    int16_t a = (int16_t)uw;
    if (abs(a-lastaccel[ai])>accelThreshold) {
      accel[ai] = a;
      changeamount += fabs(accel[ai]-lastaccel[ai]);
      lastaccel[ai] = a;
      changed = true;
    }
  }
  MLMicroSeconds now = MainLoop::now();
  if (changed) {
    FOCUSOLOG("[%06lldmS] X = %5hd, Y = %5hd, Z = %5hd, raw changeAmount = %.0f", (accelStart!=Never ? now-accelStart : 0)/MilliSecond, accel[0], accel[1], accel[2], changeamount);
  }
  // process
  changeamount -= accelChangeCutoff;
  if (changeamount<0) changeamount = 0;
  // - hit detector
  if (hitDetectorActive) {
    if (now>accelStart+hitWindowStart) {
      // after start window
      if (now<accelStart+hitWindowStart+hitWindowDuration) {
        // before end of window, look out for hit
        if (changeamount>hitMinAccelChange) {
          OLOG(LOG_NOTICE, "HIT DETECTED with raw changeamount=%.0f, at %lldmS!", changeamount, (now-accelStart)/MilliSecond);
          showHit();
          hitDetectorActive = false;
        }
      }
      else {
        // after end of window
        OLOG(LOG_NOTICE, "Hit detector timed out");
        dispNormal();
        hitDetectorActive = false;
      }
    }
  }
  // - other processing
  if (changeamount>accelMaxChange) changeamount = accelMaxChange;
  changeamount *= accelIntegrationGain;
  // integrate
  accelIntegral += changeamount - integralFadeOffset;
  accelIntegral *= integralFadeScaling;
  if (accelIntegral<0) accelIntegral = 0;
  if (accelIntegral>maxIntegral) accelIntegral = maxIntegral;
  if (accelIntegral>0) {
    FOCUSOLOG("     changeAmount = %.0f, integral = %.0f", changeamount, accelIntegral);
  }
  // possibly trigger hit detector
  if (!hitDetectorActive) {
    if (accelIntegral>=hitStartMinIntegral) {
      accelStart = now;
      hitDetectorActive = true;
      #if ENABLE_LEGACY_FEATURE_SCRIPTS
      OLOG(LOG_NOTICE, "Hit detector activated with integral = %.0f", accelIntegral);
      FeatureApi::sharedApi()->runJsonFile("scripts/game.json", NoOP, &scriptContext);
      #endif
      JsonObjectPtr message = JsonObject::newObj();
      message->add("event", JsonObject::newString("activated"));
      message->add("accelintegral", JsonObject::newDouble(accelIntegral));
      sendEventMessage(message);
    }
  }
  // show
  showAccel(accelIntegral*integralDispScaling+integralDispOffset);
  // retrigger
  measureTicket.executeOnce(boost::bind(&MixLoop::accelMeasure, this), interval);
}


void MixLoop::showAccel(double aFraction)
{
  if (ledChain1 && !hitShowing) {
    int onLeds = aFraction*numLeds;
    OLOG(LOG_DEBUG, "onLeds=%d", onLeds);
    for (int i=0; i<numLeds; i++) {
      if (i<onLeds) {
        ledChain1->setPower(numLeds-1-i, 255, 255-(255*i/numLeds), 0);
      }
      else {
        ledChain1->setPower(numLeds-1-i, 0, 0, 0);
      }
    }
    ledChain1->show();
  }
}


void MixLoop::showHit()
{
  hitShowing = true;
  if (ledChain1) {
    for (int i=0; i<numLeds; i++) {
      ledChain1->setPower(i, 200, 200, 255);
    }
    ledChain1->show();
  }
  showTicket.executeOnce(boost::bind(&MixLoop::showHitEnd, this), hitFlashTime);
  // disp
  #if ENABLE_LEGACY_FEATURE_SCRIPTS
  FeatureApi::sharedApi()->runJsonFile("scripts/hit.json", NoOP, &scriptContext);
  #endif
  // report
  JsonObjectPtr message = JsonObject::newObj();
  message->add("event", JsonObject::newString("hit"));
  sendEventMessage(message);
}


void MixLoop::showHitEnd()
{
  hitShowing = false;
}


void MixLoop::dispNormal()
{
  #if ENABLE_LEGACY_FEATURE_SCRIPTS
  dispTicket.cancel();
  FeatureApi::sharedApi()->runJsonFile("scripts/normal.json", NoOP, &scriptContext);
  #endif
  // report
  JsonObjectPtr message = JsonObject::newObj();
  message->add("event", JsonObject::newString("hit"));
  sendEventMessage(message);
}

#endif // ENABLE_FEATURE_MIXLOOP
