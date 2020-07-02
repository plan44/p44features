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

#include "splitflaps.hpp"

#if ENABLE_FEATURE_SPLITFLAPS

#include "application.hpp"

using namespace p44;

#define SBB_COMMPARAMS "19200,8,N,2"

// SBB RS485 protocol
#define SBB_SYNCBYTE 0xFF // all commands start with this
#define SBB_CMD_SETPOS 0xC0 // set position
#define SBB_CMD_GETPOS 0xD0 // get position
#define SBB_CMD_GETSERIAL 0xDF // get serial number

#define FEATURE_NAME "splitflaps"

Splitflaps::Splitflaps(
  const char *aConnectionSpec, uint16_t aDefaultPort,
  const char *aTxEnablePinSpec, const char *aRxEnablePinSpec, MLMicroSeconds aOffDelay
) :
  inherited(FEATURE_NAME),
  sbbSerial(MainLoop::currentMainLoop()),
  txOffDelay(0),
  txEnableMode(txEnable_none)
{
  sbbSerial.isMemberVariable();
  if (strcmp(aConnectionSpec,"simulation")==0) {
    // simulation mode
  }
  else {
    sbbSerial.serialComm->setConnectionSpecification(aConnectionSpec, aDefaultPort, SBB_COMMPARAMS);
    // we need a non-standard transmitter
    sbbSerial.setTransmitter(boost::bind(&Splitflaps::sbbTransmitter, this, _1, _2));
    // and want to accept extra bytes
    sbbSerial.setExtraBytesHandler(boost::bind(&Splitflaps::acceptExtraBytes, this, _1, _2));
    //    // set accept buffer for re-assembling messages before processing
    //    setAcceptBuffer(100); // we don't know yet how long SBB messages can get
  }
  // Tx driver control
  txOffDelay = aOffDelay;
  if (strcmp(aTxEnablePinSpec, "DTR")==0) {
    txEnableMode = txEnable_dtr;
  }
  else if (strcmp(aTxEnablePinSpec, "RTS")==0) {
    txEnableMode = txEnable_rts;
  }
  else {
    // digital I/O line
    txEnableMode = txEnable_io;
    txEnable = DigitalIoPtr(new DigitalIo(aTxEnablePinSpec, true, false));
    rxEnable = DigitalIoPtr(new DigitalIo(aRxEnablePinSpec, true, true));
  }
}


void Splitflaps::reset()
{
  splitflapModules.clear();
  inherited::reset();
}


Splitflaps::~Splitflaps()
{
  reset();
}


// MARK: ==== splitflaps API


