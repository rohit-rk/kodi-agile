#pragma once
/*
 *      Copyright (C) 2010-2013 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include <list>
#include <string>
#include <vector>

#include "system.h"
#include "threads/Thread.h"

#include "ActiveAESink.h"
#include "cores/AudioEngine/Interfaces/AEStream.h"
#include "cores/AudioEngine/Interfaces/AESound.h"
#include "cores/AudioEngine/Engines/ActiveAE/ActiveAEBuffer.h"

#include "guilib/DispResource.h"
#include <queue>

// ffmpeg
extern "C" {
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/avutil.h"
}

class IAESink;
class IAEEncoder;
class CServiceManager;

namespace ActiveAE
{

class CActiveAESound;
class CActiveAEStream;

struct AudioSettings
{
  std::string device;
  std::string driver;
  std::string passthroughdevice;
  int channels;
  bool ac3passthrough;
  bool ac3transcode;
  bool eac3passthrough;
  bool dtspassthrough;
  bool truehdpassthrough;
  bool dtshdpassthrough;
  bool stereoupmix;
  bool normalizelevels;
  bool passthrough;
  bool dspaddonsenabled;
  int config;
  int guisoundmode;
  unsigned int samplerate;
  AEQuality resampleQuality;
  double atempoThreshold;
  bool streamNoise;
  int silenceTimeout;
};

class CActiveAEControlProtocol : public Protocol
{
public:
  CActiveAEControlProtocol(std::string name, CEvent* inEvent, CEvent *outEvent) : Protocol(name, inEvent, outEvent) {};
  enum OutSignal
  {
    INIT = 0,
    RECONFIGURE,
    SUSPEND,
    DEVICECHANGE,
    MUTE,
    VOLUME,
    PAUSESTREAM,
    RESUMESTREAM,
    FLUSHSTREAM,
    STREAMRGAIN,
    STREAMVOLUME,
    STREAMAMP,
    STREAMRESAMPLERATIO,
    STREAMRESAMPLEMODE,
    STREAMFADE,
    STREAMFFMPEGINFO,
    STOPSOUND,
    GETSTATE,
    DISPLAYLOST,
    DISPLAYRESET,
    APPFOCUSED,
    KEEPCONFIG,
    TIMEOUT,
  };
  enum InSignal
  {
    ACC,
    ERR,
    STATS,
  };
};

class CActiveAEDataProtocol : public Protocol
{
public:
  CActiveAEDataProtocol(std::string name, CEvent* inEvent, CEvent *outEvent) : Protocol(name, inEvent, outEvent) {};
  enum OutSignal
  {
    NEWSOUND = 0,
    PLAYSOUND,
    FREESOUND,
    NEWSTREAM,
    FREESTREAM,
    STREAMSAMPLE,
    DRAINSTREAM,
  };
  enum InSignal
  {
    ACC,
    ERR,
    STREAMBUFFER,
    STREAMDRAINED,
  };
};

struct MsgStreamNew
{
  AEAudioFormat format;
  unsigned int options;
  IAEClockCallback *clock;
};

struct MsgStreamSample
{
  CSampleBuffer *buffer;
  CActiveAEStream *stream;
};

struct MsgStreamParameter
{
  CActiveAEStream *stream;
  union
  {
    float float_par;
    double double_par;
    int int_par;
  } parameter;
};

struct MsgStreamFade
{
  CActiveAEStream *stream;
  float from;
  float target;
  unsigned int millis;
};

struct MsgStreamFFmpegInfo
{
  CActiveAEStream *stream;
  int profile;
  enum AVMatrixEncoding matrix_encoding;
  enum AVAudioServiceType audio_service_type;
};

class CEngineStats
{
public:
  void Reset(unsigned int sampleRate, bool pcm);
  void UpdateSinkDelay(const AEDelayStatus& status, int samples);
  void AddSamples(int samples, std::list<CActiveAEStream*> &streams);
  void GetDelay(AEDelayStatus& status);
  void AddStream(unsigned int streamid);
  void RemoveStream(unsigned int streamid);
  void UpdateStream(CActiveAEStream *stream);
  void GetDelay(AEDelayStatus& status, CActiveAEStream *stream);
  void GetSyncInfo(CAESyncInfo& info, CActiveAEStream *stream);
  float GetCacheTime(CActiveAEStream *stream);
  float GetCacheTotal(CActiveAEStream *stream);
  float GetWaterLevel();
  void SetSuspended(bool state);
  void SetDSP(bool state);
  void SetCurrentSinkFormat(AEAudioFormat SinkFormat);
  void SetSinkCacheTotal(float time) { m_sinkCacheTotal = time; }
  void SetSinkLatency(float time) { m_sinkLatency = time; }
  bool IsSuspended();
  bool HasDSP();
  AEAudioFormat GetCurrentSinkFormat();
protected:
  float m_sinkCacheTotal;
  float m_sinkLatency;
  int m_bufferedSamples;
  unsigned int m_sinkSampleRate;
  AEDelayStatus m_sinkDelay;
  bool m_suspended;
  bool m_hasDSP;
  AEAudioFormat m_sinkFormat;
  bool m_pcmOutput;
  CCriticalSection m_lock;
  struct StreamStats
  {
    unsigned int m_streamId;
    double m_bufferedTime;
    double m_resampleRatio;
    double m_syncError;
    unsigned int m_errorTime;
    CAESyncInfo::AESyncState m_syncState;
  };
  std::vector<StreamStats> m_streamStats;
};

class CActiveAE : public IAE, public IDispResource, private CThread
{
protected:
  friend class ::CServiceManager;
  friend class CActiveAESound;
  friend class CActiveAEStream;
  friend class CSoundPacket;
  friend class CActiveAEBufferPoolResample;

  virtual bool  Initialize();

public:
  CActiveAE();
  virtual ~CActiveAE();
  virtual void   Shutdown();
  virtual bool   Suspend();
  virtual bool   Resume();
  virtual bool   IsSuspended();
  virtual void   OnSettingsChange(const std::string& setting);

  virtual float GetVolume();
  virtual void  SetVolume(const float volume);
  virtual void  SetMute(const bool enabled);
  virtual bool  IsMuted();
  virtual void  SetSoundMode(const int mode);

  /* returns a new stream for data in the specified format */
  virtual IAEStream *MakeStream(AEAudioFormat &audioFormat, unsigned int options = 0, IAEClockCallback *clock = NULL);
  virtual bool FreeStream(IAEStream *stream);

  /* returns a new sound object */
  virtual IAESound *MakeSound(const std::string& file);
  virtual void      FreeSound(IAESound *sound);

  virtual void GarbageCollect() {};

  virtual void EnumerateOutputDevices(AEDeviceList &devices, bool passthrough);
  virtual std::string GetDefaultDevice(bool passthrough);
  virtual bool SupportsRaw(AEAudioFormat &format);
  virtual bool SupportsSilenceTimeout();
  virtual bool HasStereoAudioChannelCount();
  virtual bool HasHDAudioChannelCount();
  virtual bool SupportsQualityLevel(enum AEQuality level);
  virtual bool IsSettingVisible(const std::string &settingId);
  virtual void KeepConfiguration(unsigned int millis);
  virtual void DeviceChange();
  virtual bool HasDSP();
  virtual bool GetCurrentSinkFormat(AEAudioFormat &SinkFormat);

  virtual void RegisterAudioCallback(IAudioCallback* pCallback);
  virtual void UnregisterAudioCallback(IAudioCallback* pCallback);

  virtual void OnLostDisplay();
  virtual void OnResetDisplay();
  virtual void OnAppFocusChange(bool focus);

