//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2020 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  This file is part of p44features.
//
//  p44features is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  p44features is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with p44features. If not, see <http://www.gnu.org/licenses/>.
//

// File scope debugging options
// - Set ALWAYS_DEBUG to 1 to enable DBGLOG output even in non-DEBUG builds of this file
#define ALWAYS_DEBUG 0
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL 7

#include "rfids.hpp"

#if ENABLE_FEATURE_RFIDS

#include "application.hpp"

using namespace p44;

#define FEATURE_NAME "rfids"

#define RFID_DEFAULT_POLL_INTERVAL (100*MilliSecond)
#define RFID_DEFAULT_SAME_ID_TIMEOUT (3*Second)
#define RFID_POLL_PAUSE_AFTER_DETECT (1*Second)

RFIDs::RFIDs(SPIDevicePtr aSPIGenericDev, DigitalIoBusPtr aSelectBus, DigitalIoPtr aResetOutput, DigitalIoPtr aIRQInput) :
  inherited(FEATURE_NAME),
  mSpiDevice(aSPIGenericDev),
  mReaderSelectBus(aSelectBus),
  mResetOutput(aResetOutput),
  mIrqInput(aIRQInput),
  mPauseIrqHandling(false),
  mRfidPollInterval(RFID_DEFAULT_POLL_INTERVAL),
  mSameIdTimeout(RFID_DEFAULT_SAME_ID_TIMEOUT),
  mPollPauseAfterDetect(RFID_POLL_PAUSE_AFTER_DETECT)
{

}



void RFIDs::reset()
{
  haltIrqHandling();
  mResetOutput->set(0); // put into reset, active low
  mRfidReaders.clear();
  inherited::reset();
}


RFIDs::~RFIDs()
{
  reset();
}


// MARK: ==== rfids API

void RFIDs::selectReader(int aReaderIndex)
{
  if (aReaderIndex==RFID522::Deselect) {
    // means highest index
    aReaderIndex = mReaderSelectBus->getMaxBusValue();
  }
  mReaderSelectBus->setBusValue(aReaderIndex);
}



ErrorPtr RFIDs::initialize(JsonObjectPtr aInitData)
{
  reset();
  // { "cmd":"init", "rfids": { "readers":[0,1,2,3,7,8,12,13] } }
  // { "cmd":"init", "rfids": { "pollinterval":0.1, "readers":[0,1,2,7,9,23] } }
  ErrorPtr err;
  JsonObjectPtr o;
  if (mSpiDevice) {
    if (aInitData->get("pollinterval", o)) {
      mRfidPollInterval = o->doubleValue()*Second;
    }
    if (aInitData->get("sameidtimeout", o)) {
      mSameIdTimeout = o->doubleValue()*Second;
    }
    if (aInitData->get("pauseafterdetect", o)) {
      mPollPauseAfterDetect = o->doubleValue()*Second;
    }
    if (aInitData->get("readers", o)) {
      for (int i=0; i<o->arrayLength(); i++) {
        int readerIndex = o->arrayGet(i)->int32Value();
        RFIDReader rd;
        rd.reader = RFID522Ptr(new RFID522(mSpiDevice, readerIndex, boost::bind(&RFIDs::selectReader, this, _1)));
        mRfidReaders[readerIndex] = rd;
      }
    }
  }
  if (mRfidReaders.size()==0) {
    err = TextError::err("no RFID readers configured");
  }
  if (Error::isOK(err)) {
    // start running
    initOperation();
  }
  return err;
}


ErrorPtr RFIDs::processRequest(ApiRequestPtr aRequest)
{
  return TextError::err("No API implemented yet %%%%%%");
}


JsonObjectPtr RFIDs::status()
{
  JsonObjectPtr answer = inherited::status();
  if (answer->isType(json_type_object)) {
    answer->add("activeReaders", JsonObject::newInt64(mRfidReaders.size()));
  }
  return answer;
}


void RFIDs::rfidDetected(int aReaderIndex, const string aRFIDnUID)
{
  JsonObjectPtr message = JsonObject::newObj();
  message->add("nUID", JsonObject::newString(aRFIDnUID));
  message->add("reader", JsonObject::newInt32(aReaderIndex));
  sendEventMessage(message);
}


// MARK: ==== rfids operation

#define RESET_TIME (1*Second)

void RFIDs::resetReaders(SimpleCB aDoneCB)
{
  haltIrqHandling();
  mResetOutput->set(0); // assert reset = LOW
  mStartupTimer.executeOnce(boost::bind(&RFIDs::releaseReset, this, aDoneCB), RESET_TIME);
}


void RFIDs::releaseReset(SimpleCB aDoneCB)
{
  mResetOutput->set(1); // release reset = HIGH
  mStartupTimer.executeOnce(boost::bind(&RFIDs::resetDone, this, aDoneCB), RESET_TIME);
}


void RFIDs::resetDone(SimpleCB aDoneCB)
{
  if (aDoneCB) aDoneCB();
}


void RFIDs::initOperation()
{
  OLOG(LOG_NOTICE, "- Resetting all readers");
  resetReaders(boost::bind(&RFIDs::initReaders, this));
}


