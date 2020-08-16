//
//  Copyright (c) 2016-2020 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Author: Ueli Wahlen <ueli@hotmail.com>
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

#include "feature.hpp"

using namespace p44;

Feature::Feature(const string aName) :
  name(aName),
  initialized(false)
{
}


bool Feature::isInitialized() const
{
  return initialized;
}


ErrorPtr Feature::processRequest(ApiRequestPtr aRequest)
{
  // check status command
  JsonObjectPtr reqData = aRequest->getRequest();
  JsonObjectPtr o;
  if (reqData->get("cmd", o, true)) {
    string cmd = o->stringValue();
    if (cmd=="status") {
      aRequest->sendResponse(status(), ErrorPtr());
      return ErrorPtr();
    }
    return FeatureApiError::err("Feature '%s': unknown cmd '%s'", getName().c_str(), cmd.c_str());
  }
  else {
    // decode properties
    if (reqData->get("logleveloffset", o, true)) {
      setLogLevelOffset(o->int32Value());
      return ErrorPtr();
    }
  }
  return FeatureApiError::err("Feature '%s': cannot understand request (no cmd)", getName().c_str());
}


JsonObjectPtr Feature::status()
{
  if (isInitialized()) {
    JsonObjectPtr status = JsonObject::newObj();
    status->add("logleveloffset", JsonObject::newInt32(getLogLevelOffset()));
  }
  return JsonObject::newBool(false);
}


void Feature::sendEventMessage(JsonObjectPtr aMessage)
{
  if (!aMessage) aMessage = JsonObject::newObj();
  aMessage->add("feature", JsonObject::newString(getName()));
  FeatureApi::sharedApi()->sendEventMessage(aMessage);
}



void Feature::reset()
{
  initialized = false;
}


ErrorPtr Feature::runTool()
{
  return TextError::err("Feature %s does not have command line tools", name.c_str());
}


// MARK: - Feature scripting object

#if ENABLE_P44SCRIPT

using namespace P44Script;

// status()
static void status_func(BuiltinFunctionContextPtr f)
{
  FeatureObj* ft = dynamic_cast<FeatureObj*>(f->thisObj().get());
  assert(ft);
  f->finish(new JsonValue(ft->feature()->status()));
}

// init(json_config)
static const BuiltInArgDesc init_args[] = { { json|object } };
static const size_t init_numargs = sizeof(init_args)/sizeof(BuiltInArgDesc);
static void init_func(BuiltinFunctionContextPtr f)
{
  FeatureObj* ft = dynamic_cast<FeatureObj*>(f->thisObj().get());
  assert(ft);
  ErrorPtr err = ft->feature()->initialize(f->arg(0)->jsonValue());
  if (Error::notOK(err)) {
    f->finish(new ErrorValue(err));
    return;
  }
  f->finish();
}

static void featureCallDone(BuiltinFunctionContextPtr f, JsonObjectPtr aResult, ErrorPtr aError)
{
  if (aError) {
    f->finish(new ErrorValue(aError));
    return;
  }
  if (aResult) {
    f->finish(new JsonValue(aResult));
    return;
  }
  f->finish(new AnnotatedNullValue("feature cmd without answer"));
}

static void issueCommand(BuiltinFunctionContextPtr f, JsonObjectPtr aCommand)
{
  FeatureObj* ft = dynamic_cast<FeatureObj*>(f->thisObj().get());
  assert(ft);
  ApiRequestPtr request = ApiRequestPtr(new APICallbackRequest(aCommand, boost::bind(&featureCallDone, f, _1, _2)));
  ErrorPtr err = ft->feature()->processRequest(request);
  if (err) {
    // must "send" a response now (will trigger featureCallDone)
    request->sendResponse(NULL, err);
  }
}

// cmd(command [, jsonparams])
static const BuiltInArgDesc cmd_args[] = { { text }, { json|object|optional } };
static const size_t cmd_numargs = sizeof(cmd_args)/sizeof(BuiltInArgDesc);
static void cmd_func(BuiltinFunctionContextPtr f)
{
  JsonObjectPtr jcmd;
  if (f->numArgs()>1) {
    jcmd = f->arg(1)->jsonValue();
  }
  if (!jcmd || !jcmd->isType(json_type_object)) {
    jcmd = JsonObject::newObj();
  }
  jcmd->add("cmd", JsonObject::newString(f->arg(0)->stringValue()));
  issueCommand(f, jcmd);
}

// set(property, value)
static const BuiltInArgDesc set_args[] = { { text }, { any } };
static const size_t set_numargs = sizeof(set_args)/sizeof(BuiltInArgDesc);
static void set_func(BuiltinFunctionContextPtr f)
{
  JsonObjectPtr jcmd = JsonObject::newObj();
  jcmd->add(f->arg(0)->stringValue().c_str(), f->arg(1)->jsonValue());
  issueCommand(f, jcmd);
}


static const BuiltinMemberDescriptor featureMembers[] = {
  { "status", executable|json, 0, NULL, &status_func },
  { "init", executable|null|error, init_numargs, init_args, &init_func },
  { "cmd", executable|async|any|error, cmd_numargs, cmd_args, &cmd_func },
  { "set", executable|async|any|error, set_numargs, set_args, &set_func },
  { NULL } // terminator
};


static BuiltInMemberLookup* sharedFeatureMemberLookupP = NULL;

FeatureObj::FeatureObj(FeaturePtr aFeature) :
  mFeature(aFeature)
{
  if (sharedFeatureMemberLookupP==NULL) {
    sharedFeatureMemberLookupP = new BuiltInMemberLookup(featureMembers);
    sharedFeatureMemberLookupP->isMemberVariable(); // disable refcounting
  }
  registerMemberLookup(sharedFeatureMemberLookupP);
}


#endif // ENABLE_P44SCRIPT
