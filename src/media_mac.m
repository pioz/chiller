#include "media.h"

#import <Foundation/Foundation.h>
#import <MediaPlayer/MediaPlayer.h>

#include <string.h>

static void (*handler)(MediaCommand);
static int last_playing = -1;
static char last_title[64], last_album[64];

static void handle(MPRemoteCommand *c, MediaCommand cmd)
{
    c.enabled = YES;
    [c addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent *e) {
        (void)e;
        if (handler) handler(cmd);
        return MPRemoteCommandHandlerStatusSuccess;
    }];
}

void media_start(void (*on_command)(MediaCommand))
{
    @autoreleasepool {
        handler = on_command;
        MPRemoteCommandCenter *cc = [MPRemoteCommandCenter sharedCommandCenter];
        handle(cc.playCommand, MEDIA_PLAY);
        handle(cc.pauseCommand, MEDIA_PAUSE);
        handle(cc.togglePlayPauseCommand, MEDIA_TOGGLE);
        handle(cc.nextTrackCommand, MEDIA_NEXT);
        cc.previousTrackCommand.enabled = NO;
    }
}

/* The commands arrive through the main run loop: drain whatever is pending, without waiting. */
void media_poll(void) { CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, false); }

void media_update(int playing, const char *title, const char *album)
{
    if (playing == last_playing && !strcmp(title, last_title) && !strcmp(album, last_album)) return;
    last_playing = playing;
    strlcpy(last_title, title, sizeof last_title);
    strlcpy(last_album, album, sizeof last_album);

    @autoreleasepool {
        MPNowPlayingInfoCenter *np = [MPNowPlayingInfoCenter defaultCenter];
        np.nowPlayingInfo = @{
            MPMediaItemPropertyTitle : @(title),
            MPMediaItemPropertyArtist : @"chiller",
            MPMediaItemPropertyAlbumTitle : @(album),
            MPNowPlayingInfoPropertyIsLiveStream : @YES,
            MPNowPlayingInfoPropertyPlaybackRate : @(playing ? 1.0 : 0.0),
        };
        np.playbackState = playing ? MPNowPlayingPlaybackStatePlaying : MPNowPlayingPlaybackStatePaused;
    }
}

void media_stop(void)
{
    @autoreleasepool {
        MPRemoteCommandCenter *cc = [MPRemoteCommandCenter sharedCommandCenter];
        for (MPRemoteCommand *c in @[ cc.playCommand, cc.pauseCommand, cc.togglePlayPauseCommand, cc.nextTrackCommand ])
            [c removeTarget:nil];
        MPNowPlayingInfoCenter *np = [MPNowPlayingInfoCenter defaultCenter];
        np.nowPlayingInfo = nil;
        np.playbackState = MPNowPlayingPlaybackStateStopped;
    }
    handler = NULL;
}
