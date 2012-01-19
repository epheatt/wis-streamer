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
// An interface to the WIS GO7007 capture device.
// Implementation

#include "WISInput.hh"
#include "Options.hh"
#include "Err.hh"
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <linux/soundcard.h>
#ifndef __LINUX_VIDEODEV_H
#include "videodev.h"
#endif
#include "go7007.h"
////////// WISOpenFileSource definition //////////

// A common "FramedSource" subclass, used for reading from an open file:

class WISOpenFileSource: public FramedSource {
protected:
  WISOpenFileSource(UsageEnvironment& env, WISInput& input, int fileNo);
  virtual ~WISOpenFileSource();

  virtual void readFromFile() = 0;

private: // redefined virtual functions:
  virtual void doGetNextFrame();

private:
  static void incomingDataHandler(WISOpenFileSource* source, int mask);
  void incomingDataHandler1();

protected:
  WISInput& fInput;
  int fFileNo;
};


////////// WISVideoOpenFileSource definition //////////

class WISVideoOpenFileSource: public WISOpenFileSource {
public:
  WISVideoOpenFileSource(UsageEnvironment& env, WISInput& input);
  virtual ~WISVideoOpenFileSource();

protected: // redefined virtual functions:
  virtual void readFromFile();
};


////////// WISAudioOpenFileSource definition //////////

class WISAudioOpenFileSource: public WISOpenFileSource {
public:
  WISAudioOpenFileSource(UsageEnvironment& env, WISInput& input);
  virtual ~WISAudioOpenFileSource();

protected: // redefined virtual functions:
  virtual void readFromFile();
};


////////// WISInput implementation //////////

WISInput* WISInput::createNew(UsageEnvironment& env) {
  if (!fHaveInitialized) {
    if (!initialize(env)) return NULL;
    fHaveInitialized = True;
  }

  return new WISInput(env);
}

FramedSource* WISInput::videoSource() {
  if (fOurVideoSource == NULL) {
    fOurVideoSource = new WISVideoOpenFileSource(envir(), *this);
  }
  return fOurVideoSource;
}

FramedSource* WISInput::audioSource() {
  if (fOurAudioSource == NULL) {
    fOurAudioSource = new WISAudioOpenFileSource(envir(), *this);
  }
  return fOurAudioSource;
}

WISInput::WISInput(UsageEnvironment& env)
  : Medium(env) {
}

WISInput::~WISInput() {
}

Boolean WISInput::initialize(UsageEnvironment& env) {
  do {
    if (!openFiles(env)) break;
    if (!initALSA(env)) break;
    if (!initV4L(env)) break;

    return True;
  } while (0);

  // An error occurred
  return False;
}

static void printErr(UsageEnvironment& env, char const* str = NULL) {
  if (str != NULL) err(env) << str;
  env << ": " << strerror(env.getErrno()) << "\n";
}

