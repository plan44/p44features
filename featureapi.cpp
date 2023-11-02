//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2016-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Authors: Ueli Wahlen <ueli@hotmail.com>, Lukas Zeller <luz@plan44.ch>
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

#include "featureapi.hpp"

#include "feature.hpp"

#if ENABLE_FEATURE_LIGHT
  #include "light.hpp"
#endif
#if ENABLE_FEATURE_INPUTS
  #include "inputs.hpp"
#endif
#if ENABLE_FEATURE_KEYEVENTS
  #include "keyevents.hpp"
#endif
#if ENABLE_FEATURE_DISPMATRIX
  #include "dispmatrix.hpp"
#endif
#if ENABLE_FEATURE_INDICATORS
  #include "indicators.hpp"
#endif
#if ENABLE_FEATURE_SPLITFLAPS
  #include "splitflaps.hpp"
#endif
#if ENABLE_FEATURE_RFIDS
  #include "rfids.hpp"
#endif
#if ENABLE_FEATURE_WIFITRACK
  #include "wifitrack.hpp"
#endif
#if ENABLE_FEATURE_NEURON
  #include "neuron.hpp"
#endif
#if ENABLE_FEATURE_HERMEL
  #include "hermel.hpp"
#endif
#if ENABLE_FEATURE_MIXLOOP
  #include "mixloop.hpp"
#endif


#include "extutils.hpp"
#include "macaddress.hpp"
#include "application.hpp"

using namespace p44;


// MARK: ===== FeatureApiRequest

FeatureApiRequest::FeatureApiRequest(JsonObjectPtr aRequest, JsonCommPtr aConnection) :
  inherited(aRequest),
  mConnection(aConnection)
{
}


FeatureApiRequest::~FeatureApiRequest()
{
}


void FeatureApiRequest::sendResponse(JsonObjectPtr aResponse, ErrorPtr aError)
{
  if (!Error::isOK(aError)) {
    aResponse = JsonObject::newObj();
    aResponse->add("Error", JsonObject::newString(aError->description()));
  }
  if (mConnection) mConnection->sendMessage(aResponse);
  SOLOG(*FeatureApi::sharedApi(), LOG_INFO,"answer: %s", aResponse->c_strValue());
}



// MARK: ===== InternalRequest

InternalRequest::InternalRequest(JsonObjectPtr aRequest) :
  inherited(aRequest)
{
  SOLOG(*FeatureApi::sharedApi(), LOG_DEBUG,"Internal request: %s", aRequest->c_strValue());
}


InternalRequest::~InternalRequest()
{
}


void InternalRequest::sendResponse(JsonObjectPtr aResponse, ErrorPtr aError)
{
  SOLOG(*FeatureApi::sharedApi(), LOG_DEBUG,"Internal answer: %s", aResponse->c_strValue());
}



// MARK: ===== APICallbackRequest

APICallbackRequest::APICallbackRequest(JsonObjectPtr aRequest, RequestDoneCB aRequestDoneCB) :
  inherited(aRequest),
  mRequestDoneCB(aRequestDoneCB)
{
}


APICallbackRequest::~APICallbackRequest()
{
}


void APICallbackRequest::sendResponse(JsonObjectPtr aResponse, ErrorPtr aError)
{
  if (mRequestDoneCB) mRequestDoneCB(aResponse, aError);
}



// MARK: ===== FeatureApi

static FeatureApiPtr featureApi;

FeatureApiPtr FeatureApi::existingSharedApi()
{
  return featureApi;
}


FeatureApiPtr FeatureApi::sharedApi()
{
  if (!featureApi) {
    featureApi = FeatureApiPtr(new FeatureApi);
  }
  return featureApi;
}


FeatureApi::FeatureApi()
{
}


FeatureApi::~FeatureApi()
{
}


// MARK: ==== legacy JSON scripting

#if ENABLE_LEGACY_FEATURE_SCRIPTS

