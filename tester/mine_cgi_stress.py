#!/usr/bin/env python3
# mine_cgi_stress.py
# -----------------------------------------------------------------------------
# Stress + cas tordus du CGI -- la zone la plus risquee de webserv (fork / pipe /
# execve / waitpid / timeout). Le CGI n'etait teste qu'en unitaire dans
# mine_functional ; ici on le pousse en CONCURRENCE et sur ses cas limites.
#
# A lancer IDEALEMENT sous valgrind (voir mine_leakcheck.sh) : les fuites de FD
# de pipe et les use-after-free du chemin CGI ne se voient qu'a l'instrumentation.
#
# Ce qu'on eprouve :
#   1) beaucoup de CGI GET simultanes (.py + .cgi)         -> fork storm, FD
#   2) CGI POST concurrents avec token unique              -> pas de CROSS-TALK
#      entre pipes de deux CGI qui tournent en meme temps
#   3) CGI qui boucle                                       -> timeout 504 + kill
#   4) CGI sans sortie                                      -> 502
#   5) CGI a grosse sortie (5 Mo)                           -> 200, lu jusqu'a EOF
#   6) gros POST vers un CGI qui lit stdin                  -> pas de DEADLOCK
#
# Usage (serveur lance avec configs/default.conf) :
#   ./mine_cgi_stress.py http://localhost:8080 [--workers 30] [--duration 8]
# -----------------------------------------------------------------------------

import os
import random
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mine_http import parse_base_url, do, Reporter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CGIBIN = os.path.join(ROOT, "www", "cgi-bin")

# Scripts CGI Python ecrits temporairement dans www/cgi-bin puis supprimes.
# La config mappe ".py /usr/bin/python3", donc pas besoin de +x.
SLOW_SRC = "import time\ntime.sleep(30)\nprint('Content-Type: text/plain')\nprint()\nprint('late')\n"
EMPTY_SRC = "import sys\nsys.exit(0)\n"
BIG_SRC = ("import sys\n"
           "sys.stdout.write('Content-Type: text/plain\\r\\n\\r\\n')\n"
           "sys.stdout.write('A' * (5 * 1024 * 1024))\n")


def get_arg(flag, default):
    if flag in sys.argv:
        i = sys.argv.index(flag)
        if i + 1 < len(sys.argv):
            return sys.argv[i + 1]
    return default


def write_cgi(name, src):
    path = os.path.join(CGIBIN, name)
    with open(path, "w") as f:
        f.write(src)
    return "/cgi-bin/" + name


def remove_cgi(name):
    try:
        os.remove(os.path.join(CGIBIN, name))
    except OSError:
        pass


def alive(host, port):
    try:
        return do(host, port, "GET", "/", timeout=5.0).code == 200
    except Exception:
        return False


def get_worker(host, port, stop_at, counters, lock):
    # Alterne .py et .cgi en GET jusqu'a l'heure limite.
    ok = 0
    ko = 0
    paths = ["/cgi-bin/hello.py?x=1", "/cgi-bin/hello.cgi?x=1"]
    while time.time() < stop_at:
        p = paths[ok % 2]
        try:
            if do(host, port, "GET", p, timeout=10.0).code == 200:
                ok += 1
            else:
                ko += 1
        except Exception:
            ko += 1
    with lock:
        counters[0] += ok
        counters[1] += ko


def post_worker(host, port, stop_at, counters, lock):
    # POST un token UNIQUE et verifie qu'il revient : si deux CGI concurrents
    # melangeaient leurs pipes, le token renvoye serait celui d'un autre.
    ok = 0
    mism = 0
    err = 0
    while time.time() < stop_at:
        token = "tok-%d-%d" % (os.getpid(), random.getrandbits(48))
        try:
            resp = do(host, port, "POST", "/cgi-bin/hello.py", body=token, timeout=10.0)
            if resp.code == 200 and token in resp.text:
                ok += 1
            elif resp.code == 200:
                mism += 1  # 200 mais mauvais token = cross-talk de pipes !
            else:
                err += 1
        except Exception:
            err += 1
    with lock:
        counters[0] += ok
        counters[1] += mism
        counters[2] += err


