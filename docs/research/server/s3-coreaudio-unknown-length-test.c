/* CoreAudio test: does AudioFileOpenWithCallbacks accept a WAV whose RIFF and
 * data chunk sizes are 0xFFFFFFFF, (a) with an unknown-length getSize callback
 * and (b) with a known total length?  This is the layer Safari's
 * AudioFileReaderMac / MediaPlayerPrivateAVFoundationObjC sits on. */
#include <AudioToolbox/AudioToolbox.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define HZ 44100
#define FRAMES (HZ * 4)
static unsigned char *buf;
static long len;

static OSStatus readcb(void *inClientData, SInt64 inPosition, UInt32 requestCount,
                       void *buffer, UInt32 *actualCount) {
    if (inPosition >= len) { *actualCount = 0; return noErr; }
    UInt32 n = requestCount;
    if (inPosition + n > (SInt64)len) n = (UInt32)(len - inPosition);
    memcpy(buffer, buf + inPosition, n);
    *actualCount = n;
    return noErr;
}
static SInt64 sizecb(void *inClientData) { return len; }
static SInt64 sizecb_unknown(void *inClientData) { return -1; /* kAudioFileUnspecifiedLength */ }

static void build(int endless) {
    len = 44 + FRAMES * 4;
    buf = calloc(1, len);
    unsigned int riff = endless ? 0xFFFFFFFFu : (unsigned)(len - 8);
    unsigned int data = endless ? 0xFFFFFFFFu : (unsigned)(FRAMES * 4);
    memcpy(buf, "RIFF", 4); memcpy(buf + 4, &riff, 4);
    memcpy(buf + 8, "WAVEfmt ", 8);
    unsigned int fmtlen = 16; memcpy(buf + 16, &fmtlen, 4);
    unsigned short pcm = 1, ch = 2, align = 4, bits = 16;
    unsigned int brate = HZ * 4, hz = HZ;
    memcpy(buf + 20, &pcm, 2);  memcpy(buf + 22, &ch, 2);
    memcpy(buf + 24, &hz, 4);   memcpy(buf + 28, &brate, 4);
    memcpy(buf + 32, &align, 2);memcpy(buf + 34, &bits, 2);
    memcpy(buf + 36, "data", 4);memcpy(buf + 40, &data, 4);
    short *p = (short *)(buf + 44);
    for (int i = 0; i < FRAMES * 2; i++) p[i] = (short)(12000 * sin(2 * M_PI * 440 * (i / 2) / HZ));
}

static void run(const char *label, int endless, int unknown_size) {
    build(endless);
    AudioFileID af = NULL;
    OSStatus st = AudioFileOpenWithCallbacks(NULL, readcb,
                                             NULL, /* write */
                                             unknown_size ? sizecb_unknown : sizecb,
                                             NULL, 0, &af);
    printf("%-46s AudioFileOpenWithCallbacks = %d", label, (int)st);
    if (st == noErr) {
        AudioStreamBasicDescription asbd; UInt32 sz = sizeof asbd;
        OSStatus s2 = AudioFileGetProperty(af, kAudioFilePropertyDataFormat, &sz, &asbd);
        printf("  [ok] dataFormat=%d Hz=%.0f ch=%u bits=%u", (int)s2,
               asbd.mSampleRate, asbd.mChannelsPerFrame, asbd.mBitsPerChannel);
        SInt64 total = 0; sz = sizeof total;
        AudioFileGetProperty(af, kAudioFilePropertyAudioDataByteCount, &sz, &total);
        printf(" dataByteCount=%lld", total);
        AudioFileClose(af);
    } else {
        char fourcc[5] = {0};
        unsigned int v = (unsigned int)st;
        fourcc[0] = (v >> 24) & 0xff; fourcc[1] = (v >> 16) & 0xff;
        fourcc[2] = (v >> 8) & 0xff;  fourcc[3] = v & 0xff;
        printf("  (fourcc '%s')", fourcc);
    }
    printf("\n");
    free(buf);
}

int main(void) {
    run("finite sizes, known length", 0, 0);
    run("finite sizes, UNKNOWN length (-1)", 0, 1);
    run("0xFFFFFFFF sizes, known length", 1, 0);
    run("0xFFFFFFFF sizes, UNKNOWN length (-1)", 1, 1);
    return 0;
}
