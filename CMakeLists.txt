set(COMPONENT_ADD_INCLUDEDIRS .)

set(COMPONENT_SRCS "http2_protoclient.c wcstrutils.c wcprotocol.c wcframe.c")

set(COMPONENT_REQUIRES sh2lib)
set(COMPONENT_PRIV_REQUIRES lwip esp-tls json)

register_component()
