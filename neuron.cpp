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

#include "neuron.hpp"

#if ENABLE_FEATURE_NEURON

#include "application.hpp"

#include <boost/algorithm/string.hpp>

using namespace p44;

Neuron::Neuron(const string aLedChain1Name, const string aLedChain2Name, AnalogIoPtr aSensor, const string aStartCfg) :
  inherited("neuron")
{
  ledChain1Name = aLedChain1Name;
  ledChain2Name = aLedChain2Name;
  sensor = aSensor;
  // check for commandline-triggered standalone operation
  if (aStartCfg!="0") {
    initOperation();
    std::vector<std::string> neuronOptions;
    boost::split(neuronOptions, aStartCfg, boost::is_any_of(","), boost::token_compress_on);
    if(neuronOptions.size() != 4) {
      fprintf(stderr, "neuron needs 4 parameters: mvgAvgCnt,threshold,nAxonLeds,nBodyLeds\n");
      CmdLineApp::sharedCmdLineApp()->terminateApp(EXIT_FAILURE);
      return;
    }
    int movingAverageCount = atoi(neuronOptions[0].c_str());
    int threshold = atoi(neuronOptions[1].c_str());
    int numAxonLeds = atoi(neuronOptions[2].c_str());
    int numBodyLeds = atoi(neuronOptions[3].c_str());
    start(movingAverageCount, threshold, numAxonLeds, numBodyLeds);
  }
}


// MARK: ==== neuron API


ErrorPtr Neuron::initialize(JsonObjectPtr aInitData)
{
  initOperation();
  JsonObjectPtr o;
  if (!aInitData->get("movingAverageCount", o, true)) {
    return FeatureApiError::err("missing 'movingAverageCount'");
  }
  double movingAverageCount = o->doubleValue();
  if (!aInitData->get("threshold", o, true)) {
    return FeatureApiError::err("missing 'threshold'");
  }
  double threshold = o->doubleValue();
  if (!aInitData->get("numAxonLeds", o, true)) {
    return FeatureApiError::err("missing 'numAxonLeds'");
  }
  double numAxonLeds = o->int32Value();
  if (!aInitData->get("numBodyLeds", o, true)) {
    return FeatureApiError::err("missing 'numBodyLeds'");
  }
  double numBodyLeds = o->int32Value();
  // start
  start(movingAverageCount, threshold, numAxonLeds, numBodyLeds);
  setInitialized();
  return Error::ok();
}


ErrorPtr Neuron::processRequest(ApiRequestPtr aRequest)
{
  JsonObjectPtr o = aRequest->getRequest()->get("cmd");
  if (!o) {
    return FeatureApiError::err("missing 'cmd'");
  }
  string cmd = o->stringValue();
  if (cmd=="fire") {
    return fire(aRequest);
  }
  if (cmd=="glow") {
    return glow(aRequest);
  }
  if (cmd=="mute") {
    return mute(aRequest);
  }
  return inherited::processRequest(aRequest);
}


JsonObjectPtr Neuron::status()
{
  JsonObjectPtr answer = inherited::status();
  if (answer->isType(json_type_object)) {
    // TODO: add info here
  }
  return answer;
}


ErrorPtr Neuron::fire(ApiRequestPtr aRequest)
{
  fire();
  return Error::ok();
}


ErrorPtr Neuron::glow(ApiRequestPtr aRequest)
{
  OLOG(LOG_INFO, "glow");
  if(axonState != AxonIdle || bodyState != BodyIdle) return Error::ok();
  phi = 0;
  bodyState = BodyGlowing;
  glowBrightness = 0.5;
  ticketAnimateBody.executeOnce(boost::bind(&Neuron::animateBody, this, _1));
  return Error::ok();
}

ErrorPtr Neuron::mute(ApiRequestPtr aRequest)
{
  JsonObjectPtr o = aRequest->getRequest()->get("on");
  if (!o) {
    return FeatureApiError::err("missing 'on'");
  }
  isMuted = o->boolValue();
  return Error::ok();
}

// MARK: ==== neuron operation


void Neuron::initOperation()
{
  ledChain1 = LEDChainCommPtr(new LEDChainComm("WS2813.GRB", ledChain1Name, 100));
  ledChain2 = LEDChainCommPtr(new LEDChainComm("WS2813.GRB", ledChain2Name, 100));
  ledChain1->begin();
  ledChain1->show();
  ledChain2->begin();
  ledChain2->show();
  setInitialized();
}


void Neuron::start(double aMovingAverageCount, double aThreshold,int aNumAxonLeds, int aNumBodyLeds)
{
  movingAverageCount = aMovingAverageCount;
  threshold = aThreshold;
  numAxonLeds = aNumAxonLeds;
  numBodyLeds = aNumBodyLeds;
  ticketMeasure.executeOnce(boost::bind(&Neuron::measure, this, _1));
}

void Neuron::fire(double aValue)
{
  OLOG(LOG_INFO, "fires with avg=%f", aValue);
  if(axonState == AxonFiring) return;
  neuronSpike(aValue);
  pos = 0;
  axonState = AxonFiring;
  phi = 0;
  bodyState = BodyGlowing;
  glowBrightness = 1;
  ticketAnimateAxon.executeOnce(boost::bind(&Neuron::animateAxon, this, _1));
  ticketAnimateBody.executeOnce(boost::bind(&Neuron::animateBody, this, _1));
}

void Neuron::measure(MLTimer &aTimer)
{
  double value = sensor->value();
  avg = (avg * (movingAverageCount - 1) + value) / movingAverageCount;
  if(!isMuted && avg > threshold) fire(avg);
  ticketMeasure.executeOnce(boost::bind(&Neuron::measure, this, _1), 10 * MilliSecond);
}

void Neuron::animateAxon(MLTimer &aTimer)
{
  for(int i = 0; i < numAxonLeds; i++) {
    uint8_t c = abs(i - pos) < 4 ? 255 : 0;
    if(ledChain1) ledChain1->setColorXY(i, 0, c, c, 0);
  }
  if(ledChain1) ledChain1->show();
  if(pos++ < numAxonLeds) {
    ticketAnimateAxon.executeOnce(boost::bind(&Neuron::animateAxon, this, _1), 10 * MilliSecond);
  } else {
    axonState = AxonIdle;
  }
}

void Neuron::animateBody(MLTimer &aTimer)
{
  phi += 0.02;
  if(bodyState != BodyFadeOut && phi > M_PI) phi = 0;
  if(axonState == AxonIdle && bodyState == BodyGlowing) {
    bodyState = BodyFadeOut;
  }
  if(bodyState == BodyFadeOut && phi > M_PI) {
    phi = 0;
    bodyState = BodyIdle;
  } else {
    ticketAnimateBody.executeOnce(boost::bind(&Neuron::animateBody, this, _1), 10 * MilliSecond);
  }
  for(int i = 0; i < numBodyLeds; i++) {
    uint8_t c = sin(phi) * 255;
    if(bodyState == BodyGlowing && abs(i - pos) < 4) {
      if(ledChain2) ledChain2->setColorXY(i, 0, 0, glowBrightness * 255.0, glowBrightness * 178);
    } else {
      if(ledChain2) ledChain2->setColorXY(i, 0, 0, glowBrightness * c, glowBrightness * 0.7 * c);
    }
  }
  if(ledChain2) ledChain2->show();
}


void Neuron::neuronSpike(double aValue)
{
  JsonObjectPtr message = JsonObject::newObj();
  message->add("sensor", JsonObject::newDouble(aValue));
  sendEventMessage(message);
}

#endif // ENABLE_FEATURE_NEURON