Boolean WISInput::openFiles(UsageEnvironment& env) {
  do {
    int i = 0;
#ifndef IGNORE_DRIVER_CHECK
    // Make sure sysfs is mounted and the driver is loaded:
    struct stat si;
    char const* driverDirName = "/sys/bus/usb/drivers";
    if (stat(driverDirName, &si) < 0) {
      err(env) << "Unable to read \"" << driverDirName << "\""; printErr(env);
      env << "Is sysfs mounted on /sys?\n";
      break;
    }
    char const* driverFileName = "/sys/bus/usb/drivers/go7007";
    if (stat(driverFileName, &si) < 0) {
      err(env) << "Unable to read \"" << driverFileName << "\""; printErr(env);
      env << "Is the go7007-usb kernel module loaded?\n";
      break;
    }
    
    // Find a Video4Linux device associated with the go7007 driver:
    char sympath[PATH_MAX], sympath2[PATH_MAX], canonpath[PATH_MAX], gopath[PATH_MAX];
    int const maxFileNum = 20;
    for (i = 0; i < maxFileNum; ++i) {
      snprintf(sympath, sizeof sympath, "/sys/class/video4linux/video%d/driver", i);
      snprintf(sympath2, sizeof sympath2, "/sys/class/video4linux/video%d/device/driver", i);
      if (realpath(sympath, canonpath) != NULL) {
	if (strcmp(strrchr(canonpath, '/') + 1, "go7007") == 0) break;
      } else if (realpath(sympath2, canonpath) != NULL) { // alternative path
        if (strcmp(strrchr(canonpath, '/') + 1, "go7007") == 0) break;
      }
    }
    snprintf(sympath, sizeof sympath, "/sys/class/video4linux/video%d/device", i);
    if (i == maxFileNum || realpath(sympath, gopath) == NULL) {
      err(env) << "Driver loaded but no GO7007SB devices found.\n";
      env << "Is the device connected properly?\n";
      break;
    }
#endif

    // Open it:
    char vDeviceName[PATH_MAX];
    snprintf(vDeviceName, sizeof vDeviceName, "/dev/video%d", i);
    fOurVideoFileNo = open(vDeviceName, O_RDWR);
    if (fOurVideoFileNo < 0) {
      err(env) << "Unable to open \"" << vDeviceName << "\""; printErr(env);
      break;
    }
  
#ifndef IGNORE_DRIVER_CHECK
    // Find the ALSA device associated with this USB address:
    for (i = 0; i < maxFileNum; ++i) {
      snprintf(sympath, sizeof sympath, "/sys/class/sound/pcmC%dD0c/device", i);
      if (realpath(sympath, canonpath) == NULL) continue;
      if (strcmp(gopath, canonpath) == 0) break;
    }
    if (i == maxFileNum) {
      err(env) << "Unable to find a ALSA device associated with the GO7007SB device\n";
      break;
    }
#endif

    // Find the OSS emulation minor number for this ALSA device:
    char const* ossFileName = "/proc/asound/oss/devices";
    FILE* file = fopen(ossFileName, "r");
    if (file == NULL) {
      err(env) << "Unable to open \"" << ossFileName << "\""; printErr(env);
      env << "Is the snd_pcm_oss module loaded?\n";
      break;
    }
    int minor = -1;
    char line[128];
    while (fgets(line, sizeof line, file) != NULL) {
      int m, n;
      if (sscanf(line, "%d: [%u-%*u]: digital audio\n", &m, &n) != 2) continue;
      if (n == i) {
	minor = m;
	break;
      }
    }
    fclose(file);
    if (minor < 0) {
      err(env) << "Unable to find emulated OSS device node\n";
      break;
    }

    // Find the OSS file name for this minor number:
    char const* soundDirName = "/sys/class/sound";
    DIR* dir = opendir(soundDirName);
    if (dir == NULL) {
      err(env) << "Unable to read \"" << soundDirName << "\""; printErr(env);
      break;
    }
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
      int m = -1;
      if (strncmp(ent->d_name, "dsp", 3) != 0) continue;
      char adev[PATH_MAX];
      snprintf(adev, sizeof adev, "%s/%s/dev", soundDirName, ent->d_name);
      file = fopen(adev, "r");
      if (file == NULL) continue;
      if (fgets(line, sizeof line, file) != NULL) {
	sscanf(line, "%*d:%d\n", &m);
      }
      fclose(file);
      if (m == minor) break;
    }
    closedir(dir);
    if (ent == NULL) {
      err(env) << "Unable to find emulated OSS device.\n";
      break;
    }

    // Open it:
    char aDeviceName[PATH_MAX];
    snprintf(aDeviceName, sizeof aDeviceName, "/dev/%s", ent->d_name);
    fOurAudioFileNo = open(aDeviceName, O_RDONLY);
    if (fOurAudioFileNo < 0) {
      err(env) << "Unable to open \"" << aDeviceName << "\""; printErr(env);
      break;
    }
    fcntl(fOurAudioFileNo, F_SETFL, O_NONBLOCK);
  
    return True;
  } while (0);

  // An error occurred:
  return False;
}

