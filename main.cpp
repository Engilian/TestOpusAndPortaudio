#include <QCoreApplication>
#include <QDebug>
#include <QList>
#include <QByteArray>

#include <portaudio.h>
#include <opus.h>
#include <opus_types.h>
#include <thread>
#include <iostream>

#define AUDIO_TYPE          paInt16
#define SAMPLE_RATE         48000
#define CHANNELS            1
#define BUFFER_SIZE         960
#define MIN_LATENCY         0.1


#define BITRATE             16000
#define APPLICATION         OPUS_APPLICATION_AUDIO

/// Поток воспроизведения
PaStream                    *play                       = nullptr;

/// Поток записи
PaStream                    *record                     = nullptr;

/// Декодер
OpusDecoder                 *decoder                    = nullptr;

/// Энкодер
OpusEncoder                 *encoder                    = nullptr;

/// Список звуковых фреймов
QList<QByteArray>           audioData;

/// Максимальный размер сжатого пакета
uint64_t                    maxPacketSize               = 0;

/// Минимальный размер сжатого пакета
uint64_t                    minPacketSize               = 0;

/// Средний размер сжатого пакета
uint64_t                    averagePacketSize           = 0;

QString getOpusError(int errorCode)
{
    switch (errorCode) {
    case OPUS_OK:

        return "No error.";
        break;
    case OPUS_BAD_ARG:

        return "One or more invalid/out of range arguments.";
        break;
    case OPUS_BUFFER_TOO_SMALL:

        return "Not enough bytes allocated in the buffer.";
        break;
    case OPUS_INTERNAL_ERROR:

        return "An internal error was detected.";
        break;
    case OPUS_INVALID_PACKET:

        return "The compressed data passed is corrupted.";
        break;
    case OPUS_UNIMPLEMENTED:

        return "Invalid/unsupported request number";
        break;
    case OPUS_INVALID_STATE:

        return "An encoder or decoder structure is invalid or already freed.";
        break;
    case OPUS_ALLOC_FAIL:

        break;
    default:
        break;
    }

    return "undef error ( " + QString::number ( errorCode ) + " )";
}

/// Метод воспроизведения
static int playCallback(const void *, void *outputBuffer,
                        unsigned long frameCount,
                        const PaStreamCallbackTimeInfo* , PaStreamCallbackFlags,
                        void *userData ) {

    QList<QByteArray> *audioData = (QList<QByteArray> *)userData;

    int16_t *data = ( int16_t * ) outputBuffer;

    if ( audioData->isEmpty () ) {

        for ( unsigned long i = 0; i < frameCount; ++i, ++data ) {
            *data = 0;
        }

        return paContinue;
    }

    QByteArray audio  = audioData->takeFirst ();

    int error = opus_decode ( decoder, ( unsigned char * )audio.data (), audio.size () * 2, data, frameCount, 0 );

    if ( error < 0 ) {

        qDebug() << "Decode error: " << getOpusError ( error );

        for ( unsigned long i = 0; i < frameCount; ++i, ++data ) {
            *data = 0;
        }
    }

    return paContinue;
}

/// Метод записи данных с микрофона
static int recordCallback( const void *inputBuffer, void *,
                           unsigned long frameCount,
                           const PaStreamCallbackTimeInfo* ,
                           PaStreamCallbackFlags ,
                           void *userData ) {

    int16_t *data = ( int16_t * ) inputBuffer;
    QByteArray audio;
    audio.resize ( frameCount  * 2 );

    int encoded = opus_encode ( encoder, data, frameCount, ( unsigned char * )audio.data (), audio.size () * 2 );

    if ( encoded <= 0 ) {

        qDebug() << "Encode error: " << getOpusError ( encoded );
        return paContinue;
    }

    QList<QByteArray> *audioData = (QList<QByteArray> *)userData;
    audio.resize ( encoded );
    audioData->append ( audio );

    // Статистика
    maxPacketSize       = std::max( maxPacketSize, (uint64_t)encoded );
    minPacketSize       = minPacketSize     == 0 ? encoded : std::min( minPacketSize, (uint64_t)encoded );
    averagePacketSize   = averagePacketSize == 0 ? encoded : ( averagePacketSize + encoded ) / 2;

    return paContinue;
}

