set(CHACHA20POLY1305_SOURCES
    poly1305.c
    chacha.c
    chachapoly_aead.c
)

add_library(chacha20poly1305
    ${CHACHA20POLY1305_SOURCES}
)

target_include_directories(chacha20poly1305
    PUBLIC .
)
