#include "audiorecorder.hpp"

// global constants
const int SAMPLE_RATE = 44100;
const int FRAMES_PER_BUFFER = 2205;
const int NUM_CHANNELS = 1;
#define PA_SAMPLE_TYPE paInt16
typedef short SAMPLE;

// global data, necessary for C-style transcoding callbacks
SAMPLE* encodeBuffer;

sf_count_t buffer_len(void* data) {
    return FRAMES_PER_BUFFER*sizeof(SAMPLE)*NUM_CHANNELS;
}
sf_count_t buffer_read(void *dst, sf_count_t numBytes, void *data) {
    memcpy(dst, data, numBytes);
    return numBytes;
}

// dummy functions
sf_count_t buffer_seek(sf_count_t offset, int whence, void *data) {return 0;}
sf_count_t buffer_write(const void *src, sf_count_t numBytes, void *data) {return 0;}
sf_count_t buffer_pos(void *data) {return 0;}


AudioRecorder::AudioRecorder() :
    m_outFileName(""),
    m_state(NotInit),
    m_rawSampleBlock(NULL),
    m_paStream(NULL),
    m_sfOutFile(NULL)
{
    //details of the microphone data we're going to read from
    m_sfMicrophoneEncoding.samplerate = SAMPLE_RATE;
    m_sfMicrophoneEncoding.format = SF_FORMAT_RAW | SF_FORMAT_PCM_16;
    m_sfMicrophoneEncoding.channels = NUM_CHANNELS;
    assert( sf_format_check(&m_sfMicrophoneEncoding) );

    // required callbacks for encoding microphone data
    m_encodeCallbacks.get_filelen = &buffer_len;
    m_encodeCallbacks.read = &buffer_read;
    m_encodeCallbacks.write = &buffer_write;
    m_encodeCallbacks.seek = &buffer_seek;
    m_encodeCallbacks.tell = &buffer_pos;

    // details for the file output
    m_sfOutFileEncoding.channels = 1;
    m_sfOutFileEncoding.format = SF_FORMAT_OGG | SF_FORMAT_VORBIS;
    m_sfOutFileEncoding.samplerate = 44100;
    assert( sf_format_check(&m_sfOutFileEncoding) );

    encodeBuffer = new short[FRAMES_PER_BUFFER * NUM_CHANNELS];
    memset(encodeBuffer, 0, FRAMES_PER_BUFFER * NUM_CHANNELS);
}

std::thread* AudioRecorder::RecordInAnotherThread(std::string filename) {
    std::lock_guard<std::mutex> locker(m_recordControl); 

    if (m_state == Recording) return NULL;
    // if (m_state == Recording) return;

    m_outFileName = filename;

    if (RecordInit()) {
        return new std::thread([=](){run();});
    } else {
        return NULL;
    }
}

void AudioRecorder::PauseRecord() {
    // QMutexLocker locker(&m_recordControl);
    std::lock_guard<std::mutex> locker(m_recordControl); 

    if (m_state == Recording) m_state = Paused;
}

void AudioRecorder::EndRecord() {
    std::lock_guard<std::mutex> locker(m_recordControl); 
    m_state = Dying;
}

AudioRecorder::~AudioRecorder() {
    EndRecord();
}

void AudioRecorder::run() {
    m_state = Recording;
    while (true) {
        m_recordControl.lock();
        if (m_state == Paused) {
            m_recordControl.unlock();
            // usleep(30);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        } else if (m_state == Dying) {
            m_recordControl.unlock();
            break;
        } else m_recordControl.unlock();

        m_paError = Pa_ReadStream( m_paStream, m_rawSampleBlock, FRAMES_PER_BUFFER );
        SNDFILE* memoryFile = sf_open_virtual(&m_encodeCallbacks, SFM_READ, &m_sfMicrophoneEncoding, (void*) m_rawSampleBlock);
        sf_count_t framesReadFromMem = sf_readf_short(memoryFile, encodeBuffer, FRAMES_PER_BUFFER);
        sf_count_t nWrite = sf_writef_short(m_sfOutFile, encodeBuffer, framesReadFromMem);
        sf_close(memoryFile);
    }
    DeInit();
}

