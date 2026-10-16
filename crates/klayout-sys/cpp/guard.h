// Every C ABI entry point runs its body through kl::guarded: a C++ exception
// must never unwind into Rust. Failures become -1, with the message kept for
// kl_last_error().

#pragma once

#include "tlException.h"

#include <exception>
#include <string>

namespace kl
{
std::string &error_slot ();

template <class F>
int guarded (F &&f)
{
  try {
    f ();
    return 0;
  } catch (tl::Exception &ex) {
    error_slot () = ex.msg ();
  } catch (std::exception &ex) {
    error_slot () = ex.what ();
  } catch (...) {
    error_slot () = "unknown C++ exception";
  }
  return -1;
}
}