ErrorPtr FeatureApi::runJsonFile(const string aScriptPath, SimpleCB aFinishedCallback, FeatureJsonScriptContextPtr* aContextP, SubstitutionMap* aSubstitutionsP)
{
  ErrorPtr err;
  string jsonText;
  string fpath = Application::sharedApplication()->resourcePath(aScriptPath).c_str();
  err = string_fromfile(fpath, jsonText);
  if (Error::notOK(err)) {
    err->prefixMessage("cannot open JSON file '%s': ", fpath.c_str());
    OLOG(LOG_WARNING, "Script loading error: %s", Error::text(err));
  }
  else {
    err = runJsonString(jsonText, aFinishedCallback, aContextP, aSubstitutionsP);
  }
  return err;
}


void FeatureApi::substituteVars(string &aString, SubstitutionMap *aSubstitutionsP, ErrorPtr &err)
{
  if (aSubstitutionsP) {
    // perform substitution: syntax of placeholders:
    //   @{name}
    size_t p = 0;
    while ((p = aString.find("@{",p))!=string::npos) {
      size_t e = aString.find("}",p+2);
      if (e==string::npos) {
        // syntactically incorrect, no closing "}"
        err = TextError::err("unterminated placeholder: %s", aString.c_str()+p);
        break;
      }
      string var = aString.substr(p+2,e-2-p);
      SubstitutionMap::iterator pos = aSubstitutionsP->find(var);
      if (pos==aSubstitutionsP->end()) {
        err = TextError::err("unknown placeholder: %s", var.c_str());
        break;
      }
      // replace, even if rep is empty
      aString.replace(p, e-p+1, pos->second);
      p+=pos->second.size();
    }
  }
}


ErrorPtr FeatureApi::runJsonString(string aJsonString, SimpleCB aFinishedCallback, FeatureJsonScriptContextPtr* aContextP, SubstitutionMap* aSubstitutionsP)
{
  ErrorPtr err;
  substituteVars(aJsonString, aSubstitutionsP, err);
  if (Error::isOK(err)) {
    JsonObjectPtr script = JsonObject::objFromText(aJsonString.c_str(), -1, &err);
    if (Error::isOK(err) && script) {
      err = featureApi->executeJson(script, aFinishedCallback, aContextP);
    }
  }
  if (!Error::isOK(err)) { OLOG(LOG_WARNING, "Script execution error: %s", Error::text(err)); }
  return err;
}


ErrorPtr FeatureApi::executeJson(JsonObjectPtr aJsonCmds, SimpleCB aFinishedCallback, FeatureJsonScriptContextPtr* aContextP)
{
  JsonObjectPtr cmds;
  if (!aJsonCmds->isType(json_type_array)) {
    cmds = JsonObject::newArray();
    cmds->arrayAppend(aJsonCmds);
  }
  else {
    cmds = aJsonCmds;
  }
  FeatureJsonScriptContextPtr context;
  if (aContextP && *aContextP) {
    context = *aContextP;
  }
  else {
    context = FeatureJsonScriptContextPtr(new FeatureJsonScriptContext);
    if (aContextP) *aContextP = context;
  }
  context->kill();
  executeNextCmd(cmds, 0, context, aFinishedCallback);
  return ErrorPtr();
}


void FeatureApi::executeNextCmd(JsonObjectPtr aCmds, int aIndex, FeatureJsonScriptContextPtr aContext, SimpleCB aFinishedCallback)
{
  if (!aCmds || aIndex>=aCmds->arrayLength()) {
    // done
    if (aFinishedCallback) aFinishedCallback();
    return;
  }
  // run next command
  MLMicroSeconds delay = 0;
  JsonObjectPtr cmd = aCmds->arrayGet(aIndex);
  // check for delay
  JsonObjectPtr o = cmd->get("delayby");
  if (o) {
    delay = o->doubleValue()*Second;
  }
  // now execute
  aContext->scriptTicket.executeOnce(boost::bind(&FeatureApi::runCmd, this, aCmds, aIndex, aContext, aFinishedCallback), delay);
}


