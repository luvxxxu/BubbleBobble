if(NOT DEFINED BB_SOURCE_DIR)
    message(FATAL_ERROR "BB_SOURCE_DIR is required")
endif()
# 활성 소스와 테스트만 감사한다. 별도 VS2010 호환 계층과 외부 코드는 대상이 아니다.
file(GLOB_RECURSE sources LIST_DIRECTORIES FALSE
    "${BB_SOURCE_DIR}/src/*" "${BB_SOURCE_DIR}/tests/*")
foreach(source IN LISTS sources)
    string(TOLOWER "${source}" lower_source)
    if(lower_source MATCHES "\\.(cpp|cxx|cc|c\\+\\+|hpp|hxx|hh|mm)$" OR source MATCHES "\\.C$")
        message(FATAL_ERROR "C++ source found in active project: ${source}")
    endif()
endforeach()
message(STATUS "Active project source tree contains no C++ or Objective-C++ files")
