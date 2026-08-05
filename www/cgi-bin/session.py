#!/usr/bin/env python3

import html
import os
import sys
import time
from urllib.parse import parse_qs, quote, unquote

STORE = "sessions.txt"
ID_BYTES = 16
ID_LEN = 32
HEX = "0123456789abcdef"
SESSION_TTL = 1800
MAX_SESSIONS = 200
MAX_NAME = 32
MAX_BODY = 8192

STYLE = """
:root {
  --bg: #0e1016;
  --panel: #161a24;
  --line: #262c3a;
  --fg: #e8eaf0;
  --dim: #8b93a7;
  --accent: #4ade80;
  --warn: #fbbf24;
  --danger: #f87171;
}
* { box-sizing: border-box; }
body {
  margin: 0;
  padding: 3rem 1.25rem 5rem;
  background: var(--bg);
  color: var(--fg);
  font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
  line-height: 1.6;
}
main { max-width: 48rem; margin: 0 auto; }
header.top { border-bottom: 1px solid var(--line); padding-bottom: 1.5rem; margin-bottom: 2.5rem; }
h1 { font-size: 1.5rem; margin: 0 0 0.35rem; letter-spacing: 0.02em; }
h1 span { color: var(--accent); }
.lede { color: var(--dim); margin: 0; font-size: 0.9rem; }
.badge {
  display: inline-block;
  padding: 0.15rem 0.6rem;
  border: 1px solid var(--accent);
  color: var(--accent);
  font-size: 0.72rem;
  text-transform: uppercase;
  letter-spacing: 0.12em;
  margin-bottom: 0.9rem;
}
.badge.known { border-color: var(--warn); color: var(--warn); }
.panel {
  background: var(--panel);
  border: 1px solid var(--line);
  border-left: 3px solid var(--accent);
  padding: 1.5rem;
  margin-bottom: 2.5rem;
}
.panel.muted { border-left-color: var(--line); }
label { display: block; font-size: 0.75rem; text-transform: uppercase; letter-spacing: 0.12em; color: var(--dim); margin-bottom: 0.4rem; }
input[type="text"] {
  width: 100%;
  background: var(--bg);
  color: var(--fg);
  border: 1px solid var(--line);
  padding: 0.65rem 0.8rem;
  font: inherit;
  margin-bottom: 1.25rem;
}
input[type="text"]:focus { outline: none; border-color: var(--accent); }
.row { display: flex; gap: 0.75rem; align-items: center; flex-wrap: wrap; }
button {
  background: var(--accent);
  color: #0b0d12;
  border: 1px solid var(--accent);
  padding: 0.6rem 1.3rem;
  font: inherit;
  font-weight: 700;
  cursor: pointer;
}
button:hover { filter: brightness(1.12); }
button.ghost { background: transparent; color: var(--dim); border-color: var(--line); font-weight: 400; }
button.ghost:hover { color: var(--danger); border-color: var(--danger); filter: none; }
section { margin-top: 3rem; }
h2 { font-size: 0.75rem; text-transform: uppercase; letter-spacing: 0.14em; color: var(--dim); border-bottom: 1px solid var(--line); padding-bottom: 0.6rem; }
table { width: 100%; border-collapse: collapse; font-size: 0.8rem; }
th { text-align: left; color: var(--accent); font-weight: 400; white-space: nowrap; padding: 0.3rem 1rem 0.3rem 0; vertical-align: top; }
td { color: var(--dim); word-break: break-all; padding: 0.3rem 0; }
td em { color: var(--danger); font-style: normal; }
td b { color: var(--fg); font-weight: 600; }
pre {
  background: var(--bg);
  border: 1px solid var(--line);
  padding: 0.9rem 1rem;
  margin: 0.6rem 0 0;
  overflow-x: auto;
  font-size: 0.8rem;
  color: var(--fg);
  white-space: pre-wrap;
  word-break: break-all;
}
pre.out { border-left: 3px solid var(--accent); }
pre.js { border-left: 3px solid var(--warn); }
.note { font-size: 0.8rem; color: var(--dim); margin: 0.8rem 0 0; }
.note b { color: var(--warn); font-weight: 600; }
footer { margin-top: 3rem; font-size: 0.78rem; color: var(--dim); }
footer code { color: var(--accent); }
"""


def read_body():
    try:
        length = int(os.environ.get("CONTENT_LENGTH") or 0)
    except ValueError:
        return ""
    if length <= 0:
        return ""
    if length > MAX_BODY:
        length = MAX_BODY
    return sys.stdin.buffer.read(length).decode("utf-8", errors="replace")


