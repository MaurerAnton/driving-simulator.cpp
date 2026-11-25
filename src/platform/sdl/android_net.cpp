// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The network on Android (sdl_host.h: androidFetch): Java's own HttpURLConnection, asked
 * through JNI – no Java is compiled for it, the classes are the platform's.  It runs on the
 * native host's network threads (plat_native: setNetwork); SDL attaches each of them to the
 * Java VM the first time it asks for its JNIEnv, and lets it go when the thread ends.
 */
#include "platform/sdl/sdl_host.h"

#if defined(__ANDROID__)

#include <SDL3/SDL.h>
#include <jni.h>

namespace fs::sdl {

namespace {

/** a Java exception happened: told, cleared, and true */
bool thrown(JNIEnv* env, const char* what, const std::string& url) {
  if (!env->ExceptionCheck()) return false;
  jthrowable t = env->ExceptionOccurred();
  env->ExceptionClear();
  std::string why = "?";
  if (t) {
    jclass c = env->GetObjectClass(t);
    jmethodID m = env->GetMethodID(c, "toString", "()Ljava/lang/String;");
    if (m) {
      auto s = static_cast<jstring>(env->CallObjectMethod(t, m));
      if (!env->ExceptionCheck() && s) {
        const char* chars = env->GetStringUTFChars(s, nullptr);
        why = chars ? chars : "?";
        if (chars) env->ReleaseStringUTFChars(s, chars);
      } else env->ExceptionClear();
    }
  }
  std::fprintf(stderr, "fahrsim-sdl: the network (%s) %s: %s\n", what, url.c_str(), why.c_str());
  return true;
}

jstring str(JNIEnv* env, const std::string& s) { return env->NewStringUTF(s.c_str()); }

}  // namespace

plat::Response androidFetch(const plat::Request& request) {
  plat::Response res;
  auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  if (!env) return res;
  if (env->PushLocalFrame(64) != 0) {
    env->ExceptionClear();
    return res;
  }
  const std::string& u = request.url;
  [&] {
    jclass urlClass = env->FindClass("java/net/URL");
    if (thrown(env, "URL", u) || !urlClass) return;
    jobject url = env->NewObject(urlClass, env->GetMethodID(urlClass, "<init>", "(Ljava/lang/String;)V"), str(env, u));
    if (thrown(env, "URL", u)) return;
    jobject conn = env->CallObjectMethod(url, env->GetMethodID(urlClass, "openConnection", "()Ljava/net/URLConnection;"));
    if (thrown(env, "open", u) || !conn) return;
    jclass http = env->FindClass("java/net/HttpURLConnection");
    if (thrown(env, "HttpURLConnection", u) || !http || !env->IsInstanceOf(conn, http)) return;
    env->CallVoidMethod(conn, env->GetMethodID(http, "setConnectTimeout", "(I)V"), 20000);
    env->CallVoidMethod(conn, env->GetMethodID(http, "setReadTimeout", "(I)V"), 60000);
    env->CallVoidMethod(conn, env->GetMethodID(http, "setInstanceFollowRedirects", "(Z)V"), JNI_TRUE);
    if (request.cache == "no-store" || request.cache == "no-cache" || request.cache == "reload")
      env->CallVoidMethod(conn, env->GetMethodID(http, "setUseCaches", "(Z)V"), JNI_FALSE);
    env->CallVoidMethod(conn, env->GetMethodID(http, "setRequestMethod", "(Ljava/lang/String;)V"), str(env, request.method));
    if (thrown(env, "method", u)) return;
    const jmethodID setProperty = env->GetMethodID(http, "setRequestProperty", "(Ljava/lang/String;Ljava/lang/String;)V");
    for (const auto& [name, value] : request.headers) {
      env->CallVoidMethod(conn, setProperty, str(env, name), str(env, value));
      if (thrown(env, "header", u)) return;
    }
    if (!request.body.empty()) {
      env->CallVoidMethod(conn, env->GetMethodID(http, "setDoOutput", "(Z)V"), JNI_TRUE);
      jobject out = env->CallObjectMethod(conn, env->GetMethodID(http, "getOutputStream", "()Ljava/io/OutputStream;"));
      if (thrown(env, "send", u) || !out) return;
      jbyteArray bytes = env->NewByteArray(static_cast<jsize>(request.body.size()));
      env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(request.body.size()), reinterpret_cast<const jbyte*>(request.body.data()));
      jclass os = env->GetObjectClass(out);
      env->CallVoidMethod(out, env->GetMethodID(os, "write", "([B)V"), bytes);
      env->CallVoidMethod(out, env->GetMethodID(os, "close", "()V"));
      if (thrown(env, "send", u)) return;
    }
    const jint status = env->CallIntMethod(conn, env->GetMethodID(http, "getResponseCode", "()I"));
    if (thrown(env, "answer", u)) return;
    auto type = static_cast<jstring>(env->CallObjectMethod(conn, env->GetMethodID(http, "getContentType", "()Ljava/lang/String;")));
    if (!thrown(env, "type", u) && type) {
      const char* chars = env->GetStringUTFChars(type, nullptr);
      if (chars) {
        res.contentType = chars;
        env->ReleaseStringUTFChars(type, chars);
      }
    }
    // (an error's body comes from the error stream – which may be none at all)
    jobject in = status >= 400 ? env->CallObjectMethod(conn, env->GetMethodID(http, "getErrorStream", "()Ljava/io/InputStream;"))
                               : env->CallObjectMethod(conn, env->GetMethodID(http, "getInputStream", "()Ljava/io/InputStream;"));
    if (thrown(env, "body", u)) in = nullptr;
    if (in) {
      jclass is = env->GetObjectClass(in);
      const jmethodID read = env->GetMethodID(is, "read", "([B)I");
      jbyteArray chunk = env->NewByteArray(1 << 16);
      for (;;) {
        const jint got = env->CallIntMethod(in, read, chunk);
        if (thrown(env, "read", u)) {
          env->CallVoidMethod(in, env->GetMethodID(is, "close", "()V"));
          env->ExceptionClear();
          env->CallVoidMethod(conn, env->GetMethodID(http, "disconnect", "()V"));
          env->ExceptionClear();
          return;   // (cut off: no answer, as a browser's failed fetch)
        }
        if (got < 0) break;
        const size_t at = res.body.size();
        res.body.resize(at + static_cast<size_t>(got));
        env->GetByteArrayRegion(chunk, 0, got, reinterpret_cast<jbyte*>(res.body.data() + at));
      }
      env->CallVoidMethod(in, env->GetMethodID(is, "close", "()V"));
      env->ExceptionClear();
    }
    env->CallVoidMethod(conn, env->GetMethodID(http, "disconnect", "()V"));
    env->ExceptionClear();
    res.status = status;
  }();
  env->PopLocalFrame(nullptr);
  return res;
}

}  // namespace fs::sdl

#endif  // __ANDROID__
