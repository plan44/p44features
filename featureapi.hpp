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

#ifndef __p44features_featureapi_hpp__
#define __p44features_featureapi_hpp__

#include "p44features_common.hpp"

#include "jsoncomm.hpp"
#include "p44script.hpp"
#if ENABLE_LEDARRANGEMENT
  #include "ledchaincomm.hpp"
#endif


namespace p44 {

  typedef boost::function<void (JsonObjectPtr aResponse, ErrorPtr aError)> RequestDoneCB;
  typedef boost::function<void (JsonObjectPtr aData)> InitFeatureCB;

  class ApiRequest;
  typedef boost::intrusive_ptr<ApiRequest> ApiRequestPtr;

  class FeatureApi;
  typedef boost::intrusive_ptr<FeatureApi> FeatureApiPtr;

  class Feature;
  typedef boost::intrusive_ptr<Feature> FeaturePtr;


  /// abstract API request base class
  class ApiRequest : public P44Obj
  {

    JsonObjectPtr request;

  public:

    ApiRequest(JsonObjectPtr aRequest) : request(aRequest) {};
    virtual ~ApiRequest() {};

    /// get the request to process
    /// @return get the request JSON object
    JsonObjectPtr getRequest() { return request; }

    /// send response
    /// @param aResponse JSON response to send
    virtual void sendResponse(JsonObjectPtr aResponse, ErrorPtr aError) = 0;

  };


  /// direct TCP API request
  class FeatureApiRequest : public ApiRequest
  {
    typedef ApiRequest inherited;
    JsonCommPtr connection;

  public:

    FeatureApiRequest(JsonObjectPtr aRequest, JsonCommPtr aConnection);
    virtual ~FeatureApiRequest();

    /// send response
    /// @param aResponse JSON response to send
    /// @param aError error to report back
    virtual void sendResponse(JsonObjectPtr aResponse, ErrorPtr aError) override;

  };


  /// internal request
  class InternalRequest : public ApiRequest
  {
    typedef ApiRequest inherited;

  public:

    InternalRequest(JsonObjectPtr aRequest);
    virtual ~InternalRequest();

    /// send response
    /// @param aResponse JSON response to send
    /// @param aError error to report back
    virtual void sendResponse(JsonObjectPtr aResponse, ErrorPtr aError) override;
  };


  typedef boost::function<void (JsonObjectPtr aResult, ErrorPtr aError)> RequestDoneCB;

  /// API request with callback for sending result (for embedding in other APIs)
  class APICallbackRequest : public ApiRequest
  {
    typedef ApiRequest inherited;

    RequestDoneCB requestDoneCB;

  public:

    APICallbackRequest(JsonObjectPtr aRequest, RequestDoneCB aRequestDoneCB);
    virtual ~APICallbackRequest();

    void sendResponse(JsonObjectPtr aResponse, ErrorPtr aError) override;
  };



  #if ENABLE_LEGACY_FEATURE_SCRIPTS
  class FeatureJsonScriptContext : public P44Obj
  {
    friend class FeatureApi;

    MLTicket scriptTicket;

  public:
    void kill() { scriptTicket.cancel(); }

  };
  typedef boost::intrusive_ptr<FeatureJsonScriptContext> FeatureJsonScriptContextPtr;
  #endif


  #if ENABLE_P44SCRIPT
  using namespace P44Script;
  #endif

  class FeatureApi : public P44LoggingObj
  {
    friend class FeatureApiRequest;

    SocketCommPtr apiServer;
    JsonCommPtr connection;

    typedef std::map<string, FeaturePtr> FeatureMap;
    FeatureMap featureMap;

    string devicelabel;

    MLTicket scriptTicket;

  public:

    FeatureApi();
    virtual ~FeatureApi();

    /// @return the prefix to be used for logging from this object
    virtual string logContextPrefix() { return "FeatureApi"; }


    /// singleton
    static FeatureApiPtr sharedApi();
    static FeatureApiPtr existingSharedApi();

    /// add a feature
    /// @param aFeature the to add
    void addFeature(FeaturePtr aFeature);

