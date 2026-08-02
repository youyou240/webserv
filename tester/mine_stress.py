#!/usr/bin/env python3
# mine_stress.py
# -----------------------------------------------------------------------------
# Test de STRESS / DISPONIBLITE. On martele le serveur avec beaucoup de
# requetes concurrentes pendant quelques secondes, puis on verifie :
#   - le taux de succes (combien de GET / ont recu un 200) ;
#   - que le serveur est TOUJOURS VIVANT a la fin (invariant du sujet :
#     "rester disponible en permanence", "resistance au stress").
#
# On teste aussi une "tempete de connexions" : beaucoup de connexions ouvertes
# puis fermees aussitot, pour eprouver la gestion des descripteurs de fichiers
# (le serveur ne doit pas fuiter ni saturer ses FD).
#
# Usage (serveur deja lance) :
#   ./mine_stress.py http://localhost:8080 [--workers 50] [--duration 10]
#
# IMPORTANT : ce testeur ferme scrupuleusement CHAQUE socket qu'il ouvre, pour
# ne pas epuiser SES PROPRES descripteurs et fausser le verdict.
# -----------------------------------------------------------------------------

import os
import socket
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mine_http import parse_base_url, do, Reporter, GREEN, RED, RESET


def parse_args(argv):
    # Parse "[base_url] [--workers N] [--duration S]" en ignorant correctement
    # les VALEURS des flags (sinon "100" de "--workers 100" serait pris pour
    # l'URL de base). Rend (base, workers, duration).
    base = "http://localhost:8080"
    workers = 50
    duration = 10.0
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--workers" and i + 1 < len(argv):
            workers = int(argv[i + 1])
            i += 2
            continue
        if a == "--duration" and i + 1 < len(argv):
            duration = float(argv[i + 1])
            i += 2
            continue
        if not a.startswith("--"):
            base = a
        i += 1
    return base, workers, duration


def one_request(host, port):
    # Une requete complete GET / en raw socket. Rend True si on a lu un 200.
    # On ferme le socket dans tous les cas (finally) : zero fuite de FD cote test.
    s = None
    try:
        s = socket.create_connection((host, port), timeout=5.0)
        s.settimeout(5.0)
        s.sendall(b"GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
        data = b""
        while True:
            chunk = s.recv(65536)
            if not chunk:
                break
            data += chunk
        return data.startswith(b"HTTP/1.1 200")
    except Exception:
        return False
    finally:
        if s is not None:
            try:
                s.close()
            except Exception:
                pass


def worker(host, port, stop_at, counters, lock):
    # Boucle jusqu'a l'heure limite : envoie des requetes en continu.
    ok = 0
    ko = 0
    while time.time() < stop_at:
        if one_request(host, port):
            ok += 1
        else:
            ko += 1
    with lock:
        counters[0] += ok
        counters[1] += ko


def main():
    base, workers, duration = parse_args(sys.argv[1:])
    host, port = parse_base_url(base)
    r = Reporter("STRESS " + base)

    print("  %d workers concurrents pendant %.0fs ..." % (workers, duration))

    # --- Phase 1 : charge soutenue -------------------------------------------
    counters = [0, 0]  # [succes, echecs]
    lock = threading.Lock()
    stop_at = time.time() + duration
    threads = [threading.Thread(target=worker, args=(host, port, stop_at, counters, lock))
               for _ in range(workers)]
    t0 = time.time()
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    elapsed = time.time() - t0

    ok, ko = counters
    total = ok + ko
    rate = (100.0 * ok / total) if total else 0.0
    rps = (total / elapsed) if elapsed else 0.0
    print("  -> %d requetes, %d OK, %d KO, %.1f%% succes, %.0f req/s"
          % (total, ok, ko, rate, rps))

    # Seuil de succes ADAPTATIF. En dessous de HIGH_CONCURRENCY workers, le
    # serveur doit tout encaisser (>= 99%). Au-dela, le goulot devient la rafale
    # de connexions bornee par l'OS (backlog d'accept deja a SOMAXCONN cote
    # serveur, thundering herd) : ce n'est plus le serveur qu'on mesure. On juge
    # alors sur "il repond encore a la majorite" et on s'appuie surtout sur les
    # checks "vivant" plus bas.
    HIGH_CONCURRENCY = 500
    if workers <= HIGH_CONCURRENCY:
        r.check("charge soutenue : >= 99%% de succes", rate >= 99.0,
                "%.2f%%" % rate)
    else:
        print("  (au-dela de %d workers : rafale de connexions bornee par l'OS,"
              " seuil relache -- on juge surtout la survie du serveur)"
              % HIGH_CONCURRENCY)
        r.check("charge extreme : serveur repond a la majorite (>= 50%%)",
                rate >= 50.0, "%.2f%%" % rate)

    # --- Phase 2 : tempete de connexions (eprouve les FD) --------------------
    conn_errors = 0
    for _ in range(1000):
        s = None
        try:
            s = socket.create_connection((host, port), timeout=5.0)
        except Exception:
            conn_errors += 1
        finally:
            if s is not None:
                s.close()
    r.check("1000 connexions ouvertes/fermees sans erreur", conn_errors == 0,
            "erreurs=%d" % conn_errors)

    # --- Phase 3 : le serveur est-il toujours la ? ---------------------------
    still_up = False
    for _ in range(5):
        try:
            if do(host, port, "GET", "/", timeout=5.0).code == 200:
                still_up = True
                break
        except Exception:
            pass
        time.sleep(0.5)
    r.check("serveur toujours VIVANT apres le stress", still_up)

    code = r.summary()
    if code == 0:
        print(GREEN + "Le serveur a tenu la charge." + RESET)
    else:
        print(RED + "Le serveur a flanche sous la charge -- a investiguer." + RESET)
    return code


if __name__ == "__main__":
    sys.exit(main())