void AudioRecorder::DeInit() {
    if (m_paStream) {
        Pa_StopStream( m_paStream );
        m_paStream = NULL;
    }
    if (m_rawSampleBlock) {
        delete[] m_rawSampleBlock;
        m_rawSampleBlock = NULL;
    }
    Pa_Terminate();
    if (m_sfOutFile) {
        sf_close(m_sfOutFile);
        m_sfOutFile = NULL;
    }
    m_outFileName = "";
    m_state = NotInit;
}

bool AudioRecorder::RecordInit() {
    m_sfOutFile = sf_open(m_outFileName.c_str(), SFM_WRITE, &m_sfOutFileEncoding);
    if (sf_error(m_sfOutFile)) {
        std::cout << "AudioRecorder::RecordInit(): Cannot open file.";
        DeInit();
        return false;
    }

    double encodingQuality = 0.9;
    int ret = sf_command(m_sfOutFile, SFC_SET_VBR_ENCODING_QUALITY,
      (void*) &encodingQuality, sizeof(double));
    if (ret == SF_TRUE) std::cout << "Set quality to " << encodingQuality << std::endl;
    else if (ret == SF_FALSE) std::cout << "Did not set encoding quality.\n";

    int numBytes = FRAMES_PER_BUFFER * NUM_CHANNELS * sizeof(SAMPLE) ;
    m_rawSampleBlock = new char[numBytes];
    if(!m_rawSampleBlock) {
        std::cout << "AudioRecorder::RecordInit(): Memory allocation error";
        DeInit();
        return false;
    }

    m_paError = Pa_Initialize();
    if( m_paError != paNoError ) {
        std::cout << "AudioRecorder::RecordInit(): Could not initialize environment.";
        DeInit();
        return false;
    }

    int deviceNum = -1;
    std::cout << "Enumerating microphones:\n";
    for (int i=0;i<Pa_GetDeviceCount();i++) {
        const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
        std::string devName = std::string(info->name);
        std::cout << "  " << devName << std::endl;

        if (std::string(info->name).find("Built-In Microph") != std::string::npos) {
            deviceNum = i;
            break;
        } else if (std::string(info->name).find("Realtek") != std::string::npos) {
            deviceNum = i;
            break;
        }

        // if (std::string(info->name).find("ORBBEC") != std::string::npos) {
        //     deviceNum = i;
        //     std::cout << "Chose " << devName << std::endl;
        //     break;
        // }
    }

    //TODO: implement microphone picker?
    if (deviceNum == -1) deviceNum = Pa_GetDefaultInputDevice();
    if (deviceNum == -1) {
        std::cout << "AudioRecorder::RecordInit(): No recording device\n";
        DeInit();
        return false;
    }

    const PaDeviceInfo *info = Pa_GetDeviceInfo(deviceNum);
    std::cout << "Recording with: " << info->name << std::endl;

    m_paStreamParam.device = deviceNum;
    m_paStreamParam.channelCount = NUM_CHANNELS;
    m_paStreamParam.sampleFormat = PA_SAMPLE_TYPE;
    m_paStreamParam.suggestedLatency =
      Pa_GetDeviceInfo(m_paStreamParam.device)->defaultHighInputLatency;
    m_paStreamParam.hostApiSpecificStreamInfo = NULL;
    m_paError = Pa_OpenStream(
                &m_paStream,
                &m_paStreamParam,
                NULL,
                SAMPLE_RATE,
                FRAMES_PER_BUFFER,
                paNoFlag,
                NULL,             /* no callback, use blocking API */
                NULL );           /* no callback, so no callback userData */
    if( m_paError != paNoError ) {
        std::cout << "AudioRecorder::RecordInit(): Could not open microphone.";
        DeInit();
        return false;
    }
    m_paError = Pa_StartStream( m_paStream );
    return true;
}