    #if ENABLE_LEDARRANGEMENT
    /// add features as specified on command line to the global sharedApi()
    /// @param aLedChainArrangement pass in the LED chain arrangement, if there is one
    ///   (needed for features based on p44lrgraphics)
    static void addFeaturesFromCommandLine(LEDChainArrangementPtr aLedChainArrangement);
    #else
    static void addFeaturesFromCommandLine();
    #endif

    /// handle request
    /// @param aRequest the request to process
    /// @note usually this is called internally, but method is exposed to allow injecting
    ///   api requests from other sources (such as Web API)
    void handleRequest(ApiRequestPtr aRequest);


    #if ENABLE_LEGACY_FEATURE_SCRIPTS

    /// Json-"Scripts"
    /// @{

    /// execute JSON request(s) - can be called internally, no answer
    /// @param aJsonCmds a single JSON command request or a array with multiple requests
    /// @param aFinishedCallback called when all commands are done
    /// @return ok or error
    ErrorPtr executeJson(JsonObjectPtr aJsonCmds, SimpleCB aFinishedCallback = NoOP, FeatureJsonScriptContextPtr* aContextP = NULL);

    typedef map<string, string> SubstitutionMap;

    void substituteVars(string &aString, SubstitutionMap *aSubstitutionsP, ErrorPtr &err);

    /// execute JSON request(s) from a string
    /// @param aJsonString JSON string to execute
    /// @param aFinishedCallback called when all commands are done
    /// @param aSubstitutionsP pointer to map of substitutions
    /// @return ok or error
    ErrorPtr runJsonString(string aJsonString, SimpleCB aFinishedCallback = NoOP, FeatureJsonScriptContextPtr* aContextP = NULL, SubstitutionMap* aSubstitutionsP = NULL);

    /// execute JSON request(s) from a file
    /// @param aScriptPath resource dir relative (or absolute) path to script
    /// @param aFinishedCallback called when all commands are done
    /// @return ok or error
    ErrorPtr runJsonFile(const string aScriptPath, SimpleCB aFinishedCallback = NoOP, FeatureJsonScriptContextPtr* aContextP = NULL, SubstitutionMap* aSubstitutionsP = NULL);

    #endif // ENABLE_LEGACY_FEATURE_SCRIPTS



    /// get feature by name
    /// @param aFeatureName name of the feature
    /// @return feature or NULL if no such feature
    FeaturePtr getFeature(const string aFeatureName);

    /// start the API
    void start(const string aApiPort);

    /// send (event) message to API
    void sendEventMessage(JsonObjectPtr aEventMessage);

    #if ENABLE_P44SCRIPT
    void scriptExecHandler(ApiRequestPtr aRequest, ScriptObjPtr aResult);

    EventSource mFeatureEventSource; /// feature events will be distributed via this source
    EventSource mUnhandledRequestSource; ///< not internally handled feature API requests will be distributed via this source

    #endif // ENABLE_P44SCRIPT


    ErrorPtr processRequest(ApiRequestPtr aRequest);
    void sendEventMessageToApiClient(JsonObjectPtr aEventMessage);
    void sendEventMessageInternally(JsonObjectPtr aEventMessage);

  private:

    SocketCommPtr apiConnectionHandler(SocketCommPtr aServerSocketComm);
    void apiRequestHandler(JsonCommPtr aConnection, ErrorPtr aError, JsonObjectPtr aRequest);

    /// send response via main API connection.
    /// @note: only for FeatureApiRequest
    void sendResponse(JsonObjectPtr aResponse);


    ErrorPtr init(ApiRequestPtr aRequest);
    ErrorPtr reset(ApiRequestPtr aRequest);
    ErrorPtr now(ApiRequestPtr aRequest);
    ErrorPtr status(ApiRequestPtr aRequest);
    ErrorPtr ping(ApiRequestPtr aRequest);
    ErrorPtr features(ApiRequestPtr aRequest);


    #if ENABLE_LEGACY_FEATURE_SCRIPTS

