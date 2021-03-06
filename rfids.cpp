//
//  Copyright (c) 2020 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  This file is part of p44features.
//
//  pixelboardd is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  pixelboardd is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with pixelboardd. If not, see <http://www.gnu.org/licenses/>.
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

RFIDs::RFIDs(SPIDevicePtr aSPIGenericDev, RFID522::SelectCB aReaderSelectFunc, DigitalIoPtr aResetOutput, DigitalIoPtr aIRQInput) :
  inherited(FEATURE_NAME),
  spiDevice(aSPIGenericDev),
  readerSelectFunc(aReaderSelectFunc),
  resetOutput(aResetOutput),
  irqInput(aIRQInput),
  pauseIrqHandling(false),
  rfidPollInterval(RFID_DEFAULT_POLL_INTERVAL),
  sameIdTimeout(RFID_DEFAULT_SAME_ID_TIMEOUT),
  pollPauseAfterDetect(RFID_POLL_PAUSE_AFTER_DETECT)
{

}



void RFIDs::reset()
{
  haltIrqHandling();
  resetOutput->set(0); // put into reset, active low
  rfidReaders.clear();
  inherited::reset();
}


RFIDs::~RFIDs()
{
  reset();
}


// MARK: ==== rfids API


ErrorPtr RFIDs::initialize(JsonObjectPtr aInitData)
{
  reset();
  // { "cmd":"init", "rfids": { "readers":[0,1,2,3,7,8,12,13] } }
  // { "cmd":"init", "rfids": { "pollinterval":0.1, "readers":[0,1,2,7,9,23] } }
  ErrorPtr err;
  JsonObjectPtr o;
  if (spiDevice) {
    if (aInitData->get("pollinterval", o)) {
      rfidPollInterval = o->doubleValue()*Second;
    }
    if (aInitData->get("sameidtimeout", o)) {
      sameIdTimeout = o->doubleValue()*Second;
    }
    if (aInitData->get("pauseafterdetect", o)) {
      pollPauseAfterDetect = o->doubleValue()*Second;
    }
    if (aInitData->get("readers", o)) {
      for (int i=0; i<o->arrayLength(); i++) {
        int readerIndex = o->arrayGet(i)->int32Value();
        RFIDReader rd;
        rd.reader = RFID522Ptr(new RFID522(spiDevice, readerIndex, readerSelectFunc));
        rfidReaders[readerIndex] = rd;
      }
    }
  }
  if (rfidReaders.size()==0) {
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
    answer->add("activeReaders", JsonObject::newInt64(rfidReaders.size()));
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
  resetOutput->set(0); // assert reset = LOW
  startupTimer.executeOnce(boost::bind(&RFIDs::releaseReset, this, aDoneCB), RESET_TIME);
}


void RFIDs::releaseReset(SimpleCB aDoneCB)
{
  resetOutput->set(1); // release reset = HIGH
  startupTimer.executeOnce(boost::bind(&RFIDs::resetDone, this, aDoneCB), RESET_TIME);
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
  for (RFIDReaderMap::iterator pos = rfidReaders.begin(); pos!=rfidReaders.end(); ++pos) {
    RFID522Ptr reader = pos->second.reader;
    OLOG(LOG_NOTICE, "- Enabling RFID522 reader address #%d", reader->getReaderIndex());
    reader->init();
  }
  // install IRQ
  if (!POLLING_IRQ && irqInput)
  {
    if (!irqInput->setInputChangedHandler(boost::bind(&RFIDs::irqHandler, this, _1), 0, Never)) {
      OLOG(LOG_ERR, "IRQ pin must have edge detection!");
    }
  }
  else {
    // just poll IRQ
    pauseIrqHandling = false;
    irqTimer.executeOnce(boost::bind(&RFIDs::pollIrq, this, _1), rfidPollInterval);
  }
  // initialized now
  setInitialized();
  // start scanning for cards on all readers
  for (RFIDReaderMap::iterator pos = rfidReaders.begin(); pos!=rfidReaders.end(); ++pos) {
    RFID522Ptr reader = pos->second.reader;
    FOCUSOLOG("Start probing on reader %d", reader->getReaderIndex());
    reader->probeTypeA(boost::bind(&RFIDs::detectedCard, this, reader, _1), true);
  }
}


void RFIDs::haltIrqHandling()
{
  irqTimer.cancel();
  pauseIrqHandling = true; // to exit IRQ loop and prevent retriggering timer
}


void RFIDs::pollIrq(MLTimer &aTimer)
{
  irqHandler(false); // assume active (LOW)
  if (pauseIrqHandling) {
    // prevent retriggering timer to allow pollPauseAfterDetect start immediately after card detection
    pauseIrqHandling = false;
    return;
  }
  MainLoop::currentMainLoop().retriggerTimer(aTimer, rfidPollInterval);
}


void RFIDs::irqHandler(bool aState)
{
  irqTimer.cancel();
  if (aState) {
    // going high (inactive)
    FOCUSOLOG("--- RFIDs IRQ went inactive");
  }
  else {
    // going low (active)
    FOCUSOLOG("+++ RFIDs IRQ went ACTIVE -> calling irq handlers");
    RFIDReaderMap::iterator pos = rfidReaders.begin();
    bool pending = false;
    while (pos!=rfidReaders.end()) {
      // let reader check IRQ
      if (pos->second.reader->irqHandler()) {
        pending = true;
      }
      if (pauseIrqHandling) {
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
    RFIDReader& r = rfidReaders[aReader->getReaderIndex()];
    MLMicroSeconds now = MainLoop::now();
    if (r.lastNUID!=nUID || r.lastDetect==Never || r.lastDetect+sameIdTimeout<now ) {
      r.lastDetect = now;
      r.lastNUID = nUID;
      #if POLLING_IRQ
      if (pollPauseAfterDetect>0) {
        // stop polling for now
        haltIrqHandling();
        // resume after a pause
        irqTimer.executeOnce(boost::bind(&RFIDs::pollIrq, this, _1), pollPauseAfterDetect);
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
