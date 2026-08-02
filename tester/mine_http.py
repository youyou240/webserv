#!/usr/bin/env python3
# mine_http.py
# -----------------------------------------------------------------------------
# Petite bibliotheque partagee par les testeurs "mine_*".
# Elle parle HTTP a la main sur des sockets bruts (module `socket` de la stdlib),
# SANS aucune dependance externe (pas de pip, pas de `requests`). C'est voulu :
#   1) le container n'a que python3 de base ;
#   2) tester en raw socket permet d'envoyer des requetes VOLONTAIREMENT
#      malformees (chunked bizarre, slowloris, octets poubelle) que des clients
#      "propres" comme curl refusent de produire.
#
# Contenu :
#   - parse_base_url : "http://localhost:8080" -> ("localhost", 8080)
#   - Conn           : une connexion TCP + un lecteur de reponse HTTP bufferise
#   - build_request  : fabrique les octets d'une requete
#   - do             : envoie une requete simple et rend la reponse parsee
#   - Reporter       : compteur [PASS]/[FAIL]/[SKIP] + code de sortie
# -----------------------------------------------------------------------------

import select
import socket
import time

# Codes couleur ANSI pour une sortie lisible dans le terminal du container.
GREEN = "\033[32m"
RED = "\033[31m"
YELLOW = "\033[33m"
BOLD = "\033[1m"
RESET = "\033[0m"


def parse_base_url(arg):
    # Accepte "http://host:port", "host:port" ou "host" (port 80 par defaut).
    s = arg
    if s.startswith("http://"):
        s = s[len("http://"):]
    # On jette un eventuel chemin apres le host:port.
    if "/" in s:
        s = s.split("/", 1)[0]
    if ":" in s:
        host, port = s.rsplit(":", 1)
        return host, int(port)
    return s, 80


class Response(object):
    # Reponse HTTP parsee. `headers` a ses cles en minuscules pour comparer
    # sans se soucier de la casse (HTTP dit que les noms sont insensibles).
    def __init__(self, code, reason, headers, body):
        self.code = code
        self.reason = reason
        self.headers = headers
        self.body = body

    def header(self, name):
        return self.headers.get(name.lower())

    @property
    def text(self):
        # Le corps decode en texte tolerant (latin-1 ne leve jamais d'erreur).
        return self.body.decode("latin-1")


class Conn(object):
    # Une connexion TCP + un lecteur de reponse.
    # On garde un buffer interne (`_buf`) : quand on lit les entetes on recoit
    # souvent deja un bout du corps (ou de la reponse suivante en keep-alive).
    # Ce buffer evite de perdre ces octets entre deux `read_response`.
    def __init__(self, host=None, port=None, timeout=5.0, sock=None):
        # On peut fabriquer une Conn a partir d'un couple host/port OU adopter
        # un socket deja connecte (utile pour send_probe).
        if sock is not None:
            self.sock = sock
        else:
            self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)
        self._buf = b""

    def sendall(self, data):
        if isinstance(data, str):
            data = data.encode("latin-1")
        self.sock.sendall(data)

    def _fill(self):
        # Lit un paquet depuis le socket. Rend False sur EOF (serveur a ferme).
        # Un RST (le serveur ferme alors qu'il nous reste des donnees en vol,
        # typique apres un 413/414) leve ConnectionResetError : on le traite
        # comme une fin de flux plutot que de crasher.
        try:
            chunk = self.sock.recv(65536)
        except (ConnectionResetError, BrokenPipeError, OSError):
            return False
        if not chunk:
            return False
        self._buf += chunk
        return True

    def _read_line(self):
        # Lit une ligne terminee par CRLF (rend la ligne SANS le CRLF).
        while b"\r\n" not in self._buf:
            if not self._fill():
                break
        idx = self._buf.find(b"\r\n")
        if idx == -1:
            line, self._buf = self._buf, b""
            return line
        line = self._buf[:idx]
        self._buf = self._buf[idx + 2:]
        return line

    def _read_exact(self, n):
        # Lit exactement n octets (le corps quand Content-Length est connu).
        while len(self._buf) < n:
            if not self._fill():
                break
        data = self._buf[:n]
        self._buf = self._buf[n:]
        return data

    def _read_chunked(self):
        # Decode un corps Transfer-Encoding: chunked.
        out = b""
        while True:
            size_line = self._read_line()
            # La taille peut avoir des "extensions" apres un ';' -> on les ignore.
            size_str = size_line.split(b";")[0].strip()
            try:
                size = int(size_str, 16)
            except ValueError:
                break
            if size == 0:
                # Chunk final : on avale les trailers jusqu'a la ligne vide.
                while self._read_line() != b"":
                    pass
                break
            out += self._read_exact(size)
            self._read_exact(2)  # le CRLF qui suit chaque chunk
        return out

    def read_response(self):
        # Lit UNE reponse HTTP complete depuis le socket.
        status_line = self._read_line().decode("latin-1")
        parts = status_line.split(" ", 2)
        code = int(parts[1]) if len(parts) >= 2 and parts[1].isdigit() else 0
        reason = parts[2] if len(parts) >= 3 else ""

        headers = {}
        while True:
            line = self._read_line()
            if line == b"":
                break
            if b":" in line:
                k, v = line.split(b":", 1)
                headers[k.decode("latin-1").strip().lower()] = v.decode("latin-1").strip()

        body = b""
        if "content-length" in headers:
            body = self._read_exact(int(headers["content-length"]))
        elif headers.get("transfer-encoding", "").lower() == "chunked":
            body = self._read_chunked()
        else:
            # Pas de Content-Length : 1xx/204/304 n'ont pas de corps ; sinon on
            # lit jusqu'a la fermeture du serveur (Connection: close).
            if code not in (204, 304) and code // 100 != 1:
                while self._fill():
                    pass
                body = self._buf
                self._buf = b""
        return Response(code, reason, headers, body)

    def close(self):
        try:
            self.sock.close()
        except Exception:
            pass


