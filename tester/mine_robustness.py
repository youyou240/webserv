#!/usr/bin/env python3
# mine_robustness.py
# -----------------------------------------------------------------------------
# Suite de ROBUSTESSE : on envoie des requetes VOLONTAIREMENT cassees et on
# verifie deux choses a chaque fois :
#   1) le serveur repond avec le bon code d'erreur (400/414/501/505...) OU
#      ferme proprement -- il ne doit jamais planter ni hang indefiniment ;
#   2) le serveur reste DISPONIBLE : apres chaque attaque on refait un GET /
#      normal qui doit toujours renvoyer 200 (invariant "reste up en permanence"
#      du sujet).
#
# Ces tests utilisent des sockets bruts car curl refuse de fabriquer ce genre
# de requetes malformees.
#
# Usage (serveur deja lance) :
#   ./mine_robustness.py http://localhost:8080
#
# Note : le test slowloris attend le timeout serveur (HEADER_TIMEOUT ~30s dans
# webserv), donc CE test peut prendre ~30s. Passe --no-slow pour le sauter.
# -----------------------------------------------------------------------------

import os
import socket
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mine_http import parse_base_url, do, Conn, send_probe, Reporter


def raw_send_recv(host, port, payload, timeout=5.0):
    # Envoie des octets bruts, rend (code_http, tout_le_texte) OU (None, "")
    # si le serveur a ferme sans rien envoyer. Tolerant au RST : le serveur peut
    # fermer brutalement (reset) sur une entree invalide, ce n'est pas un crash.
    s = socket.create_connection((host, port), timeout=timeout)
    s.settimeout(timeout)
    if isinstance(payload, str):
        payload = payload.encode("latin-1")
    try:
        s.sendall(payload)
    except (BrokenPipeError, ConnectionResetError, OSError):
        pass
    data = b""
    try:
        while True:
            chunk = s.recv(65536)
            if not chunk:
                break
            data += chunk
    except (socket.timeout, ConnectionResetError, BrokenPipeError, OSError):
        pass
    finally:
        s.close()
    text = data.decode("latin-1", "replace")
    code = None
    if text.startswith("HTTP/"):
        parts = text.split(" ", 2)
        if len(parts) >= 2 and parts[1].isdigit():
            code = int(parts[1])
    return code, text


def alive(host, port):
    # Le serveur repond-il encore normalement ?
    try:
        return do(host, port, "GET", "/", timeout=5.0).code == 200
    except Exception:
        return False


