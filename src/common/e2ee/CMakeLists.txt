target_sources(
  firmware
  PRIVATE aes_block_decryptor.cpp
          decryptor.cpp
          e2ee.cpp
          hmac.cpp
          key.cpp
          sha256_multiuse.cpp
          utils.cpp
  )
