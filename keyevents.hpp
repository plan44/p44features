//
//  Copyright (c) 2022 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44features_keyevents_hpp__
#define __p44features_keyevents_hpp__

#include "feature.hpp"

#if ENABLE_FEATURE_KEYEVENTS

#include "fdcomm.hpp"


namespace p44 {


  class KeyEvents : public Feature
  {
    typedef Feature inherited;

    string mInputEventDevice;
    FdComm mEventStream;

  public:

    KeyEvents(const string aInputDevice);
    virtual ~KeyEvents();

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

  private:

    void initOperation();
    void eventDataHandler(ErrorPtr aError);

  };

} // namespace p44


#endif // ENABLE_FEATURE_KEYEVENTS

#endif /* __p44features_keyevents_hpp__ */
