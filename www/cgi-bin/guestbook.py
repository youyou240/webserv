#!/usr/bin/env python3

import html
import os
import sys
import time
from urllib.parse import parse_qs, quote, unquote

STORE = "guestbook.txt"
MAX_ENTRIES = 50
MAX_NAME = 40
MAX_TEXT = 280
XSS_PROBE = "<script>alert('xss')</script>"

ENV_KEYS = (
    "REQUEST_METHOD",
    "SERVER_PROTOCOL",
    "SCRIPT_NAME",
    "SCRIPT_FILENAME",
    "REQUEST_URI",
    "QUERY_STRING",
    "REMOTE_ADDR",
    "SERVER_NAME",
    "SERVER_PORT",
    "SERVER_SOFTWARE",
    "GATEWAY_INTERFACE",
    "CONTENT_TYPE",
    "CONTENT_LENGTH",
)

STYLE = """
:root {
  --bg: #0e1016;
  --panel: #161a24;
  --line: #262c3a;
  --fg: #e8eaf0;
  --dim: #8b93a7;
  --accent: #4ade80;
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
.count { color: var(--accent); }
form.compose {
  background: var(--panel);
  border: 1px solid var(--line);
  border-left: 3px solid var(--accent);
  padding: 1.5rem;
  margin-bottom: 3rem;
}
label { display: block; font-size: 0.75rem; text-transform: uppercase; letter-spacing: 0.12em; color: var(--dim); margin-bottom: 0.4rem; }
input[type="text"], textarea {
  width: 100%;
  background: var(--bg);
  color: var(--fg);
  border: 1px solid var(--line);
  padding: 0.65rem 0.8rem;
  font: inherit;
  margin-bottom: 1.25rem;
}
textarea { resize: vertical; min-height: 5.5rem; }
input[type="text"]:focus, textarea:focus { outline: none; border-color: var(--accent); }
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
a.ghost { color: var(--dim); font-size: 0.8rem; text-decoration: none; border-bottom: 1px dotted var(--line); }
a.ghost:hover { color: var(--accent); border-color: var(--accent); }
article {
  border: 1px solid var(--line);
  border-top: none;
  padding: 1.1rem 1.25rem;
  display: flex;
  gap: 1rem;
  align-items: flex-start;
}
article:first-of-type { border-top: 1px solid var(--line); }
article:nth-child(odd) { background: rgba(255, 255, 255, 0.015); }
.body { flex: 1; min-width: 0; }
.meta { font-size: 0.75rem; color: var(--dim); margin-bottom: 0.4rem; }
.meta b { color: var(--fg); font-weight: 600; }
.meta .ip { color: var(--accent); }
.text { margin: 0; white-space: pre-wrap; word-break: break-word; }
.empty { border: 1px dashed var(--line); padding: 2.5rem 1.25rem; text-align: center; color: var(--dim); }
section.env { margin-top: 3.5rem; }
h2 { font-size: 0.75rem; text-transform: uppercase; letter-spacing: 0.14em; color: var(--dim); border-bottom: 1px solid var(--line); padding-bottom: 0.6rem; }
table { width: 100%; border-collapse: collapse; font-size: 0.8rem; }
th { text-align: left; color: var(--accent); font-weight: 400; white-space: nowrap; padding: 0.3rem 1rem 0.3rem 0; vertical-align: top; }
td { color: var(--dim); word-break: break-all; padding: 0.3rem 0; }
td em { color: var(--danger); font-style: normal; }
footer { margin-top: 3rem; font-size: 0.78rem; color: var(--dim); }
footer code { color: var(--accent); }
"""


def read_body():
    try:
        length = int(os.environ.get("CONTENT_LENGTH") or 0)
    except ValueError:
        length = 0
    if length <= 0:
        return ""
    return sys.stdin.buffer.read(length).decode("utf-8", errors="replace")


def load_entries():
    if not os.path.exists(STORE):
        return []
    entries = []
    with open(STORE, "r", encoding="utf-8") as handle:
        for line in handle:
            parts = line.rstrip("\n").split("\t")
            if len(parts) != 5:
                continue
            entries.append(
                {
                    "id": parts[0],
                    "time": parts[1],
                    "ip": unquote(parts[2]),
                    "name": unquote(parts[3]),
                    "text": unquote(parts[4]),
                }
            )
    return entries


def save_entries(entries):
    with open(STORE, "w", encoding="utf-8") as handle:
        for entry in entries:
            handle.write(
                "\t".join(
                    [
                        entry["id"],
                        entry["time"],
                        quote(entry["ip"]),
                        quote(entry["name"]),
                        quote(entry["text"]),
                    ]
                )
                + "\n"
            )


def next_id(entries):
    used = [int(e["id"]) for e in entries if e["id"].isdigit()]
    return str(max(used) + 1 if used else 1)