def main():
    args = sys.argv[1:]
    do_slow = "--no-slow" not in args
    base = next((a for a in args if not a.startswith("--")), "http://localhost:8080")
    host, port = parse_base_url(base)
    r = Reporter("ROBUSTNESS " + base)

    # --- Octets poubelle (pas du HTTP du tout) -> 400 ------------------------
    code, _ = raw_send_recv(host, port, "\x01\x02\x03 pas du http\r\n\r\n")
    r.check("octets poubelle -> 400 (ou fermeture)",
            code == 400 or code is None, "code=%s" % code)
    r.check("serveur vivant apres octets poubelle", alive(host, port))

    # --- Request-line incomplete -> 400 --------------------------------------
    code, _ = raw_send_recv(host, port, "GET\r\n\r\n")
    r.check("request-line incomplete -> 400", code == 400, "code=%s" % code)
    r.check("serveur vivant apres request-line cassee", alive(host, port))

    # --- Methode inconnue -> 501 ---------------------------------------------
    code, _ = raw_send_recv(host, port, "BREW / HTTP/1.1\r\nHost: localhost\r\n\r\n")
    r.check("methode inconnue (BREW) -> 501", code == 501, "code=%s" % code)
    r.check("serveur vivant apres methode inconnue", alive(host, port))

    # --- Version HTTP non supportee -> 505 -----------------------------------
    code, _ = raw_send_recv(host, port, "GET / HTTP/2.0\r\nHost: localhost\r\n\r\n")
    r.check("version HTTP/2.0 -> 505", code == 505, "code=%s" % code)
    r.check("serveur vivant apres version invalide", alive(host, port))

    # --- Request-line geante -> 414 ------------------------------------------
    # send_probe envoie progressivement et lit des que le serveur repond : ca
    # capture le 414 avant que le RST (close cote serveur) n'efface la reponse.
    huge_path = "/" + ("a" * 100000)
    resp = send_probe(host, port,
                      "GET " + huge_path + " HTTP/1.1\r\nHost: localhost\r\n\r\n",
                      timeout=8.0)
    r.check("URI geante -> 414", resp.code == 414, "code=%s" % resp.code)
    r.check("serveur vivant apres URI geante", alive(host, port))

    # --- Bloc de headers gigantesque -> 400/431 ------------------------------
    big_headers = "GET / HTTP/1.1\r\nHost: localhost\r\n"
    big_headers += "".join("X-Pad-%d: %s\r\n" % (i, "z" * 200) for i in range(2000))
    big_headers += "\r\n"
    resp = send_probe(host, port, big_headers, timeout=8.0)
    r.check("headers gigantesques -> 400 ou 431",
            resp.code in (400, 431), "code=%s" % resp.code)
    r.check("serveur vivant apres headers gigantesques", alive(host, port))

    # --- Requete partielle puis fermeture brutale ----------------------------
    # On envoie un debut de requete et on coupe : le serveur ne doit pas crasher.
    s = socket.create_connection((host, port), timeout=5.0)
    s.sendall(b"GET / HTTP/1.1\r\nHost: localhost\r\n")
    s.close()
    time.sleep(0.2)
    r.check("serveur vivant apres requete partielle + close", alive(host, port))

    # --- Half-close : on ferme le sens ecriture, on lit quand meme -----------
    # Un client qui fait shutdown(SHUT_WR) doit tout de meme recevoir sa reponse.
    s = socket.create_connection((host, port), timeout=5.0)
    s.sendall(b"GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
    s.shutdown(socket.SHUT_WR)
    data = b""
    try:
        while True:
            chunk = s.recv(65536)
            if not chunk:
                break
            data += chunk
    except socket.timeout:
        pass
    s.close()
    r.check("half-close (SHUT_WR) : reponse recue quand meme",
            data.startswith(b"HTTP/1.1 200"), "start=%r" % data[:20])

    # --- Tempete de connexions : ouvrir/fermer vite sans rien envoyer --------
    errors = 0
    for _ in range(300):
        try:
            s = socket.create_connection((host, port), timeout=5.0)
            s.close()
        except Exception:
            errors += 1
    r.check("300 connexions ouvertes/fermees sans crash", errors == 0,
            "erreurs=%d" % errors)
    r.check("serveur vivant apres la tempete de connexions", alive(host, port))

    # --- Slowloris : headers au compte-goutte, jamais termines ---------------
    if do_slow:
        slow = socket.create_connection((host, port), timeout=5.0)
        slow.sendall(b"GET / HTTP/1.1\r\nHost: localhost\r\n")
        # Pendant que cette connexion traine, le serveur doit servir les autres.
        r.check("slowloris n'empeche pas de servir les autres clients",
                alive(host, port))
        # Le serveur doit finir par timeout et fermer la connexion lente.
        slow.settimeout(40.0)
        closed = False
        deadline = time.time() + 38.0
        while time.time() < deadline:
            try:
                slow.sendall(b"X-Drip: 1\r\n")  # une miette de header
                chunk = slow.recv(65536)
                if not chunk:      # EOF = le serveur a ferme
                    closed = True
                    break
                if b"HTTP/1.1 4" in chunk:  # 408/400 = timeout gere
                    closed = True
                    break
            except (socket.timeout, BrokenPipeError, ConnectionResetError, OSError):
                closed = True
                break
            time.sleep(2)
        slow.close()
        r.check("slowloris finit par etre timeout/ferme par le serveur", closed)
        r.check("serveur vivant apres slowloris", alive(host, port))
    else:
        r.skip("slowloris", "desactive via --no-slow")

    return r.summary()


if __name__ == "__main__":
    sys.exit(main())
