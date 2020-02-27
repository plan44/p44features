//
//  Copyright (c) 2018 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  This file is part of p44featured
//
//  p44featured is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  p44featured is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with p44featured. If not, see <http://www.gnu.org/licenses/>.
//

#include "hermel.hpp"

#if ENABLE_FEATURE_HERMEL

#include "application.hpp"

using namespace p44;

HermelShoot::HermelShoot(AnalogIoPtr aPwmLeft, AnalogIoPtr aPwmRight) :
  inherited("hermel"),
  pwmLeft(aPwmLeft),
  pwmRight(aPwmRight)
{
  // check for commandline-triggered standalone operation
  if (CmdLineApp::sharedCmdLineApp()->getOption("hermel")) {
    initOperation();
  }
}

// MARK: ==== light API

ErrorPtr HermelShoot::initialize(JsonObjectPtr aInitData)
{
  initOperation();
  return Error::ok();
}


ErrorPtr HermelShoot::processRequest(ApiRequestPtr aRequest)
{
  JsonObjectPtr o = aRequest->getRequest()->get("cmd");
  if (!o) {
    return FeatureApiError::err("missing 'cmd'");
  }
  string cmd = o->stringValue();
  if (cmd=="shoot") {
    return shoot(aRequest);
  }
  return inherited::processRequest(aRequest);
}


JsonObjectPtr HermelShoot::status()
{
  JsonObjectPtr answer = inherited::status();
  if (answer->isType(json_type_object)) {
    // TODO: add info here
  }
  return answer;
}


ErrorPtr HermelShoot::shoot(ApiRequestPtr aRequest)
{
  JsonObjectPtr data = aRequest->getRequest();
  JsonObjectPtr o;
  double angle = 0; // straight
  if (data->get("angle", o, true)) angle = o->doubleValue();
  double intensity = 1; // full power
  if (data->get("intensity", o, true)) intensity = o->doubleValue();
  MLMicroSeconds pulseLength = 500*MilliSecond;
  if (data->get("pulse", o, true)) pulseLength = o->int64Value() * Second;
  shoot(angle, intensity, pulseLength);
  return Error::ok();
}


// MARK: ==== hermel operation


void HermelShoot::initOperation()
{
  LOG(LOG_NOTICE, "initializing hermel");
  setInitialized();
  endPulse();
}


void HermelShoot::shoot(double aAngle, double aIntensity, MLMicroSeconds aPulseLength)
{
  pwmRight->setValue(100*aIntensity*(aAngle<=0 ? 1 : 1-aAngle));
  pwmLeft->setValue(100*aIntensity*(aAngle>=0 ? 1 : 1+aAngle));
  pulseTicket.executeOnce(boost::bind(&HermelShoot::endPulse, this), aPulseLength);
}


void HermelShoot::endPulse()
{
  pwmRight->setValue(0);
  pwmLeft->setValue(0);
}

#endif // ENABLE_FEATURE_HERMEL
