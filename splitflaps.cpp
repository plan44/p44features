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

#include "splitflaps.hpp"

#if ENABLE_FEATURE_SPLITFLAPS

#include "application.hpp"

using namespace p44;

#define SBB_BUS_COMMPARAMS "19200,8,N,2"
#define SBB_CTRL_COMMPARAMS "9600,7,E,1"


#define FEATURE_NAME "splitflaps"

Splitflaps::Splitflaps(
  const char *aConnectionSpec, uint16_t aDefaultPort,
  const char *aTxEnablePinSpec, const char *aRxEnablePinSpec, MLMicroSeconds aOffDelay
) :
  inherited(FEATURE_NAME),
  mInterfaceType(interface_rs485bus),
  mSbbSerial(MainLoop::currentMainLoop()),
  mTxOffDelay(0),
  mTxEnableMode(txEnable_none),
  mOCtrlAddress(0), // none
  mOCtrlLines(2), // 2 lines (sides)...
  mOCtrlColumns(6), // ...and 6 columns (modules) is a standard "Gleisanzeiger"
  mOCtrlDirty(false)
{
  mSbbSerial.isMemberVariable();
  if (strcmp(aConnectionSpec,"simulation")==0) {
    // simulation mode
    mSimulation = true;
  }
  else {
    mSbbSerial.mSerialComm->setConnectionSpecification(aConnectionSpec, aDefaultPort, SBB_BUS_COMMPARAMS);
    // and want to accept extra bytes
    mSbbSerial.setExtraBytesHandler(boost::bind(&Splitflaps::acceptExtraBytes, this, _1, _2));
    //    // set accept buffer for re-assembling messages before processing
    //    setAcceptBuffer(100); // we don't know yet how long SBB messages can get
  }
  // Tx driver control
  mTxOffDelay = aOffDelay;
  if (strcmp(aTxEnablePinSpec, "DTR")==0) {
    mTxEnableMode = txEnable_dtr;
  }
  else if (strcmp(aTxEnablePinSpec, "RTS")==0) {
    mTxEnableMode = txEnable_rts;
  }
  else {
    // digital I/O line
    mTxEnableMode = txEnable_io;
    mTxEnable = DigitalIoPtr(new DigitalIo(aTxEnablePinSpec, true, false));
    mRxEnable = DigitalIoPtr(new DigitalIo(aRxEnablePinSpec, true, true));
  }
}


void Splitflaps::reset()
{
  mInterfaceType = interface_rs485bus;
  mSplitflapModules.clear();
  inherited::reset();
}


Splitflaps::~Splitflaps()
{
  reset();
}


// MARK: ==== splitflaps API

#define MAX_OCTRL_LINES 10
#define MAX_OCTRL_COLUMNS 100

// Standard SBB Gleisanzeiger layout with Omega Controller:
// Controller: 51 (Gleis70 module)
// Lines:      2 (front and back)
// Columns:    6 (modules)
//   0  1  2       3       4     5
//   hh:mm delay40 train62 via62 destination62