def read_jar():
    jar = {}
    for chunk in os.environ.get("HTTP_COOKIE", "").split(";"):
        chunk = chunk.strip()
        if not chunk or "=" not in chunk:
            continue
        name, _, value = chunk.partition("=")
        name = name.strip()
        if name:
            jar[name] = value.strip()
    return jar


def is_valid_id(sid):
    if len(sid) != ID_LEN:
        return False
    for c in sid:
        if c not in HEX:
            return False
    return True


def new_id():
    raw = os.urandom(ID_BYTES)
    return "".join("{:02x}".format(b) for b in raw)


def load_sessions():
    if not os.path.exists(STORE):
        return {}
    now = int(time.time())
    sessions = {}
    with open(STORE, "r", encoding="utf-8") as handle:
        for line in handle:
            parts = line.rstrip("\n").split("\t")
            if len(parts) != 5 or not is_valid_id(parts[0]):
                continue
            try:
                visits = int(parts[1])
                created = int(parts[2])
                last = int(parts[3])
            except ValueError:
                continue
            if now - last > SESSION_TTL:
                continue
            sessions[parts[0]] = {
                "visits": visits,
                "created": created,
                "last": last,
                "name": unquote(parts[4]),
            }
    return sessions


def save_sessions(sessions):
    ordered = sorted(sessions.items(), key=lambda item: item[1]["last"])
    ordered = ordered[-MAX_SESSIONS:]
    tmp = STORE + ".tmp"
    with open(tmp, "w", encoding="utf-8") as handle:
        for sid, rec in ordered:
            handle.write(
                "\t".join(
                    [
                        sid,
                        str(rec["visits"]),
                        str(rec["created"]),
                        str(rec["last"]),
                        quote(rec["name"], safe=""),
                    ]
                )
                + "\n"
            )
    os.replace(tmp, STORE)


def make_cookie(name, value, max_age=None, http_only=False):
    parts = ["{}={}".format(name, value), "Path=/"]
    if max_age is not None:
        parts.append("Max-Age={}".format(max_age))
    if http_only:
        parts.append("HttpOnly")
    parts.append("SameSite=Lax")
    return "; ".join(parts)


def self_url():
    script = os.environ.get("SCRIPT_NAME") or "/cgi-bin/session.py"
    host = os.environ.get("SERVER_NAME") or ""
    port = os.environ.get("SERVER_PORT") or ""
    if not host:
        return script
    if port and port != "80":
        return "http://{}:{}{}".format(host, port, script)
    return "http://{}{}".format(host, script)


def send(headers, body=b""):
    head = "".join(line + "\r\n" for line in headers) + "\r\n"
    sys.stdout.buffer.write(head.encode("utf-8"))
    if body:
        sys.stdout.buffer.write(body)
    sys.stdout.buffer.flush()


def touch_session(jar):
    sessions = load_sessions()
    now = int(time.time())
    sid = jar.get("session_id", "")
    cookies = []

    if is_valid_id(sid) and sid in sessions:
        record = sessions[sid]
        record["visits"] += 1
        record["last"] = now
        fresh = False
    else:
        sid = new_id()
        record = {"visits": 1, "created": now, "last": now, "name": ""}
        sessions[sid] = record
        fresh = True
        cookies.append(make_cookie("session_id", sid, http_only=True))

    cookies.append(make_cookie("last_seen", str(now), max_age=SESSION_TTL))
    save_sessions(sessions)
    return sid, record, fresh, cookies


def render_jar(jar):
    if not jar:
        return '<tr><th>&mdash;</th><td><em>aucun cookie recu</em></td></tr>'
    rows = []
    for name in sorted(jar):
        rows.append(
            "<tr><th>{}</th><td>{}</td></tr>".format(
                html.escape(name), html.escape(jar[name])
            )
        )
    return "\n".join(rows)


