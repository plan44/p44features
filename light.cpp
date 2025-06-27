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

#include "light.hpp"

#if ENABLE_FEATURE_LIGHT

#include "application.hpp"

using namespace p44;

Light::Light(AnalogIoPtr aPwmDimmer, bool doStart) :
  inherited("light"),
  pwmDimmer(aPwmDimmer)
{
  if (doStart) {
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
  if (data->get("t", o, true)) t = o->int64Value() * Second;
  MLMicroSeconds start = MainLoop::now();
  if (data->get("start", o, true)) start = MainLoop::unixTimeToMainLoopTime(o->doubleValue() * Second);
  fade(from, to, t, start);
  return Error::ok();
}


// MARK: ==== light operation


void Light::initOperation()
{
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
  animator->animate(aTo, aFadeTime, NoOP);
}

double Light::current()
{
  return currentValue;
}

double Light::brightnessToPWM(double aBrightness)
{
  return 100*((exp(aBrightness*4/1)-1)/(exp(4)-1));
}

#if ENABLE_P44SCRIPT

using namespace P44Script;

// animator()
static void animator_func(BuiltinFunctionContextPtr f)
{
  LightObj* l = dynamic_cast<LightObj*>(f->thisObj().get());
  assert(l);
  f->finish(new ValueAnimatorObj(l->animator()));
}



static const BuiltinMemberDescriptor lightMembers[] = {
  FUNC_DEF_NOARG(animator, executable|objectvalue),
  BUILTINS_TERMINATOR
};


static BuiltInMemberLookup* sharedLightMemberLookupP = NULL;

ScriptObjPtr Light::newFeatureObj()
{
  return new LightObj(this);
}


ValueAnimatorPtr LightObj::animator()
{
  Light* l = dynamic_cast<Light*>(mFeature.get());
  ValueAnimatorPtr a;
  if (l) {
    double startValue;
    a = ValueAnimatorPtr(new ValueAnimator(l->pwmDimmer->getValueSetter(startValue)));
    a->from(startValue);
  }
  return a;
}


LightObj::LightObj(FeaturePtr aFeature) :
  inherited(aFeature)
{
  registerSharedLookup(sharedLightMemberLookupP, lightMembers);
}

#endif // ENABLE_P44SCRIPT

#endif // ENABLE_FEATURE_LIGHT
