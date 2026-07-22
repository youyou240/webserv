# Conformité au sujet — la carte de contrôle

> Ce document liste **chaque point important du sujet webserv** et, pour chacun : ce qu'il exige, **pourquoi** ça compte, **comment** on l'a mis en place (fichiers/fonctions réels), et — quand ce n'est pas encore fait — **où** ça viendra se brancher.
>
> C'est le document de **soutenance** : il relie une exigence du sujet à une ligne de code. Les explications de fond (le *pourquoi du comment* déroulé pas à pas) vivent dans les docs spécialisées, référencées à chaque section :
> - moteur / boucle événementielle → **`ARCHITECTURE.md`**
> - protocole HTTP / parseur / réponse → **`HTTP_REQUESTS.md`**
> - fichier de config / grammaire / héritage → **`CONFIG.md`**
> - routing / GET statique → **`ROUTING.md`**
>
> Légende d'état : ✅ fait · 🟡 partiel · ⬜ à faire. **On met ce fichier à jour à chaque avancée** (cf. CLAUDE.md §7).

---

# Partie A — Règles absolues (violation = note 0)

Ce sont les interdits. Aucun n'est « une feature » : ce sont des **invariants** qui doivent tenir partout, tout le temps.

## A.1 — C++98 uniquement ✅

**Exigence.** Aucune feature C++11+ (`auto`, `nullptr`, lambdas, `std::to_string`, `unordered_map`, range-for, `<thread>`…).

**Pourquoi.** Le sujet fige le standard : un seul écart et la note tombe à 0. C'est aussi un exercice de discipline (on écrit les itérateurs à la main, on gère la mémoire explicitement).

**Comment.** Le Makefile impose le standard :
```
CPPFLAGS = -Wall -Wextra -Werror -g3 -std=c++98
```
`-std=c++98` + `-Werror` : toute construction C++11 est une **erreur** de compilation, pas juste un warning. On utilise `NULL` (jamais `nullptr`), des boucles `for` à index/itérateur explicite, `std::map`/`std::set`/`std::vector`, `static_cast`, et des typedefs de type `std::map<...>::iterator`. Les tests d'overflow, le percent-decoding, tout est écrit en C++98 pur.

## A.2 — Un seul multiplexeur d'I/O pour toutes les I/O ✅

**Exigence.** Un seul `poll()` (ou `select`/`kqueue`/`epoll`) pour **toutes** les I/O clients+serveur, le `listen` inclus.

**Pourquoi.** C'est le cœur du sujet : un serveur mono-thread, non-bloquant, qui multiplexe tout via un point d'attente unique. Deux boucles d'attente = architecture disqualifiée.

**Comment.** Une **unique** instance d'`Epoll` (`Epoll/epollClass.cpp`), garantie par un singleton :
```cpp
Epoll & Epoll::createEpoll(void) { static Epoll epoll; return epoll; }
```
`static` local → construit **une seule fois**, au premier appel. Un seul `epoll_create`, un seul `epoll_wait` dans tout le programme (`ServerManager::run` → `_epoll.wait()`). Les sockets d'écoute **et** les clients sont enregistrés dans ce même epoll via `add`/`mod`/`del`. **Référence : `ARCHITECTURE.md` actes 1 et 3.**

## A.3 — Jamais de `recv`/`send`/`read`/`write` sur socket/pipe sans readiness ✅

**Exigence.** Toute I/O sur un socket/pipe doit être précédée d'un événement de readiness du multiplexeur. L'inverse = note 0.

**Pourquoi.** Sur un FD non-bloquant, lire/écrire « à l'aveugle » gaspille des cycles et casse le modèle événementiel. Le sujet veut qu'on ne touche un FD que quand le noyau a dit « il est prêt ».

**Comment.** Les seuls `recv`/`send` du programme sont dans `Client::handleRead`/`handleWrite`, appelés **uniquement** depuis `Client::handle_event`, lui-même appelé **uniquement** par la boucle après qu'`epoll_wait` a rendu ce FD prêt. `accept` est dans `Acceptor::handle_event`, même chaîne. Aucun chemin de code ne fait d'I/O socket hors de cette voie. **Exception légitime : les fichiers disque réguliers** (`Router::serveFile` → `open`/`read`/`close`) qui sont explicitement **exemptés** du poll par le sujet.

