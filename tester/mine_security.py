#!/usr/bin/env python3
# mine_security.py
# -----------------------------------------------------------------------------
# Suite de tests de SECURITE. Chaque test rejoue une attaque et verifie que
# webserv la neutralise comme promis dans docs/SECURITY.md. Le [§X.Y] devant
# chaque test renvoie a la section correspondante de la doc, pour la soutenance.
#
# C'est de l'audit DEFENSIF sur TON PROPRE serveur : on cogne sur localhost /
# le container, jamais sur une machine tierce.
#
# Principe unique verifie partout (SECURITY.md §1) :
#   "Ne jamais faire confiance a une donnee externe -- la neutraliser avant de
#    l'utiliser dans un contexte sensible."
#
# Usage (serveur lance avec configs/default.conf) :
#   ./mine_security.py http://localhost:8080
# -----------------------------------------------------------------------------

import os
import socket
import sys
from urllib.parse import quote

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mine_http import parse_base_url, do, build_request, send_probe, Reporter


def raw(host, port, payload, timeout=5.0):
    # Envoie des octets bruts (pour fabriquer des requetes malformees que les
    # helpers "propres" refusent : headers en double, cle avec espace, etc.).
    # Rend (code_http_ou_None, texte_complet). Tolerant au RST.
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


def multipart(boundary, filename, content):
    # Construit un corps multipart/form-data avec un filename controle.
    return (
        "--" + boundary + "\r\n"
        'Content-Disposition: form-data; name="file"; filename="' + filename + '"\r\n'
        "Content-Type: text/plain\r\n\r\n"
        + content + "\r\n"
        "--" + boundary + "--\r\n"
    )


def alive(host, port):
    try:
        return do(host, port, "GET", "/", timeout=5.0).code == 200
    except Exception:
        return False


