//
//  Copyright (c) 2020 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44features_inputs_hpp__
#define __p44features_inputs_hpp__

#include "feature.hpp"

#if ENABLE_FEATURE_INPUTS

#include "digitalio.hpp"


namespace p44 {


  class Input
  {
  public:
    string name;
    DigitalIoPtr digitalInput;
  };


  class Inputs : public Feature
  {
    typedef Feature inherited;

    typedef std::list<Input> InputsList;
    InputsList inputs;

  public:

    Inputs();
    virtual ~Inputs();

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

  private:

    void initOperation();
    void inputChanged(Input &aInput, bool aNewValue);

  };

} // namespace p44


#endif // ENABLE_FEATURE_INPUTS

#endif /* __p44features_inputs_hpp__ */