def render(sid, record, fresh, cookies, jar):
    raw = os.environ.get("HTTP_COOKIE", "")
    emitted = "\n".join("Set-Cookie: " + c for c in cookies)
    age = int(time.time()) - record["created"]

    if fresh:
        badge = '<p class="badge">nouvelle session</p>'
        lede = "Aucun cookie valide recu &mdash; le serveur vient d'en poser deux."
    else:
        badge = '<p class="badge known">session reconnue</p>'
        lede = "Le navigateur a renvoye le cookie : le serveur t'a retrouve."

    if record["name"]:
        greeting = "Bonjour <b>{}</b>.".format(html.escape(record["name"]))
    else:
        greeting = "Tu n'as pas encore de pseudo."

    return """<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>webserv &mdash; session</title>
<style>{style}</style>
</head>
<body>
<main>
  <header class="top">
    <h1>session <span>/ cookies</span></h1>
    <p class="lede">{lede}</p>
  </header>

  {badge}

  <div class="panel">
    <table>
      <tr><th>session_id</th><td><b>{sid}</b></td></tr>
      <tr><th>visites</th><td><b>{visits}</b></td></tr>
      <tr><th>age</th><td>{age} s</td></tr>
      <tr><th>pseudo</th><td>{name}</td></tr>
    </table>
    <p class="note">{greeting} Rien de tout cela ne voyage dans le cookie &mdash;
    seul l'identifiant circule, l'etat vit dans <code>{store}</code>.</p>
  </div>

  <form method="post" class="panel muted">
    <input type="hidden" name="action" value="setname">
    <label for="name">Enregistrer un pseudo dans la session</label>
    <input type="text" id="name" name="name" maxlength="{maxname}" placeholder="ton pseudo" value="{namevalue}">
    <div class="row">
      <button type="submit">Enregistrer</button>
    </div>
  </form>

  <form method="post" class="row">
    <input type="hidden" name="action" value="logout">
    <button type="submit" class="ghost">se deconnecter (expire le cookie)</button>
  </form>

  <section>
    <h2>Ce que le navigateur a envoye</h2>
    <pre>Cookie: {raw}</pre>
    <table style="margin-top:1rem">
{jar}
    </table>
  </section>

  <section>
    <h2>Ce que le serveur renvoie dans cette reponse</h2>
    <pre class="out">{emitted}</pre>
    <p class="note">Deux lignes <code>Set-Cookie</code> distinctes lors de la premiere visite :
    c'est exactement ce qu'une <b>map</b> ne savait pas representer.</p>
  </section>

  <section>
    <h2>Ce que JavaScript arrive a lire</h2>
    <pre class="js" id="jsview"></pre>
    <p class="note"><code>session_id</code> est absent de cette ligne : il porte l'attribut
    <b>HttpOnly</b>, donc <code>document.cookie</code> ne le voit pas. <code>last_seen</code>,
    lui, est lisible.</p>
  </section>

  <footer>
    Tous les cookies sont poses avec <code>Path=/</code> : sans cela leur portee par defaut
    serait <code>/cgi-bin</code>, et le reste du site ne les recevrait jamais.
    Stockage <code>{store}</code> en chemin <b>relatif</b> &mdash; il n'atterrit au bon endroit
    que si le serveur a fait son <code>chdir</code>.
  </footer>
</main>
<script>
document.getElementById("jsview").textContent = document.cookie || "(vide)";
</script>
</body>
</html>
""".format(
        style=STYLE,
        lede=lede,
        badge=badge,
        sid=html.escape(sid),
        visits=record["visits"],
        age=age,
        name=html.escape(record["name"]) if record["name"] else "<em>non defini</em>",
        namevalue=html.escape(record["name"], quote=True),
        greeting=greeting,
        maxname=MAX_NAME,
        raw=html.escape(raw) if raw else "(aucun)",
        jar=render_jar(jar),
        emitted=html.escape(emitted),
        store=STORE,
    )


def handle_setname(jar, params):
    sid = jar.get("session_id", "")
    if not is_valid_id(sid):
        return []
    sessions = load_sessions()
    if sid not in sessions:
        return []
    name = params.get("name", [""])[0].strip()[:MAX_NAME]
    sessions[sid]["name"] = name
    sessions[sid]["last"] = int(time.time())
    save_sessions(sessions)
    return []


def handle_logout(jar):
    sid = jar.get("session_id", "")
    if is_valid_id(sid):
        sessions = load_sessions()
        if sid in sessions:
            del sessions[sid]
            save_sessions(sessions)
    return [
        make_cookie("session_id", "", max_age=0, http_only=True),
        make_cookie("last_seen", "", max_age=0),
    ]


def main():
    jar = read_jar()
    method = os.environ.get("REQUEST_METHOD", "GET").upper()

    if method == "POST":
        params = parse_qs(read_body(), keep_blank_values=True)
        action = params.get("action", [""])[0]
        if action == "logout":
            cookies = handle_logout(jar)
        elif action == "setname":
            cookies = handle_setname(jar, params)
        else:
            cookies = []
        headers = ["Set-Cookie: " + c for c in cookies]
        headers.append("Location: " + self_url())
        send(headers)
        return

    sid, record, fresh, cookies = touch_session(jar)
    page = render(sid, record, fresh, cookies, jar).encode("utf-8")
    headers = ["Content-Type: text/html; charset=utf-8"]
    headers.extend("Set-Cookie: " + c for c in cookies)
    send(headers, page)


if __name__ == "__main__":
    main()
