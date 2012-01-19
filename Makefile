LIVE_DIR = ../live

all:	wis-streamer

CC = gcc
CPLUSPLUS = g++

INCLUDES = -I . \
	-I$(LIVE_DIR)/BasicUsageEnvironment/include \
	-I$(LIVE_DIR)/UsageEnvironment/include \
	-I$(LIVE_DIR)/groupsock/include \
	-I$(LIVE_DIR)/liveMedia/include

CFLAGS = $(INCLUDES) -D_LINUX -g -Wall

LIBS =	-L$(LIVE_DIR)/BasicUsageEnvironment -lBasicUsageEnvironment \
	-L$(LIVE_DIR)/UsageEnvironment -lUsageEnvironment \
	-L$(LIVE_DIR)/groupsock -lgroupsock \
	-L$(LIVE_DIR)/liveMedia -lliveMedia \
	-LAMREncoder -lAMREncoder \
	-LAACEncoder -lAACEncoder

OBJS = wis-streamer.o Options.o TV.o Err.o WISInput.o WISServerMediaSubsession.o \
	UnicastStreaming.o MulticastStreaming.o DarwinStreaming.o AudioRTPCommon.o \
	WISJPEGStreamSource.o WISJPEGVideoServerMediaSubsession.o \
	WISMPEG1or2VideoServerMediaSubsession.o \
	WISMPEG4VideoServerMediaSubsession.o \
	WISPCMAudioServerMediaSubsession.o \
	MPEGAudioEncoder.o mpegaudio.o mpegaudiocommon.o \
	AMRAudioEncoder.o AACAudioEncoder.o \
	MPEG2TransportStreamAccumulator.o WISMPEG2TransportStreamServerMediaSubsession.o

wis-streamer: $(OBJS) AMREncoder/libAMREncoder.a AACEncoder/libAACEncoder.a
	$(CPLUSPLUS) $(CFLAGS) -o wis-streamer $(OBJS) $(LIBS)

AMREncoder/libAMREncoder.a:
	cd AMREncoder; $(MAKE)

AACEncoder/libAACEncoder.a:
	cd AACEncoder; $(MAKE)

wis-streamer.cpp:				Options.hh Err.hh UnicastStreaming.hh \
						MulticastStreaming.hh DarwinStreaming.hh
Options.hh:					MediaFormat.hh
UnicastStreaming.hh:			WISInput.hh
MulticastStreaming.hh:			WISInput.hh
DarwinStreaming.hh:			WISInput.hh

Options.cpp:				Options.hh TV.hh Err.hh
TV.cpp:					TV.hh Err.hh
Err.cpp:				Err.hh

WISInput.cpp:				WISInput.hh Options.hh Err.hh

WISServerMediaSubsession.cpp:		WISServerMediaSubsession.hh

UnicastStreaming.cpp:			UnicastStreaming.hh Options.hh \
					WISMPEG2TransportStreamServerMediaSubsession.hh \
					WISJPEGVideoServerMediaSubsession.hh \
					WISMPEG1or2VideoServerMediaSubsession.hh \
					WISMPEG4VideoServerMediaSubsession.hh \
					WISPCMAudioServerMediaSubsession.hh
WISMPEG2TransportStreamServerMediaSubsession.hh:	WISServerMediaSubsession.hh
WISJPEGVideoServerMediaSubsession.hh:	WISServerMediaSubsession.hh
WISServerMediaSubsession.hh:		WISInput.hh
WISMPEG1or2VideoServerMediaSubsession.hh:	WISServerMediaSubsession.hh
WISMPEG4VideoServerMediaSubsession.hh:	WISServerMediaSubsession.hh
WISPCMAudioServerMediaSubsession.hh:	WISServerMediaSubsession.hh MediaFormat.hh

MulticastStreaming.cpp:			MulticastStreaming.hh Options.hh AudioRTPCommon.hh \
					WISJPEGStreamSource.hh \
					MPEG2TransportStreamAccumulator.hh
WISJPEGStreamSource.hh:			WISInput.hh

DarwinStreaming.cpp:			DarwinStreaming.hh Options.hh AudioRTPCommon.hh \
					WISJPEGStreamSource.hh \
					MPEG2TransportStreamAccumulator.hh

AudioRTPCommon.cpp:			AudioRTPCommon.hh Options.hh WISInput.hh \
					MPEGAudioEncoder.hh AMRAudioEncoder.hh \
					AACAudioEncoder.hh

WISJPEGStreamSource.cpp:		WISJPEGStreamSource.hh

WISJPEGVideoServerMediaSubsession.cpp:	WISJPEGVideoServerMediaSubsession.hh WISJPEGStreamSource.hh

WISMPEG1or2VideoServerMediaSubsession.cpp:	WISMPEG1or2VideoServerMediaSubsession.hh

WISMPEG4VideoServerMediaSubsession.cpp:	WISMPEG4VideoServerMediaSubsession.hh

WISPCMAudioServerMediaSubsession.cpp:	WISPCMAudioServerMediaSubsession.hh Options.hh AudioRTPCommon.hh

MPEGAudioEncoder.cpp:			MPEGAudioEncoder.hh avcodec.h mpegaudio.h
avcodec.h:				mpegaudiocommon.h
mpegaudiocommon.h:			bswap.h
mpegaudio.c:				avcodec.h mpegaudio.h mpegaudiocommon.h
mpegaudiocommon.c:			avcodec.h

AMRAudioEncoder.cpp:			AMRAudioEncoder.hh AMREncoder/interf_enc.h AMREncoder/interf_rom.h

AACAudioEncoder.cpp:			AACAudioEncoder.hh AACEncoder/faac.h

MPEG2TransportStreamAccumulator.cpp:	MPEG2TransportStreamAccumulator.hh

WISMPEG2TransportStreamServerMediaSubsession.cpp:	WISMPEG2TransportStreamServerMediaSubsession.hh Options.hh MPEGAudioEncoder.hh MPEG2TransportStreamAccumulator.hh

.c.o:
	$(CC) -c $(CFLAGS) $< -o $@

.cpp.o:
	$(CPLUSPLUS) -c $(CFLAGS) $< -o $@

clean:
	rm -f *.o *~
	rm -f wis-streamer
	cd AMREncoder; $(MAKE) clean
	cd AACEncoder; $(MAKE) clean
