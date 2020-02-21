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

#ifndef __p44featured_neuron_hpp__
#define __p44featured_neuron_hpp__

#include "analogio.hpp"
#include "ledchaincomm.hpp"

#include "feature.hpp"

#include <math.h>

namespace p44 {

  class Neuron : public Feature
  {
    typedef Feature inherited;

    string ledChain1Name;
    LEDChainCommPtr ledChain1;
    string ledChain2Name;
    LEDChainCommPtr ledChain2;
    AnalogIoPtr sensor;

    double movingAverageCount = 20;
    double threshold = 250;
    int numAxonLeds = 70;
    int numBodyLeds = 100;
    double avg = 0;


    enum AxonState { AxonIdle, AxonFiring };
    AxonState axonState = AxonIdle;
    enum BodyState { BodyIdle, BodyGlowing, BodyFadeOut };
    BodyState bodyState = BodyIdle;

    MLTicket ticketMeasure;
    MLTicket ticketAnimateAxon;
    MLTicket ticketAnimateBody;

    int pos = 0;
    double phi = 0;
    double glowBrightness = 1;

    bool isMuted = false;

  public:

    Neuron(const string aLedChain1Name, const string aLedChain2Name, AnalogIoPtr aSensor);

    void start(double aMovingAverageCount, double aThreshold, int aNumAxonLeds, int aNumBodyLeds);
    void fire(double aValue = 0);

    /// initialize the feature
    /// @param aInitData the init data object specifying feature init details
    /// @return error if any, NULL if ok
    virtual ErrorPtr initialize(JsonObjectPtr aInitData) override;

    /// handle request
    /// @param aRequest the API request to process
    /// @return NULL to send nothing at return (but possibly later via aRequest->sendResponse),
    ///   Error::ok() to just send a empty response, or error to report back
    virtual ErrorPtr processRequest(ApiRequestPtr aRequest) override;

    /// @return status information object for initialized feature, bool false for uninitialized
    virtual JsonObjectPtr status() override;

  private:
    void initOperation();
    void measure(MLTimer &aTimer);
    void animateAxon(MLTimer &aTimer);
    void animateBody(MLTimer &aTimer);

    ErrorPtr fire(ApiRequestPtr aRequest);
    ErrorPtr glow(ApiRequestPtr aRequest);
    ErrorPtr mute(ApiRequestPtr aRequest);

    void neuronSpike(double aValue);

  };

} // namespace p44

#endif /* __p44featured_neuron_hpp__ */