void FeatureApi::runCmd(JsonObjectPtr aCmds, int aIndex, FeatureJsonScriptContextPtr aContext, SimpleCB aFinishedCallback)
{
  JsonObjectPtr cmd = aCmds->arrayGet(aIndex);
  JsonObjectPtr o = cmd->get("callscript");
  if (o) {
    runJsonFile(o->stringValue(), boost::bind(&FeatureApi::executeNextCmd, this, aCmds, aIndex+1, aContext, aFinishedCallback), &aContext);
    return;
  }
  ApiRequestPtr req = ApiRequestPtr(new InternalRequest(cmd));
  ErrorPtr err = processRequest(req);
  if (!Error::isOK(err)) { OLOG(LOG_WARNING, "script step execution error: %s", Error::text(err)); }
  executeNextCmd(aCmds, aIndex+1, aContext, aFinishedCallback);
}


ErrorPtr FeatureApi::call(ApiRequestPtr aRequest)
{
  JsonObjectPtr reqData = aRequest->getRequest();
  JsonObjectPtr o;
  // check for subsititutions
  FeatureApi::SubstitutionMap subst;
  o = reqData->get("substitutions");
  if (o) {
    string var;
    JsonObjectPtr val;
    o->resetKeyIteration();
    while (o->nextKeyValue(var, val)) {
      subst[var] = val->stringValue();
    }
  }
  o = reqData->get("script");
  if (o) {
    string scriptName = o->stringValue();
    ErrorPtr err = runJsonFile(scriptName, NoOP, NULL, &subst);
    if (Error::isOK(err)) return Error::ok();
    return err;
  }
  o = reqData->get("scripttext");
  if (o) {
    string scriptText = o->stringValue();
    ErrorPtr err = runJsonString(scriptText, NoOP, NULL, &subst);
    if (Error::isOK(err)) return Error::ok();
    return err;
  }
  o = reqData->get("json");
  if (o) {
    ErrorPtr err = executeJson(o, NoOP, NULL);
    if (Error::isOK(err)) return Error::ok();
    return err;
  }
  return FeatureApiError::err("missing 'script', 'scripttext' or 'json' attribute");
}


#endif // ENABLE_LEGACY_FEATURE_SCRIPTS


// MARK: ==== feature API


FeaturePtr FeatureApi::getFeature(const string aFeatureName)
{
  FeatureMap::iterator pos = mFeatureMap.find(aFeatureName);
  if (pos==mFeatureMap.end()) return FeaturePtr();
  return pos->second;
}




void FeatureApi::addFeature(FeaturePtr aFeature)
{
  mFeatureMap[aFeature->getName()] = aFeature;
}


SocketCommPtr FeatureApi::apiConnectionHandler(SocketCommPtr aServerSocketComm)
{
  JsonCommPtr conn = JsonCommPtr(new JsonComm(MainLoop::currentMainLoop()));
  conn->setMessageHandler(boost::bind(&FeatureApi::apiRequestHandler, this, conn, _1, _2));
  conn->setClearHandlersAtClose(); // close must break retain cycles so this object won't cause a mem leak

  mConnection = conn;
  return conn;
}


void FeatureApi::apiRequestHandler(JsonCommPtr aConnection, ErrorPtr aError, JsonObjectPtr aRequest)
{
  if (Error::isOK(aError)) {
    OLOG(LOG_INFO,"request: %s", aRequest->c_strValue());
    ApiRequestPtr req = ApiRequestPtr(new FeatureApiRequest(aRequest, aConnection));
    aError = processRequest(req);
  }
  if (!Error::isOK(aError)) {
    // error
    JsonObjectPtr resp = JsonObject::newObj();
    resp->add("Error", JsonObject::newString(aError->description()));
    aConnection->sendMessage(resp);
    OLOG(LOG_INFO,"answer: %s", resp->c_strValue());
  }
}


void FeatureApi::handleRequest(ApiRequestPtr aRequest)
{
  ErrorPtr err = processRequest(aRequest);
  if (err) {
    // something to send (empty response or error)
    if (err->isOK()) err.reset(); // do not send explicit errors, just empty response
    aRequest->sendResponse(JsonObject::newObj(), err);
  }
}



