#!/usr/bin/env bash
# mine_leakcheck.sh
# -----------------------------------------------------------------------------
# Aide a chasser les fuites MEMOIRE et les fuites de DESCRIPTEURS de fichiers
# (FD), avec valgrind, PENDANT que les suites mine_* generent du trafic.
#
# Pourquoi un script interactif plutot qu'automatique ?
#   webserv ouvre un menu interactif sur SIGINT (SHOW/START/STOP/EXIT). C'est
#   donc TOI qui lances webserv sous valgrind dans un terminal, et ce script se
#   contente de :
#     1) te rappeler la commande valgrind exacte a copier ;
#     2) generer la charge (functional + robustness + stress) contre le serveur ;
#     3) te dire quoi regarder dans le rapport valgrind apres avoir quitte.
#
# Usage (dans le container) :
#   Terminal 1 :  cd /home/webserv puis la commande valgrind affichee ci-dessous
#   Terminal 2 :  ./tester/mine_leakcheck.sh [http://localhost:8080]
# -----------------------------------------------------------------------------

set -u

BASE="${1:-http://localhost:8080}"
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
LOG="/tmp/mine_valgrind.log"

echo "=============================================================="
echo " ETAPE 1 -- Dans un AUTRE terminal, lance webserv sous valgrind :"
echo
echo "   cd $ROOT"
echo "   valgrind --leak-check=full --show-leak-kinds=all \\"
echo "            --track-fds=yes --trace-children=yes \\"
echo "            --log-file=$LOG \\"
echo "            ./webserv configs/default.conf"
echo
echo " (--track-fds=yes = detecte les FD non fermes ;"
echo "  --trace-children=yes = suit aussi les fork() du CGI.)"
echo "=============================================================="
read -r -p "Quand webserv tourne sous valgrind, appuie sur Entree..." _

echo
echo " ETAPE 2 -- Generation de la charge contre $BASE"
echo

python3 "$HERE/mine_functional.py" "$BASE"
echo
python3 "$HERE/mine_robustness.py" "$BASE" --no-slow
echo
python3 "$HERE/mine_stress.py" "$BASE" --workers 50 --duration 10

echo
echo "=============================================================="
echo " ETAPE 3 -- Arrete proprement le serveur pour vider valgrind :"
echo "   Dans le terminal de webserv : Ctrl-C puis choisis EXIT dans le menu."
echo
echo " Puis inspecte le rapport :"
echo "   grep -E 'lost|reachable|ERROR SUMMARY|FILE DESCRIPTORS|Open file' $LOG"
echo
echo " Ce qu'on veut voir :"
echo "   * 'definitely lost: 0 bytes' et 'indirectly lost: 0 bytes'"
echo "   * 'ERROR SUMMARY: 0 errors'"
echo "   * cote FD : seulement les 3 std (0,1,2) encore ouverts a la fin"
echo "=============================================================="
