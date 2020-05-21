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

#ifndef __p44features_featureapi_hpp__
#define __p44features_featureapi_hpp__

#include "p44features_common.hpp"

#include "jsoncomm.hpp"

#if ENABLE_EXPRESSIONS
  #include "expressions.hpp"
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



//#error get rid of that, use real script instead
  class FeatureJsonScriptContext : public P44Obj
  {
    friend class FeatureApi;

    MLTicket scriptTicket;

  public:
    void kill() { scriptTicket.cancel(); }

  };
  typedef boost::intrusive_ptr<FeatureJsonScriptContext> FeatureJsonScriptContextPtr;


  #if EXPRESSION_SCRIPT_SUPPORT

  class FeatureApiScriptContext : public ScriptExecutionContext
  {
    typedef ScriptExecutionContext inherited;
    friend class FeatureApi;

    JsonObjectPtr apiMessage;

  public:

    FeatureApiScriptContext(JsonObjectPtr aMessage) : apiMessage(aMessage) {};

    /// evaluation of synchronously implemented functions which immediately return a result
    virtual bool evaluateFunction(const string &aFunc, const FunctionArguments &aArgs, ExpressionValue &aResult) override;

    /// evaluation of asynchronously implemented functions which may yield execution and resume later
    virtual bool evaluateAsyncFunction(const string &aFunc, const FunctionArguments &aArgs, bool &aNotYielded) override;

  };
  typedef boost::intrusive_ptr<FeatureApiScriptContext> FeatureApiScriptContextPtr;

  #endif // EXPRESSION_SCRIPT_SUPPORT




  class FeatureApi : public P44Obj
  {
    friend class FeatureApiRequest;

    SocketCommPtr apiServer;
    JsonCommPtr connection;

    typedef std::map<string, FeaturePtr> FeatureMap;
    FeatureMap featureMap;

    string devicelabel;

    MLTicket scriptTicket;

    #if EXPRESSION_SCRIPT_SUPPORT
    string eventScript;

    typedef std::list<FeatureApiScriptContextPtr> FeatureApiScriptContextsList;
    FeatureApiScriptContextsList scriptRequests;

    TimedEvaluationContext trigger;

    #endif

  public:

    FeatureApi();
    virtual ~FeatureApi();

    /// singleton
    static FeatureApiPtr sharedApi();

    /// add a feature
    /// @param aFeature the to add
    void addFeature(FeaturePtr aFeature);

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
    ErrorPtr executeJson(JsonObjectPtr aJsonCmds, SimpleCB aFinishedCallback = NULL, FeatureJsonScriptContextPtr* aContextP = NULL);

    typedef map<string, string> SubstitutionMap;

    void substituteVars(string &aString, SubstitutionMap *aSubstitutionsP, ErrorPtr &err);

    /// execute JSON request(s) from a string
    /// @param aJsonString JSON string to execute
    /// @param aFinishedCallback called when all commands are done
    /// @param aSubstitutionsP pointer to map of substitutions
    /// @return ok or error
    ErrorPtr runJsonString(string aJsonString, SimpleCB aFinishedCallback = NULL, FeatureJsonScriptContextPtr* aContextP = NULL, SubstitutionMap* aSubstitutionsP = NULL);

    /// execute JSON request(s) from a file
    /// @param aScriptPath resource dir relative (or absolute) path to script
    /// @param aFinishedCallback called when all commands are done
    /// @return ok or error
    ErrorPtr runJsonFile(const string aScriptPath, SimpleCB aFinishedCallback = NULL, FeatureJsonScriptContextPtr* aContextP = NULL, SubstitutionMap* aSubstitutionsP = NULL);

    #endif // ENABLE_LEGACY_FEATURE_SCRIPTS



    /// get feature by name
    /// @param aFeatureName name of the feature
    /// @return feature or NULL if no such feature
    FeaturePtr getFeature(const string aFeatureName);

    /// start the API
    void start(const string aApiPort);

    /// send (event) message to API
    void sendMessage(JsonObjectPtr aMessage);

    #if EXPRESSION_SCRIPT_SUPPORT

    /// script function support
    static bool evaluateAsyncFeatureFunctions(EvaluationContext* aEvalContext, const string &aFunc, const FunctionArguments &aArgs, bool &aNotYielded);
    static void apiCallFunctionDone(EvaluationContext* aEvalContext, JsonObjectPtr aResult, ErrorPtr aError);

    /// queue script for execution
    /// @param aScriptCode the code for the script
    /// @param aMessage the message related to the script call, which will be available from messsage() built-in function
    void queueScript(const string &aScriptCode, JsonObjectPtr aMessage = NULL);

    void runNextScript();
    void scriptDone(FeatureApiScriptContextPtr aScript);
    void triggerEvaluationExecuted(ExpressionValue aEvaluationResult);

    #endif // EXPRESSION_SCRIPT_SUPPORT

  private:

    SocketCommPtr apiConnectionHandler(SocketCommPtr aServerSocketComm);
    void apiRequestHandler(JsonCommPtr aConnection, ErrorPtr aError, JsonObjectPtr aRequest);
    ErrorPtr processRequest(ApiRequestPtr aRequest);

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

} // namespace p44



#endif /* __p44features_featureapi_hpp__ */