protected:
  void PlaySound(CActiveAESound *sound);
  static uint8_t **AllocSoundSample(SampleConfig &config, int &samples, int &bytes_per_sample, int &planes, int &linesize);
  static void FreeSoundSample(uint8_t **data);
  void GetDelay(AEDelayStatus& status, CActiveAEStream *stream) { m_stats.GetDelay(status, stream); }
  void GetSyncInfo(CAESyncInfo& info, CActiveAEStream *stream) { m_stats.GetSyncInfo(info, stream); }
  float GetCacheTime(CActiveAEStream *stream) { return m_stats.GetCacheTime(stream); }
  float GetCacheTotal(CActiveAEStream *stream) { return m_stats.GetCacheTotal(stream); }
  void FlushStream(CActiveAEStream *stream);
  void PauseStream(CActiveAEStream *stream, bool pause);
  void StopSound(CActiveAESound *sound);
  void SetStreamAmplification(CActiveAEStream *stream, float amplify);
  void SetStreamReplaygain(CActiveAEStream *stream, float rgain);
  void SetStreamVolume(CActiveAEStream *stream, float volume);
  void SetStreamResampleRatio(CActiveAEStream *stream, double ratio);
  void SetStreamResampleMode(CActiveAEStream *stream, int mode);
  void SetStreamFFmpegInfo(CActiveAEStream *stream, int profile, enum AVMatrixEncoding matrix_encoding, enum AVAudioServiceType audio_service_type);
  void SetStreamFade(CActiveAEStream *stream, float from, float target, unsigned int millis);