ErrorPtr FeatureApi::processRequest(ApiRequestPtr aRequest)
{
  JsonObjectPtr reqData = aRequest->getRequest();
  JsonObjectPtr o;
  // first check for feature selector
  if (reqData->get("feature", o, true)) {
    if (!o->isType(json_type_string)) {
      return FeatureApiError::err("'feature' attribute must be a string");
    }
    string featurename = o->stringValue();
    FeatureMap::iterator f = mFeatureMap.find(featurename);
    if (f==mFeatureMap.end()) {
      #if ENABLE_P44SCRIPT
      if (mUnhandledRequestSource.hasSinks()) {
        OLOG(LOG_NOTICE, "call for internally unknown feature '%s' -> let script check", featurename.c_str());
        // let scripted feature handler process unknown feature
        mUnhandledRequestSource.sendEvent(new FeatureRequestObj(aRequest));
        return ErrorPtr(); // no default response, event handler must send it
      }
      #endif
      return FeatureApiError::err("unknown feature '%s'", featurename.c_str());
    }
    if (!f->second->isInitialized()) {
      return FeatureApiError::err("feature '%s' is not yet initialized", featurename.c_str());
    }
    // let feature handle it
    ErrorPtr err = f->second->processRequest(aRequest);
    if (!Error::isOK(err)) {
      err->prefixMessage("Feature '%s' cannot process request: ", featurename.c_str());
    }
    return err;
  }
  else {
    // must be global command
    #if ENABLE_P44SCRIPT
    if (reqData->get("run", o, true)) {
      // directly run a script.
      // Note: this is not for testing/debugging/REPL purposes, but just to fire some script commands
      //   Basic p44script edit/debug infrastructure is not implemented as part of the feature API
      ScriptHost src(sourcecode|regular|keepvars|queue|ephemeralSource, "api:run", "%T (%O)");
      src.setSource(o->stringValue());
      src.run(inherit, boost::bind(&FeatureApi::scriptExecHandler, this, aRequest, _1));
      return Error::ok();
    }
    if (reqData->get("event", o, true)) {
      // inject a event, which may be processed via featureevent() in scripts, but is NOT sent (back) to the API client
      o->add("feature", JsonObject::newString("apievent")); // not really originating from a feature, but this api call
      sendEventMessageInternally(o);
      return Error::ok();
    }
    #endif // ENABLE_P44SCRIPT
    if (!reqData->get("cmd", o, true)) {
      return FeatureApiError::err("missing 'feature' or 'cmd' attribute");
    }
    string cmd = o->stringValue();
    if (cmd=="nop") {
      // no operation (e.g. script steps that only wait)
      return Error::ok();
    }
    #if ENABLE_LEGACY_FEATURE_SCRIPTS
    if (cmd=="call") {
      return call(aRequest);
    }
    #endif
    if (cmd=="init") {
      return init(aRequest);
    }
    else if (cmd=="reset") {
      return reset(aRequest);
    }
    else if (cmd=="now") {
      return now(aRequest);
    }
    else if (cmd=="status") {
      return status(aRequest);
    }
    else if (cmd=="ping") {
      return ping(aRequest);
    }
    #if ENABLE_P44SCRIPT
    else if (mUnhandledRequestSource.hasSinks()) {
      OLOG(LOG_NOTICE, "call for internally unknown cmd '%s' -> let script check", cmd.c_str());
      // let scripted feature handler process unknown command
      mUnhandledRequestSource.sendEvent(new FeatureRequestObj(aRequest));
      return ErrorPtr(); // no default response, event handler must send it
    }
    #endif
    else {
      return FeatureApiError::err("unknown global command '%s'", cmd.c_str());
    }
  }
}


ErrorPtr FeatureApi::reset(ApiRequestPtr aRequest)
{
  bool featureFound = false;
  for (FeatureMap::iterator f = mFeatureMap.begin(); f!=mFeatureMap.end(); ++f) {
    if (aRequest->getRequest()->get(f->first.c_str())) {
      featureFound = true;
      OLOG(LOG_NOTICE, "resetting feature '%s'", f->first.c_str());
      f->second->reset();
    }
  }
  if (!featureFound) {
    return FeatureApiError::err("reset does not address any known features");
  }
  return Error::ok(); // cause empty response
}



