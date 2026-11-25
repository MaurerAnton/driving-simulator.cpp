// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The game as an APK (experimental – docs/SDL.md, "Android"): Android starts the activity
 * itself (org.libsdl.nativeapp.NativeActivity, the classes the SDL fork j4niwzis/SDL writes
 * out), SDL3 bridges it, and what runs once SDL has the window is main_sdl.cpp's main
 * (SDL_main.h names it SDL_main on Android).
 *
 * Taken from progressive-chat's src/main_android.cc (the same author, AGPL-3.0-only), with
 * the lessons written down there: Android throws stdout and stderr away – they are piped
 * into logcat here (`adb logcat -s fahrsim`), and a crash says its signal there before the
 * system's tombstone; the program runs on the thread SDL was given (the one events are told
 * to), not on a thread of its own.
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_nativeactivity.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <string>
#include <thread>
#include <utility>

extern "C" int SDL_main(int argc, char** argv);

namespace {

constexpr const char* TAG = "fahrsim";

extern "C" void crashSaysWhere(int signo, siginfo_t* why, void*) {
  __android_log_print(ANDROID_LOG_ERROR, TAG, "gone: signal %d, code %d, at %p", signo, why ? why->si_code : 0, why ? why->si_addr : nullptr);
  std::signal(signo, SIG_DFL);
  std::raise(signo);
}

struct CrashWatcher {
  CrashWatcher() {
    for (int one : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) {
      struct sigaction how {};
      how.sa_sigaction = &crashSaysWhere;
      sigemptyset(&how.sa_mask);
      how.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART;
      sigaction(one, &how, nullptr);
    }
  }
} watcher;

/** stdout and stderr into logcat, line by line (once a process: an activity made again keeps it) */
void outputToLogcat() {
  static bool done = false;
  if (std::exchange(done, true)) return;
  int ends[2];
  if (pipe(ends) != 0) return;
  (void)fcntl(ends[1], F_SETPIPE_SZ, 1 << 20);   // (room for a burst: a full pipe would hold the game's thread)
  if (dup2(ends[1], STDOUT_FILENO) < 0 || dup2(ends[1], STDERR_FILENO) < 0) {
    close(ends[0]);
    close(ends[1]);
    return;
  }
  close(ends[1]);
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);
  std::thread([from = ends[0]] {
    std::string pending;
    char chunk[1024];
    for (;;) {
      const ssize_t got = read(from, chunk, sizeof chunk);
      if (got <= 0) return;
      pending.append(chunk, static_cast<size_t>(got));
      for (size_t end; (end = pending.find('\n')) != std::string::npos; pending.erase(0, end + 1)) {
        pending[end] = '\0';
        __android_log_write(ANDROID_LOG_INFO, TAG, pending.c_str());
      }
      if (pending.size() > 3000) {
        __android_log_write(ANDROID_LOG_INFO, TAG, pending.c_str());
        pending.clear();
      }
    }
  }).detach();
}

int run(int, char**) {
  outputToLogcat();
  {
    pthread_attr_t was;
    size_t tall = 0;
    void* low = nullptr;
    if (pthread_getattr_np(pthread_self(), &was) == 0) {
      pthread_attr_getstack(&was, &low, &tall);
      pthread_attr_destroy(&was);
    }
    __android_log_print(ANDROID_LOG_INFO, TAG, "started (the stack: %zu bytes)", tall);
  }
  char name[] = "fahrsim-sdl";
  char* argv[] = {name, nullptr};
  const int said = SDL_main(1, argv);
  __android_log_print(ANDROID_LOG_INFO, TAG, "it answered %d", said);
  return said;
}

}  // namespace

extern "C" __attribute__((visibility("default"))) void ANativeActivity_onCreate(ANativeActivity* activity, void* saved, size_t size) {
  SDL_AndroidNativeActivity(activity, saved, size, run);
}
