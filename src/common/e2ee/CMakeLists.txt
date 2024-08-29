target_sources(
  firmware
  PRIVATE decryptor.cpp
          e2ee.cpp
          hmac.cpp
          key.cpp
          movable_aes_context.cpp
          printer_private_key.cpp
          sha256_multiuse.cpp
          utils.cpp
  )