ErrorPtr FeatureApi::init(ApiRequestPtr aRequest)
{
  bool featureFound = false;
  ErrorPtr err;
  JsonObjectPtr reqData = aRequest->getRequest();
  JsonObjectPtr o = reqData->get("devicelabel");
  if (o) {
    mDevicelabel = o->stringValue();
  }
  for (FeatureMap::iterator f = mFeatureMap.begin(); f!=mFeatureMap.end(); ++f) {
    JsonObjectPtr initData = reqData->get(f->first.c_str());
    if (initData) {
      featureFound = true;
      SOLOG(*(f->second), LOG_NOTICE, "initializing...");
      err = f->second->initialize(initData);
      SOLOG(*(f->second), LOG_NOTICE, "initialized: err=%s", Error::text(err));
      if (!Error::isOK(err)) {
        err->prefixMessage("Feature '%s' init failed: ", f->first.c_str());
        return err;
      }
    }
  }
  if (!featureFound) {
    #if ENABLE_P44SCRIPT
    if (mUnhandledRequestSource.hasSinks()) {
      OLOG(LOG_NOTICE, "init does not address any internal feature -> let script check");
      // let scripted feature handler process unknown feature initialisation
      mUnhandledRequestSource.sendEvent(new FeatureRequestObj(aRequest));
      return ErrorPtr(); // no default response, event handler must send it
    }
    #endif
    return FeatureApiError::err("init does not address any known features");
  }
  return Error::ok(); // cause empty response
}


ErrorPtr FeatureApi::now(ApiRequestPtr aRequest)
{
  JsonObjectPtr answer = JsonObject::newObj();
  answer->add("now", JsonObject::newInt64((double)MainLoop::unixtime()/Second));
  aRequest->sendResponse(answer, ErrorPtr());
  return ErrorPtr();
}


ErrorPtr FeatureApi::status(ApiRequestPtr aRequest)
{
  JsonObjectPtr answer = JsonObject::newObj();
  // - list initialized features
  JsonObjectPtr features = JsonObject::newObj();
  for (FeatureMap::iterator f = mFeatureMap.begin(); f!=mFeatureMap.end(); ++f) {
    features->add(f->first.c_str(), f->second->status());
  }
  answer->add("features", features);
  // - grid coordinate
  answer->add("devicelabel", JsonObject::newString(mDevicelabel));
  // - MAC address and IPv4
  answer->add("macaddress", JsonObject::newString(macAddressToString(macAddress(), ':')));
  answer->add("ipv4", JsonObject::newString(ipv4ToString(ipv4Address())));
  answer->add("now", JsonObject::newInt64((double)MainLoop::unixtime()/Second));
  answer->add("version", JsonObject::newString(Application::sharedApplication()->version()));
  // - return
  aRequest->sendResponse(answer, ErrorPtr());
  return ErrorPtr();
}


ErrorPtr FeatureApi::ping(ApiRequestPtr aRequest)
{
  JsonObjectPtr answer = JsonObject::newObj();
  answer->add("pong", JsonObject::newBool(true));
  aRequest->sendResponse(answer, ErrorPtr());
  return ErrorPtr();
}


void FeatureApi::start(const string aApiPort)
{
  mApiServer = SocketCommPtr(new SocketComm(MainLoop::currentMainLoop()));
  mApiServer->setConnectionParams(NULL, aApiPort.c_str(), SOCK_STREAM, AF_INET6);
  mApiServer->setAllowNonlocalConnections(true);
  mApiServer->startServer(boost::bind(&FeatureApi::apiConnectionHandler, this, _1), 10);
  OLOG(LOG_INFO, "listening on port %s", aApiPort.c_str());
}


void FeatureApi::sendEventMessage(JsonObjectPtr aEventMessage)
{
  sendEventMessageInternally(aEventMessage);
  sendEventMessageToApiClient(aEventMessage);
}


void FeatureApi::sendEventMessageInternally(JsonObjectPtr aEventMessage)
{
  #if ENABLE_P44SCRIPT
  if (mFeatureEventSource.hasSinks()) {
    mFeatureEventSource.sendEvent(new JsonValue(aEventMessage)); // just a regular JSON value
  }
  #endif
}