def build_request(method, path, headers=None, body=b"", http="1.1"):
    # Fabrique les octets bruts d'une requete HTTP.
    if isinstance(body, str):
        body = body.encode("latin-1")
    h = dict(headers or {})
    lines = ["%s %s HTTP/%s" % (method, path, http)]
    for k, v in h.items():
        lines.append("%s: %s" % (k, v))
    head = ("\r\n".join(lines) + "\r\n\r\n").encode("latin-1")
    return head + body


def do(host, port, method, path, headers=None, body=b"",
       host_header="localhost", timeout=5.0, http="1.1"):
    # Envoie une requete "propre" et rend la reponse. Ajoute automatiquement
    # Host, Content-Length et Connection: close si absents.
    if isinstance(body, str):
        body = body.encode("latin-1")
    h = dict(headers or {})
    lower = [k.lower() for k in h]
    if host_header is not None and "host" not in lower:
        h["Host"] = host_header
    if body and "content-length" not in lower and "transfer-encoding" not in lower:
        h["Content-Length"] = str(len(body))
    if "connection" not in lower:
        h["Connection"] = "close"
    c = Conn(host, port, timeout)
    c.sendall(build_request(method, path, h, body, http))
    resp = c.read_response()
    c.close()
    return resp


def send_probe(host, port, raw, timeout=10.0, chunk=4096):
    # Envoie une requete potentiellement ENORME (corps trop gros -> 413, URI
    # geante -> 414) par petits blocs, tout en surveillant si le serveur repond
    # DEJA. Des qu'une reponse arrive on arrete d'ecrire et on la lit.
    #
    # Pourquoi : sur une requete hors-limite, le serveur repond l'erreur puis
    # ferme la connexion alors qu'on est encore en train d'envoyer. En TCP, ca
    # provoque un RST qui peut effacer la reponse si on a tout balance d'un coup.
    # En envoyant progressivement et en lisant des que le socket est "readable",
    # on capture le 413/414 de facon fiable. Robuste au RST pendant l'envoi.
    if isinstance(raw, str):
        raw = raw.encode("latin-1")
    s = socket.create_connection((host, port), timeout=timeout)
    s.setblocking(False)
    pos = 0
    deadline = time.time() + timeout
    while pos < len(raw) and time.time() < deadline:
        rl, wl, _ = select.select([s], [s], [], 1.0)
        if rl:  # le serveur a deja commence a repondre : on arrete d'ecrire
            break
        if wl:
            try:
                pos += s.send(raw[pos:pos + chunk])
            except (BrokenPipeError, ConnectionResetError, OSError):
                break  # RST pendant l'envoi : on tentera quand meme la lecture
    c = Conn(sock=s, timeout=timeout)
    resp = c.read_response()
    c.close()
    return resp


class Reporter(object):
    # Compteur de resultats + affichage. `check` est le raccourci principal :
    # on lui passe une condition booleenne et il incremente PASS ou FAIL.
    def __init__(self, title):
        self.title = title
        self.passed = 0
        self.failed = 0
        self.skipped = 0
        print(BOLD + "== " + title + " ==" + RESET)

    def ok(self, name, detail=""):
        self.passed += 1
        print(GREEN + "[PASS]" + RESET + " " + name + (("  " + detail) if detail else ""))

    def ko(self, name, detail=""):
        self.failed += 1
        print(RED + "[FAIL]" + RESET + " " + name + (("  " + detail) if detail else ""))

    def skip(self, name, detail=""):
        self.skipped += 1
        print(YELLOW + "[SKIP]" + RESET + " " + name + (("  " + detail) if detail else ""))

    def check(self, name, cond, detail=""):
        if cond:
            self.ok(name, detail)
        else:
            self.ko(name, detail)
        return cond

    def summary(self):
        # Rend un code de sortie utilisable en shell (0 = tout vert).
        total = self.passed + self.failed + self.skipped
        print(BOLD + "-- %s : %d/%d PASS, %d FAIL, %d SKIP --%s"
              % (self.title, self.passed, total, self.failed, self.skipped, RESET))
        return 0 if self.failed == 0 else 1