void RFIDs::initReaders()
{
  for (RFIDReaderMap::iterator pos = mRfidReaders.begin(); pos!=mRfidReaders.end(); ++pos) {
    RFID522Ptr reader = pos->second.reader;
    OLOG(LOG_NOTICE, "- Enabling RFID522 reader address #%d", reader->getReaderIndex());
    reader->init();
  }
  // install IRQ
  if (!POLLING_IRQ && mIrqInput)
  {
    if (!mIrqInput->setInputChangedHandler(boost::bind(&RFIDs::irqHandler, this, _1), 0, Never)) {
      OLOG(LOG_ERR, "IRQ pin must have edge detection!");
    }
  }
  else {
    // just poll IRQ
    mPauseIrqHandling = false;
    mIrqTimer.executeOnce(boost::bind(&RFIDs::pollIrq, this, _1), mRfidPollInterval);
  }
  // initialized now
  setInitialized();
  // start scanning for cards on all readers
  for (RFIDReaderMap::iterator pos = mRfidReaders.begin(); pos!=mRfidReaders.end(); ++pos) {
    RFID522Ptr reader = pos->second.reader;
    FOCUSOLOG("Start probing on reader %d", reader->getReaderIndex());
    reader->probeTypeA(boost::bind(&RFIDs::detectedCard, this, reader, _1), true);
  }
}


void RFIDs::haltIrqHandling()
{
  mIrqTimer.cancel();
  mPauseIrqHandling = true; // to exit IRQ loop and prevent retriggering timer
}


void RFIDs::pollIrq(MLTimer &aTimer)
{
  irqHandler(false); // assume active (LOW)
  if (mPauseIrqHandling) {
    // prevent retriggering timer to allow pollPauseAfterDetect start immediately after card detection
    mPauseIrqHandling = false;
    return;
  }
  MainLoop::currentMainLoop().retriggerTimer(aTimer, mRfidPollInterval);
}


void RFIDs::irqHandler(bool aState)
{
  mIrqTimer.cancel();
  if (aState) {
    // going high (inactive)
    FOCUSOLOG("--- RFIDs IRQ went inactive");
  }
  else {
    // going low (active)
    FOCUSOLOG("+++ RFIDs IRQ went ACTIVE -> calling irq handlers");
    RFIDReaderMap::iterator pos = mRfidReaders.begin();
    bool pending = false;
    while (pos!=mRfidReaders.end()) {
      // let reader check IRQ
      if (pos->second.reader->irqHandler()) {
        pending = true;
      }
      if (mPauseIrqHandling) {
        // exit loop to allow pollPauseAfterDetect start immediately after card detection
        break;
      }
      #if !POLLING_IRQ
      if (irqInput && irqInput->isSet()==true) {
        // served!
        FOCUSOLOG("IRQ served, irqline is HIGH now");
        break;
      }
      #endif
      // next
      pos++;
    }
  }
}


void RFIDs::detectedCard(RFID522Ptr aReader, ErrorPtr aErr)
{
  if (Error::isOK(aErr)) {
    OLOG(LOG_NOTICE, "Detected card on reader %d", aReader->getReaderIndex());
    aReader->antiCollision(boost::bind(&RFIDs::gotCardNUID, this, aReader, _1, _3));
  }
  else {
    OLOG(LOG_DEBUG, "Error on reader %d, status='%s' -> restart probing again", aReader->getReaderIndex(), aErr->text());
    aReader->probeTypeA(boost::bind(&RFIDs::detectedCard, this, aReader, _1), true);
  }
}


void RFIDs::gotCardNUID(RFID522Ptr aReader, ErrorPtr aErr, const string aNUID)
{
  if (Error::isOK(aErr)) {
    string nUID;
    // nUID is LSB first, and last byte is redundant BCC. Reverse to have MSB first, and omit BCC
    for (int i=(int)aNUID.size()-2; i>=0; i--) {
      string_format_append(nUID, "%02X", (uint8_t)aNUID[i]);
    }
    OLOG(LOG_NOTICE, "Reader #%d: Card ID %s detected", aReader->getReaderIndex(), nUID.c_str());
    RFIDReader& r = mRfidReaders[aReader->getReaderIndex()];
    MLMicroSeconds now = MainLoop::now();
    if (r.lastNUID!=nUID || r.lastDetect==Never || r.lastDetect+mSameIdTimeout<now ) {
      r.lastDetect = now;
      r.lastNUID = nUID;
      #if POLLING_IRQ
      if (mPollPauseAfterDetect>0) {
        // stop polling for now
        haltIrqHandling();
        // resume after a pause
        mIrqTimer.executeOnce(boost::bind(&RFIDs::pollIrq, this, _1), mPollPauseAfterDetect);
      }
      #endif
      rfidDetected(aReader->getReaderIndex(), nUID);
    }
  }
  else {
    OLOG(LOG_NOTICE, "Reader #%d: Card ID reading error: %s", aReader->getReaderIndex(), aErr->text());
  }
  // continue probing
  aReader->probeTypeA(boost::bind(&RFIDs::detectedCard, this, aReader, _1), true);
}


#endif // ENABLE_FEATURE_RFIDS
