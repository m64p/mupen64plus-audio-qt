#include <samplerate.h>
#include <QAudioOutput>
#define M64P_PLUGIN_PROTOTYPES 1
#include "m64p_common.h"
#include "m64p_types.h"
#include "m64p_plugin.h"

#define QT_AUDIO_PLUGIN_VERSION 0x020500
#define AUDIO_PLUGIN_API_VERSION 0x020000

static int l_PluginInit = 0;
static int GameFreq = 0;
static AUDIO_INFO AudioInfo;
static unsigned char primaryBuffer[0x40000];
static float output_buffer[0x20000];
static float convert_buffer[0x20000];
static int VolIsMuted = 0;
static unsigned int paused = 0;
static int ff = 0;
static SRC_STATE *src_state;
static QAudioOutput* audio;
static QIODevice* audio_buffer;

EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle, void *, void (*)(void *, int, const char *))
{
    if (l_PluginInit)
        return M64ERR_ALREADY_INIT;

    l_PluginInit = 1;
    VolIsMuted = 0;
    ff = 0;

    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginShutdown(void)
{
    if (!l_PluginInit)
        return M64ERR_NOT_INIT;

    if (src_state) src_state = src_delete(src_state);
    src_state = NULL;

    l_PluginInit = 0;

    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginGetVersion(m64p_plugin_type *PluginType, int *PluginVersion, int *APIVersion, const char **PluginNamePtr, int *Capabilities)
{
    /* set version info */
    if (PluginType != NULL)
        *PluginType = M64PLUGIN_AUDIO;

    if (PluginVersion != NULL)
        *PluginVersion = QT_AUDIO_PLUGIN_VERSION;

    if (APIVersion != NULL)
        *APIVersion = AUDIO_PLUGIN_API_VERSION;

    if (PluginNamePtr != NULL)
        *PluginNamePtr = "Mupen64Plus Qt Audio Plugin";

    if (Capabilities != NULL)
    {
        *Capabilities = 0;
    }

    return M64ERR_SUCCESS;
}

void InitAudio()
{
    QAudioFormat format;
    // Set up the format, eg.
    format.setSampleRate(48000);
    format.setChannelCount(2);
    format.setSampleSize(32);
    format.setCodec("audio/pcm");
    format.setByteOrder(QAudioFormat::LittleEndian);
    format.setSampleType(QAudioFormat::Float);
    audio = new QAudioOutput(format);

    audio_buffer = audio->start();

    paused = 0;

    if (src_state) src_state = src_delete(src_state);
    int error;
    src_state = src_new (SRC_SINC_MEDIUM_QUALITY, 2, &error);
}

void CloseAudio()
{
    audio->stop();
    audio->deleteLater();
    if (src_state) src_state = src_delete(src_state);
    src_state = NULL;
}

EXPORT void CALL AiDacrateChanged( int SystemType )
{
    if (!l_PluginInit)
        return;

    switch (SystemType)
    {
        case SYSTEM_NTSC:
            GameFreq = 48681812 / (*AudioInfo.AI_DACRATE_REG + 1);
            break;
        case SYSTEM_PAL:
            GameFreq = 49656530 / (*AudioInfo.AI_DACRATE_REG + 1);
            break;
        case SYSTEM_MPAL:
            GameFreq = 48628316 / (*AudioInfo.AI_DACRATE_REG + 1);
            break;
    }
    if (src_state) src_state = src_delete(src_state);
    int error;
    src_state = src_new (SRC_SINC_MEDIUM_QUALITY, 2, &error);
}

EXPORT void CALL AiLenChanged( void )
{
    if (!l_PluginInit)
        return;

    unsigned int LenReg = *AudioInfo.AI_LEN_REG;
    unsigned char *p = AudioInfo.RDRAM + (*AudioInfo.AI_DRAM_ADDR_REG & 0xFFFFFF);

    unsigned int i;

    for ( i = 0 ; i < LenReg ; i += 4 )
    {
        // Left channel
        primaryBuffer[ i ] = p[ i + 2 ];
        primaryBuffer[ i + 1 ] = p[ i + 3 ];

        // Right channel
        primaryBuffer[ i + 2 ] = p[ i ];
        primaryBuffer[ i + 3 ] = p[ i + 1 ];
    }

    if (!VolIsMuted && !ff)
    {
        src_short_to_float_array ((short*)primaryBuffer, convert_buffer, LenReg / 2) ;
        SRC_DATA data;
        data.data_in = convert_buffer;
        data.input_frames = LenReg / 4;
        data.data_out = output_buffer;
        data.output_frames = sizeof(output_buffer) / 8;
        data.src_ratio = (float)audio->format().sampleRate() / GameFreq;
        data.end_of_input = 0;

        src_process(src_state, &data);
        
        int audio_queue = audio->bytesFree();
        int acceptable_latency = audio->format().bytesPerFrame() * 30;
        int min_latency = audio->format().bytesPerFrame() * 3;
        unsigned int diff = 0;
        if (audio_queue > acceptable_latency)
        {
            diff = audio_queue - acceptable_latency;
            diff &= ~7;
        }
        else if (!paused && audio_queue < min_latency)
        {
            audio->suspend();
            paused = 1;
        }
        else if (paused && audio_queue >= min_latency)
        {
            audio->resume();
            paused = 0;
        }

        unsigned int output_length = data.output_frames_gen * 8;
        if (output_length > diff)
        {
            int len = output_length - diff;
            audio_buffer->write((char*)output_buffer, len);
        }
    }
}

EXPORT int CALL InitiateAudio( AUDIO_INFO Audio_Info )
{
    if (!l_PluginInit)
        return 0;

    GameFreq = 33600;
    AudioInfo = Audio_Info;

    return 1;
}

EXPORT int CALL RomOpen(void)
{
    if (!l_PluginInit)
        return 0;

    InitAudio();

    return 1;
}

EXPORT void CALL RomClosed( void )
{
    if (!l_PluginInit)
        return;

    CloseAudio();
}

EXPORT void CALL ProcessAList(void)
{
}

EXPORT void CALL SetSpeedFactor(int percentage)
{
    if (percentage > 100)
        ff = 1;
    else
        ff = 0;
}

EXPORT void CALL VolumeMute(void)
{
    if (!l_PluginInit)
        return;

    VolIsMuted = !VolIsMuted;
}

EXPORT void CALL VolumeUp(void)
{
}

EXPORT void CALL VolumeDown(void)
{
}

EXPORT int CALL VolumeGetLevel(void)
{
    return VolIsMuted ? 0 : 100;
}

EXPORT void CALL VolumeSetLevel(int level)
{
    if (level < 0)
        level = 0;
    else if (level > 100)
        level = 100;
    audio->setVolume((float)level / 100.0);
}

EXPORT const char * CALL VolumeGetString(void)
{
    static char VolumeString[32];

    if (VolIsMuted)
    {
        strcpy(VolumeString, "Mute");
    }
    else
    {
        sprintf(VolumeString, "%i%%", 100);
    }

    return VolumeString;
}