def run_pool(target, host, port, workers, duration):
    lock = threading.Lock()
    counters = [0, 0, 0]
    stop_at = time.time() + duration
    threads = [threading.Thread(target=target, args=(host, port, stop_at, counters, lock))
               for _ in range(workers)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    return counters


def main():
    base = next((a for a in sys.argv[1:] if not a.startswith("--")),
                "http://localhost:8080")
    host, port = parse_base_url(base)
    workers = int(get_arg("--workers", "30"))
    duration = float(get_arg("--duration", "8"))
    pid = os.getpid()
    r = Reporter("CGI STRESS " + base)

    if not os.path.isdir(CGIBIN):
        r.skip("acces a www/cgi-bin", "dossier introuvable (%s)" % CGIBIN)
        return r.summary()

    # --- 1) CGI GET concurrents ---------------------------------------------
    print("  %d workers x %.0fs -- CGI GET (.py + .cgi) ..." % (workers, duration))
    ok, ko = run_pool(get_worker, host, port, workers, duration)[:2]
    total = ok + ko
    rate = (100.0 * ok / total) if total else 0.0
    print("  -> %d CGI GET, %d OK, %d KO (%.1f%%)" % (total, ok, ko, rate))
    r.check("CGI GET concurrents : >= 99%% de succes", rate >= 99.0, "%.2f%%" % rate)
    r.check("serveur vivant apres les CGI GET", alive(host, port))

    # --- 2) CGI POST concurrents : integrite des pipes (pas de cross-talk) ---
    print("  %d workers x %.0fs -- CGI POST (token unique) ..." % (workers, duration))
    ok, mism, err = run_pool(post_worker, host, port, workers, duration)
    print("  -> %d OK, %d MISMATCH, %d err" % (ok, mism, err))
    r.check("CGI POST : AUCUN cross-talk de pipes (0 mismatch)", mism == 0,
            "mismatch=%d" % mism)
    r.check("CGI POST : quasi tous servis (err faible)", err <= max(2, ok // 100),
            "err=%d ok=%d" % (err, ok))
    r.check("serveur vivant apres les CGI POST", alive(host, port))

    # --- 3) CGI qui boucle -> timeout 504 + kill ----------------------------
    slow = "mine_slow_%d.py" % pid
    url = write_cgi(slow, SLOW_SRC)
    try:
        resp = do(host, port, "GET", url, timeout=20.0)
        r.check("[timeout] CGI qui boucle -> 504", resp.code == 504, "code=%d" % resp.code)
    except Exception as e:
        r.ko("[timeout] CGI qui boucle -> 504", "exception %s" % e)
    finally:
        remove_cgi(slow)
    r.check("serveur vivant apres le CGI timeout", alive(host, port))

    # --- 4) CGI sans sortie -> 502 ------------------------------------------
    empty = "mine_empty_%d.py" % pid
    url = write_cgi(empty, EMPTY_SRC)
    try:
        resp = do(host, port, "GET", url, timeout=10.0)
        r.check("[empty] CGI sans sortie -> 502", resp.code == 502, "code=%d" % resp.code)
    finally:
        remove_cgi(empty)

    # --- 5) CGI a grosse sortie (5 Mo) -> 200 + lu jusqu'a EOF ---------------
    big = "mine_big_%d.py" % pid
    url = write_cgi(big, BIG_SRC)
    try:
        resp = do(host, port, "GET", url, timeout=15.0)
        r.check("[big] CGI 5 Mo -> 200 + corps complet",
                resp.code == 200 and len(resp.body) == 5 * 1024 * 1024,
                "code=%d len=%d" % (resp.code, len(resp.body)))
    finally:
        remove_cgi(big)

    # --- 6) Gros POST vers un CGI qui lit stdin -> pas de deadlock -----------
    payload = "D" * (256 * 1024)  # 256 Ko sur stdin
    resp = do(host, port, "POST", "/cgi-bin/hello.py", body=payload, timeout=15.0)
    r.check("[deadlock] gros POST lu par le CGI -> 200 + corps relaye",
            resp.code == 200 and payload in resp.text, "code=%d" % resp.code)

    r.check("serveur toujours vivant a la fin du stress CGI", alive(host, port))

    return r.summary()


if __name__ == "__main__":
    sys.exit(main())
