//
//  Copyright (c) 2020 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "inputs.hpp"

#if ENABLE_FEATURE_INPUTS

#include "application.hpp"

using namespace p44;

#define FEATURE_NAME "inputs"


Inputs::Inputs() :
  inherited(FEATURE_NAME)
{
  // check for commandline-triggered standalone operation
  if (CmdLineApp::sharedCmdLineApp()->getOption("light")) {
    setInitialized();
  }
}


void Inputs::reset()
{
  inputs.clear();
  inherited::reset();
}


Inputs::~Inputs()
{
  reset();
}



// MARK: ==== inputs API

ErrorPtr Inputs::initialize(JsonObjectPtr aInitData)
{
  // { "cmd":"init", "inputs": { "*input_name*":{ "pin":"*pin_spec*", "initiallyo":*initial_bool* } , ... } }
  aInitData->resetKeyIteration();
  string inputName;
  JsonObjectPtr inputCfg;
  JsonObjectPtr o;
  while (aInitData->nextKeyValue(inputName, inputCfg)) {
    Input input;
    input.name = inputName;
    string pin="missing"; if (inputCfg->get("pin", o)) pin = o->stringValue();
    bool initialVal = false; if (inputCfg->get("initially", o)) initialVal = o->boolValue();
    MLMicroSeconds debounce = 80*MilliSecond; if (inputCfg->get("debounce", o)) debounce = o->doubleValue()*Second;
    MLMicroSeconds pollinterval = 250*MilliSecond; if (inputCfg->get("pollinterval", o)) pollinterval = o->doubleValue()*Second;
    input.digitalInput = DigitalIoPtr(new DigitalIo(pin.c_str(), false, initialVal));
    // set up handler
    input.digitalInput->setInputChangedHandler(boost::bind(&Inputs::inputChanged, this, input, _1), debounce, pollinterval);
    inputs.push_back(input);
  }
  initOperation();
  return Error::ok();
}


void Inputs::inputChanged(Input &aInput, bool aNewValue)
{
  JsonObjectPtr message = JsonObject::newObj();
  message->add("name", JsonObject::newString(aInput.name));
  message->add("state", JsonObject::newBool(aNewValue));
  sendEventMessage(message);
}


ErrorPtr Inputs::processRequest(ApiRequestPtr aRequest)
{
  return inherited::processRequest(aRequest);
}


JsonObjectPtr Inputs::status()
{
  JsonObjectPtr answer = inherited::status();
  if (answer->isType(json_type_object)) {
    for (InputsList::iterator pos = inputs.begin(); pos!=inputs.end(); ++pos) {
      answer->add(pos->name.c_str(), JsonObject::newBool(pos->digitalInput->isSet()));
    }
  }
  return answer;
}


// MARK: ==== inputs operation


void Inputs::initOperation()
{
  LOG(LOG_NOTICE, "initializing " FEATURE_NAME);
  setInitialized();
}


#endif // ENABLE_FEATURE_INPUTS
