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
#include "utils.hpp"

#if ENABLE_FEATURE_RFIDS

#include "application.hpp"

using namespace p44;

#define FEATURE_NAME "rfids"

#define RFID_DEFAULT_POLL_INTERVAL (100*MilliSecond)
#define RFID_DEFAULT_SAME_ID_TIMEOUT (3*Second)
#define RFID_POLL_PAUSE_AFTER_DETECT (1*Second)

RFIDs::RFIDs(SPIDevicePtr aSPIGenericDev, DigitalIoBusPtr aSelectBus, DigitalIoPtr aResetOutput, DigitalIoPtr aIRQInput) :
  inherited(FEATURE_NAME),
  #if IN_THREAD
  mUsePollingThread(false),
  #endif
  mPollIrq(true),
  mChipTimer(0), // use default
  mCmdTimeout(250*MilliSecond), // default that used to work
  mUseIrqWatchdog(false),
  mSpiDevice(aSPIGenericDev),
  mReaderSelectBus(aSelectBus),
  mResetOutput(aResetOutput),
  mIrqInput(aIRQInput),
  mPauseIrqHandling(false),
  mDisableFields(true),
  mGroupSwitchInterval(RFID_DEFAULT_POLL_INTERVAL*3),
  mRfidPollInterval(RFID_DEFAULT_POLL_INTERVAL),
  mSameIdTimeout(RFID_DEFAULT_SAME_ID_TIMEOUT),
  mPollPauseAfterDetect(RFID_POLL_PAUSE_AFTER_DETECT)
{

}



void RFIDs::reset()
{
  OLOG(LOG_INFO,"Received reset command, request RFID polling termination")
  #if IN_THREAD
  if (mRfidPollingThread) {
    // make terminate (will do in background)
    mRfidPollingThread->terminate();
    inherited::reset();
    return;
  }
  #endif
  stopReaders();
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
  // { "cmd":"init", "rfids": { "readers":[[0,2],[7,12],[1,8,13]] } }
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
    if (aInitData->get("chiptimer", o)) {
      mChipTimer = o->int32Value();
    }
    if (aInitData->get("cmdtimeout", o)) {
      mCmdTimeout = o->doubleValue()*Second;
    }
    if (aInitData->get("groupswitchinterval", o)) {
      mGroupSwitchInterval = o->doubleValue()*Second;
    }
    if (aInitData->get("useirqwatchdog", o)) {
      mUseIrqWatchdog = o->boolValue();
    }
    if (aInitData->get("disablefields", o)) {
      mDisableFields = o->boolValue();
    }
    if (aInitData->get("regvaluepairs", o)) {
      mExtraRegValuePairs = hexToBinaryString(o->c_strValue(), true);
    }
    if (aInitData->get("readers", o)) {
      bool grouped = false;
      for (int i=0; i<o->arrayLength(); i++) {
        JsonObjectPtr a = o->arrayGet(i);
        if (a->isType(json_type_array)) {
          // Array of arrays
          grouped = true;
          // add members to a group
          RFIDReaderMap g;
          for (int j=0; j<a->arrayLength(); j++) {
            int readerIndex = a->arrayGet(j)->int32Value();
            RFIDReaderPtr rd = new RFIDReader;
            rd->reader = RFID522Ptr(new RFID522(mSpiDevice, readerIndex, boost::bind(&RFIDs::selectReader, this, _1), mChipTimer, mUseIrqWatchdog, mCmdTimeout));
            // add to group and global map
            g[readerIndex] = rd;
            mRfidReaders[readerIndex] = rd;
          }
          // add group if not empty
          if (g.size()>0) mRfidGroups.push_back(g);
        }
        else if (grouped) {
          err = TextError::err("cannot mix groups and simple readers");
          break;
        }
        else {
          // plain list, no groups
          int readerIndex = o->arrayGet(i)->int32Value();
          RFIDReaderPtr rd = new RFIDReader;
          rd->reader = RFID522Ptr(new RFID522(mSpiDevice, readerIndex, boost::bind(&RFIDs::selectReader, this, _1), mChipTimer, mUseIrqWatchdog, mCmdTimeout));
          mRfidReaders[readerIndex] = rd;
        }
      }
    }
    if (aInitData->get("pollirq", o)) {
      mPollIrq = o->boolValue(); // Note: default is true
    }
    #if IN_THREAD
    if (aInitData->get("pollingthread", o)) {
      mUsePollingThread = o->boolValue();
    }
    #endif
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
  #if IN_THREAD
  if (mUsePollingThread && mRfidPollingThread) {
    // detections runs in separate thread, must notify parent thread
    pthread_mutex_lock(&mReportMutex);
    mDetectionMessage = message;
    pthread_mutex_unlock(&mReportMutex);
    mRfidPollingThread->signalParentThread(threadSignalUserSignal);
    return;
  }
  #endif
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
  #if IN_THREAD
  if (mUsePollingThread) {
    // put entire RFID polling into background thread
    pthread_mutex_init(&mReportMutex, NULL);
    mRfidPollingThread = MainLoop::currentMainLoop().executeInThread(
      boost::bind(&RFIDs::rfidPollingThread, this, _1),
      boost::bind(&RFIDs::rfidPollingThreadSignal, this, _1, _2)
    );
    return;
  }
  #endif
  // single threaded
  OLOG(LOG_NOTICE, "- Resetting all readers (single threaded)");
  resetReaders(boost::bind(&RFIDs::initReaders, this));
}


