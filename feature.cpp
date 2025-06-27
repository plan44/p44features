//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2016-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Author: Ueli Wahlen <ueli@hotmail.com>
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
  ErrorPtr err;
  if (reqData->get("cmd", o, true)) {
    string cmd = o->stringValue();
    if (cmd=="status") {
      aRequest->sendResponse(status(), err);
      return err;
    }
    return FeatureApiError::err("Feature '%s': unknown cmd '%s'", getName().c_str(), cmd.c_str());
  }
  else {
    // decode properties
    if (reqData->get("logleveloffset", o, true)) {
      setLogLevelOffset(o->int32Value());
    }
    return err ? err : Error::ok();
  }
}


JsonObjectPtr Feature::status()
{
  if (isInitialized()) {
    JsonObjectPtr status = JsonObject::newObj();
    status->add("logleveloffset", JsonObject::newInt32(getLogLevelOffset()));
    return status;
  }
  // not initialized
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
  f->finish(ScriptObj::valueFromJSON(ft->feature()->status()));
}

// reset()
static void reset_func(BuiltinFunctionContextPtr f)
{
  FeatureObj* ft = dynamic_cast<FeatureObj*>(f->thisObj().get());
  assert(ft);
  ft->feature()->reset();
  f->finish();
}

// init(json_config)
FUNC_ARG_DEFS(init, { objectvalue|numeric } );
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
  if (Error::notOK(aError)) {
    f->finish(new ErrorValue(aError));
    return;
  }
  if (aResult) {
    f->finish(ScriptObj::valueFromJSON(aResult));
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
FUNC_ARG_DEFS(cmd, { text }, { objectvalue|optionalarg } );
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
// set(properties)
FUNC_ARG_DEFS(set, { text|objectvalue }, { anyvalid|optionalarg } );
static void set_func(BuiltinFunctionContextPtr f)
{
  JsonObjectPtr jcmd;
  if (f->numArgs()<2) {
    jcmd = f->arg(0)->jsonValue();
  }
  else {
    jcmd = JsonObject::newObj();
    jcmd->add(f->arg(0)->stringValue().c_str(), f->arg(1)->jsonValue());
  }
  issueCommand(f, jcmd);
}


static const BuiltinMemberDescriptor featureMembers[] = {
  FUNC_DEF_NOARG(status, executable|value),
  FUNC_DEF_W_ARG(init, executable|null|error),
  FUNC_DEF_NOARG(reset, executable|null|error),
  FUNC_DEF_W_ARG(cmd, executable|async|anyvalid|error),
  FUNC_DEF_W_ARG(set, executable|async|anyvalid|error),
  BUILTINS_TERMINATOR
};


static BuiltInMemberLookup* sharedFeatureMemberLookupP = NULL;

ScriptObjPtr Feature::newFeatureObj()
{
  return new FeatureObj(this);
}

FeatureObj::FeatureObj(FeaturePtr aFeature) :
  mFeature(aFeature)
{
  registerSharedLookup(sharedFeatureMemberLookupP, featureMembers);
}


#endif // ENABLE_P44SCRIPT