def main():
    base = sys.argv[1] if len(sys.argv) > 1 else "http://localhost:8080"
    host, port = parse_base_url(base)
    pid = os.getpid()
    r = Reporter("SECURITY " + base)

    # === 2. ENTREE -- parseur HTTP ==========================================

    # [2.6] Host obligatoire en HTTP/1.1
    code, _ = raw(host, port, "GET / HTTP/1.1\r\n\r\n")
    r.check("[2.6] HTTP/1.1 sans Host -> 400", code == 400, "code=%s" % code)

    # [2.2/2.3] Injection CRLF encodee dans l'URI (%0d%0a)
    code, _ = raw(host, port,
                  "GET /x%0d%0aInjected:%20yes HTTP/1.1\r\nHost: localhost\r\n\r\n")
    r.check("[2.3] CRLF encode (%0d%0a) dans l'URI -> 400", code == 400, "code=%s" % code)

    # [2.3] NUL encode (%00) : invisible encode, dangereux decode
    code, _ = raw(host, port, "GET /a%00b HTTP/1.1\r\nHost: localhost\r\n\r\n")
    r.check("[2.3] NUL encode (%00) -> 400", code == 400, "code=%s" % code)

    # [2.3] Percent-encoding invalide (% non suivi de 2 hexa)
    code, _ = raw(host, port, "GET /a%zz HTTP/1.1\r\nHost: localhost\r\n\r\n")
    r.check("[2.3] percent-encoding invalide (%zz) -> 400", code == 400, "code=%s" % code)

    # [2.4] Cle de header contenant un espace (smuggling potentiel "Ho st:")
    code, _ = raw(host, port,
                  "GET / HTTP/1.1\r\nHost: localhost\r\nBad Header: x\r\n\r\n")
    r.check("[2.4] cle de header avec espace -> 400", code == 400, "code=%s" % code)

    # [2.4] Caractere de controle dans une valeur de header
    code, _ = raw(host, port,
                  "GET / HTTP/1.1\r\nHost: localhost\r\nX-Test: a\x01b\r\n\r\n")
    r.check("[2.4] ctl char dans une valeur de header -> 400", code == 400, "code=%s" % code)

    r.check("serveur vivant apres les tests de parsing", alive(host, port))

    # [2.5] Request smuggling -- Host en double
    code, _ = raw(host, port, "GET / HTTP/1.1\r\nHost: a\r\nHost: b\r\n\r\n")
    r.check("[2.5] Host en double -> 400", code == 400, "code=%s" % code)

    # [2.5] Content-Length en double
    code, _ = raw(host, port,
                  "POST /uploads/smug HTTP/1.1\r\nHost: localhost\r\n"
                  "Content-Length: 3\r\nContent-Length: 4\r\n\r\nabc")
    r.check("[2.5] Content-Length en double -> 400", code == 400, "code=%s" % code)

    # [2.5] Content-Length ET Transfer-Encoding ensemble (le coeur du smuggling)
    code, _ = raw(host, port,
                  "POST /uploads/smug HTTP/1.1\r\nHost: localhost\r\n"
                  "Content-Length: 3\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n")
    r.check("[2.5] Content-Length + Transfer-Encoding -> 400", code == 400, "code=%s" % code)

    # [2.7] Integer overflow sur Content-Length (2^64)
    code, _ = raw(host, port,
                  "POST /uploads/ovf HTTP/1.1\r\nHost: localhost\r\n"
                  "Content-Length: 18446744073709551616\r\n\r\n")
    r.check("[2.7] Content-Length en overflow -> 400", code == 400, "code=%s" % code)

    # [2.8] Chunked : taille de chunk en hexa invalide
    code, _ = raw(host, port,
                  "POST /uploads/ck HTTP/1.1\r\nHost: localhost\r\n"
                  "Transfer-Encoding: chunked\r\n\r\nZZ\r\nabc\r\n0\r\n\r\n")
    r.check("[2.8] taille de chunk hexa invalide -> 400", code == 400, "code=%s" % code)

    # [2.8] Chunked : CRLF manquant apres les donnees d'un chunk
    code, _ = raw(host, port,
                  "POST /uploads/ck HTTP/1.1\r\nHost: localhost\r\n"
                  "Transfer-Encoding: chunked\r\n\r\n3\r\nabcXX0\r\n\r\n")
    r.check("[2.8] CRLF de chunk manquant -> 400", code == 400, "code=%s" % code)

    # [2.9] Limite de taille des headers (>32768 octets) -> 400/431
    big = ("GET / HTTP/1.1\r\nHost: localhost\r\n"
           + "".join("X-Pad-%d: %s\r\n" % (i, "z" * 200) for i in range(2000))
           + "\r\n")
    resp = send_probe(host, port, big, timeout=8.0)
    r.check("[2.9] bloc de headers gigantesque -> 400/431",
            resp.code in (400, 431), "code=%s" % resp.code)

    r.check("serveur vivant apres les tests de smuggling/limites", alive(host, port))

    # === 3. ENTREE -- routing / systeme de fichiers =========================

    # [3.1] Path traversal brut vers /etc/passwd : ne doit RIEN fuiter
    resp = do(host, port, "GET", "/../../../../etc/passwd")
    r.check("[3.1] traversal brut ne sert pas /etc/passwd",
            resp.code in (400, 403, 404) and "root:" not in resp.text,
            "code=%d" % resp.code)

    # [3.1] Traversal encode (%2e%2e = "..") : re-teste apres decodage
    resp = do(host, port, "GET", "/%2e%2e/%2e%2e/etc/passwd")
    r.check("[3.1] traversal encode (%2e%2e) neutralise",
            resp.code in (400, 403, 404) and "root:" not in resp.text,
            "code=%d" % resp.code)

    # [3.1] Double-encodage (%252e) : ne doit PAS etre re-decode en ".."
    resp = do(host, port, "GET", "/%252e%252e/etc/passwd")
    r.check("[3.1] double-encodage non re-decode (pas de fuite)",
            resp.code in (400, 403, 404) and "root:" not in resp.text,
            "code=%d" % resp.code)

    # [3.5] Autoindex off + pas d'index -> 403 (pas de divulgation d'arbo)
    resp = do(host, port, "GET", "/private/")
    r.check("[3.5] dossier prive (autoindex off) -> 403",
            resp.code == 403, "code=%d" % resp.code)

    # [3.7] Upload : filename de traversal confine dans upload_store
    trav_name = "mine_pwn_%d.txt" % pid
    boundary = "----minesec%d" % pid
    body = multipart(boundary, "../../../../" + trav_name, "pwned\n")
    resp = do(host, port, "POST", "/uploads",
              headers={"Content-Type": "multipart/form-data; boundary=" + boundary},
              body=body)
    if resp.code == 400:
        # Rejet direct : acceptable.
        r.check("[3.7] filename de traversal rejete/confine", True, "code=400")
    else:
        # Sinon il doit avoir ete reduit au basename DANS upload_store...
        in_store = do(host, port, "GET", "/uploads/" + trav_name).code == 200
        # ...et ne PAS avoir echappe vers la racine web.
        escaped = do(host, port, "GET", "/" + trav_name).code == 200
        r.check("[3.7] filename de traversal reduit au basename (reste dans upload_store)",
                resp.code == 201 and in_store and not escaped,
                "code=%d in_store=%s escaped=%s" % (resp.code, in_store, escaped))
        do(host, port, "DELETE", "/uploads/" + trav_name)  # nettoyage

    # [3.7 / 3.1] Traversal via l'URL de POST : bloque par resolveFsPath
    resp = do(host, port, "POST", "/uploads/../../" + ("mine_pwn2_%d.txt" % pid),
              body="pwned2\n")
    r.check("[3.7] POST avec URL de traversal -> pas d'ecriture hors store",
            resp.code in (400, 403, 404), "code=%d" % resp.code)

    r.check("serveur vivant apres les tests de traversal/upload", alive(host, port))

    # === 4. SORTIE -- la reponse ============================================

    # [4.1] XSS stockee via un nom de fichier affiche dans l'autoindex.
    # IMPORTANT : le payload ne doit contenir AUCUN '/', sinon sanitizeFilename
    # (§3.7, basename seul) le tronque avant l'autoindex et on testerait la
    # mauvaise defense. On utilise donc <img ...> plutot que <script>...</script>
    # (qui contient un '/' dans la balise fermante). Ici on isole bien §4.1 :
    # les '<' et '>' arrivent intacts jusqu'au listing et DOIVENT etre echappes.
    xss_name = "mine_xss_<img src=x onerror=alert(1)>_%d.txt" % pid
    boundary = "----minexss%d" % pid
    body = multipart(boundary, xss_name, "xss\n")
    resp = do(host, port, "POST", "/uploads",
              headers={"Content-Type": "multipart/form-data; boundary=" + boundary},
              body=body)
    if resp.code == 201:
        listing = do(host, port, "GET", "/uploads/").text
        r.check("[4.1] le nom XSS est HTML-echappe dans l'autoindex",
                "&lt;img" in listing and "<img" not in listing)
        do(host, port, "DELETE", "/uploads/" + quote(xss_name))  # nettoyage
    else:
        r.skip("[4.1] XSS autoindex", "upload du nom XSS refuse (code=%d)" % resp.code)

    # [4.2] Content-Length calcule (jamais fourni a la main) == taille reelle
    resp = do(host, port, "GET", "/")
    cl = resp.header("content-length")
    r.check("[4.2] Content-Length == taille reelle du corps",
            cl is not None and int(cl) == len(resp.body),
            "cl=%s body=%d" % (cl, len(resp.body)))

    # === 6. Reduction de surface ============================================

    # [6.1] Methode connue mais non geree ailleurs -> 501 (surface reduite)
    code, _ = raw(host, port, "PATCH / HTTP/1.1\r\nHost: localhost\r\n\r\n")
    r.check("[6.1] methode non geree (PATCH) -> 501", code == 501, "code=%s" % code)

    # [6.1] Version HTTP non supportee -> 505
    code, _ = raw(host, port, "GET / HTTP/3.0\r\nHost: localhost\r\n\r\n")
    r.check("[6.1] version HTTP/3.0 -> 505", code == 505, "code=%s" % code)

    r.check("serveur toujours vivant a la fin de l'audit", alive(host, port))

    return r.summary()


if __name__ == "__main__":
    sys.exit(main())