void RFIDs::stopReaders()
{
  haltIrqHandling();
  mResetOutput->set(0); // put into reset, active low
  mRfidReaders.clear();
  mRfidGroups.clear();
}


#if IN_THREAD

void RFIDs::rfidPollingThread(ChildThreadWrapper &aThread)
{
  // create mainloop
  OLOG(LOG_INFO, "Start of polling thread routine")
  aThread.threadMainLoop();
  // start with reset, will schedule first mainloop timers
  resetReaders(boost::bind(&RFIDs::initReaders, this));
  // now start the thread's mainloop
  aThread.threadMainLoop().run();
  // mainloop exits, so we need to stop readers
  stopReaders();
  OLOG(LOG_INFO, "End of polling thread routine")
}


void RFIDs::rfidPollingThreadSignal(ChildThreadWrapper &aChildThread, ThreadSignals aSignalCode)
{
  OLOG(LOG_DEBUG, "Received signal from child thread: %d", aSignalCode);
  if (aSignalCode==threadSignalUserSignal) {
    // means a new RFID was detected
    // - get the message object
    pthread_mutex_lock(&mReportMutex);
    JsonObjectPtr message = mDetectionMessage;
    mDetectionMessage.reset();
    pthread_mutex_unlock(&mReportMutex);
    // - send it
    sendEventMessage(message);
  }
  else if (aSignalCode==threadSignalCompleted) {
    OLOG(LOG_INFO, "Polling thread reports having ended");
    mRfidPollingThread.reset();
  }
}

#endif // IN_THREAD


void RFIDs::stopActiveGroup(int aExceptReader)
{
  FOCUSOLOG("- stop all readers %s", aExceptReader<0 ? "" : "EXCEPT current one");
  for (RFIDReaderMap::iterator pos = mActiveGroup->begin(); pos!=mActiveGroup->end(); ++pos) {
    if (pos->first!=aExceptReader) {
      // not the excepted reader, stop all action and possibly energy field
      pos->second->reader->returnToIdle();
      if (mDisableFields) pos->second->reader->energyField(false);
    }
  }
}

void RFIDs::switchToNextGroup()
{
  FOCUSOLOG("\n___ group timeout -> terminate current, switch to next");
  stopActiveGroup();
  runNextGroup();
}

void RFIDs::runNextGroup()
{
  mGroupSwitchTimer.cancel();
  // Next (or first) group
  if (mActiveGroup!=mRfidGroups.end()) mActiveGroup++;
  if (mActiveGroup==mRfidGroups.end()) mActiveGroup = mRfidGroups.begin();
  // break stack
  MainLoop::currentMainLoop().executeNow(boost::bind(&RFIDs::runActiveGroup, this));
}

