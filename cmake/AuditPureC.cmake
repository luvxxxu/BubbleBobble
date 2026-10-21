# cmake -D로 전달받은 저장소 루트를 기준으로 순수 C 소스 정책을 검사한다.
# 루트가 없으면 다른 디렉터리를 실수로 검사하지 않도록 즉시 실패한다.
if(NOT DEFINED BB_SOURCE_DIR)
    message(FATAL_ERROR "BB_SOURCE_DIR is required")
endif()
# 프로젝트의 소스와 테스트를 감사하며 외부 의존성은 대상에서 제외한다.
file(GLOB_RECURSE sources LIST_DIRECTORIES FALSE
    "${BB_SOURCE_DIR}/src/*" "${BB_SOURCE_DIR}/tests/*")
# 파일명을 소문자로 정규화해 대소문자 차이로 우회하는 일반 C++ 확장자를 잡는다.
# 대문자 .C는 관례상 C++이므로 원래 파일명에서도 따로 검사한다.
foreach(source IN LISTS sources)
    string(TOLOWER "${source}" lower_source)
    if(lower_source MATCHES "\\.(cpp|cxx|cc|c\\+\\+|hpp|hxx|hh|mm)$" OR source MATCHES "\\.C$")
        message(FATAL_ERROR "C++ source found in active project: ${source}")
    endif()
endforeach()
# 모든 파일을 통과한 뒤에만 성공 메시지를 출력한다.
message(STATUS "Active project source tree contains no C++ or Objective-C++ files")
