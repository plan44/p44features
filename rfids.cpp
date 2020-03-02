//
//  Copyright (c) 2020 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  This file is part of p44featured.
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

#include "rfids.hpp"

#if ENABLE_FEATURE_RFIDS

#include "application.hpp"

using namespace p44;

#define FEATURE_NAME "rfids"

#define RFID_DEFAULT_READ_INTERVAL (100*MilliSecond)


RFIDs::RFIDs(SPIDevicePtr aSPIGenericDev, RFID522::SelectCB aReaderSelectFunc, DigitalIoPtr aResetOutput, DigitalIoPtr aIRQInput) :
  inherited(FEATURE_NAME),
  spiDevice(aSPIGenericDev),
  readerSelectFunc(aReaderSelectFunc),
  resetOutput(aResetOutput),
  irqInput(aIRQInput),
  rfidPollInterval(RFID_DEFAULT_READ_INTERVAL)
{

}



void RFIDs::reset()
{
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
  LOG(LOG_NOTICE, "initializing " FEATURE_NAME);
  reset();
  // { "cmd":"init", "rfids": { "readers":[0,1,2,3,7,8,12,13] } }
  // { "cmd":"init", "rfids": { "pollinterval":0.1, "readers":[0,1,2,7,9,23] } }
  ErrorPtr err;
  JsonObjectPtr o;
  if (spiDevice) {
    if (aInitData->get("pollinterval", o)) {
      rfidPollInterval = o->doubleValue()*Second;
    }
    if (aInitData->get("readers", o)) {
      for (int i=0; i<o->arrayLength(); i++) {
        int readerIndex = o->arrayGet(i)->int32Value();
        RFID522Ptr reader = RFID522Ptr(new RFID522(spiDevice, readerIndex, readerSelectFunc));
        rfidReaders.push_back(reader);
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
  message->add("rfid", JsonObject::newString(aRFIDnUID));
  message->add("rfid_index", JsonObject::newInt32(aReaderIndex));
  FeatureApi::sharedApi()->sendMessage(message);
}


// MARK: ==== rfids operation

void RFIDs::initOperation()
{
  LOG(LOG_NOTICE, "- Resetting all readers");
  resetOutput->set(0); // release reset, active high
  MainLoop::sleep(1*Second);
  LOG(LOG_NOTICE, "- Releasing reset for all readers");
  resetOutput->set(1); // release reset, active high
  MainLoop::sleep(1*Second);
  for (RFIDReaderList::iterator pos = rfidReaders.begin(); pos!=rfidReaders.end(); ++pos) {
    RFID522Ptr reader = *pos;
    LOG(LOG_NOTICE, "- Enabling RFID522 reader address #%d", reader->getReaderIndex());
    reader->init();
    MainLoop::sleep(1*Second);
  }
  MainLoop::sleep(2*Second);
  // seems to work only on second round....
  for (RFIDReaderList::iterator pos = rfidReaders.begin(); pos!=rfidReaders.end(); ++pos) {
    RFID522Ptr reader = *pos;
    LOG(LOG_NOTICE, "- Enabling RFID522 reader address #%d AGAIN", reader->getReaderIndex());
    reader->init();
    MainLoop::sleep(1*Second);
  }
  nextReaderToPoll = rfidReaders.begin();
  pollTimer.executeOnce(boost::bind(&RFIDs::rfidRead, this, _1), 500*MilliSecond);
}


void RFIDs::rfidRead(MLTimer& aTimer)
{
  if (nextReaderToPoll==rfidReaders.end()) nextReaderToPoll = rfidReaders.begin();
  if (nextReaderToPoll!=rfidReaders.end()) {
    RFID522Ptr reader = *nextReaderToPoll;
    nextReaderToPoll++;
    if (reader->isCard()) {
      /* If so then get its serial number */
      reader->readCardSerial();
      string id = string_format("%02X%02X%02X%02X", reader->serNum[3], reader->serNum[2], reader->serNum[1], reader->serNum[0]);
      LOG(LOG_NOTICE, "Reader #%d: Card ID %s detected", reader->getReaderIndex(), id.c_str());
      rfidDetected(reader->getReaderIndex(), id);
    }
    else {
      LOG(LOG_INFO, "Reader #%d: No card detected", reader->getReaderIndex());
    }
  }
  MainLoop::currentMainLoop().retriggerTimer(aTimer, rfidPollInterval);
}


#endif // ENABLE_FEATURE_RFIDS
