# Verifica: oasis entity create "mal id!" -> exit 2 + INVALID_ARG (no muta DemoGame).
if(NOT DEFINED OASIS OR NOT DEFINED DEMO)
  message(FATAL_ERROR "Faltan OASIS/DEMO")
endif()
execute_process(
  COMMAND ${OASIS} entity create "mal id!" --project ${DEMO}
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
  RESULT_VARIABLE rc
)
set(combined "${out}\n${err}")
if(NOT rc EQUAL 2)
  message(FATAL_ERROR "exit=${rc}, se esperaba 2\n${combined}")
endif()
if(NOT combined MATCHES "INVALID_ARG")
  message(FATAL_ERROR "salida sin INVALID_ARG\n${combined}")
endif()
if(NOT combined MATCHES "\"schema_version\":1")
  message(FATAL_ERROR "salida sin schema_version\n${combined}")
endif()
