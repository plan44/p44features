//
//  Copyright (c) 2016-2020 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Author: Ueli Wahlen <ueli@hotmail.com>
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

#include "light.hpp"

#if ENABLE_FEATURE_LIGHT

#include "application.hpp"

using namespace p44;

Light::Light(AnalogIoPtr aPwmDimmer) :
  inherited("light"),
  pwmDimmer(aPwmDimmer)
{
  // check for commandline-triggered standalone operation
  if (CmdLineApp::sharedCmdLineApp()->getOption("light")) {
    setInitialized();
  }
}

// MARK: ==== light API

ErrorPtr Light::initialize(JsonObjectPtr aInitData)
{
  initOperation();
  return Error::ok();
}


ErrorPtr Light::processRequest(ApiRequestPtr aRequest)
{
  JsonObjectPtr o = aRequest->getRequest()->get("cmd");
  if (!o) {
    return FeatureApiError::err("missing 'cmd'");
  }
  string cmd = o->stringValue();
  if (cmd=="fade") {
    return fade(aRequest);
  }
  return inherited::processRequest(aRequest);
}


JsonObjectPtr Light::status()
{
  JsonObjectPtr answer = inherited::status();
  if (answer->isType(json_type_object)) {
    answer->add("brightness", JsonObject::newDouble(current()));
  }
  return answer;
}


ErrorPtr Light::fade(ApiRequestPtr aRequest)
{
  JsonObjectPtr data = aRequest->getRequest();
  JsonObjectPtr o;
  double from = -1;
  if (data->get("from", o, true)) from = o->doubleValue();
  double to = 1;
  if (data->get("to", o, true)) to = o->doubleValue();
  MLMicroSeconds t = 300*MilliSecond;
  if (data->get("t", o, true)) t = o->int64Value() * MilliSecond;
  MLMicroSeconds start = MainLoop::now();
  if (data->get("start", o, true)) start = MainLoop::unixTimeToMainLoopTime(o->int64Value() * MilliSecond);
  fade(from, to, t, start);
  return Error::ok();
}


// MARK: ==== light operation


void Light::initOperation()
{
  LOG(LOG_NOTICE, "initializing light");
  setInitialized();
}


void Light::fade(double aFrom, double aTo, MLMicroSeconds aFadeTime, MLMicroSeconds aStartTime)
{
  double startValue;
  animator = ValueAnimatorPtr(new ValueAnimator(pwmDimmer->getValueSetter(startValue)));
  if (aFrom>=0) startValue = aFrom;
  animator->from(startValue);
  ticket.executeOnceAt(boost::bind(&Light::startFading, this, aTo, aFadeTime), aStartTime);
}

void Light::startFading(double aTo, MLMicroSeconds aFadeTime)
{
  animator->animate(aTo, aFadeTime, NULL, dt);
}

double Light::current()
{
  return currentValue;
}

double Light::brightnessToPWM(double aBrightness)
{
  return 100*((exp(aBrightness*4/1)-1)/(exp(4)-1));
}

#endif // ENABLE_FEATURE_LIGHT