void FeatureApi::sendEventMessageToApiClient(JsonObjectPtr aEventMessage)
{
  if (!mConnection) {
    OLOG(LOG_INFO, "no API connection, event message not sent out: %s", JsonObject::text(aEventMessage));
    return;
  }
  ErrorPtr err = mConnection->sendMessage(aEventMessage);
  if (Error::notOK(err)) {
    OLOG(LOG_ERR, "Error sending message: %s", err->text());
  }
  else {
    OLOG(LOG_INFO,"event message: %s", aEventMessage->c_strValue());
  }
}


ErrorPtr FeatureApiError::err(const char *aFmt, ...)
{
  Error *errP = new FeatureApiError();
  va_list args;
  va_start(args, aFmt);
  errP->setFormattedMessage(aFmt, args);
  va_end(args);
  return ErrorPtr(errP);
}


// MARK: - instantiating  features from command line

#if ENABLE_FEATURE_COMMANDLINE

void FeatureApi::addFeaturesFromCommandLine(
  #if ENABLE_LEDARRANGEMENT
  LEDChainArrangementPtr aLedChainArrangement
  #endif
)
{
  CmdLineApp* a = CmdLineApp::sharedCmdLineApp();
  string s;
  int doStart;
  #if ENABLE_FEATURE_LIGHT
  // - light
  if (a->getIntOption("light", doStart)) {
    AnalogIoPtr pwmDimmer = AnalogIoPtr(new AnalogIo(a->getOption("pwmdimmer","missing"), true, 0)); // off to begin with
    sharedApi()->addFeature(FeaturePtr(new Light(pwmDimmer, doStart)));
  }
  #endif
  #if ENABLE_FEATURE_INPUTS
  // - inputs (instantiate only with command line option, as it allows free use of GPIOs etc.)
  if (a->getOption("inputs")) {
    sharedApi()->addFeature(FeaturePtr(new Inputs));
  }
  #endif
  #if ENABLE_FEATURE_KEYEVENTS
  // - keyboard events from linux input devices
  if (a->getStringOption("keyevents", s)) {
    sharedApi()->addFeature(FeaturePtr(new KeyEvents(s)));
  }
  #endif
  #if ENABLE_FEATURE_DISPMATRIX
  // - dispmatrix
  if (a->getStringOption("dispmatrix", s)) {
    if (aLedChainArrangement) sharedApi()->addFeature(FeaturePtr(new DispMatrix(aLedChainArrangement, s)));
  }
  #endif
  #if ENABLE_FEATURE_INDICATORS
  // - indicators
  if (a->getOption("indicators")) {
    if (aLedChainArrangement) sharedApi()->addFeature(FeaturePtr(new Indicators(aLedChainArrangement)));
  }
  #endif
  #if ENABLE_FEATURE_RFIDS
  // - RFIDs
  if (a->getStringOption("rfidspibus", s)) {
    // either just SPI bus number
    // or SPI bus number followed by a "-" and then SPI device options
    char* e = NULL;
    int spibusno = (int)strtol(s.c_str(), &e, 0);
    string busdevice = "generic";
    if (e && *e) busdevice += e;
    busdevice += "@0";
    // bus device
    SPIDevicePtr spiBusDevice = SPIManager::sharedManager().getDevice(spibusno, busdevice.c_str());
    // reset
    DigitalIoPtr resetPin = DigitalIoPtr(new DigitalIo(a->getOption("rfidreset","missing"), true, false)); // ResetN active to start with
    DigitalIoPtr irqPin = DigitalIoPtr(new DigitalIo(a->getOption("rfidirq","missing"), false, true)); // assume high (open drain)
    // selector
    DigitalIoBusPtr selectBus = DigitalIoBusPtr(new DigitalIoBus(a->getOption("rfidselectpins"), 8, true));
    // add
    sharedApi()->addFeature(FeaturePtr(new RFIDs(
      spiBusDevice,
      selectBus,
      resetPin,
      irqPin
    )));
  }
  #endif // ENABLE_FEATURE_RFIDS
  #if ENABLE_FEATURE_SPLITFLAPS
  if (a->getStringOption("splitflapconn", s)) {
    string tx,rx;
    int txoffdelay = 0;
    a->getStringOption("splitflaptxen", tx);
    a->getStringOption("splitflaprxen", rx);
    a->getIntOption("splitflaptxoff", txoffdelay);
    // add
    sharedApi()->addFeature(FeaturePtr(new Splitflaps(
      s.c_str(), 2121,
      tx.c_str(), rx.c_str(), txoffdelay
    )));
  }
  #endif // ENABLE_FEATURE_SPLITFLAPS
  #if ENABLE_FEATURE_WIFITRACK
  // - wifitrack
  if (a->getIntOption("wifitrack", doStart)) {
    int rtdbo = 0;
    a->getIntOption("wifidboffs",rtdbo);
    sharedApi()->addFeature(FeaturePtr(new WifiTrack(a->getOption("wifimonif",""), rtdbo, doStart)));
  }
  #endif
  #if ENABLE_FEATURE_HERMEL
  // - hermel
  if (a->getIntOption("hermel", doStart)) {
    AnalogIoPtr pwmLeft = AnalogIoPtr(new AnalogIo(a->getOption("pwmleft","missing"), true, 0)); // off to begin with
    AnalogIoPtr pwmRight = AnalogIoPtr(new AnalogIo(a->getOption("pwmright","missing"), true, 0)); // off to begin with
    sharedApi()->addFeature(FeaturePtr(new HermelShoot(pwmLeft, pwmRight, doStart)));
  }
  #endif
  #if ENABLE_FEATURE_NEURON
  #warning "legacy direct LEDchain access"
  // - neuron
  if (a->getStringOption("neuron", s)) {
    AnalogIoPtr sensor0 =  AnalogIoPtr(new AnalogIo(a->getOption("sensor0","missing"), false, 0));
    sharedApi()->addFeature(FeaturePtr(new Neuron(
      a->getOption("ledchain1","/dev/null"),
      a->getOption("ledchain2","/dev/null"),
      sensor0,
      s
    )));
  }
  #endif
  #if ENABLE_FEATURE_MIXLOOP
  #warning "legacy direct LEDchain access"
  // - mixloop
  if (a->getIntOption("mixloop", doStart)) {
    sharedApi()->addFeature(FeaturePtr(new MixLoop(
      a->getOption("ledchain2","/dev/null"),
      a->getOption("ledchain3","/dev/null"),
      doStart
    )));
  }
  #endif
  // now, start API if port is selected
  string apiport;
  if (a->getStringOption("featureapiport", apiport)) {
    sharedApi()->start(apiport);
  }
}

