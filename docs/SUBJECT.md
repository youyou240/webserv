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

## A.8 — `fork` uniquement pour le CGI ✅

**Exigence.** `fork` nulle part sauf pour exécuter un CGI.

**Pourquoi.** Le serveur est mono-process (hors CGI) ; forker ailleurs briserait le modèle.

**État.** Le **seul** `fork` du programme est dans `Cgi::start` (Partie E). Vérifiable : `grep -rn fork` ne renvoie que `Cgi/CgiClass.cpp`.

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

**Comment.** `Config/ConfigParser` : **lexer 2 passes** (jetons `{` `}` `;` + mots, commentaires `#`, n° de ligne) puis **analyse descendante** (`parseServer`/`parseLocation`/`parseDirective`, une fonction par niveau de grammaire) avec handlers **stricts** (arité, placement, validation port/code/taille) et **fail-fast** avec numéro de ligne. Modèle mémoire : `ServerConfig`/`LocationConfig` (structs), **héritage `server→location` résolu à la fin du parse** (`inheritFrom`/`resolveLocations`) via des drapeaux `hasXxx`. **Référence : `CONFIG.md` (grammaire complète) + `ARCHITECTURE.md` acte 0.**

## C.2 — Écouter sur plusieurs `interface:port` ✅

**Exigence.** Multi-socket, multi-site.

**Pourquoi.** Un serveur réaliste sert plusieurs endpoints, et plusieurs sites peuvent partager un port (virtual hosts).

**Comment.** `ServerManager` possède une `map<host:port, Socket*>` (**propriétaire unique** des sockets). `getOrCreateSocket` **déduplique** : deux blocs `server` sur le même `host:port` réutilisent le **même** `Socket` (pas de re-`bind`), et chacun s'ajoute à la liste de virtual hosts de l'`Acceptor` de cet endpoint. `SO_REUSEADDR` avant `bind` pour rouvrir un port aussitôt. **Référence : `ARCHITECTURE.md` acte 1.**

## C.3 — Méthodes GET, POST, DELETE ✅

**Exigence.** Au minimum ces trois méthodes.

**État.**
- **GET** ✅ — `Router::handleGet` (cf. C.4).
- **POST / upload** ✅ — `Router::handlePost` + `Router/Upload` (cf. C.5).
- **DELETE** ✅ — `Router::handleDelete` (cf. C.8).

Le **parseur** accepte GET/POST/DELETE (et rejette tout autre en `501`). Le **routing** distingue « méthode interdite sur cette route » (`405` + `Allow`) de « méthode inconnue » (`501`, parseur). **Référence : `ROUTING.md` §2.**

## C.4 — Servir un site statique complet ✅

**Exigence.** GET de fichiers, mapping URL→dossier, index.

**Pourquoi.** C'est la fonction de base d'un serveur HTTP.