bool initOpus() {

    int decoderError = 0, encoderError = 0;

    decoder = opus_decoder_create ( SAMPLE_RATE, 1, &decoderError );
    encoder = opus_encoder_create ( SAMPLE_RATE, 1, APPLICATION, &encoderError );

    bool okCreate = true; // Удалось ли инициализировать opus

    if ( !decoder ) {

        qDebug() << "Can not create opus decoder: " << getOpusError ( decoderError );
        okCreate = false;
    }

    if ( !encoder ) {

        qDebug() << "Can not create opus encoder: " << getOpusError ( encoderError );
        okCreate = false;
    }

    if ( !okCreate ) {

        return false;
    }

    decoderError = opus_decoder_init ( decoder, SAMPLE_RATE, 1 );
    encoderError = opus_encoder_init ( encoder, SAMPLE_RATE, 1, APPLICATION );

    if ( decoderError != OPUS_OK ) {

        return false;
    }

    if ( encoderError != OPUS_OK ) {

        return false;
    }

    encoderError = opus_encoder_ctl ( encoder, OPUS_SET_BITRATE( BITRATE ) );
    if ( encoderError != OPUS_OK ) {

        qDebug() << "Error set bitrate encoder";
        return false;
    }

    return true;
}

void destroyOpus() {

    if ( decoder ) {
        opus_decoder_destroy( decoder );
    }

    if ( encoder ) {
        opus_encoder_destroy( encoder );
    }
}

bool initPortAudio() {

    int error = Pa_Initialize ();

    if ( error != paNoError ) {

        qDebug() << Pa_GetErrorText ( error );
        return false;
    }

    return true;
}

bool createPlayStream() {

    PaDeviceIndex       index   = Pa_GetDefaultOutputDevice ();
    const PaDeviceInfo  *info   = Pa_GetDeviceInfo ( index );

    if ( !info ) {

        qDebug() << "Could not find the playback device.";
        return false;
    }

    if ( info->maxOutputChannels == 0 ) {

        qDebug() << "The playback device is currently busy";
        return false;
    }

    PaStreamParameters params;
    {
        params.device                    = Pa_HostApiDeviceIndexToDeviceIndex( info->hostApi, index );
        params.channelCount              = CHANNELS;
        params.sampleFormat              = AUDIO_TYPE;
        params.suggestedLatency          = MIN_LATENCY;
        params.hostApiSpecificStreamInfo = nullptr;
    }

    // Открываем стрим
    PaError errorOpenStream = Pa_OpenStream(
                &play
                ,nullptr
                ,&params
                ,SAMPLE_RATE
                ,BUFFER_SIZE
                ,paClipOff
                ,playCallback
                ,&audioData
                );

    if ( errorOpenStream != paNoError ) {

        qDebug() << "Could not create streaming stream: " << Pa_GetErrorText ( errorOpenStream );
        return false;
    }

    return true;
}

void destroyPlayStream() {

    if ( play ) {

        Pa_CloseStream( play );
        play = nullptr;
    }
}