Boolean WISInput::initALSA(UsageEnvironment& env) {
  do {
    int arg;
    arg = AFMT_S16_LE;
    if (ioctl(fOurAudioFileNo, SNDCTL_DSP_SETFMT, &arg) < 0) {
      printErr(env, "SNDCTL_DSP_SETFMT");
      break;
    }
    arg = audioSamplingFrequency;
    if (ioctl(fOurAudioFileNo, SNDCTL_DSP_SPEED, &arg) < 0) {
      printErr(env, "SNDCTL_DSP_SPEED");
      break;
    }
    arg = audioNumChannels > 1 ? 1 : 0;
    if (ioctl(fOurAudioFileNo, SNDCTL_DSP_STEREO, &arg) < 0) {
      printErr(env, "SNDCTL_DSP_STEREO");
      break;
    }

    return True;
  } while (0);

  // An error occurred:
  return False;
}

static Boolean checkChange(UsageEnvironment& env,
			   struct v4l2_queryctrl const& ctrl, v4l2_control& newCtrl,
			   char const* ctrlName, int ctrlId, int newValue) {
  if (strcasecmp((char const*)(ctrl.name), ctrlName) != 0) return False;

  if (newValue == useDefaultValue) {
    newValue = ctrl.default_value;
  }
#if 0
  /* do not screen new value since driver itself will handle it */
  else if (newValue == invalidValue || newValue < ctrl.minimum || newValue > ctrl.maximum) {
    err(env) << "An invalid value was specified for \"" << ctrlName << "\".\n";
    env << "\tValid values are in the range [" << ctrl.minimum << "," << ctrl.maximum << "]\n";
    env << "\t(Instead, we use the default value: " << ctrl.default_value << ")\n";
    newValue = ctrl.default_value;
  }
#endif

  // Make the change:
  newCtrl.id = ctrlId;
  newCtrl.value = newValue;
  return True;
}

static Boolean areChangingValue(UsageEnvironment& env,
				struct v4l2_queryctrl const& ctrl, v4l2_control& newCtrl) {
  if (checkChange(env, ctrl, newCtrl, "brightness", V4L2_CID_BRIGHTNESS, videoInputBrightness) ||
      checkChange(env, ctrl, newCtrl, "contrast", V4L2_CID_CONTRAST, videoInputContrast) ||
      checkChange(env, ctrl, newCtrl, "saturation", V4L2_CID_SATURATION, videoInputSaturation) ||
      checkChange(env, ctrl, newCtrl, "hue", V4L2_CID_HUE, videoInputHue)) {
    return True;
  }

  return False;
}

#define MAX_BUFFERS     32
static struct {
  unsigned char *addr;
  unsigned int length;
} buffers[MAX_BUFFERS];

static int capture_start = 1;

