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

#ifndef __p44features_feature_hpp__
#define __p44features_feature_hpp__

#include "featureapi.hpp"

namespace p44 {

  class Feature : public P44Obj
  {

    bool initialized;
    string name;

  public:

    Feature(const string aName);

    /// initialize the feature
    /// @param aInitData the init data object specifying feature init details
    /// @return error if any, NULL if ok
    virtual ErrorPtr initialize(JsonObjectPtr aInitData) = 0;

    /// reset the feature to uninitialized/re-initializable state
    virtual void reset();

    /// @return the name of the feature
    string getName() const { return name; }

    /// @return true if feature is initialized
    bool isInitialized() const;

    /// handle request
    /// @param aRequest the API request to process
    /// @return NULL to send nothing at return (but possibly later via aRequest->sendResponse),
    ///   Error::ok() to just send a empty response, or error to report back
    virtual ErrorPtr processRequest(ApiRequestPtr aRequest);

    /// @return status information object for initialized feature, bool false for uninitialized
    virtual JsonObjectPtr status();

    /// command line tool mode
    /// @return error if tool fails, ok otherwise
    virtual ErrorPtr runTool();

  protected:

    void setInitialized() { initialized = true; }

  };
  typedef boost::intrusive_ptr<Feature> FeaturePtr;

} // namespace p44



#endif /* __p44features_feature_hpp__ */