def add_entry(params):
    name = params.get("name", [""])[0].strip()[:MAX_NAME]
    text = params.get("message", [""])[0].strip()[:MAX_TEXT]
    if not text:
        return
    entries = load_entries()
    entries.append(
        {
            "id": next_id(entries),
            "time": time.strftime("%d/%m %H:%M:%S"),
            "ip": os.environ.get("REMOTE_ADDR") or "inconnue",
            "name": name or "anonyme",
            "text": text,
        }
    )
    save_entries(entries[-MAX_ENTRIES:])


def delete_entry(params):
    target = params.get("id", [""])[0]
    save_entries([e for e in load_entries() if e["id"] != target])


def self_url():
    script = os.environ.get("SCRIPT_NAME") or "/cgi-bin/guestbook.py"
    host = os.environ.get("SERVER_NAME") or ""
    port = os.environ.get("SERVER_PORT") or ""
    if not host:
        return script
    if port and port != "80":
        return "http://{}:{}{}".format(host, port, script)
    return "http://{}{}".format(host, script)


def render_entry(entry):
    return """<article>
  <div class="body">
    <p class="meta"><b>{name}</b> &middot; depuis <span class="ip">{ip}</span> &middot; {time}</p>
    <p class="text">{text}</p>
  </div>
  <form method="post">
    <input type="hidden" name="action" value="delete">
    <input type="hidden" name="id" value="{id}">
    <button type="submit" class="ghost">supprimer</button>
  </form>
</article>""".format(
        name=html.escape(entry["name"]),
        ip=html.escape(entry["ip"]),
        time=html.escape(entry["time"]),
        text=html.escape(entry["text"]),
        id=html.escape(entry["id"]),
    )


def render_env():
    rows = []
    for key in ENV_KEYS:
        value = os.environ.get(key)
        rows.append(
            "<tr><th>{}</th><td>{}</td></tr>".format(
                html.escape(key),
                html.escape(value) if value else "<em>absente</em>",
            )
        )
    return "\n".join(rows)


def render(entries, prefill):
    if entries:
        listing = "\n".join(render_entry(e) for e in reversed(entries))
        clear = """<form method="post" class="row" style="margin-top:1.5rem">
    <input type="hidden" name="action" value="clear">
    <button type="submit" class="ghost">tout effacer</button>
  </form>"""
    else:
        listing = '<p class="empty">Le livre est vide. Laisse la premiere trace.</p>'
        clear = ""

    return """<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>webserv &mdash; livre d'or</title>
<style>{style}</style>
</head>
<body>
<main>
  <header class="top">
    <h1>livre d'or <span>/ cgi</span></h1>
    <p class="lede"><span class="count">{count}</span> message(s) &mdash; ecrits par le script, pas par le serveur.</p>
  </header>

  <form method="post" class="compose">
    <input type="hidden" name="action" value="add">
    <label for="name">Ton nom</label>
    <input type="text" id="name" name="name" maxlength="{maxname}" placeholder="anonyme">
    <label for="message">Ton message</label>
    <textarea id="message" name="message" maxlength="{maxtext}" placeholder="Ecris quelque chose...">{prefill}</textarea>
    <div class="row">
      <button type="submit">Signer le livre</button>
      <a class="ghost" href="{script}?demo=xss">essayer d'injecter du HTML</a>
    </div>
  </form>

  {listing}
  {clear}

  <section class="env">
    <h2>Environnement CGI vu par le script</h2>
    <table>
{env}
    </table>
  </section>

  <footer>
    Stockage : <code>{store}</code>, chemin <b>relatif</b> &mdash; il n'atterrit au bon endroit
    que si le serveur a fait son <code>chdir</code> dans le dossier du script.
  </footer>
</main>
</body>
</html>
""".format(
        style=STYLE,
        count=len(entries),
        maxname=MAX_NAME,
        maxtext=MAX_TEXT,
        prefill=html.escape(prefill),
        script=html.escape(os.environ.get("SCRIPT_NAME") or ""),
        listing=listing,
        clear=clear,
        env=render_env(),
        store=STORE,
    )


def main():
    method = os.environ.get("REQUEST_METHOD", "GET").upper()

    if method == "POST":
        params = parse_qs(read_body(), keep_blank_values=True)
        action = params.get("action", [""])[0]
        if action == "add":
            add_entry(params)
        elif action == "delete":
            delete_entry(params)
        elif action == "clear":
            save_entries([])
        sys.stdout.buffer.write("Location: {}\r\n\r\n".format(self_url()).encode("utf-8"))
        sys.stdout.buffer.flush()
        return

    query = parse_qs(os.environ.get("QUERY_STRING", ""), keep_blank_values=True)
    prefill = XSS_PROBE if query.get("demo", [""])[0] == "xss" else ""

    page = render(load_entries(), prefill).encode("utf-8")
    sys.stdout.buffer.write(b"Content-Type: text/html; charset=utf-8\r\n\r\n")
    sys.stdout.buffer.write(page)
    sys.stdout.buffer.flush()


if __name__ == "__main__":
    main()