    ErrorPtr call(ApiRequestPtr aRequest);

    void executeNextCmd(JsonObjectPtr aCmds, int aIndex, FeatureJsonScriptContextPtr aContext, SimpleCB aFinishedCallback);
    void runCmd(JsonObjectPtr aCmds, int aIndex, FeatureJsonScriptContextPtr aContext, SimpleCB aFinishedCallback);

    #endif // ENABLE_LEGACY_FEATURE_SCRIPTS

  };


  class FeatureApiError : public Error
  {
  public:
    static const char *domain() { return "FeatureApiError"; }
    virtual const char *getErrorDomain() const { return FeatureApiError::domain(); };
    FeatureApiError() : Error(Error::NotOK) {};

    /// factory method to create string error fprint style
    static ErrorPtr err(const char *aFmt, ...) __printflike(1,2);
  };


  #if ENABLE_P44SCRIPT
  namespace P44Script {

    /// represents a feature API request/call
    class FeatureRequestObj : public JsonValue
    {
      typedef JsonValue inherited;

      ApiRequestPtr mRequest;

    public:
      FeatureRequestObj(ApiRequestPtr aRequest);
      void sendResponse(JsonObjectPtr aResponse, ErrorPtr aError);
      virtual string getAnnotation() const P44_OVERRIDE;
      virtual const ScriptObjPtr memberByName(const string aName, TypeInfo aMemberAccessFlags = none) P44_OVERRIDE;
    };


    /// represents the global objects related to p44features
    class FeatureApiLookup : public BuiltInMemberLookup
    {
      typedef BuiltInMemberLookup inherited;
    public:
      FeatureApiLookup();

      /// static helper for implementing calls
      static void featureCallDone(BuiltinFunctionContextPtr f, JsonObjectPtr aResult, ErrorPtr aError);

    };

  } // namespace P44Script
  #endif // ENABLE_P44SCRIPT


