/*
 * Copyright (C) 2017- David S. Hayden - All Rights Reserved.
*/

#ifndef AUDIORECORDER_H
#define AUDIORECORDER_H

#include <thread>
#include <chrono>
#include <mutex>
#include <string>
#include <iostream>
#include <assert.h>
#include "sndfile.h"
#include "portaudio.h"

class AudioRecorder
{
public:
  explicit AudioRecorder();
  ~AudioRecorder();

  std::thread* RecordInAnotherThread(std::string filename);
  void PauseRecord();
  void EndRecord();

private:
    void run();
    bool RecordInit();
    void DeInit();
    enum State {NotInit, Recording, Paused, Dying};

    std::string m_outFileName;
    SF_INFO m_sfMicrophoneEncoding;
    SF_VIRTUAL_IO m_encodeCallbacks;
    SF_INFO m_sfOutFileEncoding;
    SNDFILE* m_sfOutFile;
    std::mutex m_recordControl;
    State m_state;

    PaStream *m_paStream;
    PaStreamParameters m_paStreamParam;
    PaError m_paError;
    char *m_rawSampleBlock;
};

#endif // AUDIORECORDER_H