## A.4 — Surveiller lecture ET écriture simultanément ✅

**Exigence.** Le multiplexeur doit pouvoir surveiller `EPOLLIN` **et** `EPOLLOUT` en même temps.

**Pourquoi.** Un client peut être prêt en lecture et en écriture au même instant. Et certains cas (drain d'une erreur 4xx/5xx pendant qu'on reçoit encore le body) **exigent** les deux à la fois, sinon deadlock.

**Comment.** `_ev.events` bascule via `manager.mod(this)` : `EPOLLIN` seul (attente de requête), `EPOLLOUT` seul (envoi), et **`EPOLLIN | EPOLLOUT`** dans le cas « drain » (`Client::handleRead` sur `isError()`). Dans `handle_event`, deux `if` **séparés** (pas `else if`) traitent `EPOLLIN` puis `EPOLLOUT` : un FD prêt sur les deux est traité sur les deux. **Référence : `ARCHITECTURE.md` acte 5, encadré « 413 + drain ».**

## A.5 — Jamais checker `errno` après `read`/`write` ✅

**Exigence.** Interdit d'inspecter `errno` après un `recv`/`send`/`read`/`write` pour ajuster le comportement.

**Pourquoi.** Sur certains OS, lire `errno` après une I/O non-bloquante est non fiable/interdit par le sujet. On doit déduire l'état de la **valeur de retour** seule, et laisser les vraies erreurs revenir par le multiplexeur.

**Comment.** Après `recv`/`send` on ne regarde que la valeur :
- `recv == 0` → le client a fermé (valeur de retour, pas `errno`) → on ferme (ou on bascule en drain).
- `recv < 0` / `send <= 0` → « pas maintenant » → on rend la main, on retentera au prochain réveil.
- Les vraies erreurs réseau reviennent via `EPOLLERR|EPOLLHUP`, testés **en premier** dans `handle_event`.
`errno` reste utilisé **ailleurs** (autorisé) : `strerror(errno)` pour un message d'échec de `socket`/`bind`/`listen`. **Référence : `ARCHITECTURE.md` acte 5.**

## A.6 — Tous les FD socket/pipe non-bloquants ✅

**Exigence.** `fcntl(fd, F_SETFL, O_NONBLOCK)` sur tout FD socket/pipe.

**Pourquoi.** Un seul FD bloquant peut figer tout le serveur mono-thread sur un `recv`/`accept` qui n'aboutit pas.

**Comment.** Le socket d'écoute est passé non-bloquant dans `Socket` (`fcntl(F_SETFL, O_NONBLOCK)`), et **chaque** client accepté l'est aussi, immédiatement après `accept` (`Acceptor::handle_event`). Les pipes CGI le seront à leur création (Partie E, ⬜).

## A.7 — macOS : seuls `F_SETFL`, `O_NONBLOCK`, `FD_CLOEXEC` avec `fcntl` ✅

**Exigence.** Sur macOS, aucun autre flag `fcntl`.

**Pourquoi.** Contrainte de portabilité imposée par le sujet.

**Comment.** On n'utilise `fcntl` que sous la forme `fcntl(fd, F_SETFL, O_NONBLOCK)`. Aucun `F_GETFL` combiné, aucun autre flag.

## A.8 — `fork` uniquement pour le CGI ⬜

**Exigence.** `fork` nulle part sauf pour exécuter un CGI.

**Pourquoi.** Le serveur est mono-process (hors CGI) ; forker ailleurs briserait le modèle.

**État.** Aucun `fork` dans le code actuel. Il **apparaîtra uniquement** dans le module CGI (Partie E). À la revue : `grep -rn fork` ne doit renvoyer que le code CGI.

## A.9 — Pas d'`execve` d'un autre serveur web ✅

**Exigence.** Interdit de déléguer à un vrai serveur.

**Comment.** N/A — `execve` n'apparaîtra que pour lancer l'interpréteur CGI (`python3`, `php-cgi`), jamais un serveur.

## A.10 — Une requête ne doit jamais hang indéfiniment ✅

**Exigence.** Timeouts clients obligatoires.

**Pourquoi.** Sans deadline, un client lent ou malveillant (slowloris) garde un slot ouvert pour toujours → déni de service.

