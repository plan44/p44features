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
  FeatureApi::sharedApi()->sendMessage(aMessage);
}



void Feature::reset()
{
  initialized = false;
}


ErrorPtr Feature::runTool()
{
  return TextError::err("Feature %s does not have command line tools", name.c_str());
}
