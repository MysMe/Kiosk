FetchContent_Declare(
  osmanip
  GIT_REPOSITORY "https://github.com/JustWhit3/osmanip"
  GIT_TAG b0ba4e8e84d798839300227d3544c2f8d93a9449
)

message(STATUS "Fetching Osmanip...")

SET(OSMANIP_TESTS OFF)

FetchContent_MakeAvailable(osmanip)
target_include_directories(${PROJECT_NAME} PUBLIC "${osmanip-build_SOURCE_DIR}/include")