ErrorPtr Splitflaps::initialize(JsonObjectPtr aInitData)
{
  reset();
  // RS485 bus modules: { "cmd":"init", "splitflaps": { "modules":[ { "name":"name", "addr":xx, "type":"alphanum"|"hour"|"minute"|"40"|"62" } ] } }
  // Omega Controller: { "cmd":"init", "splitflaps": { "controller":51, "lines":2, "columns":6, "modules":[ { "name":"name", "addr":xx, "type":"alphanum"|"hour"|"minute"|"40"|"62" } ] } }
  ErrorPtr err;
  JsonObjectPtr o;
  if (aInitData->get("controller", o)) {
    // setting a controller address enables Omega Controller mode
    mInterfaceType = interface_omegacontroller;
    mOCtrlAddress = o->int32Value();
  }
  if (aInitData->get("lines", o)) {
    mOCtrlLines = (uint16_t)(o->int32Value());
    if (mOCtrlLines>MAX_OCTRL_LINES || mOCtrlLines<1) return TextError::err("lines must be 1..%d", MAX_OCTRL_LINES);
  }
  if (aInitData->get("columns", o)) {
    mOCtrlColumns = (uint16_t)(o->int32Value());
    if (mOCtrlLines>MAX_OCTRL_COLUMNS || mOCtrlLines<1) return TextError::err("columns must be 1..%d", MAX_OCTRL_COLUMNS);
  }
  if (aInitData->get("modules", o)) {
    for (int i=0; i<o->arrayLength(); i++) {
      JsonObjectPtr m = o->arrayGet(i);
      SplitflapModule module;
      JsonObjectPtr mp;
      if (!m->get("name", mp)) {
        return TextError::err("module must specify name");
      }
      else {
        module.mName = mp->stringValue();
        if (!m->get("addr", mp)) {
          return TextError::err("module must specify addr (RS485 addr or 100*line+column)");
        }
        else {
          module.mAddr = mp->int32Value();
          if (m->get("type", mp)) {
            string mtyp = mp->stringValue();
            if (mtyp=="alphanum") module.mType = moduletype_alphanum;
            else if (mtyp=="hour") module.mType = moduletype_hour;
            else if (mtyp=="minute") module.mType = moduletype_minute;
            else if (mtyp=="40") module.mType = moduletype_40;
            else if (mtyp=="62") module.mType = moduletype_62;
          }
          // add to list
          mSplitflapModules.push_back(module);
        }
      }
    }
  }
  initOperation();
  return err;
}


ErrorPtr Splitflaps::processRequest(ApiRequestPtr aRequest)
{
  ErrorPtr err;
  JsonObjectPtr data = aRequest->getRequest();
  JsonObjectPtr o = data->get("cmd");
  if (o) {
    string cmd = o->stringValue();
    // decode commands
    if (cmd=="raw") {
      // send raw command
      // { "cmd":"raw", "data":[ byte, byte, byte ...] }
      // { "cmd":"raw", "data":"hexstring" }
      // { "cmd":"raw", "data":"hexstring", "answer":3 }
      if (!data->get("data", o)) {
        return TextError::err("missing data");
      }
      else {
        string bytes;
        if (o->isType(json_type_string)) {
          // hex string of bytes
          bytes = hexToBinaryString(o->stringValue().c_str(), true);
        }
        else if (o->isType(json_type_array)) {
          // array of bytes
          size_t nb = o->arrayLength();
          for (int i=0; i<nb; i++) {
            bytes[i] = (uint8_t)(o->arrayGet(i)->int32Value());
          }
        }
        else {
          return TextError::err("specify command as array of bytes or hexstring");
        }
        // possibly we want an initiation delay
        MLMicroSeconds initiationDelay = -1; // standard
        if (data->get("delay", o)) initiationDelay = o->doubleValue()*Second;
        // possibly we want an answer
        size_t answerBytes = 0;
        if (data->get("answer", o)) answerBytes = o->int32Value();
        sendRawCommand(bytes, answerBytes, boost::bind(&Splitflaps::rawCommandAnswer, this, aRequest, _1, _2), initiationDelay);
        return ErrorPtr(); // handler will send reply
      }
    }
    else if (cmd=="position") {
      // set or read module position
      // { "cmd":"position", "name":name [, "value":value] }
      if (!data->get("name", o, true)) {
        return TextError::err("missing module name");
      }
      else {
        // find module
        SplitFlapModuleVector::iterator mpos;
        for (mpos=mSplitflapModules.begin(); mpos!=mSplitflapModules.end(); ++mpos) {
          if (mpos->mName==o->stringValue()) {
            // module found
            if (!data->get("value", o)) {
              // read back current module value
              JsonObjectPtr ans;
              uint8_t v = getModuleValue(*mpos);
              if (mpos->mType==moduletype_alphanum) {
                ans = JsonObject::newString((const char*)&v, 1);
              }
              else {
                ans = JsonObject::newInt32(v);
              }
              aRequest->sendResponse(ans, ErrorPtr());
            }
            int value = 0;
            if (o->isType(json_type_string) && mpos->mType==moduletype_alphanum) {
              value = o->c_strValue()[0]; // take first char's ASCII-code as value
            }
            else {
              value = o->int32Value();
            }
            setModuleValue(*mpos, value);
            return Error::ok();
          }
        }
        return TextError::err("module '%s' not found", o->c_strValue());
      }
    }
    else if (cmd=="info") {
      // TODO: implement
      return TextError::err("info not yet implemented");
    }
    return inherited::processRequest(aRequest);
  }
  else {
    // decode properties
    //%%% none yet
    return err ? err : Error::ok();
  }
}