**Comment.** **Deux horloges** par client (`Client::getInactiveTime`, passées en revue à chaque tour par `Arole::launchActiveRoutine`) :
- **Idle** (`IDLE_TIMEOUT` 60 s, `_lastActivity` rafraîchi à chaque I/O réussie) : tue les connexions muettes.
- **Deadline absolue** (`_requestStart`, **jamais** rafraîchie) : `HEADER_TIMEOUT` 30 s tant que les headers ne sont pas finis (anti-slowloris), `REQUEST_TIMEOUT` 300 s au total.
`epoll_wait` a un timeout de 5 s (`EPOLL_TIMEOUT_MS`) pour que ce gardien tourne régulièrement même sans trafic. **Référence : `ARCHITECTURE.md` acte 8.**

## A.11 — Rester disponible en permanence (résistance au stress) 🟡

**Exigence.** Le serveur reste up sous charge et gère les déconnexions client.

**Pourquoi.** Critère noté : on stresse le serveur, il ne doit ni crasher ni fuir.

**Comment (déjà là).** Fermetures propres (`closeConnection` → `delete this`), half-close géré (drain), pas de check `errno`, `EPOLLERR|EPOLLHUP` traités en premier, timeouts. **Manque (🟡)** : la campagne de **tests de stress** dédiée (Partie C.11).

## A.12 — Pas de fuite mémoire / FD ✅

**Exigence.** Aucune fuite.

**Pourquoi.** Noté à `valgrind` ; un serveur qui fuit finit par tomber.

**Comment.** Mémoire : `new`/`delete` explicites, `ServerManager` détruit ses `Server` et `Socket`. FD : **un seul `close`, dans `~Arole`** (gardé par `if (_fd != -1)`), jamais ailleurs → pas de double-close, pas de FD orphelin. L'objet `Client` se détruit lui-même (`delete this`) après `manager.del`. Invariant `delete this` respecté : tout appel pouvant fermer est suivi d'un `return`. **Référence : `ARCHITECTURE.md` acte 7.**

## A.13 — Fonctions externes : liste fermée ✅

**Exigence.** N'utiliser que les fonctions autorisées (cf. CLAUDE.md §0).

**Comment.** Conséquence notable pour le routing : **`realpath` n'est pas autorisé** → l'anti-traversal est **lexical** (`normalizeRemainder`, cf. `ROUTING.md` §5), pas basé sur une résolution disque. Idem, la résolution d'adresse passe par `getaddrinfo` (autorisé), pas `inet_ntoa` (formatage manuel des octets).

---

# Partie B — Lancement & Makefile

## B.1 — `./webserv [config]` ✅

**Exigence.** Exécution avec un fichier de config en argument (ou chemin par défaut).

**Comment.** `main.cpp` prend `argv[1]` comme chemin de config, défaut `configs/default.conf`. Il parse (`ConfigParser`), affiche la config, construit la topologie (`ServerManager::build`) et lance `run()`. **Référence : `ARCHITECTURE.md` acte 0.**

## B.2 — Règles Makefile `NAME`/`all`/`clean`/`fclean`/`re` ✅

**Comment.** Présentes. `NAME = Webserv`. Pas de relink inutile (règles `.PHONY`, compilation par objet). Sources listées explicitement (dont `Router/RouterClass.cpp`, `Router/MimeTypesClass.cpp`).

---

# Partie C — Features mandatory (la checklist du sujet)

## C.1 — Parser un fichier de configuration ✅

**Exigence.** Config inspirée du bloc `server` de NGINX.

**Pourquoi.** Toute la topologie (ports, sites, routes, limites) vient de là ; une config douteuse doit être rejetée tôt.

**Comment.** `Config/ConfigParser` : **lexer 2 passes** (jetons `{` `}` `;` + mots, commentaires `#`, n° de ligne) puis **descente récursive** (`parseServer`/`parseLocation`/`parseDirective`) avec handlers **stricts** (arité, placement, validation port/code/taille) et **fail-fast** avec numéro de ligne. Modèle mémoire : `ServerConfig`/`LocationConfig` (structs), **héritage `server→location` résolu à la fin du parse** (`inheritFrom`/`resolveLocations`) via des drapeaux `hasXxx`. **Référence : `CONFIG.md` (grammaire complète) + `ARCHITECTURE.md` acte 0.**

