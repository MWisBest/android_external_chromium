// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/debug/stack_trace.h"

#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>
#include <vector>

#if defined(__GLIBCXX__)
#include <cxxabi.h>
#endif

#if defined(OS_MACOSX)
#include <AvailabilityMacros.h>
#endif

#include <iostream>

#include "base/basictypes.h"
#include "base/eintr_wrapper.h"
#include "base/logging.h"
#include "base/safe_strerror_posix.h"
#include "base/scoped_ptr.h"
#include "base/string_piece.h"
#include "base/stringprintf.h"

#if defined(USE_SYMBOLIZE)
#include "base/third_party/symbolize/symbolize.h"
#endif

namespace base {
namespace debug {

namespace {

// The prefix used for mangled symbols, per the Itanium C++ ABI:
// http://www.codesourcery.com/cxx-abi/abi.html#mangling
const char kMangledSymbolPrefix[] = "_Z";

// Characters that can be used for symbols, generated by Ruby:
// (('a'..'z').to_a+('A'..'Z').to_a+('0'..'9').to_a + ['_']).join
const char kSymbolCharacters[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";

#if !defined(USE_SYMBOLIZE)
// Demangles C++ symbols in the given text. Example:
//
// "out/Debug/base_unittests(_ZN10StackTraceC1Ev+0x20) [0x817778c]"
// =>
// "out/Debug/base_unittests(StackTrace::StackTrace()+0x20) [0x817778c]"
void DemangleSymbols(std::string* text) {
#if defined(__GLIBCXX__)

  std::string::size_type search_from = 0;
  while (search_from < text->size()) {
    // Look for the start of a mangled symbol, from search_from.
    std::string::size_type mangled_start =
        text->find(kMangledSymbolPrefix, search_from);
    if (mangled_start == std::string::npos) {
      break;  // Mangled symbol not found.
    }

    // Look for the end of the mangled symbol.
    std::string::size_type mangled_end =
        text->find_first_not_of(kSymbolCharacters, mangled_start);
    if (mangled_end == std::string::npos) {
      mangled_end = text->size();
    }
    std::string mangled_symbol =
        text->substr(mangled_start, mangled_end - mangled_start);

    // Try to demangle the mangled symbol candidate.
    int status = 0;
    scoped_ptr_malloc<char> demangled_symbol(
        abi::__cxa_demangle(mangled_symbol.c_str(), NULL, 0, &status));
    if (status == 0) {  // Demangling is successful.
      // Remove the mangled symbol.
      text->erase(mangled_start, mangled_end - mangled_start);
      // Insert the demangled symbol.
      text->insert(mangled_start, demangled_symbol.get());
      // Next time, we'll start right after the demangled symbol we inserted.
      search_from = mangled_start + strlen(demangled_symbol.get());
    } else {
      // Failed to demangle.  Retry after the "_Z" we just found.
      search_from = mangled_start + 2;
    }
  }

#endif  // defined(__GLIBCXX__)
}
#endif  // !defined(USE_SYMBOLIZE)

// Gets the backtrace as a vector of strings. If possible, resolve symbol
// names and attach these. Otherwise just use raw addresses. Returns true
// if any symbol name is resolved.  Returns false on error and *may* fill
// in |error_message| if an error message is available.
bool GetBacktraceStrings(void **trace, int size,
                         std::vector<std::string>* trace_strings,
                         std::string* error_message) {
#ifdef ANDROID
  return false;
#endif
  bool symbolized = false;

#if defined(USE_SYMBOLIZE)
  for (int i = 0; i < size; ++i) {
    char symbol[1024];
    // Subtract by one as return address of function may be in the next
    // function when a function is annotated as noreturn.
    if (google::Symbolize(static_cast<char *>(trace[i]) - 1,
                          symbol, sizeof(symbol))) {
      // Don't call DemangleSymbols() here as the symbol is demangled by
      // google::Symbolize().
      trace_strings->push_back(
          base::StringPrintf("%s [%p]", symbol, trace[i]));
      symbolized = true;
    } else {
      trace_strings->push_back(base::StringPrintf("%p", trace[i]));
    }
  }
#else
  scoped_ptr_malloc<char*> trace_symbols(backtrace_symbols(trace, size));
  if (trace_symbols.get()) {
    for (int i = 0; i < size; ++i) {
      std::string trace_symbol = trace_symbols.get()[i];
      DemangleSymbols(&trace_symbol);
      trace_strings->push_back(trace_symbol);
    }
    symbolized = true;
  } else {
    if (error_message)
      *error_message = safe_strerror(errno);
    for (int i = 0; i < size; ++i) {
      trace_strings->push_back(base::StringPrintf("%p", trace[i]));
    }
  }
#endif  // defined(USE_SYMBOLIZE)

  return symbolized;
}

}  // namespace

StackTrace::StackTrace() {
#if (defined(OS_MACOSX) && MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_5) || defined(ANDROID)
#if defined(ANDROID)
  return;
#else
  if (backtrace == NULL) {
    count_ = 0;
    return;
  }
#endif // ANDROID
#endif
  // Though the backtrace API man page does not list any possible negative
  // return values, we take no chance.
  count_ = std::max(backtrace(trace_, arraysize(trace_)), 0);
}

void StackTrace::PrintBacktrace() {
#if (defined(OS_MACOSX) && MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_5) || defined(ANDROID)
#if defined(ANDROID)
  return;
#else
  if (backtrace_symbols_fd == NULL)
    return;
#endif // ANDROID
#endif
  fflush(stderr);
  std::vector<std::string> trace_strings;
  GetBacktraceStrings(trace_, count_, &trace_strings, NULL);
  for (size_t i = 0; i < trace_strings.size(); ++i) {
    std::cerr << "\t" << trace_strings[i] << "\n";
  }
}

void StackTrace::OutputToStream(std::ostream* os) {
#if (defined(OS_MACOSX) && MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_5) || defined(ANDROID)
#if defined(ANDROID)
  return;
#else
  if (backtrace_symbols == NULL)
    return;
#endif // ANDROID
#endif
  std::vector<std::string> trace_strings;
  std::string error_message;
  if (GetBacktraceStrings(trace_, count_, &trace_strings, &error_message)) {
    (*os) << "Backtrace:\n";
  } else {
    if (!error_message.empty())
      error_message = " (" + error_message + ")";
    (*os) << "Unable to get symbols for backtrace" << error_message << ". "
          << "Dumping raw addresses in trace:\n";
  }

  for (size_t i = 0; i < trace_strings.size(); ++i) {
    (*os) << "\t" << trace_strings[i] << "\n";
  }
}

}  // namespace debug
}  // namespace base
