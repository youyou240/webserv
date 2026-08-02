#!/usr/bin/env python3
# mine_fuzz.py
# -----------------------------------------------------------------------------
# Fuzzer de mutation du parseur HTTP. On part de requetes VALIDES ("seeds"), on
# les mutile aleatoirement (byte-flip, insertion, suppression, troncature, ligne
# dupliquee, nombres delirants...) et on bombarde le serveur. Le but n'est PAS
# de verifier un code de retour precis, mais de repondre a UNE question :
#
#   "Une requete a laquelle personne n'a pense peut-elle faire CRASHER ou HANG
#    le serveur ?"
#
# Critere de reussite : le serveur reste VIVANT du debut a la fin (il a le droit
# de repondre 400, de fermer, peu importe -- il n'a PAS le droit de mourir ni de
# se figer). A lancer sous valgrind pour aussi attraper les corruptions memoire.
#
# Reproductibilite : --seed fige le generateur aleatoire ; si le serveur meurt,
# la derniere charge envoyee est affichee en clair (repr) pour rejouer le bug.
#
# Usage (serveur lance) :
#   ./mine_fuzz.py http://localhost:8080 [--iterations 5000] [--seed 42]
# -----------------------------------------------------------------------------

import os
import random
import socket
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mine_http import parse_base_url, do, Reporter

# Requetes de depart, volontairement variees (chaque forme stresse un chemin du
# parseur : simple, avec corps, chunked, multipart, CGI).
SEEDS = [
    b"GET / HTTP/1.1\r\nHost: localhost\r\n\r\n",
    b"GET /uploads/ HTTP/1.1\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n",
    b"POST /uploads/x HTTP/1.1\r\nHost: localhost\r\nContent-Length: 5\r\n\r\nhello",
    b"POST /uploads/x HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n"
    b"3\r\nabc\r\n0\r\n\r\n",
    b"GET /cgi-bin/hello.py?a=b HTTP/1.1\r\nHost: localhost\r\n\r\n",
    b"DELETE /uploads/x HTTP/1.1\r\nHost: localhost\r\n\r\n",
]


def mutate(data, rng):
    # Applique 1 a 3 mutations aleatoires a une requete.
    b = bytearray(data)
    for _ in range(rng.randint(1, 3)):
        if not b:
            b = bytearray(b"A")
        kind = rng.randint(0, 8)
        if kind == 0:  # flip d'un octet
            i = rng.randrange(len(b))
            b[i] ^= 1 << rng.randint(0, 7)
        elif kind == 1:  # remplace un octet par du bruit
            b[rng.randrange(len(b))] = rng.randint(0, 255)
        elif kind == 2:  # insere des octets aleatoires
            i = rng.randrange(len(b) + 1)
            b[i:i] = bytes(rng.randint(0, 255) for _ in range(rng.randint(1, 16)))
        elif kind == 3:  # supprime une tranche
            if len(b) > 2:
                i = rng.randrange(len(b) - 1)
                del b[i:i + rng.randint(1, 8)]
        elif kind == 4:  # troncature brutale
            b = b[:rng.randrange(len(b) + 1)]
        elif kind == 5:  # injecte des CRLF (casser la structure)
            i = rng.randrange(len(b) + 1)
            b[i:i] = b"\r\n" * rng.randint(1, 4)
        elif kind == 6:  # nombre delirant (overflow Content-Length / chunk)
            i = rng.randrange(len(b) + 1)
            b[i:i] = str(rng.getrandbits(80)).encode()
        elif kind == 7:  # duplique une ligne (headers en double)
            lines = bytes(b).split(b"\r\n")
            if len(lines) > 1:
                j = rng.randrange(len(lines))
                lines.insert(j, lines[j])
                b = bytearray(b"\r\n".join(lines))
        elif kind == 8:  # methode/version bidon
            b[:0] = bytes(rng.randint(65, 90) for _ in range(rng.randint(1, 6))) + b" "
    return bytes(b)


def send_once(host, port, payload, timeout=2.0):
    # Envoie une charge, rend "resp" (on a lu des octets), "close" (ferme sans
    # rien), "timeout", ou "neterr". Ne leve jamais : les erreurs reseau sont
    # des issues NORMALES d'un fuzz -- seul un serveur MORT est un probleme.
    s = None
    try:
        s = socket.create_connection((host, port), timeout=timeout)
        s.settimeout(timeout)
        s.sendall(payload)
        data = s.recv(4096)
        return "resp" if data else "close"
    except socket.timeout:
        return "timeout"
    except (ConnectionResetError, BrokenPipeError, OSError):
        return "neterr"
    finally:
        if s is not None:
            try:
                s.close()
            except Exception:
                pass


def alive(host, port):
    try:
        return do(host, port, "GET", "/", timeout=5.0).code == 200
    except Exception:
        return False


def get_arg(flag, default):
    if flag in sys.argv:
        i = sys.argv.index(flag)
        if i + 1 < len(sys.argv):
            return sys.argv[i + 1]
    return default


def main():
    base = next((a for a in sys.argv[1:] if not a.startswith("--")),
                "http://localhost:8080")
    host, port = parse_base_url(base)
    iterations = int(get_arg("--iterations", "5000"))
    seed = int(get_arg("--seed", "1337"))
    rng = random.Random(seed)
    r = Reporter("FUZZ " + base)

    print("  %d requetes fuzzees (seed=%d) ..." % (iterations, seed))
    stats = {"resp": 0, "close": 0, "timeout": 0, "neterr": 0}
    dead_at = None
    last_payload = None

    for i in range(iterations):
        payload = mutate(rng.choice(SEEDS), rng)
        last_payload = payload
        stats[send_once(host, port, payload)] += 1
        # Verifie regulierement que le serveur n'est pas mort/fige.
        if i % 200 == 199:
            if not alive(host, port):
                dead_at = i
                break

    print("  -> resp=%(resp)d close=%(close)d timeout=%(timeout)d neterr=%(neterr)d"
          % stats)

    if dead_at is not None:
        r.ko("le serveur a SURVECU au fuzzing",
             "MORT vers l'iteration %d" % dead_at)
        print("  Derniere charge envoyee (a rejouer) :")
        print("  " + repr(last_payload))
    else:
        r.ok("le serveur a SURVECU a %d requetes fuzzees" % iterations)

    r.check("serveur toujours vivant a la fin du fuzz", alive(host, port))

    # Un taux de timeouts eleve n'est pas un crash, mais c'est un signal (le
    # parseur attend peut-etre indefiniment un octet sur certaines mutations).
    if stats["timeout"] > iterations * 0.2:
        print("  NOTE: beaucoup de timeouts -- des mutations laissent des requetes"
              " incompletes que le parseur attend (borne par les timeouts serveur).")

    return r.summary()


if __name__ == "__main__":
    sys.exit(main())