void Splitflaps::rawCommandAnswer(ApiRequestPtr aRequest, const string &aResponse, ErrorPtr aError)
{
  aRequest->sendResponse(JsonObject::newString(binaryToHexString(aResponse, ' ')), aError);
}


JsonObjectPtr Splitflaps::status()
{
  JsonObjectPtr answer = inherited::status();
  if (answer->isType(json_type_object)) {
    JsonObjectPtr ms = JsonObject::newArray();
    for (SplitFlapModuleVector::iterator pos = mSplitflapModules.begin(); pos!=mSplitflapModules.end(); ++pos) {
      JsonObjectPtr m = JsonObject::newObj();
      m->add("name", JsonObject::newString(pos->mName));
      m->add("addr", JsonObject::newInt32(pos->mAddr));
      m->add("value", JsonObject::newInt32(pos->mLastSentValue));
      ms->arrayAppend(m);
    }
    answer->add("modules", ms);
    if (mInterfaceType==interface_omegacontroller) {
      answer->add("controller", JsonObject::newInt32(mOCtrlAddress));
      answer->add("lines", JsonObject::newInt32(mOCtrlLines));
      answer->add("columns", JsonObject::newInt32(mOCtrlColumns));
    }
  }
  return answer;
}


// MARK: - common RS485 interface

void Splitflaps::enableSendingImmediate(bool aEnable)
{
  switch(mTxEnableMode) {
    case txEnable_dtr:
      mSbbSerial.mSerialComm->setDTR(aEnable);
      return;
    case txEnable_rts:
      mSbbSerial.mSerialComm->setRTS(aEnable);
      return;
    case txEnable_io:
      mRxEnable->set(!aEnable);
      mTxEnable->set(aEnable);
      return;
    default:
      return; // NOP
  }
}


void Splitflaps::enableSending(bool aEnable)
{
  MainLoop::currentMainLoop().cancelExecutionTicket(mTxOffTicket);
  if (aEnable || mTxOffDelay==0) {
    enableSendingImmediate(aEnable);
  }
  else {
    mTxOffTicket.executeOnce(boost::bind(&Splitflaps::enableSendingImmediate, this, aEnable), mTxOffDelay);
  }
}


ssize_t Splitflaps::acceptExtraBytes(size_t aNumBytes, const uint8_t *aBytes)
{
  // got bytes with no command expecting them in particular
  if (LOGENABLED(LOG_INFO)) {
    string m;
    for (size_t i=0; i<aNumBytes; i++) {
      string_format_append(m, " %02X", aBytes[i]);
    }
    OLOG(LOG_NOTICE, "received extra bytes:%s", m.c_str());
  }
  return (ssize_t)aNumBytes;
}


// MARK: ==== common splitflaps operation API

void Splitflaps::initOperation()
{
  if (mSimulation) {
    OLOG(LOG_WARNING, "Simulation only, no output to serial interface!");
  }
  else {
    // open connection so we can receive from start
    if (mSbbSerial.mSerialComm->requestConnection()) {
      mSbbSerial.mSerialComm->setRTS(false); // not sending
    }
    else {
      OLOG(LOG_WARNING, "Could not open serial connection");
    }
  }
  // anyway, start operation
  if (mInterfaceType==interface_rs485bus) {
    initBusOperation();
  }
  else if (mInterfaceType==interface_omegacontroller) {
    initCtrlOperation();
  }
  setInitialized();
}