Boolean WISInput::initV4L(UsageEnvironment& env) {
  do {
    // Begin by enumerating the available video input ports, and noting which of these
    // we wish to use:
    listVideoInputDevices(env);
    env << "(Using video input device #" << videoInputDeviceNumber << ")\n";

    // Set the video input port:
    struct v4l2_input inp;
    inp.index = videoInputDeviceNumber;
    if (ioctl(fOurVideoFileNo, VIDIOC_ENUMINPUT, &inp) < 0
	|| ioctl(fOurVideoFileNo, VIDIOC_S_INPUT, &videoInputDeviceNumber) < 0) {
      err(env) << "Unable to set video input device " << videoInputDeviceNumber; printErr(env);
      break;
    }

    // Set the video norm (should be done before setting the format):
    if (ioctl(fOurVideoFileNo, VIDIOC_S_STD, &videoType) < 0) {
      printErr(env, "Unable to set video type");
      break;
    }

    // Set the tuner frequency, if specified:
    if (tvFreq >= 0) {
      if (inp.type != V4L2_INPUT_TYPE_TUNER) {
	warn(env) << "Ignoring specified TV channel, because video input device "
		  << videoInputDeviceNumber << " is not a tuner\n";
      } else {
	struct v4l2_frequency freq;
	memset(&freq, 0, sizeof freq);
	freq.tuner = inp.tuner;
	freq.type = V4L2_TUNER_ANALOG_TV;
	freq.frequency = tvFreq;
	if (ioctl(fOurVideoFileNo, VIDIOC_S_FREQUENCY, &freq) < 0) {
	  printErr(env, "Unable to set TV tuner frequency");
	  break;
	}
      }
    }

    // Set the encoding format for the streamed output video:
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = videoWidth;
    fmt.fmt.pix.height = videoHeight;
    switch (videoFormat) {
    case VFMT_MJPEG:
      fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
      break;
    case VFMT_MPEG1:
    case VFMT_MPEG2:
    case VFMT_MPEG4:
    default:
      fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MPEG;
      break;
    }
    if (ioctl(fOurVideoFileNo, VIDIOC_S_FMT, &fmt) < 0) {
      printErr(env, "Unable to set the video format (and width,height)");
      break;
    }

    // Set and then query the video frame rate
    // (actually, the reciprocal of this: the inter-frame period):
    struct v4l2_streamparm parm;
    memset(&parm, 0, sizeof(parm));
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = videoFrameRateDenominator;
    parm.parm.capture.timeperframe.denominator = videoFrameRateNumerator;
    if (ioctl(fOurVideoFileNo, VIDIOC_S_PARM, &parm) < 0) {
      printErr(env, "Unable to set the video frame rate");
      break;
    }
    if (ioctl(fOurVideoFileNo, VIDIOC_G_PARM, &parm) < 0) {
      printErr(env, "Unable to query the video frame rate");
      break;
    }
    env << "Encoding video frame rate: "
	<< videoFrameRateNumerator << "/" << videoFrameRateDenominator
	<< " (= " << (double)videoFrameRateNumerator/(double)videoFrameRateDenominator
	<< ") frames per second\n";

    // Set the video input parameters (brightness, contrast, saturation, hue):
    for (int i = V4L2_CID_BASE; i < V4L2_CID_LASTP1; ++i) {
      struct v4l2_queryctrl ctrl;
      memset(&ctrl, 0, sizeof ctrl);

      // Get the existing value:
      ctrl.id = i;
      if (ioctl(fOurVideoFileNo, VIDIOC_QUERYCTRL, &ctrl) < 0
	  || ctrl.flags & V4L2_CTRL_FLAG_DISABLED) {
	continue;
      }

      // Check whether we want to change the value; if so, change it:
      v4l2_control newCtrl;
      if (areChangingValue(env, ctrl, newCtrl)) {
	if (ioctl(fOurVideoFileNo, VIDIOC_S_CTRL, &newCtrl) < 0) {
	  err(env) << "Failed to set \"" << ctrl.name << "\" control"; printErr(env);
	}
      }
    }

    // Set the compression parameters:
    if (videoFormat != VFMT_MJPEG) {
      struct go7007_comp_params comp;
      memset(&comp, 0, sizeof comp);
      comp.gop_size = videoGopsize;
      comp.max_b_frames = videoBframe;
      if (videoBitrate >= 8000000 && videoBframe == 1)
        comp.gop_size = 14;
      else if (videoBitrate >= 8000000)
        comp.gop_size = 15;
      else if (videoBitrate >= 5000000)
        comp.gop_size = 30;
      comp.aspect_ratio = GO7007_ASPECT_RATIO_1_1;
      comp.flags |= GO7007_COMP_CLOSED_GOP;
      if (ioctl(fOurVideoFileNo, GO7007IOC_S_COMP_PARAMS, &comp) < 0) {
	printErr(env, "Unable to set compression params");
	break;
      }

      struct go7007_mpeg_params mpeg;
      memset(&mpeg, 0, sizeof mpeg);
      switch (videoFormat) {
      case VFMT_MPEG1:
	mpeg.mpeg_video_standard = GO7007_MPEG_VIDEO_MPEG1;
	break;
      case VFMT_MPEG2:
	mpeg.mpeg_video_standard = GO7007_MPEG_VIDEO_MPEG2;
	break;
      case VFMT_MPEG4:
      default:
	mpeg.mpeg_video_standard = GO7007_MPEG_VIDEO_MPEG4;
	break;
      }
      mpeg.flags |= GO7007_MPEG_REPEAT_SEQHEADER;
          // for streaming - ensures that a sequence header appears periodically
      if (ioctl(fOurVideoFileNo, GO7007IOC_S_MPEG_PARAMS, &mpeg) < 0) {
	printErr(env, "Unable to set MPEG parameters");
	break;
      }
    }

#ifndef NEW_BITRATE_SETTING_CODE
    // Set the bitrate:
    if (ioctl(fOurVideoFileNo, GO7007IOC_S_BITRATE, &videoBitrate) < 0) {
      printErr(env, "Unable to set video bitrate");
      break;
    }
#else
    struct v4l2_mpeg_compression compression;
    memset(&compression, 0, sizeof compression);
    if( videoQuant != 0) {
      compression.st_bitrate.mode = V4L2_BITRATE_VBR;
      compression.st_bitrate.max = videoQuant;
      compression.st_bitrate.min = videoQuant;
      compression.st_bitrate.target = 0;
    }
    else {
      compression.st_bitrate.mode = V4L2_BITRATE_CBR;
      compression.st_bitrate.max = videoBitrate/1000;
      compression.st_bitrate.min = videoBitrate/1000;
      compression.st_bitrate.target = videoBitrate/1000;
    }
    if (ioctl(fOurVideoFileNo, VIDIOC_S_MPEGCOMP, &compression) < 0) {
      printErr(env, "Unable to set MPEG bitrate parameters");
      break;
    }
#endif

    // Request that buffers be allocated for memory mapping:
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof req);
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    req.count = MAX_BUFFERS;
    if (ioctl(fOurVideoFileNo, VIDIOC_REQBUFS, &req) < 0) {
      printErr(env, "VIDIOC_REQBUFS");
      break;
    }
    
    // Map each of the buffers into this process's memory,
    // and queue them for frame capture:
    unsigned j;
    for (j = 0; j < req.count; ++j) {
      struct v4l2_buffer buf;
      memset(&buf, 0, sizeof buf);
      buf.index = j;
      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      if (ioctl(fOurVideoFileNo, VIDIOC_QUERYBUF, &buf) < 0) {
	printErr(env, "VIDIOC_QUERYBUF");
	break;
      }
      buffers[buf.index].addr
	= (unsigned char *)mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED,
				fOurVideoFileNo, buf.m.offset);
      if (buffers[buf.index].addr == MAP_FAILED) {
	printErr(env, "mmap() failed");
	break;
      }
      buffers[buf.index].length = buf.length;
    }
    if (j < req.count) break; // an error occurred
    
    capture_start = 1;

    return True;
  } while (0);

  // An error occurred:
  return False;
}