ErrorPtr Splitflaps::initialize(JsonObjectPtr aInitData)
{
  reset();
  // { "cmd":"init", "splitflaps": { "modules":[ { "name":"name", "addr":xx, "type":"alphanum"|"hour"|"minute"|"40"|"62" } ] } }
  ErrorPtr err;
  JsonObjectPtr o;
  if (aInitData->get("modules", o)) {
    for (int i=0; i<o->arrayLength(); i++) {
      JsonObjectPtr m = o->arrayGet(i);
      SplitflapModule module;
      JsonObjectPtr mp;
      if (!m->get("name", mp)) {
        return TextError::err("module must specify name");
      }
      else {
        module.name = mp->stringValue();
        if (!m->get("addr", mp)) {
          return TextError::err("module must specify addr");
        }
        else {
          module.addr = mp->int32Value();
          if (m->get("type", mp)) {
            string mtyp = mp->stringValue();
            if (mtyp=="alphanum") module.type = moduletype_alphanum;
            else if (mtyp=="hour") module.type = moduletype_hour;
            else if (mtyp=="minute") module.type = moduletype_minute;
            else if (mtyp=="40") module.type = moduletype_40;
            else if (mtyp=="62") module.type = moduletype_62;
          }
          // add to list
          splitflapModules.push_back(module);
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
        // possibly we want an answer
        size_t answerBytes = 0;
        if (data->get("answer", o)) answerBytes = o->int32Value();
        sendRawCommand(bytes, answerBytes, boost::bind(&Splitflaps::rawCommandAnswer, this, aRequest, _1, _2));
        return ErrorPtr(); // handler will send reply
      }
    }
    else if (cmd=="position") {
      // set or read module position
      // { "cmd":"position", "name":name [, "value":value] }
      if (!data->get("name", o)) {
        return TextError::err("missing module name");
      }
      else {
        // find module
        SplitFlapModuleVector::iterator mpos;
        for (mpos=splitflapModules.begin(); mpos!=splitflapModules.end(); ++mpos) {
          if (mpos->name==o->stringValue()) {
            // module found
            if (!data->get("value", o)) {
              // FIXME: implement read back
              return TextError::err("reading module position not yet implemented");
            }
            int value = 0;
            if (o->isType(json_type_string) && mpos->type==moduletype_alphanum) {
              value = o->c_strValue()[0]; // take first char's ASCII-code as value
            }
            else {
              value = o->int32Value();
            }
            setModuleValue(mpos->addr, mpos->type, value);
            return Error::ok();
          }
        }
        return TextError::err("module '%s' not found", o->c_strValue());
      }
    }
    else if (cmd=="info") {

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
    for (SplitFlapModuleVector::iterator pos = splitflapModules.begin(); pos!=splitflapModules.end(); ++pos) {
      JsonObjectPtr m = JsonObject::newObj();
      m->add("name", JsonObject::newString(pos->name));
      m->add("addr", JsonObject::newInt32(pos->addr));
      ms->arrayAppend(m);
    }
    answer->add("modules", ms);
  }
  return answer;
}


// MARK: ==== splitflaps operation


void Splitflaps::initOperation()
{
  // open connection so we can receive from start
  if (sbbSerial.serialComm->requestConnection()) {
    sbbSerial.serialComm->setRTS(false); // not sending
  }
  else {
    OLOG(LOG_WARNING, "Could not open serial connection");
  }
  setInitialized();
}


void Splitflaps::enableSendingImmediate(bool aEnable)
{
  switch(txEnableMode) {
    case txEnable_dtr:
      sbbSerial.serialComm->setDTR(aEnable);
      return;
    case txEnable_rts:
      sbbSerial.serialComm->setRTS(aEnable);
      return;
    case txEnable_io:
      rxEnable->set(!aEnable);
      txEnable->set(aEnable);
      return;
    default:
      return; // NOP
  }
}


void Splitflaps::enableSending(bool aEnable)
{
  MainLoop::currentMainLoop().cancelExecutionTicket(txOffTicket);
  if (aEnable || txOffDelay==0) {
    enableSendingImmediate(aEnable);
  }
  else {
    txOffTicket.executeOnce(boost::bind(&Splitflaps::enableSendingImmediate, this, aEnable), txOffDelay);
  }
}



size_t Splitflaps::sbbTransmitter(size_t aNumBytes, const uint8_t *aBytes)
{
  ssize_t res = 0;
  ErrorPtr err = sbbSerial.serialComm->establishConnection();
  if (Error::isOK(err)) {
    if (LOGENABLED(LOG_NOTICE)) {
      string m;
      for (size_t i=0; i<aNumBytes; i++) {
        string_format_append(m, " %02X", aBytes[i]);
      }
      OLOG(LOG_NOTICE, "transmitting bytes:%s", m.c_str());
    }
    // enable sending
    enableSending(true);
    // send break
    sbbSerial.serialComm->sendBreak();
    // now let standard transmitter do the rest
    res = sbbSerial.standardTransmitter(aNumBytes, aBytes);
    // disable sending
    enableSending(false);
  }
  else {
    OLOG(LOG_DEBUG, "sbbTransmitter error - connection could not be established!");
  }
  return res;
}


void Splitflaps::sendRawCommand(const string aCommand, size_t aExpectedBytes, SBBResultCB aResultCB, MLMicroSeconds aInitiationDelay)
{
  OLOG(LOG_INFO, "Posting command (size=%zu)", aCommand.size());
  SerialOperationSendPtr req = SerialOperationSendPtr(new SerialOperationSend);
  req->setDataSize(aCommand.size());
  req->appendData(aCommand.size(), (uint8_t *)aCommand.c_str());
  req->setInitiationDelay(aInitiationDelay);
  if (aExpectedBytes>0) {
    // we expect some answer bytes
    SerialOperationReceivePtr resp = SerialOperationReceivePtr(new SerialOperationReceive);
    resp->setCompletionCallback(boost::bind(&Splitflaps::sbbCommandComplete, this, aResultCB, resp, _1));
    resp->setExpectedBytes(aExpectedBytes);
    resp->setTimeout(2*Second);
    req->setChainedOperation(resp);
  }
  else {
    req->setCompletionCallback(boost::bind(&Splitflaps::sbbCommandComplete, this, aResultCB, SerialOperationPtr(), _1));
  }
  sbbSerial.queueSerialOperation(req);
  // process operations
  sbbSerial.processOperations();
}


void Splitflaps::sbbCommandComplete(SBBResultCB aResultCB, SerialOperationPtr aSerialOperation, ErrorPtr aError)
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


void Splitflaps::setModuleValue(uint8_t aModuleAddr, SbbModuleType aType, uint8_t aValue)
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
  sendRawCommand(poscmd, 0, NULL);
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


#endif // ENABLE_FEATURE_SPLITFLAPS