#endif // ENABLE_FEATURE_COMMANDLINE

// MARK: - script support

#if ENABLE_P44SCRIPT

using namespace P44Script;

void FeatureApi::scriptExecHandler(ApiRequestPtr aRequest, ScriptObjPtr aResult)
{
  // just returns the exit code of the script as JSON
  // (this API is not indended for editing/debugging scripts)
  JsonObjectPtr ans;
  if (aResult) {
    ans = aResult->jsonValue();
  }
  aRequest->sendResponse(ans, ErrorPtr());
}


FeatureRequestObj::FeatureRequestObj(ApiRequestPtr aRequest) :
  inherited(aRequest->getRequest()),
  mRequest(aRequest)
{
}

void FeatureRequestObj::sendResponse(JsonObjectPtr aResponse, ErrorPtr aError)
{
  if(mRequest) mRequest->sendResponse(aResponse, aError);
  mRequest.reset(); // done now
}

string FeatureRequestObj::getAnnotation() const
{
  return "feature call";
}


// answer([answer value|error])        answer the request/call
static const BuiltInArgDesc answer_args[] = { { any|error|optionalarg } };
static const size_t answer_numargs = sizeof(answer_args)/sizeof(BuiltInArgDesc);
static void answer_func(BuiltinFunctionContextPtr f)
{
  FeatureRequestObj* reqObj = dynamic_cast<FeatureRequestObj *>(f->thisObj().get());
  if (f->arg(0)->isErr()) {
    reqObj->sendResponse(JsonObjectPtr(), f->arg(0)->errorValue());
  }
  else {
    reqObj->sendResponse(f->arg(0)->jsonValue(), ErrorPtr());
  }
  f->finish();
}
static const BuiltinMemberDescriptor answer_desc =
  { "answer", executable|any, answer_numargs, answer_args, &answer_func };


