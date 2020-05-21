//
//  Copyright (c) 2020 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44features_ledbars_hpp__
#define __p44features_ledbars_hpp__

#include "feature.hpp"

#if ENABLE_FEATURE_INDICATORS

#include "ledchaincomm.hpp"
#include "viewstack.hpp"

namespace p44 {

  class IndicatorEffect : public P44Obj
  {
  public:
    MLTicket ticket;
    P44ViewPtr view;
  };
  typedef boost::intrusive_ptr<IndicatorEffect> IndicatorEffectPtr;


  class Indicators : public Feature
  {
    typedef Feature inherited;

    LEDChainArrangementPtr ledChainArrangement;
    ViewStackPtr indicatorsView; // the view to put indicators onto

    typedef std::list<IndicatorEffectPtr> EffectsList;
    EffectsList activeIndicators;

  public:

    /// create indicators
    /// @param aLedChainArrangement the LED chain arrangement containing all indicators
    Indicators(LEDChainArrangementPtr aLedChainArrangement);
    virtual ~Indicators();

    /// reset the feature to uninitialized/re-initializable state
    virtual void reset() override;

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

    /// stop all running indicator effects
    void stop();

  private:

    void initOperation();

    void runEffect(P44ViewPtr aView, MLMicroSeconds aDuration, JsonObjectPtr aConfig);
    void effectDone(IndicatorEffectPtr aEffect);

  };

} // namespace p44


#endif // ENABLE_FEATURE_INDICATORS

#endif /* __p44features_ledbars_hpp__ */