void RFIDs::runActiveGroup()
{
  // Start field and do a probe on all group members
  FOCUSOLOG("\n=== Start running new group of readers");
  for (RFIDReaderMap::iterator pos = mActiveGroup->begin(); pos!=mActiveGroup->end(); ++pos) {
    RFID522Ptr reader = pos->second->reader;
    FOCUSOLOG("\nenable energy field and initiate single probing on reader %d", reader->getReaderIndex());
    reader->energyField(true);
    reader->probeTypeA(boost::bind(&RFIDs::probeTypeAResult, this, reader, _1), false); // NO "wait" == NO automatic re-issue of probe!
  }
  // schedule group switching
  mGroupSwitchTimer.executeOnce(boost::bind(&RFIDs::switchToNextGroup, this), mGroupSwitchInterval);
}


void RFIDs::probeTypeAResult(RFID522Ptr aReader, ErrorPtr aErr)
{
  FOCUSOLOG("\nprobeTypeAResult from reader #%d", aReader->getReaderIndex());
  if (Error::isOK(aErr)) {
    // Card detected: Stop all other readers in group
    OLOG(LOG_NOTICE, "\nDetected card when probing reader #%d", aReader->getReaderIndex());
    mGroupSwitchTimer.cancel(); // no group switching now
    stopActiveGroup(aReader->getReaderIndex());
    // run antiCollision on the one we have detected
    FOCUSOLOG("- start antiCollision to get ID");
    aReader->antiCollision(boost::bind(&RFIDs::antiCollisionResult, this, aReader, _1, _3));
  }
  else {
    // Error probing card
    if (aErr->isError(RFIDError::domain(), RFIDError::ChipTimeout)) {
      // Chip timeout: just means there is no card
      OLOG(LOG_DEBUG, "- reader #%d - chip has timed out -> continue transceiving", aReader->getReaderIndex());
      // PCD_TRANSCEIVE continues running, but re-trigger sending data
      aReader->continueTransceiving();
    }
    else {
      // real error
      OLOG(LOG_DEBUG, "Error on reader %d, status='%s' -> disable reader", aReader->getReaderIndex(), aErr->text());
      aReader->returnToIdle();
      if (mDisableFields) aReader->energyField(false);
    }
  }
}



void RFIDs::antiCollisionResult(RFID522Ptr aReader, ErrorPtr aErr, const string aNUID)
{
  if (Error::isOK(aErr)) {
    string nUID;
    // nUID is LSB first, and last byte is redundant BCC. Reverse to have MSB first, and omit BCC
    for (int i=(int)aNUID.size()-2; i>=0; i--) {
      string_format_append(nUID, "%02X", (uint8_t)aNUID[i]);
    }
    OLOG(LOG_NOTICE, "\nReader #%d: Card ID %s detected", aReader->getReaderIndex(), nUID.c_str());
    RFIDReaderPtr r = mRfidReaders[aReader->getReaderIndex()];
    MLMicroSeconds now = MainLoop::now();
    if (r->lastNUID!=nUID || r->lastDetect==Never || r->lastDetect+mSameIdTimeout<now ) {
      r->lastDetect = now;
      r->lastNUID = nUID;
      rfidDetected(aReader->getReaderIndex(), nUID);
    }
    else {
      FOCUSOLOG("- not reported because detected just recently");
    }
    runNextGroup();
  }
  else {
    OLOG(LOG_NOTICE, "\nReader #%d: Card ID reading error, restarting same group: %s", aReader->getReaderIndex(), aErr->text());
    runActiveGroup();
  }
}