## C.2 — Écouter sur plusieurs `interface:port` ✅

**Exigence.** Multi-socket, multi-site.

**Pourquoi.** Un serveur réaliste sert plusieurs endpoints, et plusieurs sites peuvent partager un port (virtual hosts).

**Comment.** `ServerManager` possède une `map<host:port, Socket*>` (**propriétaire unique** des sockets). `getOrCreateSocket` **déduplique** : deux blocs `server` sur le même `host:port` réutilisent le **même** `Socket` (pas de re-`bind`), et chacun s'ajoute à la liste de virtual hosts de l'`Acceptor` de cet endpoint. `SO_REUSEADDR` avant `bind` pour rouvrir un port aussitôt. **Référence : `ARCHITECTURE.md` acte 1.**

## C.3 — Méthodes GET, POST, DELETE 🟡

**Exigence.** Au minimum ces trois méthodes.

**État.**
- **GET** ✅ — servi par `Router::handleGet` (cf. C.4).
- **POST / upload** ⬜ — actuellement `501` (branche non-GET de `Router::run`). Partie C.7.
- **DELETE** ⬜ — actuellement `501`. Partie C.8.

Le **parseur** accepte déjà GET/POST/DELETE (et rejette tout autre en `501`). Le **routing** distingue « méthode interdite sur cette route » (`405` + `Allow`) de « méthode connue mais handler pas écrit » (`501` temporaire). **Référence : `ROUTING.md` §2.**

## C.4 — Servir un site statique complet ✅

**Exigence.** GET de fichiers, mapping URL→dossier, index.

**Pourquoi.** C'est la fonction de base d'un serveur HTTP.

**Comment.** `Router` : matching `location` **longest-prefix** (frontière stricte sur `/`), mapping `root` de type **alias** (préfixe de location retiré, conforme à l'exemple `/kapouet`→`/tmp/www` du sujet), résolution `index`, lecture fichier (`stat`/`open`/`read`/`close`), `Content-Type` via `Router/MimeTypes`. Anti-traversal lexical. **Référence : `ROUTING.md` (tout le doc) + arbo de test `www/`.**

## C.5 — Upload de fichiers par les clients ⬜

**Exigence.** Le client peut uploader ; dossier de stockage configuré (`upload_store`).

**Pourquoi.** Feature notée ; c'est la partie la plus fastidieuse de POST.

**À faire.** Parser le corps `multipart/form-data` (lire le `boundary`, découper les parties, extraire `filename`), écrire dans `upload_store` de la location, répondre `201 Created`. Gérer `Expect: 100-continue` (émettre `HTTP/1.1 100 Continue\r\n\r\n` quand les headers passent les checks — le refus anticipé 413 marche déjà). **Point d'insertion : branche non-GET de `Router::run` (aujourd'hui `501`).** Le dé-chunking est déjà fait par le parseur. **Référence : `HTTP_REQUESTS.md` §10 (multipart) et §6 (100-continue).**

## C.6 — Codes de statut HTTP exacts 🟡

**Exigence.** 200, 201, 204, 301/302, 400, 403, 404, 405, 413, 500, 501, 504…

**Pourquoi.** Le **bon** code est explicitement noté ; en cas de doute on compare avec NGINX.

**État — déjà émis :**
- Parseur : `400`, `413`, `414`, `501`, `505` (cf. `HTTP_REQUESTS.md` §17.10).
- Router : `200`, `301/302`, `403`, `404`, `405`(+`Allow`), `500` (vhost nul), `501` (handler manquant).

