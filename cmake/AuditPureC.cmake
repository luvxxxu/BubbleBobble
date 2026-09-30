if(NOT DEFINED BB_SOURCE_DIR)
    message(FATAL_ERROR "BB_SOURCE_DIR is required")
endif()
# 프로젝트의 소스와 테스트를 감사하며 외부 의존성은 대상에서 제외한다.
file(GLOB_RECURSE sources LIST_DIRECTORIES FALSE
    "${BB_SOURCE_DIR}/src/*" "${BB_SOURCE_DIR}/tests/*")
foreach(source IN LISTS sources)
    string(TOLOWER "${source}" lower_source)
    if(lower_source MATCHES "\\.(cpp|cxx|cc|c\\+\\+|hpp|hxx|hh|mm)$" OR source MATCHES "\\.C$")
        message(FATAL_ERROR "C++ source found in active project: ${source}")
    endif()
endforeach()
message(STATUS "Active project source tree contains no C++ or Objective-C++ files")
