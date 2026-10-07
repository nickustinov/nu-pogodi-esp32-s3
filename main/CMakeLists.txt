file(GLOB assets ${CMAKE_CURRENT_LIST_DIR}/assets/*) # tools/assets.sh
idf_component_register(SRCS main.cpp board.cpp game.cpp digits.c INCLUDE_DIRS . EMBED_FILES ${assets})