void Splitflaps::setModuleValue(SplitflapModule& aSplitFlapModule, uint8_t aValue)
{
  aSplitFlapModule.mLastSentValue = aValue;
  if (mInterfaceType==interface_rs485bus) {
    setModuleValueBus((uint8_t)aSplitFlapModule.mAddr, aSplitFlapModule.mType, aValue);
  }
  else if (mInterfaceType==interface_omegacontroller) {
    setModuleValueCtrl(aSplitFlapModule.mAddr / 100, aSplitFlapModule.mAddr % 100, aSplitFlapModule.mType, aValue);
  }
}


uint8_t Splitflaps::getModuleValue(SplitflapModule& aSplitFlapModule)
{
  return aSplitFlapModule.mLastSentValue;
}


void Splitflaps::sendRawCommand(const string aCommand, size_t aExpectedBytes, SBBResultCB aResultCB, MLMicroSeconds aInitiationDelay)
{
  if (mInterfaceType==interface_rs485bus) {
    sendRawBusCommand(aCommand, aExpectedBytes, aResultCB, aInitiationDelay);
  }
  else if (mInterfaceType==interface_omegacontroller) {
    sendRawCtrlCommand(aCommand, aResultCB);
  }
}



// MARK: - RS485 bus modules


// SBB RS485 protocol
#define SBB_SYNCBYTE 0xFF // all commands start with this
#define SBB_CMD_SETPOS 0xC0 // set position
#define SBB_CMD_GETPOS 0xD0 // get position
#define SBB_CMD_GETSERIAL 0xDF // get serial number


void Splitflaps::initBusOperation()
{
  // we need a non-standard transmitter
  mSbbSerial.setTransmitter(boost::bind(&Splitflaps::sbbBusTransmitter, this, _1, _2));
}



size_t Splitflaps::sbbBusTransmitter(size_t aNumBytes, const uint8_t *aBytes)
{
  ssize_t res = 0;
  ErrorPtr err = mSbbSerial.mSerialComm->establishConnection();
  if (Error::isOK(err)) {
    OLOG(LOG_NOTICE, "transmitting bytes: %s", dataToHexString(aBytes, aNumBytes, ' ').c_str());
    // enable sending
    enableSending(true);
    // send break
    mSbbSerial.mSerialComm->sendBreak();
    // now let standard transmitter do the rest
    res = mSbbSerial.standardTransmitter(aNumBytes, aBytes);
    // disable sending
    enableSending(false);
  }
  else {
    OLOG(LOG_DEBUG, "sbbTransmitter error - connection could not be established!");
  }
  return res;
}


#define STANDARD_INITIATION_DELAY (0.2*Second)

void Splitflaps::sendRawBusCommand(const string aCommand, size_t aExpectedBytes, SBBResultCB aResultCB, MLMicroSeconds aInitiationDelay)
{
  if (mSimulation) {
    OLOG(LOG_NOTICE, "Simulation only, NOT sending command: %s", binaryToHexString(aCommand, ' ').c_str());
    if (aResultCB) aResultCB("", ErrorPtr());
    return; // NOP
  }
  if (aInitiationDelay<0) aInitiationDelay = STANDARD_INITIATION_DELAY;
  OLOG(LOG_INFO, "Posting command (size=%zu)", aCommand.size());
  SerialOperationSendPtr req = SerialOperationSendPtr(new SerialOperationSend);
  req->setDataSize(aCommand.size());
  req->appendData(aCommand.size(), (uint8_t *)aCommand.c_str());
  req->setInitiationDelay(aInitiationDelay);
  if (aExpectedBytes>0) {
    // we expect some answer bytes
    SerialOperationReceivePtr resp = SerialOperationReceivePtr(new SerialOperationReceive);
    resp->setCompletionCallback(boost::bind(&Splitflaps::sbbBusCommandComplete, this, aResultCB, resp, _1));
    resp->setExpectedBytes(aExpectedBytes);
    resp->setTimeout(2*Second);
    req->setChainedOperation(resp);
  }
  else {
    req->setCompletionCallback(boost::bind(&Splitflaps::sbbBusCommandComplete, this, aResultCB, SerialOperationPtr(), _1));
  }
  mSbbSerial.queueSerialOperation(req);
  // process operations
  mSbbSerial.processOperations();
}


