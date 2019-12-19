file(GLOB CANBOATS ${CMAKE_CURRENT_SOURCE_DIR}/canboat/*.cpp)

add_library(n2k_ngt1 SHARED f_n2k_ngt1.cpp ${CHANS} ${CANBOATS})

target_include_directories(n2k_ngt1 PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_include_directories(n2k_ngt1 PUBLIC ${PROJECT_SOURCE_DIR}/channels)
target_include_directories(n2k_ngt1 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/canboat)

install(TARGETS n2k_ngt1 DESTINATION lib)
