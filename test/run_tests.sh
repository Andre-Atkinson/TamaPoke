#!/usr/bin/env bash
# Ejecuta toda la bateria de tests (logica del firmware + herramientas).
#   ./test/run_tests.sh          normal
#   ./test/run_tests.sh --asan   ademas con AddressSanitizer + UBSan
set -uo pipefail
cd "$(dirname "$0")"

fail=0

# las dos imagenes que escribe test_sim.cpp y lee test_tools.py: fuera las viejas, para
# que una suite C++ que no corre no se tape con datos de una ejecucion anterior
rm -f build/sim_png_selftest.png build/sim_png_selftest_round.png

echo "== logica del firmware (pet.cpp / i18n.cpp / dex.h) =="
make --no-print-directory run || fail=1

if [[ "${1:-}" == "--asan" ]]; then
  echo
  echo "== la misma bateria con AddressSanitizer + UBSan =="
  make --no-print-directory asan || fail=1
fi

echo
echo "== herramientas y ficheros generados (tools/) =="
TAMAPOKE_RUN_TESTS=1 python3 test_tools.py || fail=1

echo
if [[ $fail -eq 0 ]]; then
  echo "TODO OK"
else
  echo "HAY FALLOS"
fi
exit $fail
