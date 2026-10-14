#include "cocos/simpleaudioengine.h"

#include <3ds.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <unistd.h>
#include <vector>

#include "cocos/director.h"
#include "debuglog.h"

namespace CocosDenshion {

namespace {

// ndsp's channels: the music on 0, the effects' 20 streams on 1..20.
const int kMusicChannel = 0;
const int kFirstEffectChannel = 1;
const int kMaxStreams = 20; // Cocos2dxSound's SoundPool(20, ...)

float clamp(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void setChannelVolume(int channel, float volume)
{
    float mix[12] = {};
    mix[0] = volume;
    mix[1] = volume;
    ndspChnSetMix(channel, mix);
}

// How far the far speaker drops at a full pan: to a quarter. The near one
// keeps the volume, so a sound is never quieter than centred.
const float kPanFalloff = 0.75f;

// volume on both speakers, the far one lowered by pan (-1 left .. 1 right).
// A stereo sample's left channel goes to the left speaker and its right to
// the right, so it is balanced the same way.
void setChannelMix(int channel, float volume, float pan)
{
    float mix[12] = {};
    mix[0] = volume * (pan > 0.0f ? 1.0f - kPanFalloff * pan : 1.0f);
    mix[1] = volume * (pan < 0.0f ? 1.0f + kPanFalloff * pan : 1.0f);
    ndspChnSetMix(channel, mix);
}

// A RIFF WAVE file's PCM16 data: where it starts and its format.
struct WavInfo {
    u32 rate = 0;
    u32 channels = 0;
    u32 dataOffset = 0;
    u32 dataSize = 0;
};

bool readWavHeader(FILE* f, WavInfo& info)
{
    char riff[12];
    if (fread(riff, 1, 12, f) != 12 || memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0)
        return false;
    u32 bits = 0;
    for (;;) {
        char id[4];
        u32 size;
        if (fread(id, 1, 4, f) != 4 || fread(&size, 4, 1, f) != 1)
            return false;
        if (memcmp(id, "fmt ", 4) == 0) {
            u8 fmt[16];
            if (size < 16 || fread(fmt, 1, 16, f) != 16)
                return false;
            u16 format, channels, bps;
            memcpy(&format, fmt + 0, 2);
            memcpy(&channels, fmt + 2, 2);
            memcpy(&info.rate, fmt + 4, 4);
            memcpy(&bps, fmt + 14, 2);
            if (format != 1 || bps != 16 || channels < 1 || channels > 2)
                return false;
            info.channels = channels;
            bits = bps;
            fseek(f, (long)((size - 16 + 1) & ~1u), SEEK_CUR);
        } else if (memcmp(id, "data", 4) == 0) {
            if (bits != 16)
                return false;
            info.dataOffset = (u32)ftell(f);
            info.dataSize = size;
            return true;
        } else {
            fseek(f, (long)((size + 1) & ~1u), SEEK_CUR);
        }
    }
}

// ------------------------------------------------------------- effects

struct Sample {
    s16* pcm = nullptr; // linear memory
    u32 frames = 0;
    u32 rate = 0;
    u32 channels = 0;
};

struct Stream {
    int id = 0;             // 0: free
    int priority = 0;
    u32 order = 0;          // when it started: the oldest of equals goes first
    bool loop = false;
    float volume = 0.0f;    // the effects volume times the stream's own
    float pan = 0.0f;
    ndspWaveBuf wb;
};

std::vector<Sample> s_samples;
std::map<std::string, int> s_sampleIds; // mPathSoundIDMap
Stream s_streams[kMaxStreams];
int s_nextStreamId = 1;
u32 s_nextOrder = 0;
int s_nextLowPriority = 999999999; // _nextLowPrioritySoundPriority
float s_effectsVolume = 0.5f;      // mLeftVolume = mRightVolume

bool streamDone(const Stream& s)
{
    return s.id == 0 || (!s.loop && s.wb.status == NDSP_WBUF_DONE);
}

void stopStream(int index)
{
    Stream& s = s_streams[index];
    if (s.id == 0)
        return;
    int channel = kFirstEffectChannel + index;
    ndspChnWaveBufClear(channel);
    ndspChnReset(channel);
    s.id = 0;
}

Stream* findStream(int soundId)
{
    if (soundId <= 0)
        return nullptr;
    for (Stream& s : s_streams)
        if (s.id == soundId && !streamDone(s))
            return &s;
    return nullptr;
}

// ------------------------------------------------------------- music

// The music streams off the card: kMusicBuffers buffers of kMusicFrames
// frames go round on channel 0, refilled by a thread as the DSP finishes
// each. 8 x 16384 frames at 44.1 kHz is 3 s queued (512 KB of linear
// memory): the card can be busy that long (the imagery's writes, the HOME
// Menu's) without a gap. 1.0.0's 0.74 s was not enough.
const int kMusicBuffers = 8;
const u32 kMusicFrames = 16384;
// Queued by musicStart itself, on the caller's (the main) thread: the rest
// are the music thread's, within its next pass, so a start costs a frame
// no more than one read.
const int kMusicStartBuffers = 1;

struct Music {
    LightLock lock;
    FILE* file = nullptr;
    std::string path;
    WavInfo info;
    u32 position = 0; // bytes into the data
    bool loop = false;
    bool paused = false;
    bool playing = false; // started and not played out
    bool ended = false;   // every byte queued (not looping)
    float volume = 0.5f;  // Cocos2dxMusic's mLeftVolume until set
    u32 buffersPlayed = 0; // for the log
    s16* buffers[kMusicBuffers] = {};
    ndspWaveBuf wb[kMusicBuffers];
};

Music s_music;
Thread s_musicThread = nullptr;
volatile bool s_musicThreadRun = false;

// Fills buffer i from the file, wrapping round when looping, and queues it.
// False when there is nothing left to queue. With the lock held.
bool musicQueue(int i)
{
    Music& m = s_music;
    if (!m.file || m.ended)
        return false;
    const u32 frameBytes = 2 * m.info.channels;
    const u32 want = kMusicFrames * frameBytes;
    u8* dst = reinterpret_cast<u8*>(m.buffers[i]);
    u32 got = 0;
    while (got < want) {
        if (m.position >= m.info.dataSize) {
            if (!m.loop)
                break;
            m.position = 0;
        }
        u32 n = want - got;
        if (n > m.info.dataSize - m.position)
            n = m.info.dataSize - m.position;
        fseek(m.file, (long)(m.info.dataOffset + m.position), SEEK_SET);
        size_t r = fread(dst + got, 1, n, m.file);
        if (r == 0) {
            m.position = m.info.dataSize; // a short file: treat as its end
            if (!m.loop)
                break;
            continue;
        }
        got += (u32)r;
        m.position += (u32)r;
    }
    got -= got % frameBytes;
    if (got == 0) {
        m.ended = true;
        return false;
    }
    if (!m.loop && m.position >= m.info.dataSize)
        m.ended = true;
    DSP_FlushDataCache(dst, got);
    ndspWaveBuf& wb = m.wb[i];
    memset(&wb, 0, sizeof(wb));
    wb.data_pcm16 = m.buffers[i];
    wb.nsamples = got / frameBytes;
    ndspChnWaveBufAdd(kMusicChannel, &wb);
    return true;
}

void musicClose()
{
    Music& m = s_music;
    ndspChnWaveBufClear(kMusicChannel);
    ndspChnReset(kMusicChannel);
    if (m.file) {
        fclose(m.file);
        m.file = nullptr;
    }
    for (ndspWaveBuf& wb : m.wb)
        memset(&wb, 0, sizeof(wb));
    m.playing = false;
    m.ended = false;
    m.paused = false;
}

// Opens path and queues its first buffer, from the start; the music thread
// queues the rest. With the lock held.
bool musicStart(const std::string& path)
{
    Music& m = s_music;
    musicClose();
    m.file = fopen(path.c_str(), "rb");
    if (!m.file) {
        dbg("audio: no music %s", path.c_str());
        return false;
    }
    // Each read is a whole buffer: one request to the card, not stdio's
    // pieces.
    setvbuf(m.file, nullptr, _IONBF, 0);
    if (!readWavHeader(m.file, m.info)) {
        dbg("audio: bad music %s", path.c_str());
        fclose(m.file);
        m.file = nullptr;
        return false;
    }
    m.path = path;
    m.position = 0;
    m.ended = false;
    ndspChnReset(kMusicChannel);
    ndspChnSetInterp(kMusicChannel, NDSP_INTERP_POLYPHASE);
    ndspChnSetRate(kMusicChannel, (float)m.info.rate);
    ndspChnSetFormat(kMusicChannel, m.info.channels == 2 ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16);
    setChannelVolume(kMusicChannel, m.volume);
    for (int i = 0; i < kMusicStartBuffers; i++)
        if (!musicQueue(i))
            break;
    m.playing = true;
    return true;
}

void musicThread(void*)
{
    while (s_musicThreadRun) {
        Music& m = s_music;
        LightLock_Lock(&m.lock);
        if (m.playing) {
            bool busy = false;
            for (int i = 0; i < kMusicBuffers; i++) {
                ndspWaveBuf& wb = m.wb[i];
                if (wb.status == NDSP_WBUF_DONE || wb.status == NDSP_WBUF_FREE) {
                    if (wb.status == NDSP_WBUF_DONE)
                        m.buffersPlayed++;
                    wb.status = NDSP_WBUF_FREE;
                    if (musicQueue(i))
                        busy = true;
                } else {
                    busy = true;
                }
            }
            // Not looping and played out.
            if (!busy && m.ended)
                m.playing = false;
        }
        LightLock_Unlock(&m.lock);
        svcSleepThread(20 * 1000 * 1000);
    }
}

} // namespace

// ------------------------------------------------------------- engine

SimpleAudioEngine* SimpleAudioEngine::getInstance()
{
    static SimpleAudioEngine* s = new SimpleAudioEngine();
    return s;
}

void SimpleAudioEngine::end()
{
    getInstance()->shutdown();
}

SimpleAudioEngine::SimpleAudioEngine()
{
    // ndspInit loads the DSP's firmware from sdmc:/3ds/dspfirm.cdc, or from
    // a 3dsx loader's hb:ndsp handle. Without either it does not reliably
    // fail (it can fault), so check first.
    if (access("sdmc:/3ds/dspfirm.cdc", F_OK) != 0 && envGetHandle("hb:ndsp") == 0) {
        dbg("audio: no sdmc:/3ds/dspfirm.cdc, no sound");
        return;
    }
    Result rc = ndspInit();
    if (R_FAILED(rc)) {
        dbg("audio: ndspInit failed %08lx, no sound", (unsigned long)rc);
        return;
    }
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspSetClippingMode(NDSP_CLIP_SOFT);
    ndspSetMasterVol(1.0f);

    for (Stream& s : s_streams)
        memset(&s.wb, 0, sizeof(s.wb));

    LightLock_Init(&s_music.lock);
    const u32 bytes = kMusicFrames * 2 * 2;
    for (int i = 0; i < kMusicBuffers; i++) {
        s_music.buffers[i] = static_cast<s16*>(linearAlloc(bytes));
        memset(&s_music.wb[i], 0, sizeof(ndspWaveBuf));
    }
    // Above the main thread, so the music keeps up through a long frame.
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    s_musicThreadRun = true;
    // On the main thread's core (-2): an application's other core is
    // time-limited.
    s_musicThread = threadCreate(musicThread, nullptr, 16 * 1024, prio > 0x18 ? prio - 1 : prio, -2, false);
    if (!s_musicThread)
        s_musicThreadRun = false;
    _ready = true;
    dbg("audio: ndsp up");
}

SimpleAudioEngine::~SimpleAudioEngine()
{
    shutdown();
}

void SimpleAudioEngine::shutdown()
{
    if (!_ready)
        return;
    if (s_musicThread) {
        s_musicThreadRun = false;
        threadJoin(s_musicThread, U64_MAX);
        threadFree(s_musicThread);
        s_musicThread = nullptr;
    }
    musicClose();
    for (int i = 0; i < kMusicBuffers; i++) {
        linearFree(s_music.buffers[i]);
        s_music.buffers[i] = nullptr;
    }
    for (int i = 0; i < kMaxStreams; i++)
        stopStream(i);
    for (Sample& s : s_samples)
        linearFree(s.pcm);
    s_samples.clear();
    s_sampleIds.clear();
    ndspExit();
    _ready = false;
}

int SimpleAudioEngine::effectsPlaying() const
{
    int n = 0;
    for (const Stream& s : s_streams)
        if (!streamDone(s))
            n++;
    return n;
}

unsigned SimpleAudioEngine::musicSamplePosition() const
{
    return _ready ? s_music.buffersPlayed * kMusicFrames + ndspChnGetSamplePos(kMusicChannel) : 0;
}

// ------------------------------------------------------------- music

void SimpleAudioEngine::playBackgroundMusic(const char* filePath, bool loop)
{
    if (!_ready)
        return;
    std::string path = cocos2d::FileUtils::getInstance()->fullPathForFilename(filePath);
    LightLock_Lock(&s_music.lock);
    // Cocos2dxMusic: the same path playing starts again from the top.
    s_music.loop = loop;
    musicStart(path);
    LightLock_Unlock(&s_music.lock);
}

void SimpleAudioEngine::stopBackgroundMusic(bool)
{
    if (!_ready)
        return;
    LightLock_Lock(&s_music.lock);
    musicClose();
    LightLock_Unlock(&s_music.lock);
}

void SimpleAudioEngine::pauseBackgroundMusic()
{
    if (!_ready)
        return;
    LightLock_Lock(&s_music.lock);
    if (s_music.playing) {
        s_music.paused = true;
        ndspChnSetPaused(kMusicChannel, true);
    }
    LightLock_Unlock(&s_music.lock);
}

void SimpleAudioEngine::resumeBackgroundMusic()
{
    if (!_ready)
        return;
    LightLock_Lock(&s_music.lock);
    if (s_music.paused) {
        s_music.paused = false;
        ndspChnSetPaused(kMusicChannel, false);
    }
    LightLock_Unlock(&s_music.lock);
}

void SimpleAudioEngine::rewindBackgroundMusic()
{
    if (!_ready)
        return;
    LightLock_Lock(&s_music.lock);
    if (!s_music.path.empty())
        musicStart(s_music.path);
    LightLock_Unlock(&s_music.lock);
}

bool SimpleAudioEngine::willPlayBackgroundMusic()
{
    return true;
}

bool SimpleAudioEngine::isBackgroundMusicPlaying()
{
    return _ready && s_music.playing && !s_music.paused;
}

float SimpleAudioEngine::getBackgroundMusicVolume()
{
    return s_music.volume;
}

void SimpleAudioEngine::setBackgroundMusicVolume(float volume)
{
    s_music.volume = clamp(volume, 0.0f, 1.0f);
    if (_ready)
        setChannelVolume(kMusicChannel, s_music.volume);
}

// ------------------------------------------------------------- effects

float SimpleAudioEngine::getEffectsVolume()
{
    return s_effectsVolume;
}

void SimpleAudioEngine::setEffectsVolume(float volume)
{
    // Cocos2dxSound sets every stream to the new volume, dropping each one's
    // own (SoundPool keeps no gain apart from the volume).
    s_effectsVolume = clamp(volume, 0.0f, 1.0f);
    if (!_ready)
        return;
    for (int i = 0; i < kMaxStreams; i++) {
        Stream& s = s_streams[i];
        if (!streamDone(s)) {
            s.volume = s_effectsVolume;
            setChannelMix(kFirstEffectChannel + i, s.volume, s.pan);
        }
    }
}

void SimpleAudioEngine::setEffectVolume(int soundId, float volume)
{
    if (!_ready)
        return;
    Stream* s = findStream(soundId);
    if (!s)
        return;
    s->volume = s_effectsVolume * clamp(volume, 0.0f, 1.0f);
    setChannelMix(kFirstEffectChannel + (int)(s - s_streams), s->volume, s->pan);
}

void SimpleAudioEngine::setEffectPan(int soundId, float pan)
{
    if (!_ready)
        return;
    Stream* s = findStream(soundId);
    if (!s)
        return;
    pan = clamp(pan, -1.0f, 1.0f);
    if (pan == s->pan)
        return;
    s->pan = pan;
    setChannelMix(kFirstEffectChannel + (int)(s - s_streams), s->volume, s->pan);
}

int SimpleAudioEngine::playEffectById(int sampleId, bool loop, float pitch, float pan, float gain, int priorityGroup)
{
    if (!_ready || sampleId < 0 || sampleId >= (int)s_samples.size())
        return 0;
    int priority;
    if (priorityGroup < 1) {
        priority = s_nextLowPriority--;
        if (s_nextLowPriority < 1)
            s_nextLowPriority = 999999999;
    } else {
        priority = priorityGroup + 1000000000;
    }

    // SoundPool's allocation: a free stream, or else the lowest priority one
    // (the oldest of equals), if the new sound ranks no lower.
    int index = -1;
    for (int i = 0; i < kMaxStreams; i++) {
        if (streamDone(s_streams[i])) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        for (int i = 0; i < kMaxStreams; i++) {
            const Stream& s = s_streams[i];
            if (index < 0 || s.priority < s_streams[index].priority ||
                (s.priority == s_streams[index].priority && (s32)(s.order - s_streams[index].order) < 0))
                index = i;
        }
        if (priority < s_streams[index].priority)
            return 0;
    }
    stopStream(index);

    const Sample& sample = s_samples[sampleId];
    Stream& s = s_streams[index];
    int channel = kFirstEffectChannel + index;
    ndspChnReset(channel);
    ndspChnSetInterp(channel, NDSP_INTERP_POLYPHASE);
    ndspChnSetRate(channel, sample.rate * clamp(pitch, 0.5f, 2.0f));
    ndspChnSetFormat(channel, sample.channels == 2 ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16);
    s.volume = clamp(s_effectsVolume * gain, 0.0f, 1.0f);
    s.pan = clamp(pan, -1.0f, 1.0f);
    setChannelMix(channel, s.volume, s.pan);
    memset(&s.wb, 0, sizeof(s.wb));
    s.wb.data_pcm16 = sample.pcm;
    s.wb.nsamples = sample.frames;
    s.wb.looping = loop;
    s.id = s_nextStreamId++;
    if (s_nextStreamId <= 0)
        s_nextStreamId = 1;
    s.priority = priority;
    s.order = s_nextOrder++;
    s.loop = loop;
    ndspChnWaveBufAdd(channel, &s.wb);
    return s.id;
}

void SimpleAudioEngine::pauseAllEffects()
{
    if (!_ready)
        return;
    for (int i = 0; i < kMaxStreams; i++)
        if (!streamDone(s_streams[i]))
            ndspChnSetPaused(kFirstEffectChannel + i, true);
}

void SimpleAudioEngine::resumeAllEffects()
{
    if (!_ready)
        return;
    for (int i = 0; i < kMaxStreams; i++)
        if (!streamDone(s_streams[i]))
            ndspChnSetPaused(kFirstEffectChannel + i, false);
}

void SimpleAudioEngine::stopEffect(int soundId)
{
    if (!_ready)
        return;
    Stream* s = findStream(soundId);
    if (s)
        stopStream((int)(s - s_streams));
}

void SimpleAudioEngine::stopAllEffects()
{
    if (!_ready)
        return;
    for (int i = 0; i < kMaxStreams; i++)
        stopStream(i);
}

int SimpleAudioEngine::preloadEffect(const char* filePath)
{
    if (!_ready)
        return -1;
    auto it = s_sampleIds.find(filePath);
    if (it != s_sampleIds.end())
        return it->second;

    std::string path = cocos2d::FileUtils::getInstance()->fullPathForFilename(filePath);
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        dbg("audio: no effect %s", path.c_str());
        return -1;
    }
    WavInfo info;
    Sample sample;
    if (readWavHeader(f, info) && info.dataSize >= 2 * info.channels) {
        sample.pcm = static_cast<s16*>(linearAlloc(info.dataSize));
        if (sample.pcm && fread(sample.pcm, 1, info.dataSize, f) == info.dataSize) {
            sample.frames = info.dataSize / (2 * info.channels);
            sample.rate = info.rate;
            sample.channels = info.channels;
            DSP_FlushDataCache(sample.pcm, info.dataSize);
        } else {
            dbg("audio: %s: %s", path.c_str(), sample.pcm ? "short read" : "out of linear memory");
            linearFree(sample.pcm);
            sample.pcm = nullptr;
        }
    } else {
        dbg("audio: bad effect %s", path.c_str());
    }
    fclose(f);
    if (!sample.pcm)
        return -1;
    int id = (int)s_samples.size();
    s_samples.push_back(sample);
    s_sampleIds[filePath] = id;
    return id;
}

} // namespace CocosDenshion
