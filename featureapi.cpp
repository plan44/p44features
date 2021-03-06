//
//  Copyright (c) 2016-2020 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Authors: Ueli Wahlen <ueli@hotmail.com>, Lukas Zeller <luz@plan44.ch>
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

#include "featureapi.hpp"

#include "feature.hpp"

#include "extutils.hpp"
#include "macaddress.hpp"
#include "application.hpp"

using namespace p44;


// MARK: ===== FeatureApiRequest

FeatureApiRequest::FeatureApiRequest(JsonObjectPtr aRequest, JsonCommPtr aConnection) :
  inherited(aRequest),
  connection(aConnection)
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
  if (connection) connection->sendMessage(aResponse);
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
  requestDoneCB(aRequestDoneCB)
{
}


APICallbackRequest::~APICallbackRequest()
{
}


void APICallbackRequest::sendResponse(JsonObjectPtr aResponse, ErrorPtr aError)
{
  if (requestDoneCB) requestDoneCB(aResponse, aError);
}



// MARK: ===== FeatureApi

static FeatureApiPtr featureApi;

FeatureApiPtr FeatureApi::sharedApi()
{
  if (!featureApi) {
    featureApi = FeatureApiPtr(new FeatureApi);
  }
  return featureApi;
}


FeatureApi::FeatureApi()
  #if EXPRESSION_SCRIPT_SUPPORT
  : trigger(NULL) // FIXME: add geolocation here
  #endif
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
    ErrorPtr err = runJsonFile(scriptName, NULL, NULL, &subst);
    if (Error::isOK(err)) return Error::ok();
    return err;
  }
  o = reqData->get("scripttext");
  if (o) {
    string scriptText = o->stringValue();
    ErrorPtr err = runJsonString(scriptText, NULL, NULL, &subst);
    if (Error::isOK(err)) return Error::ok();
    return err;
  }
  o = reqData->get("json");
  if (o) {
    ErrorPtr err = executeJson(o, NULL, NULL);
    if (Error::isOK(err)) return Error::ok();
    return err;
  }
  return FeatureApiError::err("missing 'script', 'scripttext' or 'json' attribute");
}


#endif // ENABLE_LEGACY_FEATURE_SCRIPTS


// MARK: ==== feature API


FeaturePtr FeatureApi::getFeature(const string aFeatureName)
{
  FeatureMap::iterator pos = featureMap.find(aFeatureName);
  if (pos==featureMap.end()) return FeaturePtr();
  return pos->second;
}




void FeatureApi::addFeature(FeaturePtr aFeature)
{
  featureMap[aFeature->getName()] = aFeature;
}


