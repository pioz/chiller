#include "audio.h"
#include "synth.h"

#include <AudioToolbox/AudioToolbox.h>

#define N_BUFFERS 3
#define BUFFER_FRAMES 1024

static AudioQueueRef queue;

static void fill(void *user, AudioQueueRef q, AudioQueueBufferRef buf)
{
    (void)user;
    int frames = (int)(buf->mAudioDataBytesCapacity / (2 * sizeof(float)));
    synth_render((float *)buf->mAudioData, frames);
    buf->mAudioDataByteSize = (UInt32)(frames * 2 * sizeof(float));
    AudioQueueEnqueueBuffer(q, buf, 0, NULL);
}

int audio_start(void)
{
    AudioStreamBasicDescription fmt = {0};
    fmt.mSampleRate = SAMPLE_RATE;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kLinearPCMFormatFlagIsFloat | kLinearPCMFormatFlagIsPacked;
    fmt.mFramesPerPacket = 1;
    fmt.mChannelsPerFrame = 2;
    fmt.mBitsPerChannel = 32;
    fmt.mBytesPerFrame = 2 * sizeof(float);
    fmt.mBytesPerPacket = fmt.mBytesPerFrame;

    /* NULL run loop: callbacks run on an internal AudioQueue thread */
    if (AudioQueueNewOutput(&fmt, fill, NULL, NULL, NULL, 0, &queue) != noErr) return -1;

    for (int i = 0; i < N_BUFFERS; i++) {
        AudioQueueBufferRef buf;
        if (AudioQueueAllocateBuffer(queue, BUFFER_FRAMES * 2 * sizeof(float), &buf) != noErr) return -1;
        fill(NULL, queue, buf);
    }
    return AudioQueueStart(queue, NULL) == noErr ? 0 : -1;
}

void audio_stop(void)
{
    if (!queue) return;
    AudioQueueStop(queue, true);
    AudioQueueDispose(queue, true);
    queue = NULL;
}
