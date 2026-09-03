#import "macos_display_link.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include <mach/mach_time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_changed = PTHREAD_COND_INITIALIZER;
static Dkc2MacDisplayTick s_latest;
static _Atomic bool s_running;
static id s_link;         /* CADisplayLink, retained while running */
static id s_target;       /* Dkc2MacDisplayLinkTarget, retained */
static NSThread *s_thread;
static CFRunLoopRef s_run_loop;

double Dkc2MacHostSeconds(void) {
  static mach_timebase_info_data_t timebase;
  if (!timebase.denom)
    (void)mach_timebase_info(&timebase);
  return (double)mach_absolute_time() * (double)timebase.numer /
         (double)timebase.denom / 1000000000.0;
}

@interface Dkc2MacDisplayLinkTarget : NSObject
- (void)tick:(id)sender;
- (void)runLoopMain:(id)argument;
@end

@implementation Dkc2MacDisplayLinkTarget
- (void)tick:(id)sender {
  if (@available(macOS 14.0, *)) {
    CADisplayLink *link = (CADisplayLink *)sender;
    const double now = Dkc2MacHostSeconds();
    pthread_mutex_lock(&s_mutex);
    s_latest.interval = s_latest.sequence ? now - s_latest.timestamp : 0.0;
    s_latest.sequence++;
    s_latest.timestamp = now;
    s_latest.target = link.targetTimestamp;
    s_latest.duration = link.duration;
    pthread_cond_broadcast(&s_changed);
    pthread_mutex_unlock(&s_mutex);
  }
}

- (void)runLoopMain:(id)argument {
  @autoreleasepool {
    if (@available(macOS 14.0, *)) {
      CADisplayLink *link = (CADisplayLink *)argument;
      s_run_loop = CFRunLoopGetCurrent();
      [link addToRunLoop:[NSRunLoop currentRunLoop]
                 forMode:NSDefaultRunLoopMode];
      while (atomic_load(&s_running)) {
        @autoreleasepool {
          [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                   beforeDate:[NSDate distantFuture]];
        }
      }
    }
  }
}
@end

static void SetError(char *error, size_t capacity, const char *message) {
  if (error && capacity)
    (void)snprintf(error, capacity, "%s", message);
}

bool Dkc2MacDisplayLinkStart(void *native_window, double preferred_hz,
                             char *error, size_t error_capacity) {
  if (atomic_load(&s_running)) return true;
  if (!native_window) {
    SetError(error, error_capacity, "no native window for the display link");
    return false;
  }
  if (@available(macOS 14.0, *)) {
    NSWindow *window = (NSWindow *)native_window;
    Dkc2MacDisplayLinkTarget *target = [[Dkc2MacDisplayLinkTarget alloc] init];
    CADisplayLink *link = [window displayLinkWithTarget:target
                                               selector:@selector(tick:)];
    if (!link) {
      [target release];
      SetError(error, error_capacity, "the window has no display link");
      return false;
    }
    [link retain];
    if (preferred_hz > 0.0) {
      link.preferredFrameRateRange = CAFrameRateRangeMake(
          (float)preferred_hz, (float)preferred_hz, (float)preferred_hz);
    }
    pthread_mutex_lock(&s_mutex);
    s_latest.sequence = 0;
    s_latest.timestamp = 0.0;
    s_latest.target = 0.0;
    s_latest.duration = 0.0;
    s_latest.interval = 0.0;
    pthread_mutex_unlock(&s_mutex);
    s_target = target;
    s_link = link;
    atomic_store(&s_running, true);
    s_thread = [[NSThread alloc] initWithTarget:target
                                       selector:@selector(runLoopMain:)
                                         object:link];
    s_thread.name = @"DKC2 display link";
    s_thread.qualityOfService = NSQualityOfServiceUserInteractive;
    [s_thread start];
    return true;
  }
  SetError(error, error_capacity, "display links need macOS 14");
  return false;
}

bool Dkc2MacDisplayLinkWait(uint64_t sequence, double timeout_seconds,
                            Dkc2MacDisplayTick *tick) {
  if (!atomic_load(&s_running)) {
    if (tick) memset(tick, 0, sizeof *tick);
    return false;
  }
  struct timespec relative;
  if (timeout_seconds < 0.0) timeout_seconds = 0.0;
  relative.tv_sec = (time_t)timeout_seconds;
  relative.tv_nsec =
      (long)((timeout_seconds - (double)relative.tv_sec) * 1000000000.0);
  pthread_mutex_lock(&s_mutex);
  bool arrived = s_latest.sequence >= sequence;
  if (!arrived && timeout_seconds > 0.0) {
    (void)pthread_cond_timedwait_relative_np(&s_changed, &s_mutex, &relative);
    arrived = s_latest.sequence >= sequence;
  }
  if (tick) *tick = s_latest;
  pthread_mutex_unlock(&s_mutex);
  return arrived;
}

bool Dkc2MacDisplayLinkLatest(Dkc2MacDisplayTick *tick) {
  pthread_mutex_lock(&s_mutex);
  if (tick) *tick = s_latest;
  pthread_mutex_unlock(&s_mutex);
  return atomic_load(&s_running);
}

void Dkc2MacDisplayLinkStop(void) {
  if (!atomic_load(&s_running)) return;
  atomic_store(&s_running, false);
  if (@available(macOS 14.0, *)) {
    CADisplayLink *link = (CADisplayLink *)s_link;
    [link invalidate];
  }
  if (s_run_loop) CFRunLoopStop(s_run_loop);
  pthread_mutex_lock(&s_mutex);
  pthread_cond_broadcast(&s_changed);
  pthread_mutex_unlock(&s_mutex);
  /* The run-loop thread leaves its loop on the next wake; the link and
   * target stay allocated for the process's remaining lifetime so a late
   * callback never touches freed objects. */
  s_run_loop = NULL;
}