void RFIDs::initIrq()
{
  // install IRQ
  if (!mPollIrq) {
    if (!mIrqInput || !mIrqInput->setInputChangedHandler(boost::bind(&RFIDs::irqHandler, this, _1), 0, Never)) {
      OLOG(LOG_ERR, "Need and IRQ pin, and it must have edge detection! -> switch to polling");
      mPollIrq = true;
    }
  }
  if (mPollIrq) {
    // need to poll IRQ
    mPauseIrqHandling = false;
    mIrqTimer.executeOnce(boost::bind(&RFIDs::pollIrq, this, _1), mRfidPollInterval);
  }
}



void RFIDs::initReaders()
{
  if (mRfidGroups.size()>0) {
    // init all, but no energy field enabled
    RFIDReaderMap::iterator pos = mRfidReaders.begin();
    for (RFIDReaderMap::iterator pos = mRfidReaders.begin(); pos!=mRfidReaders.end(); ++pos) {
      RFID522Ptr reader = pos->second->reader;
      OLOG(LOG_NOTICE, "- Enabling RFID522 reader address #%d, but energy field stays DISABLED", reader->getReaderIndex());
      if (!reader->init(mExtraRegValuePairs)) {
        OLOG(LOG_ERR, "Unknown or missing reader #%d", reader->getReaderIndex());
        // FIXME: remove it, and from groups lists as well
      }
      if (!mDisableFields) reader->energyField(true);
    }
    // init IRQ handling
    initIrq();
    // new, grouped operation mode
    mActiveGroup = mRfidGroups.end();
    runNextGroup();
    // initialized now
    setInitialized();
  }
  else {
    // old mode that does not seem to work correctly in some case, but does in others
    RFIDReaderMap::iterator pos = mRfidReaders.begin();
    while(pos!=mRfidReaders.end()) {
      RFID522Ptr reader = pos->second->reader;
      OLOG(LOG_NOTICE, "- Enabling RFID522 reader address #%d", reader->getReaderIndex());
      if (!reader->init(mExtraRegValuePairs)) {
        OLOG(LOG_ERR, "Unknown or missing reader #%d -> removing it", reader->getReaderIndex());
        pos = mRfidReaders.erase(pos);
        continue;
      }
      else {
        OLOG(LOG_INFO, "- Activating Energy field for reader address #%d", reader->getReaderIndex());
        reader->energyField(true);
      }
      ++pos;
    }
    // init IRQ handling
    initIrq();
    // initialized now
    setInitialized();
    // start scanning for cards on all readers
    for (RFIDReaderMap::iterator pos = mRfidReaders.begin(); pos!=mRfidReaders.end(); ++pos) {
      RFID522Ptr reader = pos->second->reader;
      FOCUSOLOG("Start probing on reader %d", reader->getReaderIndex());
      reader->probeTypeA(boost::bind(&RFIDs::detectedCard, this, reader, _1), true);
    }
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
    FOCUSOLOG("\n+++ RFIDs IRQ went ACTIVE (or we are polling) -> calling irq handlers");
    RFIDReaderMap::iterator pos = mRfidReaders.begin();
    bool pending = false;
    while (pos!=mRfidReaders.end()) {
      // let reader check IRQ
      if (pos->second->reader->irqHandler()) {
        pending = true;
      }
      if (mPauseIrqHandling) {
        // exit loop to allow pollPauseAfterDetect start immediately after card detection
        break;
      }
      if (!mPollIrq && mIrqInput && mIrqInput->isSet()==true) {
        // served!
        FOCUSOLOG("IRQ served, irqline is HIGH now");
        break;
      }
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
    RFIDReaderPtr r = mRfidReaders[aReader->getReaderIndex()];
    MLMicroSeconds now = MainLoop::now();
    if (r->lastNUID!=nUID || r->lastDetect==Never || r->lastDetect+mSameIdTimeout<now ) {
      r->lastDetect = now;
      r->lastNUID = nUID;
      if (mPollIrq && (mPollPauseAfterDetect>0)) {
        // stop polling for now
        haltIrqHandling();
        // resume after a pause
        mIrqTimer.executeOnce(boost::bind(&RFIDs::pollIrq, this, _1), mPollPauseAfterDetect);
      }
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
