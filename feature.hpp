//
//  Copyright (c) 2016-2020 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44features_feature_hpp__
#define __p44features_feature_hpp__

#include "featureapi.hpp"

namespace p44 {

  class Feature;
  typedef boost::intrusive_ptr<Feature> FeaturePtr;

  class Feature : public P44LoggingObj
  {

    bool initialized;
    string name;

  public:

    Feature(const string aName);

    /// @return the prefix to be used for logging from this object
    virtual string logContextPrefix() { return string_format("Feature '%s'", name.c_str()); }

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

    #if ENABLE_P44SCRIPT
    /// @return a new script object representing this feature. Derived device classes might return different types of device object.
    virtual ScriptObjPtr newFeatureObj();
    #endif

  protected:

    void setInitialized() { initialized = true; }

    /// send event message
    /// @param aMessage the API request to process
    /// @note event messages are messages sent by a feature without a preceeding request
    void sendEventMessage(JsonObjectPtr aMessage);

  };


  #if ENABLE_P44SCRIPT
  namespace P44Script {

    /// represents a single "feature"
    class FeatureObj : public StructuredLookupObject
    {
      typedef StructuredLookupObject inherited;
    protected:
      FeaturePtr mFeature;
    public:
      FeatureObj(FeaturePtr aFeature);
      virtual string getAnnotation() const P44_OVERRIDE { return "feature"; };
      FeaturePtr feature() { return mFeature; }
    };

  } // namespace P44Script
  #endif // ENABLE_P44SCRIPT

} // namespace p44

#endif /* __p44features_feature_hpp__ */
