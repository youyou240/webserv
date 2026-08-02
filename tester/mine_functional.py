#!/usr/bin/env python3
# mine_functional.py
# -----------------------------------------------------------------------------
# Suite de tests FONCTIONNELS (correctness). On verifie que webserv rend les
# BONS codes de statut, les bons headers et le bon corps pour chaque feature du
# sujet, en s'appuyant sur `configs/default.conf` :
#
#   :8080 localhost -> / (GET only), /uploads (GET POST DELETE, autoindex on),
#                      /cgi-bin (.cgi/.py/.php), /old (301)
#   :8080 site-b.local -> root ./www-b (virtual host par Host)
#   :9090           -> autoindex on
#
# Usage (dans le container, serveur deja lance) :
#   ./mine_functional.py http://localhost:8080
#
# Chaque assertion affiche [PASS] / [FAIL] / [SKIP]. Le code de sortie vaut 0
# si tout est vert (pratique pour chainer dans un script).
# -----------------------------------------------------------------------------

import os
import sys

# Permet d'importer mine_http.py meme si on lance le script depuis ailleurs.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mine_http import parse_base_url, do, Conn, build_request, send_probe, Reporter


def main():
    base = sys.argv[1] if len(sys.argv) > 1 else "http://localhost:8080"
    host, port = parse_base_url(base)
    r = Reporter("FUNCTIONAL " + base)

    # --- GET statique + types MIME -------------------------------------------
    resp = do(host, port, "GET", "/")
    r.check("GET / -> 200", resp.code == 200, "code=%d" % resp.code)
    r.check("GET / est du text/html",
            (resp.header("content-type") or "").startswith("text/html"),
            "ct=%s" % resp.header("content-type"))
    r.check("GET / renvoie l'index attendu", "<h1>webserv</h1>" in resp.text)

    resp = do(host, port, "GET", "/style.css")
    r.check("GET /style.css -> 200", resp.code == 200, "code=%d" % resp.code)
    r.check("GET /style.css est du text/css",
            (resp.header("content-type") or "").startswith("text/css"),
            "ct=%s" % resp.header("content-type"))

    resp = do(host, port, "GET", "/about.html")
    r.check("GET /about.html -> 200", resp.code == 200, "code=%d" % resp.code)

    # --- Content-Length correct ----------------------------------------------
    resp = do(host, port, "GET", "/")
    cl = resp.header("content-length")
    r.check("Content-Length == taille du corps",
            cl is not None and int(cl) == len(resp.body),
            "cl=%s body=%d" % (cl, len(resp.body)))

    # --- 404 + page d'erreur custom ------------------------------------------
    resp = do(host, port, "GET", "/nope.html")
    r.check("GET /nope.html -> 404", resp.code == 404, "code=%d" % resp.code)
    r.check("404 sert la page custom (/errors/404.html)",
            "custom" in resp.text.lower())

    # --- 403 : dossier sans index et autoindex off ---------------------------
    resp = do(host, port, "GET", "/private/")
    r.check("GET /private/ -> 403 (autoindex off, pas d'index)",
            resp.code == 403, "code=%d" % resp.code)

    # --- 405 + header Allow ---------------------------------------------------
    resp = do(host, port, "POST", "/", body="x")
    r.check("POST / -> 405 (location / = GET only)",
            resp.code == 405, "code=%d" % resp.code)
    r.check("405 renvoie un header Allow avec GET",
            "GET" in (resp.header("allow") or ""),
            "allow=%s" % resp.header("allow"))

    resp = do(host, port, "DELETE", "/")
    r.check("DELETE / -> 405", resp.code == 405, "code=%d" % resp.code)

    # --- Redirection 301 ------------------------------------------------------
    resp = do(host, port, "GET", "/old")
    r.check("GET /old -> 301", resp.code == 301, "code=%d" % resp.code)
    r.check("301 renvoie Location: http://localhost:8080/",
            resp.header("location") == "http://localhost:8080/",
            "loc=%s" % resp.header("location"))

    # --- Autoindex (directory listing) ---------------------------------------
    resp = do(host, port, "GET", "/uploads/")
    r.check("GET /uploads/ -> 200 (autoindex on)",
            resp.code == 200, "code=%d" % resp.code)
    r.check("autoindex liste bien hello.txt", "hello.txt" in resp.text)

    resp = do(host, port, "GET", "/uploads/hello.txt")
    r.check("GET /uploads/hello.txt -> 200", resp.code == 200, "code=%d" % resp.code)

    # --- POST upload (corps brut) + GET + DELETE -----------------------------
    raw_name = "mine_raw_%d.txt" % os.getpid()
    raw_body = "corps-brut-de-test\n"
    resp = do(host, port, "POST", "/uploads/" + raw_name, body=raw_body)
    r.check("POST /uploads/%s (brut) -> 201" % raw_name,
            resp.code == 201, "code=%d" % resp.code)
    resp = do(host, port, "GET", "/uploads/" + raw_name)
    r.check("le fichier uploade est relisible et intact",
            resp.code == 200 and resp.text == raw_body,
            "code=%d" % resp.code)
    resp = do(host, port, "DELETE", "/uploads/" + raw_name)
    r.check("DELETE /uploads/%s -> 204" % raw_name,
            resp.code == 204, "code=%d" % resp.code)
    resp = do(host, port, "GET", "/uploads/" + raw_name)
    r.check("apres DELETE le fichier renvoie 404",
            resp.code == 404, "code=%d" % resp.code)

    # --- POST upload (multipart/form-data) -----------------------------------
    boundary = "----minetestboundary%d" % os.getpid()
    mp_name = "mine_multipart_%d.txt" % os.getpid()
    mp_content = "multipart-content\n"
    mp_body = (
        "--" + boundary + "\r\n"
        'Content-Disposition: form-data; name="file"; filename="' + mp_name + '"\r\n'
        "Content-Type: text/plain\r\n\r\n"
        + mp_content + "\r\n"
        "--" + boundary + "--\r\n"
    )
    resp = do(host, port, "POST", "/uploads",
              headers={"Content-Type": "multipart/form-data; boundary=" + boundary},
              body=mp_body)
    r.check("POST /uploads (multipart) -> 201", resp.code == 201, "code=%d" % resp.code)
    resp = do(host, port, "GET", "/uploads/" + mp_name)
    r.check("le fichier multipart est relisible",
            resp.code == 200 and mp_content.strip() in resp.text,
            "code=%d" % resp.code)
    do(host, port, "DELETE", "/uploads/" + mp_name)  # nettoyage

    # --- POST chunked (le serveur doit de-chunker) ---------------------------
    ck_name = "mine_chunked_%d.txt" % os.getpid()
    ck_body = "abcdefghij" * 10  # 100 octets
    # On envoie le corps en deux chunks a la main.
    c = Conn(host, port)
    head = build_request("POST", "/uploads/" + ck_name,
                         {"Host": "localhost",
                          "Transfer-Encoding": "chunked",
                          "Connection": "close"})
    c.sendall(head)
    part1, part2 = ck_body[:40], ck_body[40:]
    c.sendall("%x\r\n%s\r\n" % (len(part1), part1))
    c.sendall("%x\r\n%s\r\n" % (len(part2), part2))
    c.sendall("0\r\n\r\n")  # chunk final
    resp = c.read_response()
    c.close()
    r.check("POST chunked -> 201", resp.code == 201, "code=%d" % resp.code)
    resp = do(host, port, "GET", "/uploads/" + ck_name)
    r.check("le corps de-chunke est intact",
            resp.code == 200 and resp.text == ck_body, "code=%d" % resp.code)
    do(host, port, "DELETE", "/uploads/" + ck_name)  # nettoyage

    # --- 413 : corps trop gros (limite 1m sur /cgi-bin) ----------------------
    # On envoie via send_probe : le serveur repond 413 et ferme avant qu'on ait
    # fini d'envoyer le 1 Mo, ce qui declenche un RST -> send_probe le gere.
    big = "A" * (1024 * 1024 + 4096)  # ~1 Mo + un poil
    raw413 = build_request("POST", "/cgi-bin/hello.py",
                           {"Host": "localhost",
                            "Content-Length": str(len(big)),
                            "Connection": "close"}, big)
    resp = send_probe(host, port, raw413, timeout=10.0)
    r.check("POST corps > client_max_body_size -> 413",
            resp.code == 413, "code=%d" % resp.code)

    # --- Virtual host par header Host ----------------------------------------
    resp = do(host, port, "GET", "/", host_header="site-b.local")
    r.check("Host: site-b.local sert le vhost www-b",
            resp.code == 200 and "site-b.local" in resp.text, "code=%d" % resp.code)

    # --- CGI GET (Python, C, PHP) --------------------------------------------
    resp = do(host, port, "GET", "/cgi-bin/hello.py?foo=bar")
    r.check("CGI GET .py -> 200", resp.code == 200, "code=%d" % resp.code)
    r.check("CGI .py voit la QUERY_STRING", "foo=bar" in resp.text)

    resp = do(host, port, "GET", "/cgi-bin/hello.cgi?x=1")
    r.check("CGI GET .cgi (binaire C) -> 200", resp.code == 200, "code=%d" % resp.code)
    r.check("CGI .cgi repond bien du C", "C CGI" in resp.text)

    resp = do(host, port, "GET", "/cgi-bin/hello.php")
    if resp.code == 200:
        r.check("CGI GET .php -> 200", True)
    else:
        r.skip("CGI GET .php", "php-cgi probablement absent (code=%d)" % resp.code)

    # --- CGI POST (corps sur stdin) ------------------------------------------
    resp = do(host, port, "POST", "/cgi-bin/hello.py", body="ping-cgi")
    r.check("CGI POST .py -> 200", resp.code == 200, "code=%d" % resp.code)
    r.check("CGI .py relaie le corps POST (stdin)", "ping-cgi" in resp.text)

    # --- Expect: 100-continue -------------------------------------------------
    exp_name = "mine_expect_%d.txt" % os.getpid()
    exp_body = "corps-apres-100-continue\n"
    c = Conn(host, port)
    c.sendall(build_request("POST", "/uploads/" + exp_name,
                            {"Host": "localhost",
                             "Expect": "100-continue",
                             "Content-Length": str(len(exp_body)),
                             "Connection": "close"}))
    interim = c.read_response()  # on attend d'abord un 100 Continue
    r.check("Expect: 100-continue -> reponse intermediaire 100",
            interim.code == 100, "code=%d" % interim.code)
    c.sendall(exp_body)          # puis on envoie le corps
    final = c.read_response()
    c.close()
    r.check("apres le 100, le corps est accepte -> 201",
            final.code == 201, "code=%d" % final.code)
    do(host, port, "DELETE", "/uploads/" + exp_name)  # nettoyage

    # --- Keep-alive : deux requetes sur la meme connexion --------------------
    c = Conn(host, port)
    c.sendall(build_request("GET", "/", {"Host": "localhost", "Connection": "keep-alive"}))
    a = c.read_response()
    c.sendall(build_request("GET", "/about.html", {"Host": "localhost", "Connection": "close"}))
    b = c.read_response()
    c.close()
    r.check("keep-alive : 2 requetes / meme socket -> 200 + 200",
            a.code == 200 and b.code == 200, "codes=%d,%d" % (a.code, b.code))

    # --- Deuxieme port (9090, autoindex on) ----------------------------------
    # HTTP/1.1 impose un header Host meme sans server_name : on en envoie un
    # (sinon le serveur renvoie 400, ce qui est correct de sa part).
    try:
        resp = do(host, 9090, "GET", "/", host_header="localhost")
        r.check("GET :9090/ -> 200 (autoindex on)",
                resp.code == 200, "code=%d" % resp.code)
    except Exception as e:
        r.skip("GET :9090/", "port 9090 non ouvert ? (%s)" % e)

    return r.summary()


if __name__ == "__main__":
    sys.exit(main())
