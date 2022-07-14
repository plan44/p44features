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

#include "keyevents.hpp"

#if ENABLE_FEATURE_KEYEVENTS

#include "application.hpp"

#include <sys/ioctl.h>

using namespace p44;

#define FEATURE_NAME "keyevents"


KeyEvents::KeyEvents(const string aInputDevice) :
  inherited(FEATURE_NAME),
  mInputEventDevice(aInputDevice),
  mEventFd(-1)
{
  // must always be explicitly initialized
}


void KeyEvents::reset()
{
  if (mEventFd>=0) {
    MainLoop::currentMainLoop().unregisterPollHandler(mEventFd);
    close(mEventFd);
    mEventFd = -1;
  }
  inherited::reset();
}


KeyEvents::~KeyEvents()
{
  reset();
}



// MARK: ==== KeyEvents API

ErrorPtr KeyEvents::initialize(JsonObjectPtr aInitData)
{
  // { "cmd":"init", "keyevents": true }
  initOperation();
  return Error::ok();
}


ErrorPtr KeyEvents::processRequest(ApiRequestPtr aRequest)
{
  return inherited::processRequest(aRequest);
}


// MARK: ==== KeyEvents operation

#ifndef EV_KEY
  #define EV_KEY      0x01
#endif

struct input_event {
  struct timeval time;
  unsigned short type;
  unsigned short code;
  unsigned int value;
};


void KeyEvents::initOperation()
{
  reset();
  mEventFd = open(mInputEventDevice.c_str(), O_RDONLY|O_NONBLOCK);
  if (mEventFd<0) {
    ErrorPtr err = SysError::errNo();
    OLOG(LOG_ERR, "Cannot initialize: %s", Error::text(err));
  }
  else {
    MainLoop::currentMainLoop().registerPollHandler(mEventFd, POLLIN, boost::bind(&KeyEvents::eventDataHandler, this, _1, _2));
    OLOG(LOG_INFO, "expecting event packets of %lu bytes each from %s", sizeof(input_event), mInputEventDevice.c_str());
  }
  setInitialized();
}


bool KeyEvents::eventDataHandler(int aFD, int aPollFlags)
{
  union {
    struct input_event ev;
    uint8_t bytes[sizeof(input_event)];
  } eventbuf;

  if (aPollFlags & POLLIN) {
    // get data
    ErrorPtr err;
    DBGOLOG(LOG_DEBUG,"evendata poll handler with flags = 0x%x", aPollFlags);
    ssize_t res = 1; // to get it started
    while (true) {
      res = read(aFD, eventbuf.bytes, sizeof(input_event));
      if (res<0) {
        // EAGAIN means no more events for now
        if (errno!=EAGAIN) {
          // real error
          err = SysError::errNo();
        }
        break;
      }
      if (res<sizeof(input_event)) {
        err = TextError::err("event read(%lu) only returns %zu bytes", sizeof(input_event), res);
        break;
      }
      if (res==0) break; // done
      // decode event
      if (eventbuf.ev.type==EV_KEY) {
        // is a keyboard event. Check value: 0=release, 1=press, 2=autorepeat
        // (see https://www.kernel.org/doc/Documentation/input/input.txt)
        if (eventbuf.ev.value==0 || eventbuf.ev.value==1) {
          // code is the key's code, see: https://github.com/torvalds/linux/blob/master/include/uapi/linux/input-event-codes.h
          JsonObjectPtr message = JsonObject::newObj();
          message->add("keycode", JsonObject::newInt32(eventbuf.ev.code));
          message->add("pressed", JsonObject::newBool(eventbuf.ev.value!=0));
          sendEventMessage(message);
        }
      }
    }
    if (Error::notOK(err)) {
      OLOG(LOG_WARNING, "error reading event device: %s", Error::text(err));
    }
    return true; // processed some IO
  }
  return false; // nothing processed, only checked flags
}

#endif // ENABLE_FEATURE_KEYEVENTS