void Splitflaps::sbbBusCommandComplete(SBBResultCB aResultCB, SerialOperationPtr aSerialOperation, ErrorPtr aError)
{
  OLOG(LOG_INFO, "Command complete");
  string result;
  if (Error::isOK(aError)) {
    SerialOperationReceivePtr resp = boost::dynamic_pointer_cast<SerialOperationReceive>(aSerialOperation);
    if (resp) {
      result.assign((char *)resp->getDataP(), resp->getDataSize());
    }
  }
  if (aResultCB) aResultCB(result, aError);
}


void Splitflaps::setModuleValueBus(uint8_t aModuleAddr, SbbModuleType aType, uint8_t aValue)
{
  uint8_t pos;
  switch (aType) {
    case moduletype_alphanum :
      // use characters. Order in module is
      // ABCDEFGHIJKLMNOPQRSTUVWXYZ/-1234567890.<space>
      // 0123456789012345678901234567890123456789
      // 0         1         2         3        3
      if (aValue>='A' && aValue<='Z') {
        pos = aValue-'A';
      }
      else if (aValue=='/') {
        pos = 26;
      }
      else if (aValue=='-') {
        pos = 27;
      }
      else if (aValue>='1' && aValue<='9') {
        pos = aValue-'1'+28;
      }
      else if (aValue=='.') {
        pos = 38;
      }
      else {
        // everything else: space
        pos = 39;
      }
      break;
    case moduletype_hour :
      // hours 0..23, >23 = space
      pos = aValue>23 ? 24 : aValue;
      break;
    case moduletype_minute :
      // 0..59, >59 = space
      // pos 0..28 are minutes 31..59
      // pos 29 is space
      // pos 30..60 are minutes 00..30
      // pos 61 is space
      pos = aValue>59 ? 29 : (aValue<31 ? 30+aValue : aValue-31);
      break;
    case moduletype_40 :
    case moduletype_62 :
      // just use as-is
      pos = aValue;
      break;
  }
  string poscmd = "\xFF\xC0";
  poscmd += (char)aModuleAddr;
  poscmd += (char)pos;
  sendRawBusCommand(poscmd, 0, NoOP, -1 /* default */);
}


// MARK: Omega Controller

void Splitflaps::initCtrlOperation()
{
  // Set standard transmitter
  mSbbSerial.setTransmitter(boost::bind(&SerialOperationQueue::standardTransmitter, &mSbbSerial, _1, _2));
  // prepare buffer, fill with spaces
  mOCtrlData.assign((mOCtrlLines+1)*(mOCtrlColumns+1), 0x20);
  setCtrlDirty();
}


#define CTRL_UPDATING_DELAY (0.5*Second)

void Splitflaps::setCtrlDirty()
{
  if (mOCtrlDirty) return; // already dirty, will get sent anyway
  // not yet dirty, open a new update collection time window
  mOCtrlDirty = true;
  mOCtrlUpdater.cancel();
  // schedule update to hardware
  mOCtrlUpdater.executeOnce(boost::bind(&Splitflaps::updateCtrlDisplay, this), CTRL_UPDATING_DELAY);
}


void Splitflaps::sendCtrlCommand(const char *aCmd, SBBResultCB aResultCB)
{
  // general format is ^A ^R moduleaddress ^B actual command ^D
  string msg = string_format("\x01\x12%03d\x02%s\x04", mOCtrlAddress, aCmd);
  sendRawCtrlCommand(msg.c_str(), aResultCB);
}


