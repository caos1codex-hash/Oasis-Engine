# Verifica: oasis entity set-model Cube FantasmaInexistente -> exit 1 + NOT_FOUND.
# Regresión del fix P1: antes aceptaba asset_id fantasma y guardaba; ahora debe rechazar sin mutar.
if(NOT DEFINED OASIS OR NOT DEFINED DEMO)
  message(FATAL_ERROR "Faltan OASIS/DEMO")
endif()
execute_process(
  COMMAND ${OASIS} entity set-model Cube FantasmaInexistente --project ${DEMO}
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
  RESULT_VARIABLE rc
)
set(combined "${out}\n${err}")
if(NOT rc EQUAL 1)
  message(FATAL_ERROR "exit=${rc}, se esperaba 1\n${combined}")
endif()
if(NOT combined MATCHES "NOT_FOUND")
  message(FATAL_ERROR "salida sin NOT_FOUND\n${combined}")
endif()
if(NOT combined MATCHES "\"schema_version\":1")
  message(FATAL_ERROR "salida sin schema_version\n${combined}")
endif()
