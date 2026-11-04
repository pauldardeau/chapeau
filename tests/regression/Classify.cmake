# Classifies how a child process ended. Shared by RunCase.cmake (one CTest
# case) and KnownDefectsReport.cmake (the known-defect inventory).
#
#   chapeau_classify(<result-variable-text> <captured output> <out-variable>)
#
# sets <out-variable> to exactly one of
#
#   timeout        execute_process had to kill the child (a hang)
#   sanitizer      the output holds an AddressSanitizer / LeakSanitizer /
#                  ThreadSanitizer report or a UBSan "runtime error:", whatever
#                  the exit status was (UBSan keeps going and exits 0; ASan
#                  exits 1, which would otherwise look like an ordinary failure)
#   crash:<what>   the child was ended by a signal or otherwise abnormally
#                  (execute_process then reports a message, not a number)
#   success        exit status 0
#   exit:<n>       exit status n, n != 0
#
# The order matters: a hung process says nothing reliable about its exit; a
# sanitizer report outranks the exit status that follows it; a crash is never
# mistaken for an exit status because its result is not a number.
#
# Windows only: execute_process reports most unhandled exceptions as text ("Access
# violation", "Exit code 0xc0000409", ...), but some as a plain number, for example the
# unhandled breakpoint 0x80000003 (-2147483645) and the 0xE... exceptions. A program
# can only choose an exit status from 0 to 255 here, so on Windows any other number is
# a crash: "crash:status <n>". Numbers from 0 to 255 keep their meaning ("exit:3" is an
# ordinary exit 3 and is never taken for an abort). POSIX results are unchanged.

function(chapeau_classify rc output outVar)
   if("${rc}" MATCHES "terminated due to timeout")
      set(outcome "timeout")
   elseif("${output}" MATCHES "(Address|Leak|Thread|Memory|UndefinedBehavior)Sanitizer:|runtime error:|SUMMARY: [A-Za-z]+Sanitizer")
      set(outcome "sanitizer")
   elseif("${rc}" MATCHES "^-?[0-9]+$")
      if("${rc}" STREQUAL "0")
         set(outcome "success")
      elseif(CMAKE_HOST_WIN32 AND ("${rc}" GREATER 255 OR "${rc}" LESS 0))
         set(outcome "crash:status ${rc}")
      else()
         set(outcome "exit:${rc}")
      endif()
   else()
      set(outcome "crash:${rc}")
   endif()
   set(${outVar} "${outcome}" PARENT_SCOPE)
endfunction()

# Windows: is this outcome a fast fail, status 0xC0000409? That is how abort() ends once
# process_setup.h has made it fail fast, in Debug and in Release. Sets <out-variable> to
# TRUE or FALSE.
#
# CMake 3.30.3 and 3.31.6 report it as the text "Exit code 0xc0000409" (followed by a
# newline). The same status as a signed 32-bit number is 0xC0000409 - 2^32 = -1073740791
# (SelfCheck.cmake recomputes this); no CMake version tried reports it that way, and
# chapeau_classify() would call it "crash:status -1073740791", which is accepted here too
# for a version that might. Nothing else is: not another crash, not an exit status.
function(chapeau_is_failfast outcome outVar)
   string(TOLOWER "${outcome}" lowered)
   if(lowered MATCHES "^crash:exit code 0xc0000409[ \t\r\n]*$" OR lowered STREQUAL "crash:status -1073740791")
      set(${outVar} TRUE PARENT_SCOPE)
   else()
      set(${outVar} FALSE PARENT_SCOPE)
   endif()
endfunction()