const ScriptObjPtr FeatureRequestObj::memberByName(const string aName, TypeInfo aMemberAccessFlags)
{
  ScriptObjPtr val;
  if (uequals(aName, "answer")) {
    val = new BuiltinFunctionObj(&answer_desc, this, NULL);
  }
  else {
    val = inherited::memberByName(aName, aMemberAccessFlags);
  }
  return val;
}


// featureevent(json)    send a feature event
// featureevent()        return feature event (only returns something in a trigger expressions, NULL otherwise)
static const BuiltInArgDesc featureevent_args[] = { { structured|optionalarg } };
static const size_t featureevent_numargs = sizeof(featureevent_args)/sizeof(BuiltInArgDesc);
static void featureevent_func(BuiltinFunctionContextPtr f)
{
  if (f->numArgs()==0) {
    // return placeholder for incoming feature events
    f->finish(new OneShotEventNullValue(dynamic_cast<EventSource *>(&(FeatureApi::sharedApi()->mFeatureEventSource)), "feature event"));
    return;
  }
  // send a feature API event message (to API client)
  JsonObjectPtr jevent = f->arg(0)->jsonValue();
  FeatureApi::sharedApi()->sendEventMessageToApiClient(jevent); // without triggering featureevent()
  f->finish();
}


// featurecall(json)      send a feature api call/request (for local processing)
// featurecall()          return unhandled feature api call (only returns something in a trigger expressions, NULL otherwise)
static const BuiltInArgDesc featurecall_args[] = { { object|optionalarg } };
static const size_t featurecall_numargs = sizeof(featurecall_args)/sizeof(BuiltInArgDesc);
static void featurecall_func(BuiltinFunctionContextPtr f)
{
  if (f->numArgs()==0) {
    // return placeholder for incoming unhandled feature api calls
    f->finish(new OneShotEventNullValue(dynamic_cast<EventSource *>(&(FeatureApi::sharedApi()->mUnhandledRequestSource)), "feature call"));
    return;
  }
  // send a feature API request message
  JsonObjectPtr jreq = f->arg(0)->jsonValue();
  ApiRequestPtr request = ApiRequestPtr(new APICallbackRequest(jreq, boost::bind(&FeatureApiLookup::featureCallDone, f, _1, _2)));
  ErrorPtr err = FeatureApi::sharedApi()->processRequest(request);
  if (err) {
    // must "send" a response now (will trigger featureCallDone)
    request->sendResponse(NULL, err);
  }
}


// feature(featurename)
static const BuiltInArgDesc feature_args[] = { { text } };
static const size_t feature_numargs = sizeof(feature_args)/sizeof(BuiltInArgDesc);
static void feature_func(BuiltinFunctionContextPtr f)
{
  FeaturePtr feature = FeatureApi::sharedApi()->getFeature(f->arg(0)->stringValue());
  if (!feature) {
    f->finish(new ErrorValue(ScriptError::NotFound, "no feature '%s' found", f->arg(0)->stringValue().c_str()));
    return;
  }
  f->finish(feature->newFeatureObj());
}


static const BuiltinMemberDescriptor featureApiGlobals[] = {
  { "feature", executable|any, feature_numargs, feature_args, &feature_func },
  { "featurecall", executable|value|null, featurecall_numargs, featurecall_args, &featurecall_func },
  { "featureevent", executable|value|null, featureevent_numargs, featureevent_args, &featureevent_func },
  { NULL } // terminator
};

FeatureApiLookup::FeatureApiLookup() :
  inherited(featureApiGlobals)
{
}

// static helper for implementing calls
void FeatureApiLookup::featureCallDone(BuiltinFunctionContextPtr f, JsonObjectPtr aResult, ErrorPtr aError)
{
  // Note: do not report Error::OK as error
  if (Error::notOK(aError)) {
    f->finish(new ErrorValue(aError));
    return;
  }
  if (aResult) {
    f->finish(new JsonValue(aResult));
    return;
  }
  f->finish(new AnnotatedNullValue("feature api request returns no answer"));
}

#endif // ENABLE_P44SCRIPT