void WISInput::listVideoInputDevices(UsageEnvironment& env) {
  env << "Input devices available:\n";
  for (int i = 0; ; ++i) {
    struct v4l2_input inp;
    memset(&inp, 0, sizeof inp);
    inp.index = i;
    if (ioctl(fOurVideoFileNo, VIDIOC_ENUMINPUT, &inp) < 0) break; // no more
    env << "\tdevice #" << i << ": " << (char*)(inp.name) << " (";
    for (int j = 0; ; ++j) {
      struct v4l2_standard s;
      memset(&s, 0, sizeof s);
      s.index = j;
      if (ioctl(fOurVideoFileNo, VIDIOC_ENUMSTD, &s) < 0) break;
      if (j > 0) env << ", ";
      env << (char*)(s.name);
    }
    env << ")\n";
  }
}

Boolean WISInput::fHaveInitialized = False;
int WISInput::fOurVideoFileNo = -1;
FramedSource* WISInput::fOurVideoSource = NULL;
int WISInput::fOurAudioFileNo = -1;
FramedSource* WISInput::fOurAudioSource = NULL;


////////// WISOpenFileSource implementation //////////

WISOpenFileSource
::WISOpenFileSource(UsageEnvironment& env, WISInput& input, int fileNo)
  : FramedSource(env),
    fInput(input), fFileNo(fileNo) {
}

WISOpenFileSource::~WISOpenFileSource() {
  envir().taskScheduler().turnOffBackgroundReadHandling(fFileNo);
}

