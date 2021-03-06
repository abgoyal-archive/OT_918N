// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP_HTTP_AUTH_HANDLER_NTLM_H_
#define NET_HTTP_HTTP_AUTH_HANDLER_NTLM_H_

#include "build/build_config.h"

// This contains the portable and the SSPI implementations for NTLM.
// We use NTLM_SSPI for Windows, and NTLM_PORTABLE for other platforms.
#if defined(OS_WIN)
#define NTLM_SSPI
#else
#define NTLM_PORTABLE
#endif

#if defined(NTLM_SSPI)
#define SECURITY_WIN32 1
#include <windows.h>
#include <security.h>
#include "net/http/http_auth_sspi_win.h"
#endif

#include <string>

#include "base/basictypes.h"
#include "base/string16.h"
#include "net/http/http_auth_handler.h"

namespace net {

// Code for handling HTTP NTLM authentication.
class HttpAuthHandlerNTLM : public HttpAuthHandler {
 public:
#if defined(NTLM_PORTABLE)
  // A function that generates n random bytes in the output buffer.
  typedef void (*GenerateRandomProc)(uint8* output, size_t n);

  // A function that returns the local host name. Returns an empty string if
  // the local host name is not available.
  typedef std::string (*HostNameProc)();

  // For unit tests to override and restore the GenerateRandom and
  // GetHostName functions.
  class ScopedProcSetter {
   public:
    ScopedProcSetter(GenerateRandomProc random_proc,
                     HostNameProc host_name_proc) {
      old_random_proc_ = SetGenerateRandomProc(random_proc);
      old_host_name_proc_ = SetHostNameProc(host_name_proc);
    }

    ~ScopedProcSetter() {
      SetGenerateRandomProc(old_random_proc_);
      SetHostNameProc(old_host_name_proc_);
    }

   private:
    GenerateRandomProc old_random_proc_;
    HostNameProc old_host_name_proc_;
  };
#endif

  HttpAuthHandlerNTLM();

  virtual bool NeedsIdentity();

  virtual bool IsFinalRound();

  virtual std::string GenerateCredentials(const std::wstring& username,
                                          const std::wstring& password,
                                          const HttpRequestInfo* request,
                                          const ProxyInfo* proxy);

 protected:
  virtual bool Init(std::string::const_iterator challenge_begin,
                    std::string::const_iterator challenge_end) {
    return ParseChallenge(challenge_begin, challenge_end);
  }

  // This function acquires a credentials handle in the SSPI implementation.
  // It does nothing in the portable implementation.
  int InitializeBeforeFirstChallenge();

 private:
  ~HttpAuthHandlerNTLM();

#if defined(NTLM_PORTABLE)
  // For unit tests to override the GenerateRandom and GetHostName functions.
  // Returns the old function.
  static GenerateRandomProc SetGenerateRandomProc(GenerateRandomProc proc);
  static HostNameProc SetHostNameProc(HostNameProc proc);
#endif

  // Parse the challenge, saving the results into this instance.
  // Returns true on success.
  bool ParseChallenge(std::string::const_iterator challenge_begin,
                      std::string::const_iterator challenge_end);

  // Given an input token received from the server, generate the next output
  // token to be sent to the server.
  int GetNextToken(const void* in_token,
                   uint32 in_token_len,
                   void** out_token,
                   uint32* out_token_len);

#if defined(NTLM_SSPI)
  HttpAuthSSPI auth_sspi_;
#endif

#if defined(NTLM_PORTABLE)
  static GenerateRandomProc generate_random_proc_;
  static HostNameProc get_host_name_proc_;
#endif

  string16 domain_;
  string16 username_;
  string16 password_;

  // The base64-encoded string following "NTLM" in the "WWW-Authenticate" or
  // "Proxy-Authenticate" response header.
  std::string auth_data_;
};

}  // namespace net

#endif  // NET_HTTP_HTTP_AUTH_HANDLER_NTLM_H_
