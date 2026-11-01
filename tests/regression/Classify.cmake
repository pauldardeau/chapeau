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

function(chapeau_classify rc output outVar)
   if("${rc}" MATCHES "terminated due to timeout")
      set(outcome "timeout")
   elseif("${output}" MATCHES "(Address|Leak|Thread|Memory|UndefinedBehavior)Sanitizer:|runtime error:|SUMMARY: [A-Za-z]+Sanitizer")
      set(outcome "sanitizer")
   elseif("${rc}" MATCHES "^-?[0-9]+$")
      if("${rc}" STREQUAL "0")
         set(outcome "success")
      else()
         set(outcome "exit:${rc}")
      endif()
   else()
      set(outcome "crash:${rc}")
   endif()
   set(${outVar} "${outcome}" PARENT_SCOPE)
endfunction()
