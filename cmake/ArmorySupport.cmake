# random utility functions/macros

# make sure architecture is set
if(NOT CMAKE_SYSTEM_PROCESSOR)
    if(NOT CMAKE_TOOLCHAIN_FILE AND CMAKE_HOST_SYSTEM_PROCESSOR)
        set(CMAKE_SYSTEM_PROCESSOR ${CMAKE_HOST_SYSTEM_PROCESSOR})
    elseif(CMAKE_TOOLCHAIN_FILE MATCHES mxe)
        if(CMAKE_TOOLCHAIN_FILE MATCHES "i[3-9]86")
            set(CMAKE_SYSTEM_PROCESSOR i686)
        else()
            set(CMAKE_SYSTEM_PROCESSOR x86_64)
        endif()
    endif()
endif()

macro(string_option opt doc_string initial_value)
    if(NOT DEFINED ${${opt}})
        set(${opt} ${initial_value})
    endif()

    set(${opt} ${${opt}} CACHE STRING ${doc_string})
endmacro()

# This is from:
# https://stackoverflow.com/a/31010221
macro(use_cxx11)
    if (CMAKE_VERSION VERSION_LESS 3.1)
	if(CMAKE_CXX_COMPILER_ID STREQUAL GNU OR CMAKE_CXX_COMPILER_ID STREQUAL Clang)
	    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11")
	endif()
    else()
        # Fix behavior of CMAKE_CXX_STANDARD when targeting macOS.
        if(POLICY CMP0025)
            cmake_policy(SET CMP0025 NEW)
        endif()

	set(CMAKE_CXX_STANDARD 11)
    endif()
endmacro(use_cxx11)

unset(X86_CPU_FEATURES_COMPILER_FLAGS)

# check for x86 cpu features and sets X86_CPU_FEATURES_COMPILER_FLAGS for gcc/clang
function(check_x86_cpu_features)
    if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "i.86|x86_64")
        return()
    endif()

    if(DEFINED X86_CPU_FEATURES_COMPILER_FLAGS) # already computed, do nothing
        return()
    endif()

    include(CheckCXXSourceCompiles)

    check_cxx_source_compiles("
        #include <stdlib.h>

        int main(int argc, char** argv)
        {
            __builtin_cpu_init();
        }
    " HAVE_CPU_INIT)

    if(HAVE_CPU_INIT)
        include(CheckCXXSourceRuns)

        foreach(cpu_feature mmx popcnt sse sse2 sse3 sse4.1 sse4.2 sse4a avx avx2 avx512f fma fma4 bmi bmi2)
            string(REPLACE . _ cpu_feature_var ${cpu_feature})

            check_cxx_source_runs("
                #include <stdlib.h>

                int main(int argc, char** argv)
                {
                    __builtin_cpu_init();

                    if (__builtin_cpu_supports(\"${cpu_feature}\"))
                        return 0;

                    return 1;
                }
            " HAVE_${cpu_feature_var})

            if(HAVE_${cpu_feature_var})
                list(APPEND X86_CPU_FEATURES_COMPILER_FLAGS -m${cpu_feature})
            endif()
        endforeach()

        set(X86_CPU_FEATURES_COMPILER_FLAGS ${X86_CPU_FEATURES_COMPILER_FLAGS} CACHE STRING "gcc/clang cpu feature flags for the build host" FORCE)
    endif()
endfunction()