SocketCommPtr FeatureApi::apiConnectionHandler(SocketCommPtr aServerSocketComm)
{
  JsonCommPtr conn = JsonCommPtr(new JsonComm(MainLoop::currentMainLoop()));
  conn->setMessageHandler(boost::bind(&FeatureApi::apiRequestHandler, this, conn, _1, _2));
  conn->setClearHandlersAtClose(); // close must break retain cycles so this object won't cause a mem leak

  connection = conn;
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
    FeatureMap::iterator f = featureMap.find(featurename);
    if (f==featureMap.end()) {
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
    #if EXPRESSION_SCRIPT_SUPPORT
    if (reqData->get("eventscript", o, true)) {
      // set a script handling internally generated events
      eventScript = o->stringValue();
      return Error::ok();
    }
    if (reqData->get("triggerexpression", o, true)) {
      // set a global trigger expression
      trigger.setCode(o->stringValue());
      trigger.setEvaluationResultHandler(boost::bind(&FeatureApi::triggerEvaluationExecuted, this, _1));
      trigger.triggerEvaluation(evalmode_initial);
      return Error::ok();
    }
    if (reqData->get("run", o, true)) {
      // directly run a script
      queueScript("API run cmd", o->stringValue());
      return Error::ok();
    }
    #endif // EXPRESSION_SCRIPT_SUPPORT
    #if ENABLE_P44SCRIPT
    if (reqData->get("run", o, true)) {
      // directly run a script.
      // Note: this is not for testing/debugging/REPL purposes, but just to fire some script commands
      //   Basic p44script edit/debug infrastructure is not implemented as part of the feature API
      ScriptSource src(sourcecode+regular+keepvars+queue+floatingGlobs, "featurapi_run");
      src.setSource(o->stringValue());
      src.run(inherit, boost::bind(&FeatureApi::scriptExecHandler, this, aRequest, _1));
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
    else {
      return FeatureApiError::err("unknown global command '%s'", cmd.c_str());
    }
  }
}


#if EXPRESSION_SCRIPT_SUPPORT
void FeatureApi::triggerEvaluationExecuted(ExpressionValue aEvaluationResult)
{
  if (aEvaluationResult.boolValue()) {
    JsonObjectPtr message = JsonObject::newObj();
    message->add("triggerresult", aEvaluationResult.jsonValue() );
    sendMessage(message);
  }
}
#endif


ErrorPtr FeatureApi::reset(ApiRequestPtr aRequest)
{
  bool featureFound = false;
  for (FeatureMap::iterator f = featureMap.begin(); f!=featureMap.end(); ++f) {
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
    devicelabel = o->stringValue();
  }
  for (FeatureMap::iterator f = featureMap.begin(); f!=featureMap.end(); ++f) {
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
  for (FeatureMap::iterator f = featureMap.begin(); f!=featureMap.end(); ++f) {
    features->add(f->first.c_str(), f->second->status());
  }
  answer->add("features", features);
  // - grid coordinate
  answer->add("devicelabel", JsonObject::newString(devicelabel));
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
  apiServer = SocketCommPtr(new SocketComm(MainLoop::currentMainLoop()));
  apiServer->setConnectionParams(NULL, aApiPort.c_str(), SOCK_STREAM, AF_INET6);
  apiServer->setAllowNonlocalConnections(true);
  apiServer->startServer(boost::bind(&FeatureApi::apiConnectionHandler, this, _1), 10);
  OLOG(LOG_INFO, "listening on port %s", aApiPort.c_str());
}


void FeatureApi::sendEventMessage(JsonObjectPtr aEventMessage)
{
  #if EXPRESSION_SCRIPT_SUPPORT
  if (!eventScript.empty()) {
    // call event script
    queueScript("API event", eventScript, aEventMessage);
  }
  #endif // EXPRESSION_SCRIPT_SUPPORT
  #if ENABLE_P44SCRIPT
  if (numSinks()>0) {
    mPendingEvent = aEventMessage;
    sendEvent(new FeatureEventObj(mPendingEvent)); // needs to have one-shot marker
  }
  #endif
  sendEventMessageInternal(aEventMessage);
}

void FeatureApi::sendEventMessageInternal(JsonObjectPtr aEventMessage)
{
  if (!connection) {
    OLOG(LOG_WARNING, "no connection, event message cannot be sent: %s", aEventMessage ? aEventMessage->json_c_str() : "<none>");
    return;
  }
  ErrorPtr err = connection->sendMessage(aEventMessage);
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


// MARK: - script support

#if ENABLE_P44SCRIPT

using namespace P44Script;


#if ENABLE_P44SCRIPT
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
#endif


JsonObjectPtr FeatureApi::pendingEvent()
{
  JsonObjectPtr res = mPendingEvent;
  mPendingEvent.reset();
  return res;
}


FeatureEventObj::FeatureEventObj(JsonObjectPtr aJson) :
  inherited(aJson)
{
}


TypeInfo FeatureEventObj::getTypeInfo() const
{
  return inherited::getTypeInfo()|oneshot; // returns the event only once
}


string FeatureEventObj::getAnnotation() const
{
  return "feature event";
}


EventSource *FeatureEventObj::eventSource() const
{
  // the API is the event source
  return dynamic_cast<EventSource *>(FeatureApi::sharedApi().get());
}


// featureevent(json)    send a feature event
// featureevent()        return latest unprocessed feature event
static const BuiltInArgDesc featureevent_args[] = { { json|structured|optionalarg } };
static const size_t featureevent_numargs = sizeof(featureevent_args)/sizeof(BuiltInArgDesc);
static void featureevent_func(BuiltinFunctionContextPtr f)
{
  if (f->numArgs()==0) {
    // return latest unprocessed API event
    f->finish(new FeatureEventObj(FeatureApi::sharedApi()->pendingEvent()));
    return;
  }
  // send a feature API event message (to API client)
  JsonObjectPtr jevent = f->arg(0)->jsonValue();
  FeatureApi::sharedApi()->sendEventMessageInternal(jevent); // without triggering featureevent()
  f->finish();
}


// featurecall(json)      send a feature api call/request (for local processing)
static const BuiltInArgDesc featurecall_args[] = { { json|object } };
static const size_t featurecall_numargs = sizeof(featurecall_args)/sizeof(BuiltInArgDesc);
static void featurecall_func(BuiltinFunctionContextPtr f)
{
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
  { "featurecall", executable|async|any, featurecall_numargs, featurecall_args, &featurecall_func },
  { "featureevent", executable|json|null, featureevent_numargs, featureevent_args, &featureevent_func },
  { NULL } // terminator
};

FeatureApiLookup::FeatureApiLookup() :
  inherited(featureApiGlobals)
{
}

// static helper for implementing calls
void FeatureApiLookup::featureCallDone(BuiltinFunctionContextPtr f, JsonObjectPtr aResult, ErrorPtr aError)
{
  if (aError) {
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



#if EXPRESSION_SCRIPT_SUPPORT

bool FeatureApi::evaluateAsyncFeatureFunctions(EvaluationContext* aEvalContext, const string &aFunc, const FunctionArguments &aArgs, bool &aNotYielded)
{
  if (aFunc=="call" && aArgs.size()>=1 && aArgs.size()<=2) {
    // call a command or property change on the feature API
    //   call(feature_api_json [, concurrent])
    if (aArgs[0].notValue()) return aEvalContext->errorInArg(aArgs[0], true);
    bool concurrent = aArgs[1].boolValue();
    ExpressionValue res;
    JsonObjectPtr call;
    ErrorPtr err;
    #if EXPRESSION_JSON_SUPPORT
    call = aArgs[0].jsonValue(&err);
    #else
    call = JsonObject::objFromText(aArgs[0].stringValue().c_str(), -1, &err, false);
    #endif
    if (Error::notOK(err)) return aEvalContext->throwError(err);
    // concurrent or blocking requests are possible
    ApiRequestPtr request;
    if (concurrent) {
      request = ApiRequestPtr(new APICallbackRequest(call, NULL));
      aEvalContext->continueWithAsyncFunctionResult(res);
      return true;
    }
    else {
      request = ApiRequestPtr(new APICallbackRequest(call, boost::bind(&FeatureApi::apiCallFunctionDone, aEvalContext, _1, _2)));
    }
    FeatureApi::sharedApi()->handleRequest(request);
    aNotYielded = false; // callback will get result
    return true;
  }
  else if (aFunc=="message") {
    // Issue a API message (not interpreted except possibly by a message handler script
    //   message(feature_api_json)
    if (aArgs[0].notValue()) return aEvalContext->errorInArg(aArgs[0], true);
    JsonObjectPtr msg;
    ErrorPtr err;
    #if EXPRESSION_JSON_SUPPORT
    msg = aArgs[0].jsonValue(&err);
    #else
    msg = JsonObject::objFromText(aArgs[0].stringValue().c_str(), -1, &err, false);
    #endif
    if (Error::notOK(err)) return aEvalContext->throwError(err);
    sharedApi()->sendMessage(msg);
  }
  return false;
}


void FeatureApi::apiCallFunctionDone(EvaluationContext* aEvalContext, JsonObjectPtr aResult, ErrorPtr aError)
{
  SOLOG(*FeatureApi::sharedApi(), LOG_INFO, "call returns '%s', error = %s", aResult->c_strValue(), Error::text(aError));
  ExpressionValue res;
  if (Error::isOK(aError)) {
    res.setJson(aResult);
  }
  else {
    res.setError(aError);
  }
  aEvalContext->continueWithAsyncFunctionResult(res);
}


void FeatureApi::queueScript(const char *aScriptContextInfo, const string &aScriptCode, JsonObjectPtr aMessage)
{
  FeatureApiScriptContextPtr script = FeatureApiScriptContextPtr(new FeatureApiScriptContext(aMessage));
  script->setCode(aScriptCode);
  script->setContextInfo(aScriptContextInfo, this);
  scriptRequests.push_back(script);
  if (scriptRequests.size()==1) {
    // there was no script pending, must start
    runNextScript();
  }
}

void FeatureApi::runNextScript()
{
  if (!scriptRequests.empty()) {
    FeatureApiScriptContextPtr script = scriptRequests.front();
    OLOG(LOG_INFO, "+++ Starting script");
    script->execute(true, boost::bind(&FeatureApi::scriptDone, this, script));
  }
}


void FeatureApi::scriptDone(FeatureApiScriptContextPtr aScript)
{
  OLOG(LOG_INFO, "--- Finished script");
  scriptRequests.pop_front();
  runNextScript();
}



// MARK: - FeatureApiScriptContext


bool FeatureApiScriptContext::evaluateAsyncFunction(const string &aFunc, const FunctionArguments &aArgs, bool &aNotYielded)
{
  if (FeatureApi::evaluateAsyncFeatureFunctions(this, aFunc, aArgs, aNotYielded)) return true;
  return inherited::evaluateAsyncFunction(aFunc, aArgs, aNotYielded);
}


bool FeatureApiScriptContext::evaluateFunction(const string &aFunc, const FunctionArguments &aArgs, ExpressionValue &aResult)
{
  if (aFunc=="message" && aArgs.size()==0) {
    aResult.setJson(apiMessage);
  }
  else {
    return inherited::evaluateFunction(aFunc, aArgs, aResult);
  }
  return true;
}


#endif // EXPRESSION_SCRIPT_SUPPORT
