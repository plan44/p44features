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

#include "indicators.hpp"

#if ENABLE_FEATURE_INDICATORS

#include "application.hpp"

using namespace p44;

#define FEATURE_NAME "indicators"

#include "viewfactory.hpp"
#include "lightspotview.hpp"

Indicators::Indicators(LEDChainArrangementPtr aLedChainArrangement) :
  inherited(FEATURE_NAME),
  ledChainArrangement(aLedChainArrangement)
{
}


void Indicators::reset()
{
  stop();
  if (ledChainArrangement) ledChainArrangement->end();
  if (indicatorsView) indicatorsView.reset();
  inherited::reset();
}


Indicators::~Indicators()
{
  reset();
}


// MARK: ==== indicators API


ErrorPtr Indicators::initialize(JsonObjectPtr aInitData)
{
  LOG(LOG_NOTICE, "initializing " FEATURE_NAME);
  reset();
  // { "cmd":"init", "indicators": {} }
  // { "cmd":"init", "indicators": { "rootview": <p44lrgraphics-view-config> } }
  ErrorPtr err;
  JsonObjectPtr o;
  if (ledChainArrangement) {
    // get the ledChainArrangement's current rootview (possibly shared with other feature)
    P44ViewPtr rootView = ledChainArrangement->getRootView();
    if (aInitData->get("rootview", o)) {
      // configure the root view
      err = rootView->configureFromResourceOrObj(o, FEATURE_NAME "/");
    }
    if (!rootView) {
      // no existing or explicitly initialized rootview: install default viewstack as root
      PixelRect r = ledChainArrangement->totalCover();
      indicatorsView = ViewStackPtr(new ViewStack);
      indicatorsView->setLabel("INDICATORS");
      indicatorsView->setFrame(r);
      indicatorsView->setFullFrameContent();
      indicatorsView->setBackgroundColor(black);
      indicatorsView->setPositioningMode(P44View::noAdjust);
      // the stack is also the root view
      rootView = indicatorsView;
    }
    if (Error::isOK(err)) {
      // install root view
      ledChainArrangement->setRootView(rootView);
      // start running
      initOperation();
    }
  }
  else {
    err = TextError::err("no led chains configured");
  }
  return err;
}