bool createRecordStream() {

    PaDeviceIndex       index   = Pa_GetDefaultInputDevice ();
    const PaDeviceInfo  *info   = Pa_GetDeviceInfo ( index );

    if ( !info ) {

        qDebug() << "Could not find recorder";
        return false;
    }

    if ( info->maxInputChannels == 0 ) {

        qDebug() << "The recording device is currently busy";
        return false;
    }

    PaStreamParameters params;
    {
        params.device                    = Pa_HostApiDeviceIndexToDeviceIndex( info->hostApi, index );
        params.channelCount              = CHANNELS;
        params.sampleFormat              = AUDIO_TYPE;
        params.suggestedLatency          = MIN_LATENCY;
        params.hostApiSpecificStreamInfo = nullptr;
    }

    // Открываем стрим
    PaError errorOpenStream = Pa_OpenStream(
                &record
                ,&params
                ,nullptr
                ,SAMPLE_RATE
                ,BUFFER_SIZE
                ,paClipOff
                ,recordCallback
                ,&audioData
                );

    if ( errorOpenStream != paNoError ) {

        qDebug() << "Could not create stream record: " << Pa_GetErrorText ( errorOpenStream );
        return false;
    }

    return true;
}

void destroyRecordStream() {

    if ( record ) {

        Pa_CloseStream( record );
        record = nullptr;
    }
}

bool createStreams() {

    if ( !createPlayStream () )  {

        return false;
    }

    if ( !createRecordStream () ) {

        destroyPlayStream ();
        return false;
    }

    return true;
}

void destroyStreams() {

    destroyPlayStream ();
    destroyRecordStream ();
}

bool startPlayStream() {

    int error = Pa_StartStream( play );

    if ( error != paNoError ) {

        qDebug() << "Unable to start streaming playback: " << Pa_GetErrorText ( error );
        return false;
    }

    return true;
}

void stopPlayStream() {

    if ( play ) {

        Pa_StopStream( play );
    }
}

bool startRecordStream() {

    int error = Pa_StartStream( record );

    if ( error != paNoError ) {

        qDebug() << "Could not start stream record: " << Pa_GetErrorText ( error );
        return false;
    }

    return true;
}

void stopRecordStream() {

    if ( record ) {

        Pa_StopStream( record );
    }
}

bool startStreams() {

    if ( !startPlayStream () ) {

        return false;
    }

    if ( !startRecordStream () ) {

        stopRecordStream ();
        return false;
    }

    return true;
}


void stopStreams() {

    stopPlayStream ();
    stopRecordStream ();
}

float toKbit( int audioFrameSize ) {

    audioFrameSize *= 2;

    return audioFrameSize / 1024.0;
}

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    Q_UNUSED(a);

    if ( !initPortAudio () ) {

        qDebug() << "Failed to initialize PortAudio";
        return 1;
    }

    qDebug() << "PortAudio was successfully initialized";

    if ( !initOpus () ) {

        qDebug() << "Failed to initialize codec";
        return 1;
    }

    qDebug() << "Codec initialized";

    if ( !createStreams () ) {

        destroyOpus ();
        qDebug() << "I can not create I / O streams";
        return 1;
    }

    if ( !startStreams () ) {

        destroyStreams ();
        destroyOpus ();
        qDebug() << "I can not start I / O streams";
        return 1;
    }

    qDebug() << "";
    qDebug() << "The audio codec test is started ...";
    qDebug() << "To complete, type q";

    char q;
    for( int i = 30; i != 0; --i ) {

        std::cin >> q;

        if ( q == 'q' ) {
            break;
        }
        std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
    }
    qDebug() << "";

    qDebug() << "Test complete";

    std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );

    std::cout << "Stats: "                   << "\n"
              << "encoded min: \t\t\t"       << toKbit ( minPacketSize )     << " Kbit " << "\n"
              << "encoded max: \t\t\t"       << toKbit ( maxPacketSize )     << " Kbit " << "\n"
              << "encoded average: \t\t"     << toKbit ( averagePacketSize ) << " Kbit " << "\n"
              << "buffer: \t\t\t"            << toKbit ( BUFFER_SIZE )       << " Kbit" << "\n";

    qDebug() << "Ending the program ...";
    std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );

    /// Destroy
    {
        stopStreams ();
        destroyStreams ();

        destroyOpus ();
    }

    return 0;
}