**Manque (⬜) :** `201` (après upload), `204` (après DELETE), `504` (timeout — aujourd'hui on **ferme** la connexion sans réponse ; à décider si on émet un `504`), les `error_page` **custom**. **Référence : `HTTP_REQUESTS.md` §12.**

## C.7 — POST + upload ⬜

Voir **C.5** (même chantier).

## C.8 — DELETE ⬜

**Exigence.** Supprimer une ressource.

**À faire.** Résoudre le chemin (déjà fait par `resolveFsPath`), `unlink`, répondre `204` (ou `200`), `404`/`403` sinon. **Point d'insertion : branche non-GET de `Router::run`.**

## C.9 — Directory listing (autoindex) 🟡

**Exigence.** Listing on/off + fichier `index` par défaut quand la ressource est un dossier.

**État.** La **résolution `index`** est faite (`handleGet` essaie les fichiers `index` dans l'ordre). Le **listing** manque : un dossier sans index renvoie `403` pour l'instant.

**À faire.** Dans la branche « aucun index trouvé » de `handleGet` : si `autoindex on`, générer une page HTML de listing (`opendir`/`readdir`/`closedir`) ; si `off`, garder `403`. **Point d'insertion clairement isolé.** **Référence : `ROUTING.md` §6 et §8.**

## C.10 — Redirection HTTP (301/302) ✅

**Exigence.** Règle de redirection en config.

**Comment.** `Router::makeRedirect` : `status = redirectCode`, header `Location: redirectTarget`, body vide. **Court-circuite** méthode et fichier (comme le `return` de NGINX) — testé sur `location /old`. **Référence : `ROUTING.md` §7 et §9.8.**

## C.11 — Pages d'erreur par défaut 🟡

**Exigence.** Pages d'erreur par défaut si non fournies + `error_page` custom.

**État.** Pages **par défaut** servies via `HttpResponse::defaultErrorPage` (style NGINX), centralisées dans `Router::makeError`. **`error_page` custom** pas encore branché.

**À faire.** Dans `Router::makeError` : si `_config.errorPages` contient le code, lire le fichier déclaré et le servir ; sinon retomber sur la page par défaut. Fichiers d'exemple déjà fournis : `www/errors/404.html`, `www/errors/5xx.html`. **Point d'insertion unique.** **Référence : `ROUTING.md` §7.**

## C.12 — Compatible navigateur standard 🟡

**Exigence.** Se comporter comme NGINX vu d'un navigateur (headers, codes).

**État.** Headers de réponse conformes (`Server`, `Date` GMT, `Content-Length` **calculé**, `Content-Type`, `Connection`), keep-alive géré. À revalider dans un vrai navigateur une fois GET/POST/DELETE complets. **Référence : `HTTP_REQUESTS.md` §18.**

## C.13 — CGI par extension ⬜

Voir **Partie E**.

## C.14 — Timeouts + déconnexions propres ✅

Voir **A.10** et `ARCHITECTURE.md` acte 8.

## C.15 — Tests de stress ⬜

**Exigence.** Reste up en permanence sous charge.

**À faire.** Écrire des testeurs (Python/Go) : connexions massives, déconnexions brutales, requêtes lentes, vérif absence de fuite FD/mémoire (`valgrind`), le serveur ne tombe jamais. **Référence : CLAUDE.md §9.**

---

# Partie D — Directives de configuration

| Directive | Niveau | État | Où |
|---|---|---|---|
| `listen` (interface:port, multi) | server | ✅ | `ConfigParser` + `ServerManager` (C.2) |
| `server_name` (virtual hosts) | server | ✅ | `Client::selectVhost` par `Host` |
| `error_page` | server/loc | 🟡 | modèle ✅, service custom ⬜ (C.11) |
| `client_max_body_size` | server/loc | 🟡 | server ✅, **par location** ⬜ (voir note) |
| `methods` (méthodes acceptées) | location | ✅ | `Router::run` → `405`+`Allow` |
| `return` (redirection) | location | ✅ | `Router::makeRedirect` (C.10) |
| `root` (mapping URL→dossier, alias) | server/loc | ✅ | `Router::resolveFsPath` (C.4) |
| `autoindex` (on/off) | server/loc | 🟡 | modèle ✅, listing ⬜ (C.9) |
| `index` (fichier par défaut) | server/loc | ✅ | `Router::handleGet` |
| `upload_store` (dossier upload) | location | ⬜ | modèle ✅, upload ⬜ (C.5) |
| `cgi` (par extension) | location | ⬜ | modèle ✅, exécution ⬜ (E) |

**Note `client_max_body_size` par location.** La limite appliquée est celle du **server** (`resolveMaxBodySize` via `selectVhost`). Le raffinement par location bute sur un **problème de timing** : la limite doit être posée dès `headersDone()` (avant de lire le body), or le `Router` ne matche la location qu'à requête **complète**. Solution prévue : matcher la location plus tôt (réutiliser `Router::matchLocation` côté `Client::handleRead`). **Référence : `HTTP_REQUESTS.md` §17.9.**

---

# Partie E — CGI (⬜ dans son ensemble)

**Exigence.** CGI par extension (`.py`, `.php`…), env vars complètes, dé-chunk avant passage, EOF = fin si pas de `Content-Length` en sortie, `chdir` dans le bon dossier, communication `pipe`+`fork`+`execve`, **FD pipe non-bloquants et gérés par le poll**.

**Pourquoi c'est le morceau le plus délicat.** Le CGI introduit de **nouveaux FD** (les pipes) qui doivent passer par l'**unique** epoll comme tout le reste (A.2/A.3). Un `read`/`write` direct sur un pipe CGI = note 0. Et il y a un risque de **deadlock** (le CGI attend tout le body pendant qu'on attend sa sortie).

**Comment on le fera (l'architecture est déjà prête).** Le pattern polymorphe `Arole` a été conçu exactement pour ça : on créera une sous-classe **`CgiProcess : public Arole`** (ou `CgiPipe`), on l'enregistrera avec `manager.add(...)`, et le même epoll surveillera ses pipes. `fork` (le **seul** du programme), `execve` de l'interpréteur, `chdir` dans le dossier du script, env vars construites depuis la requête (méthode, path, query en `QUERY_STRING`, headers…), body dé-chunké fourni sur stdin, sortie lue jusqu'à EOF si pas de `Content-Length`. `waitpid` pour récupérer le statut. **Point d'insertion routing : avant `handleGet`, sur détection d'une extension présente dans `location.cgi`.** **Référence : CLAUDE.md §4 + `ARCHITECTURE.md` épilogue (« Le CGI sera un nouveau `Arole` »).**

---

# Partie F — Rendu

## F.1 — README ⬜

**Exigence.** À la racine, en anglais. 1ʳᵉ ligne italique `This project has been created as part of the 42 curriculum by <login>.`. Sections **Description**, **Instructions** (compilation/exécution), **Resources** (références + description de l'usage de l'IA : quelles tâches). **À écrire.** **Référence : CLAUDE.md §5.**

## F.2 — Fichiers de config + fichiers de test par feature 🟡

**Exigence.** Fournir des configs et fichiers démontrant **chaque** feature à l'éval.

**État.** `configs/default.conf` démontre virtual hosts, héritage, methods, autoindex, upload, cgi, redirect, error_page. Arbo `www/` + `www-b/` fournie (index, sous-pages, MIME, dossier sans index, uploads, errors). À **compléter** au fil des features (fichiers de test d'upload, scripts CGI, etc.).

---

# Partie G — Synthèse

| # | Point du sujet | État |
|---|---|---|
| A.1 | C++98 | ✅ |
| A.2 | Un seul multiplexeur | ✅ |
| A.3 | Pas d'I/O socket sans readiness | ✅ |
| A.4 | Lecture+écriture simultanées | ✅ |
| A.5 | Pas d'`errno` après read/write | ✅ |
| A.6 | FD socket/pipe non-bloquants | ✅ |
| A.7 | `fcntl` macOS restreint | ✅ |
| A.8 | `fork` seulement CGI | ⬜ (aucun fork encore) |
| A.10 | Timeouts | ✅ |
| A.12 | Pas de fuite mémoire/FD | ✅ |
| B | Lancement + Makefile | ✅ |
| C.1 | Parser la config | ✅ |
| C.2 | Multi interface:port | ✅ |
| C.3 | GET / POST / DELETE | 🟡 (GET ✅) |
| C.4 | Site statique | ✅ |
| C.5 | Upload | ⬜ |
| C.6 | Codes de statut exacts | 🟡 |
| C.8 | DELETE | ⬜ |
| C.9 | Directory listing | 🟡 (index ✅) |
| C.10 | Redirection | ✅ |
| C.11 | Pages d'erreur | 🟡 (défaut ✅) |
| C.14 | Timeouts + déconnexions | ✅ |
| C.15 | Tests de stress | ⬜ |
| D | Directives config | 🟡 |
| E | CGI | ⬜ |
| F.1 | README | ⬜ |

**Prochaine étape naturelle** (cf. roadmap CLAUDE.md §8) : autoindex (C.9) et pages d'erreur custom (C.11) sont les deux plus petits restants et complètent le statique ; puis POST/upload (C.5) et DELETE (C.8) ; le CGI (E) en dernier, isolé.