ErrorPtr Indicators::processRequest(ApiRequestPtr aRequest)
{
  ErrorPtr err;
  JsonObjectPtr data = aRequest->getRequest();
  JsonObjectPtr o = data->get("cmd");
  if (o) {
    string cmd = o->stringValue();
    // decode commands
    //  minimally: { cmd: "indicate" } /* full area */
    //  normally: { cmd: "indicate", x:0, dx:20, effect="swipe" }
    //  full: { cmd: "indicate", x:0, dx:20, y:0, dy:1, effect:"pulse", t:1 }
    if (cmd=="indicate" && indicatorsView) {
      PixelRect f = indicatorsView->getContent(); // default to full view
      // common parameters
      if (data->get("x", o)) f.x = o->int32Value();
      if (data->get("y", o)) f.y = o->int32Value();
      if (data->get("dx", o)) f.dx = o->int32Value();
      if (data->get("dy", o)) f.dy = o->int32Value();
      MLMicroSeconds t = 0.5*Second;
      if (data->get("t", o)) t = o->doubleValue()*Second;
      PixelColor col = { 255, 0, 0, 255 }; // default to red
      if (data->get("color", o)) col = webColorToPixel(o->stringValue());
      // effect
      JsonObjectPtr viewCfg;
      P44ViewPtr effectView;
      if (!data->get("effect", o)) {
        o = JsonObject::newString("plain"); // default effect
      }
      if (o->isType(json_type_string)) {
        string effectName = o->stringValue();
        // check predefined effects
        if (effectName=="plain") {
          effectView = P44ViewPtr(new P44View);
          effectView->setBackgroundColor(col);
          effectView->setFrame(f);
          //effectView->setFullFrameContent();
        }
        else if (effectName=="swipe") {
          effectView = P44ViewPtr(new P44View);
          effectView->setForegroundColor(col);
          effectView->setFrame(f);
          effectView->setFullFrameContent();
          effectView->animatorFor("content_x")->from(-f.dx)->animate(f.dx, t);
        }
        else if (effectName=="pulse") {
          effectView = P44ViewPtr(new P44View);
          effectView->setBackgroundColor(col);
          effectView->setFrame(f);
          //effectView->setFullFrameContent();
          effectView->animatorFor("alpha")->from(0)->repeat(true, 2)->animate(255, t/2);
        }
        else if (effectName=="spot") {
          bool radial = false;
          if (data->get("radial", o)) radial = o->boolValue();
          LightSpotViewPtr lsp = LightSpotViewPtr(new LightSpotView);
          effectView = lsp;
          effectView->setFrame(f);
          effectView->setFullFrameContent();
          lsp->setRelativeContentOrigin(0, 0);
          lsp->setRelativeExtent(1); // fill the area
          lsp->setColoringParameters(col, -1, gradient_curve_cos, 0, gradient_none, 0, gradient_none, radial);
          effectView->animatorFor("alpha")->from(0)->repeat(true, 2)->animate(255, t/2);
        }
      }
      // not a predefined effect: could be JSON literal config or filename
      if (!viewCfg && !effectView) {
        viewCfg = Application::jsonObjOrResource(o, &err, FEATURE_NAME "/");
        if (Error::notOK(err)) return err;
      }
      if (viewCfg) {
        // add-in frame
        viewCfg->add("x", JsonObject::newInt32(f.x));
        viewCfg->add("y", JsonObject::newInt32(f.y));
        viewCfg->add("dx", JsonObject::newInt32(f.dx));
        viewCfg->add("dy", JsonObject::newInt32(f.dy));
        err = p44::createViewFromConfig(viewCfg, effectView, indicatorsView);
        if (Error::notOK(err)) return err;
      }
      if (!effectView) return TextError::err("No valid indicator effect");
      // now run
      runEffect(effectView, t, data);
      return Error::ok();
    }
    else if (cmd=="stop") {
      stop();
      return Error::ok();
    }
    return inherited::processRequest(aRequest);
  }
  else {
    // decode properties
    //%%% none yet
    return err ? err : Error::ok();
  }
}


JsonObjectPtr Indicators::status()
{
  JsonObjectPtr answer = inherited::status();
  if (answer->isType(json_type_object)) {
    answer->add("activeIndicators", JsonObject::newInt64(activeIndicators.size()));
  }
  return answer;
}


// MARK: ==== indicators operation

void Indicators::stop()
{
  for (EffectsList::iterator pos = activeIndicators.begin(); pos!=activeIndicators.end(); ++pos) {
    IndicatorEffectPtr eff = *pos;
    eff->ticket.cancel();
    indicatorsView->removeView(eff->view);
  }
  activeIndicators.clear();
}


void Indicators::runEffect(P44ViewPtr aView, MLMicroSeconds aDuration, JsonObjectPtr aConfig)
{
  LOG(LOG_INFO, "Starting effect");
  indicatorsView->pushView(aView);
  IndicatorEffectPtr effect = IndicatorEffectPtr(new IndicatorEffect);
  effect->view = aView;
  // and make sure it gets cleaned up after given time
  effect->ticket.executeOnce(boost::bind(&Indicators::effectDone, this, effect), aDuration);
  activeIndicators.push_back(effect);
  indicatorsView->requestUpdate();
}


void Indicators::effectDone(IndicatorEffectPtr aEffect)
{
  aEffect->view->stopAnimations();
  indicatorsView->removeView(aEffect->view);
  activeIndicators.remove(aEffect);
  indicatorsView->requestUpdate();
  LOG(LOG_INFO, "Effect Done");
}


void Indicators::initOperation()
{
  if (ledChainArrangement) {
    indicatorsView = boost::dynamic_pointer_cast<ViewStack>(ledChainArrangement->getRootView()->getView("INDICATORS"));
    ledChainArrangement->begin(true);
  }
  else {
    LOG(LOG_WARNING, FEATURE_NAME ": NOP: no ledchain connected");
  }
  setInitialized();
}



#endif // ENABLE_FEATURE_INDICATORS