protected:
  void Process();
  void StateMachine(int signal, Protocol *port, Message *msg);
  bool InitSink();
  void DrainSink();
  void UnconfigureSink();
  void Start();
  void Dispose();
  void LoadSettings();
  bool NeedReconfigureBuffers();
  bool NeedReconfigureSink();
  void ApplySettingsToFormat(AEAudioFormat &format, AudioSettings &settings, int *mode = NULL);
  void Configure(AEAudioFormat *desiredFmt = NULL);
  AEAudioFormat GetInputFormat(AEAudioFormat *desiredFmt = NULL);
  CActiveAEStream* CreateStream(MsgStreamNew *streamMsg);
  void DiscardStream(CActiveAEStream *stream);
  void SFlushStream(CActiveAEStream *stream);
  void FlushEngine();
  void ClearDiscardedBuffers();
  void SStopSound(CActiveAESound *sound);
  void DiscardSound(CActiveAESound *sound);
  void ChangeResamplers();

  bool RunStages();
  bool HasWork();
  CSampleBuffer* SyncStream(CActiveAEStream *stream);

  void ResampleSounds();
  bool ResampleSound(CActiveAESound *sound);
  void MixSounds(CSoundPacket &dstSample);
  void Deamplify(CSoundPacket &dstSample);

  bool CompareFormat(AEAudioFormat &lhs, AEAudioFormat &rhs);

  CEvent m_inMsgEvent;
  CEvent m_outMsgEvent;
  CActiveAEControlProtocol m_controlPort;
  CActiveAEDataProtocol m_dataPort;
  int m_state;
  bool m_bStateMachineSelfTrigger;
  int m_extTimeout;
  bool m_extError;
  bool m_extDrain;
  XbmcThreads::EndTime m_extDrainTimer;
  unsigned int m_extKeepConfig;
  bool m_extDeferData;
  std::queue<time_t> m_extLastDeviceChange;

  enum
  {
    MODE_RAW,
    MODE_TRANSCODE,
    MODE_PCM
  }m_mode;

  CActiveAESink m_sink;
  AEAudioFormat m_sinkFormat;
  AEAudioFormat m_sinkRequestFormat;
  AEAudioFormat m_encoderFormat;
  AEAudioFormat m_internalFormat;
  AEAudioFormat m_inputFormat;
  AudioSettings m_settings;
  CEngineStats m_stats;
  IAEEncoder *m_encoder;
  std::string m_currDevice;

  // buffers
  CActiveAEBufferPoolResample *m_sinkBuffers;
  CActiveAEBufferPoolResample *m_vizBuffers;
  CActiveAEBufferPool *m_vizBuffersInput;
  CActiveAEBufferPool *m_silenceBuffers;  // needed to drive gui sounds if we have no streams
  CActiveAEBufferPool *m_encoderBuffers;

  // streams
  std::list<CActiveAEStream*> m_streams;
  std::list<CActiveAEBufferPool*> m_discardBufferPools;
  unsigned int m_streamIdGen;

  // gui sounds
  struct SoundState
  {
    CActiveAESound *sound;
    int samples_played;
  };
  std::list<SoundState> m_sounds_playing;
  std::vector<CActiveAESound*> m_sounds;

  float m_volume; // volume on a 0..1 scale corresponding to a proportion along the dB scale
  float m_volumeScaled; // multiplier to scale samples in order to achieve the volume specified in m_volume
  bool m_muted;
  bool m_sinkHasVolume;

  // viz
  std::vector<IAudioCallback*> m_audioCallback;
  bool m_vizInitialized;
  CCriticalSection m_vizLock;

  // polled via the interface
  float m_aeVolume;
  bool m_aeMuted;
  bool m_aeGUISoundForce;
};
};
