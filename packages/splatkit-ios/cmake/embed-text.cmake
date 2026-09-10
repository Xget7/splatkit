# Script mode: writes OUTPUT with INPUT's text as a raw string literal named SYMBOL.
file(READ ${INPUT} text)
file(WRITE ${OUTPUT}
  "#pragma once\n// Generated from ${INPUT}; do not edit.\ninline constexpr const char ${SYMBOL}[] = R\"SPLATKIT(${text})SPLATKIT\";\n")
