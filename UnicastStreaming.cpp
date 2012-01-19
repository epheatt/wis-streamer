/*
 * Copyright (C) 2005-2006 WIS Technologies International Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and the associated README documentation file (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
// Set up and start unicast streaming
// Implementation

#include "UnicastStreaming.hh"
#include "Options.hh"
#include "WISMPEG2TransportStreamServerMediaSubsession.hh"
#include "WISJPEGVideoServerMediaSubsession.hh"
#include "WISMPEG1or2VideoServerMediaSubsession.hh"
#include "WISMPEG4VideoServerMediaSubsession.hh"
#include "WISPCMAudioServerMediaSubsession.hh"

void setupUnicastStreaming(WISInput& inputDevice, ServerMediaSession* sms) {
  // Add a subsession for the desired video format (if any):
  if (packageFormat == PFMT_TRANSPORT_STREAM) {
    sms->addSubsession(WISMPEG2TransportStreamServerMediaSubsession::
		       createNew(sms->envir(), inputDevice));
  } else {
    switch (videoFormat) {
    case VFMT_NONE:
      break; // do nothing
    case VFMT_MJPEG:
      sms->addSubsession(WISJPEGVideoServerMediaSubsession
			 ::createNew(sms->envir(), inputDevice, videoBitrate));
      break;
    case VFMT_MPEG1:
    case VFMT_MPEG2:
      sms->addSubsession(WISMPEG1or2VideoServerMediaSubsession
			 ::createNew(sms->envir(), inputDevice, videoBitrate));
      break;
    case VFMT_MPEG4:
      sms->addSubsession(WISMPEG4VideoServerMediaSubsession
			 ::createNew(sms->envir(), inputDevice, videoBitrate));
      break;
    }
  }

  // Add a subsession for the desired audio format (if any):
  if (audioFormat == AFMT_NONE || packageFormat == PFMT_TRANSPORT_STREAM) return;
  sms->addSubsession(WISPCMAudioServerMediaSubsession::createNew(sms->envir(), inputDevice));
}