  #if ENABLE_FEATURE_LIGHT
    #define FEATURE_LIGHT_CMDLINEOPTS \
      { 0  , "light",          true,  "doinit;enable light feature (and optionally init)" }, \
      { 0  , "pwmdimmer",      true,  "pinspec;PWM dimmer output pin" },
  #else
    #define FEATURE_LIGHT_CMDLINEOPTS
  #endif
  #if ENABLE_FEATURE_INPUTS
    #define FEATURE_INPUTS_CMDLINEOPTS \
      { 0  , "inputs",         false, "enable generic inputs" },
  #else
    #define FEATURE_INPUTS_CMDLINEOPTS
  #endif
  #if ENABLE_FEATURE_KEYEVENTS
    #define FEATURE_KEYEVENTS_CMDLINEOPTS \
      { 0  , "keyevents",      true, "eventdevice;enable (e.g. USB) keyboard event inputs" },
  #else
    #define FEATURE_KEYEVENTS_CMDLINEOPTS
  #endif
  #if ENABLE_FEATURE_DISPMATRIX
    #define FEATURE_DISPMATRIX_CMDLINEOPTS \
      { 0  , "dispmatrix",     true,  "viewcfg|0;enable display matrix (and optionally init with viewcfg)" },
  #else
    #define FEATURE_DISPMATRIX_CMDLINEOPTS
  #endif
  #if ENABLE_FEATURE_INDICATORS
    #define FEATURE_INDICATORS_CMDLINEOPTS \
      { 0  , "indicators",     false,  "enable LED indicators" },
  #else
    #define FEATURE_INDICATORS_CMDLINEOPTS
  #endif
  #if ENABLE_FEATURE_SPLITFLAPS
    #define FEATURE_SPLITFLAPS_CMDLINEOPTS \
      { 0  , "splitflapconn",  true,  "serial_if;RS485 serial interface where display is connected (/device or IP:port)" }, \
      { 0  , "splitflaptxen",  true,  "pinspec;a digital output pin specification for TX driver enable or DTR or RTS" }, \
      { 0  , "splitflaptxoff", true,  "delay;time to keep tx enabled after sending [ms], defaults to 0" }, \
      { 0  , "splitflaprxen",  true,  "pinspec;a digital output pin specification for RX driver enable" },
  #else
    #define FEATURE_SPLITFLAPS_CMDLINEOPTS
  #endif
  #if ENABLE_FEATURE_RFIDS
    #define FEATURE_RFIDS_CMDLINEOPTS \
      { 0  , "rfidspibus",     true,  "spi_bus;enable RFIDs with SPI bus specification (10s=bus number, 1s=CS number)" }, \
      { 0  , "rfidselectpins", true,  "pinspec[,pinspec...];List of GPIO numbers driving the CS selector multiplexer, MSBit first" }, \
      { 0  , "rfidreset",      true,  "pinspec;RFID hardware reset signal (assuming noninverted connection to RFID readers)" }, \
      { 0  , "rfidirq",        true,  "pinspec;RFID hardware IRQ signal (assuming noninverted connection to RFID readers)" },
  #else
    #define XXX
  #endif
  #if ENABLE_FEATURE_WIFITRACK
    #define FEATURE_WIFITRACK_CMDLINEOPTS \
      { 0  , "wifitrack",      true,  "doinit;enable wifitrack (and optionally init)" }, \
      { 0  , "wifimonif",      true,  "interface;wifi monitoring interface to use" }, \
      { 0  , "wifidboffs",     true,  "offset;offset into radiotap to get RSSi (driver dependent)" },
  #else
    #define FEATURE_WIFITRACK_CMDLINEOPTS
  #endif
  #if ENABLE_FEATURE_HERMEL
    #define FEATURE_HERMEL_CMDLINEOPTS \
      { 0  , "hermel",         false, "doinit;enable hermel (and optionally init)" }, \
      { 0  , "pwmleft",        true,  "pinspec;PWM left bumper output pin" }, \
      { 0  , "pwmright",       true,  "pinspec;PWM right bumper output pin" },
  #else
    #define FEATURE_HERMEL_CMDLINEOPTS
  #endif
  #if ENABLE_FEATURE_NEURON
    #define FEATURE_NEURON_CMDLINEOPTS \
      { 0  , "neuron",         true,  "mvgAvgCnt,threshold,nAxonLeds,nBodyLeds;start neuron" }, \
      { 0  , "sensor0",        true,  "pinspec;analog sensor0 input to use" }, \
      { 0  , "sensor1",        true,  "pinspec;analog sensor1 input to use" },
  #else
    #define FEATURE_NEURON_CMDLINEOPTS
  #endif
  #if ENABLE_FEATURE_MIXLOOP
    #define FEATURE_MIXLOOP_CMDLINEOPTS \
      { 0  , "mixloop",        true,  "doinit;enable mixloop (and optionally init)" }, \
      { 0  , "ledchain2",      true,  "devicepath;ledchain2 device to use" }, \
      { 0  , "ledchain3",      true,  "devicepath;ledchain3 device to use" },
  #else
    #define FEATURE_MIXLOOP_CMDLINEOPTS
  #endif

  /// - P44 features related command options
  #define P44FEATURE_CMDLINE_OPTIONS \
    FEATURE_LIGHT_CMDLINEOPTS \
    FEATURE_INPUTS_CMDLINEOPTS \
    FEATURE_KEYEVENTS_CMDLINEOPTS \
    FEATURE_DISPMATRIX_CMDLINEOPTS \
    FEATURE_INDICATORS_CMDLINEOPTS \
    FEATURE_SPLITFLAPS_CMDLINEOPTS \
    FEATURE_RFIDS_CMDLINEOPTS \
    FEATURE_WIFITRACK_CMDLINEOPTS \
    FEATURE_NEURON_CMDLINEOPTS \
    FEATURE_HERMEL_CMDLINEOPTS \
    FEATURE_MIXLOOP_CMDLINEOPTS \
    { 0  , "featureapiport", true,  "port;server port number for Feature JSON API (default=none)" }


} // namespace p44



#endif /* __p44features_featureapi_hpp__ */