void Splitflaps::updateCtrlDisplay()
{
  if (mOCtrlDirty) {
    mOCtrlDirty = false;
    string msg = "\x08"; // ^H - go to beginning of "screen"
    for (int l=0; l<mOCtrlLines; ++l) {
      for (int c=0; c<mOCtrlColumns; ++c) {
        msg += mOCtrlData[l*mOCtrlColumns+c];
      }
      if (l<mOCtrlLines-1) msg+='\x0A'; // ^J go to next "line"
    }
    sendCtrlCommand(msg.c_str(), NoOP);
  }
}


void Splitflaps::sendRawCtrlCommand(const string aCommand, SBBResultCB aResultCB)
{
  size_t numBytes = aCommand.size();
  if (LOGENABLED(LOG_INFO)) {
    string m;
    for (size_t i=0; i<numBytes; i++) {
      if (aCommand[i]>=0x20) {
        m += aCommand[i];
      }
      else {
        m += "^";
        m += aCommand[i]+'A'-1;
      }
    }
    OLOG(LOG_INFO, "transmitting command: %s", m.c_str());
  }
  if (mSimulation) {
    if (aResultCB) aResultCB("", ErrorPtr());
    OLOG(LOG_INFO, "Simulated command complete");
    return; // NOP
  }
  OLOG(LOG_INFO, "Posting command");
  SerialOperationSendPtr op = SerialOperationSendPtr(new SerialOperationSend);
  op->setDataSize(numBytes);
  op->appendData(numBytes, (uint8_t*)aCommand.c_str());
  op->setCompletionCallback(boost::bind(&Splitflaps::sbbCtrlCommandComplete, this, aResultCB, _1));
  mSbbSerial.queueSerialOperation(op);
  // process operations
  mSbbSerial.processOperations();
}


void Splitflaps::sbbCtrlCommandComplete(SBBResultCB aResultCB, ErrorPtr aError)
{
  OLOG(LOG_INFO, "Command complete");
  if (aResultCB) aResultCB("", aError);
}


void Splitflaps::setModuleValueCtrl(uint8_t aLine, uint8_t aColumn, SbbModuleType aType, uint8_t aValue)
{
  if (aLine>=mOCtrlLines || aColumn>=mOCtrlColumns) return;
  uint8_t val;
  switch (aType) {
    case moduletype_alphanum :
      // use characters as-is. Modules can display A-Z, ', -, 0-9
      val = aValue<0x20 ? 0x20 : aValue;
      break;
    case moduletype_hour :
      // hours 0..23 are represented by A..X, >23 = space
      val = aValue>23 ? 0x20 : 'A'+aValue;
      break;
    case moduletype_minute :
      // minutes 0..30 are represented by A-Z,[\]^_
      // minutes 31..59 are represented by !..=
      // >59 = space
      val = aValue>59 ? 0x20 : (aValue<31 ? 'A'+aValue : '!'+(aValue-31));
      break;
    case moduletype_40 :
      // position 0 is represented by space
      // positions 1..26 are represented by A-Z
      // position 27 is represented by ' (apostrophe, single quote)
      // position 28 is represented by - (minus, dash)
      // position 29-37 are represented by 1..9
      // position 38 is represented by 0
      // position 39 is represented by ???? (unknown at this time)
      if (aValue==0)
        val = 0x20;
      else if (aValue<=26)
        val = 'A'+(aValue-1);
      else if (aValue==27)
        val = '\'';
      else if (aValue==28)
        val = '-';
      else if (aValue<=37)
        val = '1'+(aValue-29);
      else if (aValue==38)
        val = '0';
      else
        val = 0x20; // 39 is missing so far!
      break;
    case moduletype_62 :
      // positions 0..30 are represented by ASCII Space..>
      // positions 31..61 are represented by ASCII A.._
      val = aValue<31 ? 0x20+aValue : 'A'+(aValue-31);
      break;
  }
  size_t bufPos = aLine*mOCtrlColumns+aColumn;
  if (val!=mOCtrlData[bufPos]) {
    mOCtrlData[bufPos] = val;
    setCtrlDirty();
  }
}




#endif // ENABLE_FEATURE_SPLITFLAPS