void WISOpenFileSource::doGetNextFrame() {
  // Await the next incoming data on our FID:
  envir().taskScheduler().turnOnBackgroundReadHandling(fFileNo,
	       (TaskScheduler::BackgroundHandlerProc*)&incomingDataHandler, this);
}

void WISOpenFileSource
::incomingDataHandler(WISOpenFileSource* source, int /*mask*/) {
  source->incomingDataHandler1();
}

void WISOpenFileSource::incomingDataHandler1() {
  // Read the data from our file into the client's buffer:
  readFromFile();

  // Stop handling any more input, until we're ready again:
  envir().taskScheduler().turnOffBackgroundReadHandling(fFileNo);

  // Tell our client that we have new data:
  afterGetting(this);
}


////////// WISVideoOpenFileSource implementation //////////

WISVideoOpenFileSource
::WISVideoOpenFileSource(UsageEnvironment& env, WISInput& input)
  : WISOpenFileSource(env, input, input.fOurVideoFileNo) {
}

WISVideoOpenFileSource::~WISVideoOpenFileSource() {
  fInput.fOurVideoSource = NULL;
}

void WISVideoOpenFileSource::readFromFile() {
  // Retrieve a filled video buffer from the kernel:
  unsigned i;  
  struct v4l2_buffer buf;

  if (capture_start) {
    capture_start = 0;
    for (i = 0; i < MAX_BUFFERS; ++i) {
      memset(&buf, 0, sizeof buf);
      buf.index = i;
      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buf.memory = V4L2_MEMORY_MMAP;
      if (ioctl(fFileNo, VIDIOC_QBUF, &buf) < 0) {
        printErr(envir(), "VIDIOC_QBUF");
        return;
      }
    }
    
    // Start capturing:
    i = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fFileNo, VIDIOC_STREAMON, &i) < 0) {
      printErr(envir(), "VIDIOC_STREAMON");
      return;
    }
  }
  
  memset(&buf, 0, sizeof buf);
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;
  if (ioctl(fFileNo, VIDIOC_DQBUF, &buf) < 0) {
    printErr(envir(), "VIDIOC_DQBUF");
    return;
  }

  // Note the timestamp and size:
  fPresentationTime = buf.timestamp;
  fFrameSize = buf.bytesused;
  if (fFrameSize > fMaxSize) {
    fNumTruncatedBytes = fFrameSize - fMaxSize;
    fFrameSize = fMaxSize;
  } else {
    fNumTruncatedBytes = 0;
  }

  // Copy to the desired place:
  memmove(fTo, buffers[buf.index].addr, fFrameSize);

  // Send the buffer back to the kernel to be filled in again:
  if (ioctl(fFileNo, VIDIOC_QBUF, &buf) < 0) {
    printErr(envir(), "VIDIOC_QBUF");
    return;
  }
}


////////// WISAudioOpenFileSource implementation //////////

WISAudioOpenFileSource
::WISAudioOpenFileSource(UsageEnvironment& env, WISInput& input)
  : WISOpenFileSource(env, input, input.fOurAudioFileNo) {
}

WISAudioOpenFileSource::~WISAudioOpenFileSource() {
  fInput.fOurAudioSource = NULL;
}

void WISAudioOpenFileSource::readFromFile() {
  // Read available audio data:
  int timeinc;
  int ret = read(fInput.fOurAudioFileNo, fTo, fMaxSize);
  if (ret < 0) ret = 0;
  fFrameSize = (unsigned)ret;
  gettimeofday(&fPresentationTime, NULL);

  /* PR#2665 fix from Robin
   * Assuming audio format = AFMT_S16_LE
   * Get the current time
   * Substract the time increment of the audio oss buffer, which is equal to
   * buffer_size / channel_number / sample_rate / sample_size ==> 400+ millisec
   */
  timeinc = fFrameSize * 1000 / audioNumChannels / (audioSamplingFrequency/1000) / 2;
  while (fPresentationTime.tv_usec < timeinc)
  {
    fPresentationTime.tv_sec -= 1;
    timeinc -= 1000000;
  }
  fPresentationTime.tv_usec -= timeinc;
}