**Comment.** `Router` : matching `location` **longest-prefix** (frontière stricte sur `/`), mapping `root` de type **alias** (préfixe de location retiré, conforme à l'exemple `/kapouet`→`/tmp/www` du sujet), résolution `index`, lecture fichier (`stat`/`open`/`read`/`close`), `Content-Type` via `Router/MimeTypes`. Anti-traversal lexical. **Référence : `ROUTING.md` (tout le doc) + arbo de test `www/`.**

## C.5 — Upload de fichiers par les clients ✅

**Exigence.** Le client peut uploader ; dossier de stockage configuré (`upload_store`).

**Pourquoi.** Feature notée ; c'est la partie la plus fastidieuse de POST.

**Comment.** `Router::handlePost` → `Router/Upload`. Deux formats : **`multipart/form-data`** (extraction du `boundary`, découpage des parties, écriture de chaque partie avec un `filename` dans `upload_store`) et **corps brut** (nom depuis le dernier segment de l'URL, ou généré). `201 Created` avec la liste des fichiers ; `500` sur échec d'écriture ; `400` sur multipart malformé ; `403` si POST sur une location sans `upload_store`. Le corps est déjà assemblé/dé-chunké par le parseur. **Sécurité : `sanitizeFilename`** ne garde que le basename → l'upload ne peut pas sortir de `upload_store` (traversal via `filename`, cf. `SECURITY.md` §3). **Référence : `UPLOAD.md` (chapitre complet), `ROUTING.md` §6bis, `HTTP_REQUESTS.md` §10.**

**`Expect: 100-continue` ✅.** Émis dès `headersDone` quand les checks passent (et `HTTP/1.1`, et pas d'erreur/413) : `Client::handleRead` pose `_response = "HTTP/1.1 100 Continue\r\n\r\n"` et bascule en `EPOLLOUT` ; une fois envoyé (`_sendingContinue`), `handleWrite` **ne ferme pas et ne reset pas** — il repasse en `EPOLLIN` pour lire le corps, puis flux normal. Le refus anticipé `413` (sans 100) marche toujours. **Référence : `HTTP_REQUESTS.md` §6.**

## C.6 — Codes de statut HTTP exacts 🟡

**Exigence.** 200, 201, 204, 301/302, 400, 403, 404, 405, 413, 500, 501, 504…

**Pourquoi.** Le **bon** code est explicitement noté ; en cas de doute on compare avec NGINX.

**État — déjà émis :**
- Parseur : `400`, `413`, `414`, `501`, `505` (cf. `HTTP_REQUESTS.md` §17.10).
- Router : `200`, `201` (upload), `204` (DELETE), `301/302`, `403`, `404`, `405`(+`Allow`), `500` (vhost nul).
- Intermédiaire : `100 Continue`.

**Manque (⬜) :** `504` (timeout — aujourd'hui on **ferme** la connexion sans réponse ; à décider si on émet un `504`, notamment pour le CGI). Les `error_page` custom sont faits (C.11). **Référence : `HTTP_REQUESTS.md` §12.**

## C.7 — POST + upload ✅

Voir **C.5** (même chantier, `Expect: 100-continue` inclus).

## C.8 — DELETE ✅

**Exigence.** Supprimer une ressource.

**Comment.** `Router::handleDelete` réutilise le `fsPath` résolu (donc l'anti-traversal de `resolveFsPath`) : `stat` absent → `404`, dossier → `403` (on ne supprime pas un répertoire), sinon `std::remove` → **`204 No Content`** (échec droits → `403`). **Contrainte fonctions autorisées :** `unlink` **n'est pas** dans la liste fermée du sujet → on utilise `std::remove` (`<cstdio>`, standard C++) ; **à valider avec l'évaluateur**. **Référence : `ROUTING.md` §6ter.**

## C.9 — Directory listing (autoindex) ✅

**Exigence.** Listing on/off + fichier `index` par défaut quand la ressource est un dossier.

**Pourquoi.** Sans index ET avec `autoindex on`, un serveur affiche le contenu du dossier — pratique et attendu par l'éval. `off` doit au contraire **refuser** (`403`), ne pas divulguer l'arborescence.

**Comment.** `handleGet` : sur un dossier, on tente les `index` dans l'ordre ; si aucun n'existe **et** `autoindex on` → `Router::autoIndex` génère la page HTML (`opendir`/`readdir`/`closedir`, chaque entrée `stat`ée pour suffixer les dossiers d'un `/`, liens construits sur l'URL de la requête). **`index` est prioritaire sur `autoindex`** (comme NGINX). `autoindex off` sans index → `403`. **Sécurité : les noms de fichiers sont HTML-échappés** (`htmlEscape`) pour empêcher une XSS via un nom de fichier piégé (`<script>…`). **Référence : `ROUTING.md` §6.**

## C.10 — Redirection HTTP (301/302) ✅

**Exigence.** Règle de redirection en config.

**Comment.** `Router::makeRedirect` : `status = redirectCode`, header `Location: redirectTarget`, body vide. **Court-circuite** méthode et fichier (comme le `return` de NGINX) — testé sur `location /old`. **Référence : `ROUTING.md` §7 et §9.8.**

## C.11 — Pages d'erreur (défaut + custom) ✅

**Exigence.** Pages d'erreur par défaut si non fournies + `error_page` custom.

**Pourquoi.** Un serveur doit toujours renvoyer une page d'erreur lisible ; et permettre de la personnaliser par config.

**Comment.** `Router::makeError` (point **unique** par où passent toutes les erreurs du Router) : si `_config.errorPages` contient le code, il résout le chemin de la page contre le **root du server** (`error_page 404 /errors/404.html` + `root ./www` → `./www/errors/404.html`) et la sert ; **fallback** sur `HttpResponse::defaultErrorPage(code)` (style NGINX) si le fichier custom est absent/illisible ou si aucune page n'est déclarée. Pas de récursion (le fallback n'appelle pas `makeError`). Fichiers d'exemple : `www/errors/404.html`, `www/errors/5xx.html`. **Limite connue :** résolution au niveau **server**, pas encore par location. **Référence : `ROUTING.md` §7.**

## C.12 — Compatible navigateur standard 🟡

**Exigence.** Se comporter comme NGINX vu d'un navigateur (headers, codes).

**État.** Headers de réponse conformes (`Server`, `Date` GMT, `Content-Length` **calculé**, `Content-Type`, `Connection`), keep-alive géré. À revalider dans un vrai navigateur une fois GET/POST/DELETE complets. **Référence : `HTTP_REQUESTS.md` §18.**

## C.13 — CGI par extension ✅

Voir **Partie E** (GET + POST, C/Python/PHP).

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
| `error_page` | server/loc | ✅ | `Router::makeError` (custom + fallback), niveau server |
| `client_max_body_size` | server/loc | ✅ | `Client::resolveMaxBodySize` → `Router::matchLocation` |
| `methods` (méthodes acceptées) | location | ✅ | `Router::run` → `405`+`Allow` |
| `return` (redirection) | location | ✅ | `Router::makeRedirect` (C.10) |
| `root` (mapping URL→dossier, alias) | server/loc | ✅ | `Router::resolveFsPath` (C.4) |
| `autoindex` (on/off) | server/loc | ✅ | `Router::autoIndex` (listing HTML) / `403` si off |
| `index` (fichier par défaut) | server/loc | ✅ | `Router::handleGet` |
| `upload_store` (dossier upload) | location | ✅ | `Router::handlePost` → `Router/Upload` |
| `cgi` (par extension) | server/loc | ✅ | GET+POST (`Cgi`), interpréteur optionnel (C/py/php) |

**Note `client_max_body_size` par location.** ✅ Résolu. Le « problème de timing » (la limite doit être posée dès `headersDone`, avant de lire le body) est levé en matchant la location **tôt** : `Client::resolveMaxBodySize` appelle `Router::matchLocation(cfg, path)` — le path est disponible dès la request line, donc avant les headers. Un upload vers `/uploads` bénéficie bien de sa limite `10m`, pas du `1m` du server. **Référence : `HTTP_REQUESTS.md` §17.9.**

---

# Partie E — CGI ✅ (GET + POST)

**Exigence.** CGI par extension (`.py`, `.php`…), env vars complètes, dé-chunk avant passage, EOF = fin si pas de `Content-Length` en sortie, `chdir` dans le bon dossier, communication `pipe`+`fork`+`execve`, **FD pipe non-bloquants et gérés par le poll**.

**Pourquoi c'est le morceau le plus délicat.** Le CGI introduit de **nouveaux FD** (les pipes) qui doivent passer par l'**unique** epoll (A.2/A.3). Un `read`/`write` direct sur un pipe CGI = note 0. Risque de **deadlock**, de **zombies**, de **fuites**.

**Comment ✅.** Moteur `Cgi/CgiClass` : coordinateur **`Cgi`** (possédé par le `Client`) + **`CgiOut`** (pipe stdout) et **`CgiIn`** (pipe stdin), tous deux **`public Arole`** dans l'epoll unique — le pattern annoncé. `Router::resolveCgi` détecte + résout le script (anti-traversal partagé) ; `Client::tryStartCgi` (**GET et POST**) fork/`pipe`/`dup2`/`chdir`/`execve`. **Interpréteur optionnel** → **C** (binaire direct), **Python**, **PHP**. Env CGI/1.1 complet. **POST** : le corps (dé-chunké) est écrit sur `stdin` via `CgiIn` (les deux pipes en epoll → pas de deadlock), `SIGPIPE` ignoré. Sortie lue jusqu'à **EOF**, parsée (`Status:`/`Content-Type:`), `502` si vide. **Robustesse** : timeout `504` + `kill`, reap non-bloquant (`waitpid(WNOHANG)`, pas de zombie), `FD_CLOEXEC` (CGI concurrents), suppression **différée** des pipes (anti use-after-free), teardown si le client part. **`fork` uniquement ici** (A.8). Scripts : `www/cgi-bin/hello.{c,py,php}` (renvoient aussi le corps en POST). **Détail complet : `docs/CGI.md`.**

**Reste (raffinements ⬜).** `PATH_INFO`, et `FD_CLOEXEC` global (epoll/sockets d'écoute, pas seulement les pipes).

---

# Partie F — Bonus

> Le sujet ne les évalue **que si le mandatory est parfait**. Ils sont listés ici pour la soutenance, pas pour compenser un manque.

## F.1 — Plusieurs types de CGI ✅

**Exigence.** Supporter plus d'un interpréteur CGI.

**Comment ✅.** La directive `cgi <extension> [interpréteur]` rend l'interpréteur **optionnel** : absent, le script est un **binaire** exécuté directement (`www/cgi-bin/hello.cgi`, compilé depuis `hello.c`) ; présent, il est passé en `argv[0]` de l'`execve` (`cgi .py /usr/bin/python3`, `cgi .php /usr/bin/php-cgi`). **Trois langages, un seul mécanisme** — c'est le point à faire remarquer : rien dans le moteur ne connaît Python ou PHP. Cf. `docs/CGI.md` §2.

## F.2 — Cookies + gestion de session ✅

**Exigence.** Cookies et sessions, **avec exemples**.

**Le partage des rôles, qui est le vrai sujet.** Le serveur **ne gère pas de sessions** : il transporte deux en-têtes, `Cookie` à l'aller et `Set-Cookie` au retour. L'identifiant, le stockage, l'expiration et la déconnexion vivent dans l'application — ici un script CGI. C'est la répartition correcte, et c'est ce qui explique qu'un **seul** changement C++ ait suffi.

**Côté requête : rien à ajouter ✅.** `HttpRequest` met les noms d'en-tête en minuscules et **rejette les caractères de contrôle** dans les valeurs (`400`), donc `cookie` arrive validé ; `isExcludedFromEnv` le laisse passer (ni hop-by-hop, ni `_` dans le nom). Le script reçoit `HTTP_COOKIE` intact et découpe lui-même sur `; `.

**Côté réponse : `std::map` → `std::multimap` ✅.** C'était le seul blocage réel, et il n'est pas cosmétique. La RFC 7230 §3.2.2 autorise à fusionner deux en-têtes de même nom par une virgule **seulement si** leur valeur est une liste séparée par des virgules — et elle nomme **`Set-Cookie` comme l'exception**, parce que sa valeur en contient déjà une (`Expires=Wed, 21 Oct 2026 …`). Fusionner par `;` ne sauve rien non plus : dans `Set-Cookie`, le `;` sépare les **attributs d'un seul cookie** (l'inverse exact de la ligne `Cookie`, où il sépare les cookies) — le second cookie serait lu comme un **attribut inconnu du premier** et jeté en silence (RFC 6265 §5.2). Un en-tête `Set-Cookie` = **un** cookie ; N cookies = N lignes. D'où `setHeader` (`erase` + `insert`, « remplace », sémantique inchangée pour les ~20 appelants) et **`addHeader`** (`insert`, « ajoute ») ; `serialize()` n'a pas bougé, parcourir un multimap visite chaque élément. `Cgi::buildResponse` route `set-cookie` → `addHeader`.

**Durcissement associé ✅.** `setHeader`/`addHeader` rejettent toute valeur portant un caractère de contrôle (`hasCtl` — même prédicat que le parseur de requête). Sans cela, un `CRLF` dans une valeur **termine l'en-tête et en ouvre un autre** : *response splitting*. Le vecteur est réel dès qu'on émet des cookies, un script de session recopiant volontiers une donnée client dans un `Set-Cookie`. L'invariant est posé dans `HttpResponse` — la classe qui sérialise — donc il couvre aussi le Router et le Client.

**L'exemple demandé par le sujet ✅.** `www/cgi-bin/session.py` : ID de 32 hexadécimaux tiré d'`os.urandom` (`random` serait prédictible), posé en `Set-Cookie: session_id=…; Path=/; HttpOnly; SameSite=Lax`, **plus** un second cookie `last_seen` sans `HttpOnly` dans la **même** réponse — c'est lui qui prouve le multimap à l'œil nu. La page affiche l'en-tête `Cookie` reçu, les `Set-Cookie` émis, et ce que `document.cookie` arrive à lire (`session_id` en est **absent** : démonstration visuelle de `HttpOnly`). L'état — compteur de visites, pseudo — est dans `sessions.txt`, chemin **relatif**, donc preuve supplémentaire du `chdir`. TTL 30 min, logout en `Max-Age=0` sur le **même `Path`** puis Post/Redirect/Get.

**Le piège à savoir expliquer.** Sans attribut `Path`, la portée par défaut d'un cookie est le **répertoire de l'URL qui l'a posé**, soit `/cgi-bin` — le reste du site ne le recevrait jamais. Et une expiration sur un `Path` différent ne remplace pas le cookie : elle en crée un **second**, que le navigateur renvoie à côté du premier. D'où `Path=/` partout, à la pose comme à la suppression.

**Détail complet : `docs/CGI.md` §9ter, `docs/HEADERS.md` §6.2, `docs/SECURITY.md` §7.3.**

**Reste ⬜.** Exécution des tests dans le container (`docs/CGI.md` §9) — le contrôle décisif étant les **deux** lignes `Set-Cookie` de la première réponse.